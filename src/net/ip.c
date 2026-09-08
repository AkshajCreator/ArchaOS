// src/net/ip.c — IPv4 dispatcher, ICMP ping engine

#include "ip.h"
#include "net.h"
#include "arp.h"
#include "tcp.h"
#include "../serial.h"
#include "../pit.h"
#include <stdint.h>

static void mem_set(void *d, uint8_t v, uint32_t n)
{
    uint8_t *p = (uint8_t *)d;
    while (n--) *p++ = v;
}
static void mem_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *dp = (uint8_t *)d;
    const uint8_t *sp = (const uint8_t *)s;
    while (n--) *dp++ = *sp++;
}

/* UDP receive notification from ip_handle → dhcp/dns */
void dhcp_handle(const void *udp_payload, uint16_t len);
void dns_handle(const void *udp_payload, uint16_t len);

/* ICMP ping reply notifier — defined at the bottom of this file */
static void icmp_notify_reply(void);

/* ICMP ping state — declared here so ip_handle can check ping_id */
static volatile int ping_reply_received = 0;
static uint16_t     ping_id  = 0x1234;
static uint16_t     ping_seq = 0;

/* ============================================================
 * ip_handle — demux incoming IPv4 packet
 * ============================================================ */
void ip_handle(const void *data, uint16_t len)
{
    if (len < (uint16_t)sizeof(ip_hdr_t)) return;
    const ip_hdr_t *ip = (const ip_hdr_t *)data;

    /* Basic version / header length sanity */
    if ((ip->ver_ihl >> 4) != 4) return;
    uint8_t  ihl     = (ip->ver_ihl & 0xF) * 4;
    uint16_t total   = ntohs(ip->total_len);
    if (total > len || ihl < 20) return;

    const uint8_t *payload = (const uint8_t *)data + ihl;
    uint16_t       plen    = total - ihl;
    uint32_t       src_ip  = ntohl(ip->src_ip);

    switch (ip->proto) {
        case IP_PROTO_ICMP:
            if (plen >= sizeof(icmp_hdr_t)) {
                const icmp_hdr_t *icmp = (const icmp_hdr_t *)payload;
                /* Reply to echo requests addressed to us */
                if (icmp->type == ICMP_ECHO_REQUEST && net_if.up &&
                    ntohl(ip->dst_ip) == net_if.ip)
                {
                    /* Build echo reply */
                    static uint8_t reply_buf[512];
                    uint16_t rlen = plen < 512 ? plen : 512;
                    mem_copy(reply_buf, payload, rlen);
                    icmp_hdr_t *r = (icmp_hdr_t *)reply_buf;
                    r->type     = ICMP_ECHO_REPLY;
                    r->code     = 0;
                    r->checksum = 0;
                    r->checksum = net_checksum(reply_buf, rlen);
                    ip_send(src_ip, IP_PROTO_ICMP, reply_buf, rlen);
                }
                /* Handle echo replies to our own pings */
                else if (icmp->type == ICMP_ECHO_REPLY &&
                         ntohs(icmp->id) == ping_id) {
                    icmp_notify_reply();
                }
            }
            break;
        case IP_PROTO_UDP: {
            if (plen < sizeof(udp_hdr_t)) break;
            const udp_hdr_t *udp = (const udp_hdr_t *)payload;
            uint16_t dst_port    = ntohs(udp->dst_port);
            const uint8_t *udp_payload = payload + sizeof(udp_hdr_t);
            uint16_t udp_plen    = ntohs(udp->length);
            if (udp_plen >= sizeof(udp_hdr_t)) udp_plen -= sizeof(udp_hdr_t);
            else udp_plen = 0;

            if (dst_port == 68)    dhcp_handle(udp_payload, udp_plen);  /* DHCP reply */
            else if (dst_port == 1053 || dst_port == 53)
                                   dns_handle(udp_payload, udp_plen);   /* DNS reply  */
            break;
        }
        case IP_PROTO_TCP:
            tcp_handle(src_ip, payload, plen);
            break;
        default:
            break;
    }
}

/* ============================================================
 * ip_send — send an IPv4 packet
 * ============================================================ */
static uint16_t ip_id_counter = 1;

int ip_send(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t plen)
{
    if (!net_if.up && proto != IP_PROTO_UDP) return 0;  /* Allow UDP for DHCP */

    static uint8_t pkt[2048];
    uint16_t total = (uint16_t)(sizeof(ip_hdr_t) + plen);
    if (total > sizeof(pkt)) return 0;

    ip_hdr_t *hdr = (ip_hdr_t *)pkt;
    hdr->ver_ihl   = 0x45;   /* IPv4, IHL=5 (20 bytes) */
    hdr->tos       = 0;
    hdr->total_len = htons(total);
    hdr->id        = htons(ip_id_counter++);
    hdr->frag_off  = 0;
    hdr->ttl       = 64;
    hdr->proto     = proto;
    hdr->checksum  = 0;
    hdr->src_ip    = htonl(net_if.ip);
    hdr->dst_ip    = htonl(dst_ip);
    hdr->checksum  = net_checksum(hdr, sizeof(ip_hdr_t));

    mem_copy(pkt + sizeof(ip_hdr_t), payload, plen);

    /* Resolve next-hop MAC */
    uint32_t next_hop = dst_ip;
    if (net_if.up && net_if.subnet &&
        (dst_ip & net_if.subnet) != (net_if.ip & net_if.subnet))
        next_hop = net_if.gateway;

    uint8_t dst_mac[6];
    /* Broadcast for DHCP (dst_ip == 0xFFFFFFFF) */
    if (dst_ip == 0xFFFFFFFF) {
        for (int i = 0; i < 6; i++) dst_mac[i] = 0xFF;
    } else {
        if (!arp_resolve(next_hop, dst_mac)) return 0;
    }

    return net_send_eth(dst_mac, ETH_TYPE_IP, pkt, total);
}

/* ============================================================
 * icmp_ping — send echo request, return RTT in ms or -1
 * ============================================================ */
int icmp_ping(uint32_t dst_ip, uint32_t timeout_ms)
{
    uint8_t buf[64];
    mem_set(buf, 0, sizeof(buf));
    icmp_hdr_t *icmp = (icmp_hdr_t *)buf;
    icmp->type     = ICMP_ECHO_REQUEST;
    icmp->code     = 0;
    icmp->id       = htons(ping_id);
    icmp->seq      = htons(ping_seq++);
    icmp->checksum = 0;
    /* Add a small payload for timing */
    for (uint32_t i = sizeof(icmp_hdr_t); i < 64; i++) buf[i] = (uint8_t)i;
    icmp->checksum = net_checksum(buf, 64);

    ping_reply_received = 0;
    uint32_t t0 = pit_ticks();
    ip_send(dst_ip, IP_PROTO_ICMP, buf, 64);

    while (pit_ticks() - t0 < timeout_ms) {
        net_poll();
        if (ping_reply_received) {
            return (int)(pit_ticks() - t0);
        }
        pit_sleep(1);
    }
    return -1;
}

/* Called by ip_handle when an ICMP reply arrives for our ping */
/* (Inline check inside ip_handle for ECHO_REPLY sets this flag) */
/* We expose this symbol so ip_handle can call it */
static void icmp_notify_reply(void) { ping_reply_received = 1; }
