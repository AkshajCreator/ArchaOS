// src/net/tcp.h — TCP state machine (up to 4 simultaneous connections)
#ifndef TCP_H
#define TCP_H
#include "net.h"

#define TCP_MAX_SOCKETS   4
#define TCP_RECV_BUF_SIZE 32768

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
} tcp_state_t;

typedef struct {
    tcp_state_t state;
    uint32_t    remote_ip;
    uint16_t    local_port;
    uint16_t    remote_port;
    uint32_t    seq;          /* Next seq to send  */
    uint32_t    ack;          /* Next seq expected */
    uint8_t     recv_buf[TCP_RECV_BUF_SIZE];
    uint32_t    recv_head;
    uint32_t    recv_tail;
} tcp_socket_t;

/* Connect to remote_ip:remote_port. Returns socket index (0-3) or -1 on error. */
int  tcp_connect(uint32_t remote_ip, uint16_t remote_port);

/* Send data on socket. Returns 1 on success. */
int  tcp_send(int sock, const void *buf, uint16_t len);

/* Receive data from socket. Blocks up to timeout_ms. Returns bytes received. */
uint16_t tcp_recv(int sock, void *buf, uint16_t max_len, uint32_t timeout_ms);

/* Close a socket gracefully. */
void tcp_close(int sock);

/* Handle incoming TCP frame (called by ip_handle) */
void tcp_handle(uint32_t src_ip, const void *tcp_frame, uint16_t len);

/* Returns the socket struct (for diagnostics) */
tcp_socket_t *tcp_get_socket(int sock);

#endif
