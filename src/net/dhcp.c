// src/net/dhcp.c — DHCP Client (DORA: Discover→Offer→Request→Ack)

#include "dhcp.h"
#include "net.h"
#include "ip.h"
#include "../serial.h"
#include "../pit.h"
#include <stdint.h>

#define DHCP_CLIENT_PORT  68
#define DHCP_SERVER_PORT  67
#define DHCP_MAGIC_COOKIE 0x63825363u

#define DHCP_MSGTYPE_DISCOVER 1
#define DHCP_MSGTYPE_OFFER    2
#define DHCP_MSGTYPE_REQUEST  3
#define DHCP_MSGTYPE_ACK      5

/* DHCP packet (fixed header 236 bytes + options) */
typedef struct __attribute__((packed)) {
    uint8_t  op;         /* 1=BOOTREQUEST, 2=BOOTREPLY */
    uint8_t  htype;      /* 1=Ethernet */
    uint8_t  hlen;       /* 6 */
    uint8_t  hops;
    uint32_t xid;        /* Transaction ID */
    uint16_t secs;
    uint16_t flags;      /* 0x8000 = broadcast */
    uint32_t ciaddr;     /* Client IP (0 on DISCOVER) */
    uint32_t yiaddr;     /* Your IP (from server) */
    uint32_t siaddr;     /* Server IP */
    uint32_t giaddr;     /* Relay agent */
    uint8_t  chaddr[16]; /* Client hardware address */
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;      /* 0x63825363 */
    uint8_t  options[308];
} dhcp_pkt_t;

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

/* State shared with dhcp_handle() */
static volatile int dhcp_got_offer = 0;
static volatile int dhcp_got_ack   = 0;
static uint32_t     offered_ip  = 0;
static uint32_t     offered_gw  = 0;
static uint32_t     offered_sub = 0;
static uint32_t     offered_dns = 0;
static uint32_t     dhcp_xid    = 0xDEADBEEFu;

/* ============================================================
 * Build and send a DHCP packet via UDP broadcast
 * ============================================================ */
static void dhcp_send(uint8_t msg_type, uint32_t request_ip)
{
    static uint8_t udp_buf[sizeof(dhcp_pkt_t) + sizeof(udp_hdr_t)];
    mem_set(udp_buf, 0, sizeof(udp_buf));

    udp_hdr_t  *udp  = (udp_hdr_t *)udp_buf;
    dhcp_pkt_t *dhcp = (dhcp_pkt_t *)(udp_buf + sizeof(udp_hdr_t));

    dhcp->op    = 1;   /* BOOTREQUEST */
    dhcp->htype = 1;   /* Ethernet    */
    dhcp->hlen  = 6;
    dhcp->xid   = htonl(dhcp_xid);
    dhcp->flags = htons(0x8000); /* Broadcast flag */
    mem_copy(dhcp->chaddr, net_if.mac, 6);
    dhcp->magic = htonl(DHCP_MAGIC_COOKIE);

    /* Options */
    uint8_t *opt = dhcp->options;
    /* Message type */
    *opt++ = 53; *opt++ = 1; *opt++ = msg_type;
    /* Client identifier */
    *opt++ = 61; *opt++ = 7; *opt++ = 1;
    mem_copy(opt, net_if.mac, 6); opt += 6;
    /* Requested IP (for REQUEST) */
    if (request_ip) {
        *opt++ = 50; *opt++ = 4;
        *opt++ = (uint8_t)(request_ip >> 24);
        *opt++ = (uint8_t)(request_ip >> 16);
        *opt++ = (uint8_t)(request_ip >>  8);
        *opt++ = (uint8_t)(request_ip);
    }
    /* Parameter request list */
    *opt++ = 55; *opt++ = 4;
    *opt++ = 1;  /* Subnet mask      */
    *opt++ = 3;  /* Router           */
    *opt++ = 6;  /* DNS              */
    *opt++ = 51; /* Lease time       */
    *opt++ = 255; /* End             */

    uint16_t dhcp_len = sizeof(dhcp_pkt_t);
    uint16_t udp_len  = (uint16_t)(sizeof(udp_hdr_t) + dhcp_len);

    udp->src_port = htons(DHCP_CLIENT_PORT);
    udp->dst_port = htons(DHCP_SERVER_PORT);
    udp->length   = htons(udp_len);
    udp->checksum = 0;  /* UDP checksum optional for IPv4 */

    /* Save our IP temporarily as 0 for DISCOVER, send to broadcast */
    uint32_t saved_ip = net_if.ip;
    net_if.ip = 0;
    ip_send(0xFFFFFFFF, IP_PROTO_UDP, udp_buf, udp_len);
    net_if.ip = saved_ip;
}

