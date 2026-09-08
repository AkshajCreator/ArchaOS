// src/net/dns.c — Robust Multi-Server DNS Resolver (UDP, A records, CNAME support)

#include "dns.h"
#include "net.h"
#include "ip.h"
#include "../serial.h"
#include "../pit.h"
#include <stdint.h>

#define DNS_PORT        53
#define DNS_LOCAL_PORT  1053   /* Source port we send from */
#define DNS_CACHE_SIZE  32

typedef struct {
    char name[64];
    uint32_t ip;
    uint32_t last_used;
    int valid;
} dns_entry_t;

static dns_entry_t dns_cache[DNS_CACHE_SIZE];
static uint32_t dns_cache_clock = 0;

static volatile int    dns_got_reply = 0;
static volatile uint32_t dns_reply_ip = 0;
static uint16_t dns_txid = 0xBEEF;

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

static uint32_t str_len(const char *s) { uint32_t n = 0; while (*s++) n++; return n; }

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int str_ieq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Helper to skip a compressed or uncompressed DNS domain name */
static uint16_t skip_dns_name(const uint8_t *buf, uint16_t len, uint16_t pos) {
    while (pos < len) {
        if (buf[pos] == 0) {
            return pos + 1;
        }
        if ((buf[pos] & 0xC0) == 0xC0) {
            /* 2-byte pointer terminates this domain name */
            return pos + 2;
        }
        pos += (uint16_t)(buf[pos] + 1);
    }
    return pos;
}

/* ============================================================
 * Build a DNS query for an A record
 * ============================================================ */
static uint16_t build_query(const char *name, uint8_t *buf, uint16_t max)
{
    mem_set(buf, 0, max > 512 ? 512 : max);
    uint16_t pos = 0;

    /* Header */
    buf[pos++] = (uint8_t)(dns_txid >> 8); buf[pos++] = (uint8_t)dns_txid; /* ID */
    buf[pos++] = 0x01; buf[pos++] = 0x00; /* Flags: recursion desired       */
    buf[pos++] = 0x00; buf[pos++] = 0x01; /* QDCOUNT = 1                    */
    buf[pos++] = 0x00; buf[pos++] = 0x00; /* ANCOUNT = 0                    */
    buf[pos++] = 0x00; buf[pos++] = 0x00; /* NSCOUNT = 0                    */
    buf[pos++] = 0x00; buf[pos++] = 0x00; /* ARCOUNT = 0                    */

    /* QNAME: encode "www.google.com" as \x03www\x06google\x03com\x00 */
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        uint8_t label_len = (uint8_t)(dot - p);
        if (label_len == 0 || pos + label_len + 2 > 512) break;
        buf[pos++] = label_len;
        while (p < dot) buf[pos++] = (uint8_t)*p++;
        if (*p == '.') p++;
    }
    buf[pos++] = 0;    /* Root label */
    buf[pos++] = 0x00; buf[pos++] = 0x01; /* QTYPE  = A   */
    buf[pos++] = 0x00; buf[pos++] = 0x01; /* QCLASS = IN  */
    return pos;
}

/* ============================================================
 * Send a DNS query packet via UDP to a specific DNS server
 * ============================================================ */
static void dns_send_query(const char *name, uint32_t dns_server)
{
    static uint8_t udp_buf[512 + sizeof(udp_hdr_t)];
    mem_set(udp_buf, 0, sizeof(udp_buf));

    udp_hdr_t *udp = (udp_hdr_t *)udp_buf;
    uint8_t   *qbuf = udp_buf + sizeof(udp_hdr_t);

    uint16_t qlen   = build_query(name, qbuf, 512);
    uint16_t udplen = (uint16_t)(sizeof(udp_hdr_t) + qlen);

    udp->src_port = htons(DNS_LOCAL_PORT);
    udp->dst_port = htons(DNS_PORT);
    udp->length   = htons(udplen);
    udp->checksum = 0;

    ip_send(dns_server, IP_PROTO_UDP, udp_buf, udplen);
}

