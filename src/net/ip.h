// src/net/ip.h — IPv4 + ICMP dispatcher
#ifndef IP_H
#define IP_H
#include "net.h"

/* Handle an incoming IPv4 frame (called from net_poll) */
void ip_handle(const void *frame, uint16_t len);

/* Send an IPv4 packet. Resolves dst_ip → MAC via ARP automatically. */
int  ip_send(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t len);

/* Send ICMP echo request. Returns RTT in ms, or -1 on timeout. */
int  icmp_ping(uint32_t dst_ip, uint32_t timeout_ms);

#endif