/* ============================================================
 * dhcp_handle — called by ip_handle when UDP dst_port == 68
 * ============================================================ */
void dhcp_handle(const void *data, uint16_t len)
{
    if (len < sizeof(dhcp_pkt_t)) return;
    const dhcp_pkt_t *d = (const dhcp_pkt_t *)data;

    if (ntohl(d->magic) != DHCP_MAGIC_COOKIE) return;
    if (ntohl(d->xid)   != dhcp_xid)          return;

    /* Parse options */
    uint8_t msg_type = 0;
    uint32_t subnet  = 0, router = 0, dns = 0;
    const uint8_t *opt = d->options;
    const uint8_t *end = opt + 308;
    while (opt < end && *opt != 255) {
        uint8_t code = *opt++;
        if (code == 0) continue; /* Pad */
        uint8_t olen = *opt++;
        if (code == 53 && olen == 1) msg_type = opt[0];
        if (code ==  1 && olen == 4) subnet = ((uint32_t)opt[0]<<24)|((uint32_t)opt[1]<<16)|((uint32_t)opt[2]<<8)|opt[3];
        if (code ==  3 && olen >= 4) router = ((uint32_t)opt[0]<<24)|((uint32_t)opt[1]<<16)|((uint32_t)opt[2]<<8)|opt[3];
        if (code ==  6 && olen >= 4) dns    = ((uint32_t)opt[0]<<24)|((uint32_t)opt[1]<<16)|((uint32_t)opt[2]<<8)|opt[3];
        opt += olen;
    }

    if (msg_type == DHCP_MSGTYPE_OFFER && !dhcp_got_offer) {
        offered_ip  = ntohl(d->yiaddr);
        offered_gw  = router;
        offered_sub = subnet ? subnet : 0xFFFFFF00u;
        offered_dns = dns    ? dns    : router;
        dhcp_got_offer = 1;
        serial_printf(COM1_BASE, "[DHCP] OFFER: %u.%u.%u.%u\n",
            (offered_ip>>24)&0xFF,(offered_ip>>16)&0xFF,(offered_ip>>8)&0xFF,offered_ip&0xFF);
    }
    if (msg_type == DHCP_MSGTYPE_ACK) {
        net_if.ip      = ntohl(d->yiaddr);
        net_if.subnet  = subnet   ? subnet  : 0xFFFFFF00u;
        net_if.gateway = router   ? router  : (net_if.ip & 0xFFFFFF00u) | 1;
        net_if.dns     = dns      ? dns     : net_if.gateway;
        net_if.up      = 1;
        dhcp_got_ack   = 1;
    }
}

/* ============================================================
 * dhcp_request — full DORA sequence with 3 retry attempts
 * ============================================================ */
int dhcp_request(void)
{
    /* Seed XID from PIT ticks to avoid replaying a cached server offer */
    dhcp_xid = (uint32_t)pit_ticks() ^ 0xA3C5F711u;

    for (int attempt = 0; attempt < 3; attempt++) {
        dhcp_got_offer = 0;
        dhcp_got_ack   = 0;
        offered_ip     = 0;

        serial_printf(COM1_BASE, "[DHCP] DISCOVER (attempt %d)\n", attempt + 1);
        dhcp_send(DHCP_MSGTYPE_DISCOVER, 0);

        /* Wait up to 3 seconds for OFFER */
        for (uint32_t ms = 0; ms < 3000; ms++) {
            net_poll();
            if (dhcp_got_offer) break;
            pit_sleep(1);
        }
        if (!dhcp_got_offer) continue;

        /* Send REQUEST for the offered IP */
        dhcp_send(DHCP_MSGTYPE_REQUEST, offered_ip);

        /* Wait up to 2 seconds for ACK */
        for (uint32_t ms = 0; ms < 2000; ms++) {
            net_poll();
            if (dhcp_got_ack) break;
            pit_sleep(1);
        }
        if (dhcp_got_ack) {
            char ip_str[16], gw_str[16];
            ip_to_str(net_if.ip, ip_str);
            ip_to_str(net_if.gateway, gw_str);
            serial_printf(COM1_BASE, "[DHCP] ACK: IP=%s GW=%s\n", ip_str, gw_str);
            return 1;
        }
    }

    /* Fallback to QEMU default static address */
    serial_puts(COM1_BASE, "[DHCP] Timeout — falling back to 10.0.2.15\n");
    net_if.ip      = IP4(10,0,2,15);
    net_if.subnet  = IP4(255,255,255,0);
    net_if.gateway = IP4(10,0,2,2);
    net_if.dns     = IP4(10,0,2,3);
    net_if.up      = 1;
    return 0;
}