/* ============================================================
 * dns_handle — parse incoming DNS response (called from ip_handle)
 * ============================================================ */
void dns_handle(const void *data, uint16_t len)
{
    if (len < 12) return;
    const uint8_t *buf = (const uint8_t *)data;

    /* Check transaction ID */
    uint16_t rxid = ((uint16_t)buf[0] << 8) | buf[1];
    if (rxid != dns_txid) return;

    uint16_t ancount = ((uint16_t)buf[6] << 8) | buf[7];
    if (ancount == 0) { dns_got_reply = 1; dns_reply_ip = 0; return; }

    /* Skip question section */
    uint16_t pos = skip_dns_name(buf, len, 12);
    pos += 4;   /* QTYPE + QCLASS */

    /* Parse answer records */
    for (uint16_t an = 0; an < ancount && pos + 10 <= len; an++) {
        pos = skip_dns_name(buf, len, pos);
        if (pos + 10 > len) break;

        uint16_t rtype  = ((uint16_t)buf[pos] << 8) | buf[pos+1];
        uint16_t rdlen  = ((uint16_t)buf[pos+8] << 8) | buf[pos+9];
        pos += 10;

        if (rtype == 1 && rdlen == 4 && pos + 4 <= len) {
            /* A record found! */
            dns_reply_ip = ((uint32_t)buf[pos] << 24) |
                           ((uint32_t)buf[pos+1] << 16) |
                           ((uint32_t)buf[pos+2] << 8) |
                           (uint32_t)buf[pos+3];
            dns_got_reply = 1;
            return;
        }
        pos += rdlen;
    }
    dns_got_reply = 1;
}

/* Static Fallback Seed Cache for Core Services */
typedef struct { const char *name; uint32_t ip; } dns_fallback_entry_t;
static const dns_fallback_entry_t dns_fallbacks[] = {
    { "duckduckgo.com",         IP4(20, 204, 244, 192) },
    { "lite.duckduckgo.com",    IP4(20, 204, 244, 192) },
    { "html.duckduckgo.com",    IP4(20, 204, 244, 192) },
    { "www.google.com",         IP4(142, 251, 156, 119) },
    { "google.com",             IP4(142, 251, 156, 119) },
    { "en.wikipedia.org",       IP4(103, 102, 166, 224) },
    { "en.m.wikipedia.org",     IP4(103, 102, 166, 224) },
    { "www.wikipedia.org",      IP4(103, 102, 166, 224) },
    { "upload.wikimedia.org",   IP4(103, 102, 166, 240) },
    { "wttr.in",                IP4(5, 9, 243, 187) }
};

/* ============================================================
 * dns_resolve — public API with Multi-Server Failover
 * ============================================================ */
