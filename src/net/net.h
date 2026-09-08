// src/net/net.h — ArchaOS Lightweight Network Stack
// All protocol headers, byte-order helpers, and shared state.
#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * Byte-order helpers (ArchaOS is x86 — little-endian)
 * ============================================================ */
static inline uint16_t htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) <<  8) |
           ((x & 0x00FF0000u) >>  8) |
           ((x & 0xFF000000u) >> 24);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

/* IPv4 address helpers */
#define IP4(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d))

/* ============================================================
 * Ethernet II Frame Header (14 bytes)
 * ============================================================ */
#define ETH_ALEN       6
#define ETH_TYPE_IP    0x0800
#define ETH_TYPE_ARP   0x0806

typedef struct __attribute__((packed)) {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t type;           /* big-endian EtherType */
} eth_hdr_t;

/* ============================================================
 * ARP Packet (28 bytes, inside Ethernet payload)
 * ============================================================ */
#define ARP_HW_ETHER   1
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

typedef struct __attribute__((packed)) {
    uint16_t hw_type;    /* 1 = Ethernet                    */
    uint16_t proto;      /* 0x0800 = IPv4                   */
    uint8_t  hw_len;     /* 6                               */
    uint8_t  proto_len;  /* 4                               */
    uint16_t op;         /* 1 = request, 2 = reply          */
    uint8_t  sender_mac[ETH_ALEN];
    uint32_t sender_ip;
    uint8_t  target_mac[ETH_ALEN];
    uint32_t target_ip;
} arp_pkt_t;

/* ============================================================
 * IPv4 Header (20 bytes minimum)
 * ============================================================ */
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;     /* Version (4) | IHL in 32-bit words */
    uint8_t  tos;
    uint16_t total_len;   /* big-endian total length            */
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ip_hdr_t;

/* ============================================================
 * ICMP Header
 * ============================================================ */
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_hdr_t;

/* ============================================================
 * UDP Header
 * ============================================================ */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

/* ============================================================
 * TCP Header
 * ============================================================ */
#define TCP_FLAG_FIN  (1 << 0)
#define TCP_FLAG_SYN  (1 << 1)
#define TCP_FLAG_RST  (1 << 2)
#define TCP_FLAG_PSH  (1 << 3)
#define TCP_FLAG_ACK  (1 << 4)

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;   /* Upper 4 bits = header length in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_hdr_t;

/* ============================================================
 * Network Interface State
 * ============================================================ */
typedef struct {
    uint8_t  mac[ETH_ALEN];
    uint32_t ip;
    uint32_t subnet;
    uint32_t gateway;
    uint32_t dns;
    int      up;         /* 1 = network is configured */
} net_if_t;

extern net_if_t net_if;   /* Global interface state */

/* ============================================================
 * Core net functions
 * ============================================================ */

/* Called from kernel_main after e1000_init() */
void net_init(void);

/* Call periodically from the shell input loop */
void net_poll(void);

/* Send a raw Ethernet frame (fills src MAC automatically) */
int  net_send_eth(const uint8_t dst_mac[6], uint16_t ethertype,
                  const void *payload, uint16_t len);

/* Internet checksum (RFC 1071) */
uint16_t net_checksum(const void *data, uint32_t len);

/* Utility: print IP address to a fixed buffer (at least 16 bytes) */
void ip_to_str(uint32_t ip, char *out);

/* Utility: print MAC address to a fixed buffer (at least 18 bytes) */
void mac_to_str(const uint8_t mac[6], char *out);

#endif /* NET_H */
