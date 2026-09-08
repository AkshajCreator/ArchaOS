// src/net/net.c — Core network glue: init, poll, utilities

#include "net.h"
#include "e1000.h"
#include "arp.h"
#include "ip.h"
#include "../serial.h"
#include <stdint.h>

/* Global network interface state */
net_if_t net_if = {0};

/* Scratch receive buffer */
static uint8_t rx_frame[2048];

/* ============================================================
 * net_init — called from kernel_main after e1000_init()
 * ============================================================ */
void net_init(void)
{
    if (!e1000_is_active()) return;
    e1000_get_mac(net_if.mac);
    arp_init();
    serial_puts(COM1_BASE, "[NET] Stack initialised\n");
}

/* ============================================================
 * net_poll — call from the shell input loop every iteration.
 * Drains ALL pending frames from the RX ring in one call.
 * ============================================================ */
void net_poll(void)
{
    if (!e1000_is_active()) return;

    /* Drain up to 32 frames per call so we never miss a burst */
    for (int i = 0; i < 32; i++) {
        uint16_t len = e1000_recv(rx_frame);
        if (len < (uint16_t)sizeof(eth_hdr_t)) break;   /* Ring empty */

        eth_hdr_t *eth = (eth_hdr_t *)rx_frame;
        uint16_t type  = ntohs(eth->type);
        const uint8_t *payload = rx_frame + sizeof(eth_hdr_t);
        uint16_t plen  = len - (uint16_t)sizeof(eth_hdr_t);

        if (type == ETH_TYPE_ARP)
            arp_handle(rx_frame, len);
        else if (type == ETH_TYPE_IP)
            ip_handle(payload, plen);
    }
}

/* ============================================================
 * net_send_eth — send a raw Ethernet frame
 * Fills in the source MAC from net_if.mac automatically.
 * ============================================================ */
int net_send_eth(const uint8_t dst_mac[6], uint16_t ethertype,
                 const void *payload, uint16_t len)
{
    static uint8_t frame[2048];
    if (len + sizeof(eth_hdr_t) > sizeof(frame)) return 0;

    eth_hdr_t *eth = (eth_hdr_t *)frame;
    for (int i = 0; i < 6; i++) { eth->dst[i] = dst_mac[i]; eth->src[i] = net_if.mac[i]; }
    eth->type = htons(ethertype);

    uint8_t *p = frame + sizeof(eth_hdr_t);
    const uint8_t *src = (const uint8_t *)payload;
    for (uint16_t i = 0; i < len; i++) p[i] = src[i];

    return e1000_send(frame, (uint16_t)(sizeof(eth_hdr_t) + len));
}

/* ============================================================
 * net_checksum — RFC 1071 Internet checksum (native word order)
 * ============================================================ */
uint16_t net_checksum(const void *data, uint32_t len)
{
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* ============================================================
 * ip_to_str — convert IPv4 (host byte order) to "a.b.c.d"
 * ============================================================ */
static void u8_to_dec(uint8_t v, char *out, int *pos)
{
    if (v >= 100) { out[(*pos)++] = (char)('0' + v/100); v %= 100; }
    if (v >= 10)  { out[(*pos)++] = (char)('0' + v/10);  v %= 10;  }
    out[(*pos)++] = (char)('0' + v);
}

void ip_to_str(uint32_t ip, char *out)
{
    int pos = 0;
    u8_to_dec((uint8_t)(ip >> 24), out, &pos); out[pos++] = '.';
    u8_to_dec((uint8_t)(ip >> 16), out, &pos); out[pos++] = '.';
    u8_to_dec((uint8_t)(ip >>  8), out, &pos); out[pos++] = '.';
    u8_to_dec((uint8_t)(ip      ), out, &pos);
    out[pos] = '\0';
}

/* ============================================================
 * mac_to_str — "xx:xx:xx:xx:xx:xx"
 * ============================================================ */
static char hex_nibble(uint8_t n) { return n < 10 ? (char)('0'+n) : (char)('a'+n-10); }

void mac_to_str(const uint8_t mac[6], char *out)
{
    int pos = 0;
    for (int i = 0; i < 6; i++) {
        out[pos++] = hex_nibble(mac[i] >> 4);
        out[pos++] = hex_nibble(mac[i] & 0xF);
        if (i < 5) out[pos++] = ':';
    }
    out[pos] = '\0';
}