int dns_resolve(const char *hostname, uint32_t *out_ip)
{
    if (!hostname || !out_ip) return 0;
    while (*hostname == ' ') hostname++;
    if (!*hostname) return 0;
    dns_cache_clock++;

    /* 1. Dotted-decimal IP check */
    uint32_t quick_ip = 0; int dots = 0; uint32_t octet = 0;
    const char *q = hostname;
    while (*q) {
        if (*q >= '0' && *q <= '9') { octet = octet * 10 + (*q - '0'); }
        else if (*q == '.' && dots < 3) { quick_ip = (quick_ip << 8) | (octet & 0xFF); octet = 0; dots++; }
        else { goto not_ip; }
        q++;
    }
    if (dots == 3) { *out_ip = (quick_ip << 8) | (octet & 0xFF); return 1; }
not_ip:

    /* 2. Special Localhost / Local Domains */
    if (str_ieq(hostname, "localhost") || str_ieq(hostname, "127.0.0.1")) {
        *out_ip = IP4(127, 0, 0, 1);
        return 1;
    }
    if (str_ieq(hostname, "archaos.local") || str_ieq(hostname, "home.local")) {
        *out_ip = net_if.ip ? net_if.ip : IP4(10, 0, 2, 15);
        return 1;
    }

    /* 3. Check dynamic LRU cache (case-insensitive) */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && str_ieq(dns_cache[i].name, hostname)) {
            dns_cache[i].last_used = dns_cache_clock;
            *out_ip = dns_cache[i].ip;
            return 1;
        }
    }

    if (!net_if.up) return 0;

    /* 4. Multi-Server Failover List: DHCP Gateway -> Google DNS -> Cloudflare DNS */
    uint32_t dns_servers[] = {
        net_if.dns ? net_if.dns : IP4(10, 0, 2, 3),
        IP4(8, 8, 8, 8),
        IP4(1, 1, 1, 1),
        IP4(8, 8, 4, 4)
    };
    int num_servers = (int)(sizeof(dns_servers) / sizeof(dns_servers[0]));

    for (int attempt = 0; attempt < num_servers; attempt++) {
        uint32_t target_dns = dns_servers[attempt];
        if (target_dns == 0) continue;

        dns_txid++;
        dns_got_reply = 0;
        dns_reply_ip  = 0;

        dns_send_query(hostname, target_dns);

        for (uint32_t ms = 0; ms < 1500; ms++) {
            net_poll();
            if (dns_got_reply && dns_reply_ip) break;
            extern void gui_cooperative_pump(const char *msg);
            extern volatile int g_net_cancel_requested;
            gui_cooperative_pump("Resolving DNS...");
            if (g_net_cancel_requested) return 0;
            pit_sleep(1);
        }

        if (dns_got_reply && dns_reply_ip) {
            *out_ip = dns_reply_ip;

            /* Insert into LRU slot in cache */
            int lru_slot = 0;
            uint32_t oldest_time = 0xFFFFFFFF;
            for (int i = 0; i < DNS_CACHE_SIZE; i++) {
                if (!dns_cache[i].valid) {
                    lru_slot = i;
                    break;
                }
                if (dns_cache[i].last_used < oldest_time) {
                    oldest_time = dns_cache[i].last_used;
                    lru_slot = i;
                }
            }

            uint32_t nl = str_len(hostname);
            if (nl > 63) nl = 63;
            mem_copy(dns_cache[lru_slot].name, hostname, nl);
            dns_cache[lru_slot].name[nl] = '\0';
            dns_cache[lru_slot].ip = dns_reply_ip;
            dns_cache[lru_slot].last_used = dns_cache_clock;
            dns_cache[lru_slot].valid = 1;

            serial_printf(COM1_BASE, "[DNS] Resolved %s -> %u.%u.%u.%u (via server %d)\n",
                          hostname,
                          (dns_reply_ip >> 24) & 0xFF, (dns_reply_ip >> 16) & 0xFF,
                          (dns_reply_ip >> 8) & 0xFF, dns_reply_ip & 0xFF,
                          attempt);
            return 1;
        }
    }

    /* 5. Fallback to Pre-Seeded Seed IPs for Core Domains */
    for (size_t i = 0; i < sizeof(dns_fallbacks) / sizeof(dns_fallbacks[0]); i++) {
        if (str_ieq(hostname, dns_fallbacks[i].name)) {
            *out_ip = dns_fallbacks[i].ip;
            serial_printf(COM1_BASE, "[DNS] Fallback seed hit for %s -> %u.%u.%u.%u\n",
                          hostname,
                          (dns_fallbacks[i].ip >> 24) & 0xFF, (dns_fallbacks[i].ip >> 16) & 0xFF,
                          (dns_fallbacks[i].ip >> 8) & 0xFF, dns_fallbacks[i].ip & 0xFF);
            return 1;
        }
    }

    serial_printf(COM1_BASE, "[DNS] Failed to resolve: %s\n", hostname);
    return 0;
}
