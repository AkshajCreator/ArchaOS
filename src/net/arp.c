// src/net/arp.c — ARP table and packet engine

#include "arp.h"
#include "net.h"
#include "e1000.h"
#include "../serial.h"
#include "../pit.h"
#include <stdint.h>

#define ARP_CACHE_SIZE 16
#define ARP_TTL_TICKS  30000   /* ~30 seconds in ms */

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint32_t expires;   /* PIT tick at expiry */
    int      valid;
} arp_entry_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

/* Pending resolution state */
static uint32_t pending_ip  = 0;
static uint8_t  pending_mac[6];
static int      pending_ok  = 0;

static void mem_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    while (n--) *dd++ = *ss++;
}
static void mem_set(void *d, uint8_t v, uint32_t n)
{
    uint8_t *dd = (uint8_t *)d;
    while (n--) *dd++ = v;
}

void arp_init(void)
{
    mem_set(arp_cache, 0, sizeof(arp_cache));
}

/* ============================================================
 * Cache lookup / update
 * ============================================================ */
static arp_entry_t *cache_find(uint32_t ip)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip)
            return &arp_cache[i];
    return 0;
}

static void cache_insert(uint32_t ip, const uint8_t mac[6])
{
    /* Update existing or find empty slot */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            mem_copy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            mem_copy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    }
    /* Overwrite slot 0 as LRU fallback */
    arp_cache[0].ip = ip;
    mem_copy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = 1;
}

/* ============================================================
 * arp_request — broadcast an ARP request
 * ============================================================ */
void arp_request(uint32_t target_ip)
{
    uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

    uint8_t pkt[sizeof(arp_pkt_t)];
    arp_pkt_t *a = (arp_pkt_t *)pkt;
    a->hw_type   = htons(ARP_HW_ETHER);
    a->proto     = htons(ETH_TYPE_IP);
    a->hw_len    = 6;
    a->proto_len = 4;
    a->op        = htons(ARP_OP_REQUEST);
    mem_copy(a->sender_mac, net_if.mac, 6);
    a->sender_ip = htonl(net_if.ip);
    mem_set(a->target_mac, 0, 6);
    a->target_ip = htonl(target_ip);

    net_send_eth(broadcast, ETH_TYPE_ARP, pkt, sizeof(pkt));
}

/* ============================================================
 * arp_handle — process incoming ARP frames
 * ============================================================ */
void arp_handle(const void *frame, uint16_t len)
{
    if (len < (uint16_t)(sizeof(eth_hdr_t) + sizeof(arp_pkt_t))) return;
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    const arp_pkt_t *a   = (const arp_pkt_t *)(eth + 1);

    if (ntohs(a->hw_type)   != ARP_HW_ETHER) return;
    if (ntohs(a->proto)     != ETH_TYPE_IP)  return;
    if (a->hw_len != 6 || a->proto_len != 4) return;

    uint32_t sender_ip = ntohl(a->sender_ip);

    /* Update our cache with the sender's mapping */
    cache_insert(sender_ip, a->sender_mac);

    /* If this is a reply to our pending request, capture it */
    if (ntohs(a->op) == ARP_OP_REPLY && sender_ip == pending_ip) {
        mem_copy(pending_mac, a->sender_mac, 6);
        pending_ok = 1;
    }

    /* If this is a request targeting our IP, send a reply */
    if (ntohs(a->op) == ARP_OP_REQUEST && net_if.up &&
        ntohl(a->target_ip) == net_if.ip)
    {
        uint8_t reply[sizeof(arp_pkt_t)];
        arp_pkt_t *r  = (arp_pkt_t *)reply;
        r->hw_type    = htons(ARP_HW_ETHER);
        r->proto      = htons(ETH_TYPE_IP);
        r->hw_len     = 6;
        r->proto_len  = 4;
        r->op         = htons(ARP_OP_REPLY);
        mem_copy(r->sender_mac, net_if.mac, 6);
        r->sender_ip  = htonl(net_if.ip);
        mem_copy(r->target_mac, a->sender_mac, 6);
        r->target_ip  = a->sender_ip;
        net_send_eth(a->sender_mac, ETH_TYPE_ARP, reply, sizeof(reply));
    }
}

/* ============================================================
 * arp_resolve — blocking (cooperative) MAC lookup for an IP
 * ============================================================ */
int arp_resolve(uint32_t ip, uint8_t out_mac[6])
{
    /* Check cache first */
    arp_entry_t *e = cache_find(ip);
    if (e) { mem_copy(out_mac, e->mac, 6); return 1; }

    /* Send request and poll for up to 1000 ms */
    pending_ip = ip;
    pending_ok = 0;
    arp_request(ip);

    for (uint32_t ms = 0; ms < 1000; ms++) {
        net_poll();
        if (pending_ok) {
            mem_copy(out_mac, pending_mac, 6);
            cache_insert(ip, pending_mac);
            return 1;
        }
        pit_sleep(1);
    }
    return 0;
}
