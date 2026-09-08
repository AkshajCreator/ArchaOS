// src/net/tls.h — Native HTTPS / TLS 1.2 Engine for ArchaOS
#ifndef TLS_H
#define TLS_H

#include <stdint.h>
#include <stddef.h>

/* Establish a TLS 1.2 connection to remote_ip:port with SNI server_name */
int      tls_connect(uint32_t remote_ip, uint16_t remote_port, const char *server_name);

/* Send plaintext data over the encrypted TLS tunnel */
int      tls_send(int sock, const void *buf, uint16_t len);

/* Read decrypted plaintext data from the TLS tunnel */
uint16_t tls_recv(int sock, void *buf, uint16_t max_len, uint32_t timeout_ms);

/* Perform clean TLS teardown and close underlying TCP socket */
void     tls_close(int sock);

#endif /* TLS_H */
