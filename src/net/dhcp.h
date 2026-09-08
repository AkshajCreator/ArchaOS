// src/net/dhcp.h — DHCP client
#ifndef DHCP_H
#define DHCP_H
#include "net.h"

/* Send DHCP DISCOVER and wait for an ACK.
 * On success: net_if.ip, .subnet, .gateway, .dns are populated.
 * Returns 1 on success, 0 on failure (falls back to 10.0.2.15). */
int dhcp_request(void);

/* Handle an incoming DHCP reply (called by net_poll → udp_handle) */
void dhcp_handle(const void *udp_payload, uint16_t len);

#endif
