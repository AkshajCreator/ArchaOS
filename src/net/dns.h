// src/net/dns.h — DNS resolver
#ifndef DNS_H
#define DNS_H
#include "net.h"

/* Resolve hostname → IPv4. Returns 1 on success, 0 on failure. */
int dns_resolve(const char *hostname, uint32_t *out_ip);

/* Handle an incoming DNS response (called by net_poll → udp_handle) */
void dns_handle(const void *udp_payload, uint16_t payload_len);

#endif
