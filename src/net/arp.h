// src/net/arp.h — ARP engine
#ifndef ARP_H
#define ARP_H
#include "net.h"

void arp_init(void);

/* Handle an incoming ARP packet (called by net_poll) */
void arp_handle(const void *frame, uint16_t len);

/* Resolve IP → MAC. Blocks with net_poll retries up to 1 second.
 * Returns 1 on success, 0 on timeout. */
int  arp_resolve(uint32_t ip, uint8_t out_mac[6]);

/* Send an ARP request for the given IP */
void arp_request(uint32_t target_ip);

#endif
