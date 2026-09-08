// src/net/tcp.c — Lightweight TCP state machine (4 concurrent sockets)

#include "tcp.h"
#include "net.h"
#include "ip.h"
#include "../serial.h"
#include "../pit.h"
#include <stdint.h>

static tcp_socket_t sockets[TCP_MAX_SOCKETS];
static uint16_t     next_local_port = 49152;

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

/* ============================================================
 * Build and send a TCP segment
 * ============================================================ */
static int tcp_send_seg(tcp_socket_t *s, uint8_t flags,
                        const void *data, uint16_t dlen)
{
    static uint8_t buf[1500];
    uint16_t hdr_len = sizeof(tcp_hdr_t);
    uint16_t total   = hdr_len + dlen;
    if (total > sizeof(buf)) return 0;

    tcp_hdr_t *hdr   = (tcp_hdr_t *)buf;
    hdr->src_port    = htons(s->local_port);
    hdr->dst_port    = htons(s->remote_port);
    hdr->seq         = htonl(s->seq);
    hdr->ack         = htonl(s->ack);
    hdr->data_off    = (uint8_t)((hdr_len / 4) << 4);
    hdr->flags       = flags;
    hdr->window      = htons(8192);
    hdr->checksum    = 0;
    hdr->urgent      = 0;

    if (dlen && data) mem_copy(buf + hdr_len, data, dlen);

    /* Sum pseudo-header:
     * src_ip (4 bytes) + dst_ip (4 bytes) + zero (1 byte) + proto (1 byte) + total_len (2 bytes)
     * Summed as 16-bit little-endian words to match native x86 word summation.
     */
    uint32_t sum = 0;
    sum += ((uint16_t)(net_if.ip >> 24))       | (((uint16_t)(net_if.ip >> 16) & 0xFF) << 8);
    sum += (((uint16_t)(net_if.ip >> 8) & 0xFF)) | (((uint16_t)(net_if.ip & 0xFF)) << 8);
    sum += ((uint16_t)(s->remote_ip >> 24))    | (((uint16_t)(s->remote_ip >> 16) & 0xFF) << 8);
    sum += (((uint16_t)(s->remote_ip >> 8) & 0xFF)) | (((uint16_t)(s->remote_ip & 0xFF)) << 8);
    sum += ((uint16_t)IP_PROTO_TCP << 8);
    sum += ((uint16_t)(total >> 8)) | (((uint16_t)(total & 0xFF)) << 8);

    /* Sum TCP segment */
    const uint16_t *tw = (const uint16_t *)buf;
    for (uint32_t i = 0; i < total/2; i++) {
        sum += tw[i];
    }
    if (total & 1) sum += (uint8_t)buf[total-1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    hdr->checksum = (uint16_t)(~sum);

    serial_printf(COM1_BASE, "[TCP] send: sip=%x dip=%x sp=%u dp=%u flags=%x csum=%04x\n",
                  net_if.ip, s->remote_ip, s->local_port, s->remote_port, flags, (uint32_t)hdr->checksum);

    return ip_send(s->remote_ip, IP_PROTO_TCP, buf, total);
}

/* ============================================================
 * tcp_connect — initiate a TCP connection
 * ============================================================ */
int tcp_connect(uint32_t remote_ip, uint16_t remote_port)
{
    /* Find a free socket slot */
    int idx = -1;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (sockets[i].state == TCP_CLOSED) { idx = i; break; }
    }
    if (idx < 0) { serial_puts(COM1_BASE, "[TCP] No free socket\n"); return -1; }

    tcp_socket_t *s = &sockets[idx];
    mem_set(s, 0, sizeof(*s));
    s->remote_ip   = remote_ip;
    s->local_port  = next_local_port++;
    if (next_local_port == 0) next_local_port = 49152;
    s->remote_port = remote_port;
    s->seq         = 0x12345678u;  /* Initial sequence number */
    s->ack         = 0;
    s->state       = TCP_SYN_SENT;

    /* Send SYN */
    tcp_send_seg(s, TCP_FLAG_SYN, 0, 0);
    s->seq++;  /* SYN consumes one sequence number */

    /* Wait up to 5 seconds for SYN-ACK */
    for (uint32_t ms = 0; ms < 5000; ms++) {
        net_poll();
        if (s->state == TCP_ESTABLISHED) {
            serial_printf(COM1_BASE, "[TCP] Connected socket %d port %u\n", idx, s->local_port);
            return idx;
        }
        extern void gui_cooperative_pump(const char *msg);
        extern volatile int g_net_cancel_requested;
        gui_cooperative_pump("Connecting TCP...");
        if (g_net_cancel_requested) {
            s->state = TCP_CLOSED;
            return -1;
        }
        pit_sleep(1);
    }

    s->state = TCP_CLOSED;
    serial_puts(COM1_BASE, "[TCP] Connect timeout\n");
    return -1;
}

/* ============================================================
 * tcp_send
 * ============================================================ */
int tcp_send(int sock, const void *buf, uint16_t len)
{
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return 0;
    tcp_socket_t *s = &sockets[sock];
    if (s->state != TCP_ESTABLISHED) return 0;

    int ret = tcp_send_seg(s, TCP_FLAG_ACK | TCP_FLAG_PSH, buf, len);
    if (ret) s->seq += len;
    return ret;
}

/* ============================================================
 * tcp_recv — pull bytes from the socket's receive ring buffer
 * ============================================================ */
uint16_t tcp_recv(int sock, void *buf, uint16_t max_len, uint32_t timeout_ms)
{
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return 0;
    tcp_socket_t *s = &sockets[sock];

    uint32_t deadline = pit_ticks() + timeout_ms;
    while (pit_ticks() < deadline) {
        net_poll();
        extern void gui_cooperative_pump(const char *msg);
        extern volatile int g_net_cancel_requested;
        gui_cooperative_pump("Receiving TCP...");
        if (g_net_cancel_requested) break;

        uint32_t avail = (s->recv_tail - s->recv_head + TCP_RECV_BUF_SIZE) % TCP_RECV_BUF_SIZE;
        if (avail > 0) {
            uint16_t to_copy = (uint16_t)(avail < max_len ? avail : max_len);
            uint8_t *out = (uint8_t *)buf;
            for (uint16_t i = 0; i < to_copy; i++) {
                out[i] = s->recv_buf[s->recv_head % TCP_RECV_BUF_SIZE];
                s->recv_head = (s->recv_head + 1) % TCP_RECV_BUF_SIZE;
            }
            return to_copy;
        }
        if (s->state != TCP_ESTABLISHED) break;
        pit_sleep(1);
    }
    return 0;
}

/* ============================================================
 * tcp_close — send FIN
 * ============================================================ */
void tcp_close(int sock)
{
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return;
    tcp_socket_t *s = &sockets[sock];
    if (s->state == TCP_ESTABLISHED) {
        tcp_send_seg(s, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
        s->seq++;
        /* Give the other side a brief moment to respond */
        for (int ms = 0; ms < 50; ms++) {
            net_poll();
            if (s->state == TCP_CLOSED) break;
            pit_sleep(1);
        }
    }
    s->state = TCP_CLOSED;
}

/* ============================================================
 * tcp_get_socket — diagnostics
 * ============================================================ */
tcp_socket_t *tcp_get_socket(int sock)
{
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return 0;
    return &sockets[sock];
}

/* ============================================================
 * tcp_handle — process incoming TCP segments
 * ============================================================ */
void tcp_handle(uint32_t src_ip, const void *data, uint16_t len)
{
    if (len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t *hdr = (const tcp_hdr_t *)data;
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint8_t  flags    = hdr->flags;
    uint8_t  doff     = (hdr->data_off >> 4) * 4;
    if (doff < sizeof(tcp_hdr_t) || doff > len) return;
    const uint8_t *payload = (const uint8_t *)data + doff;
    uint16_t       plen    = len - doff;

    serial_printf(COM1_BASE, "[TCP] rx: sip=%x sp=%u dp=%u flags=%x plen=%u\n",
                  src_ip, src_port, dst_port, flags, (uint32_t)plen);

    /* Find the matching socket */
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t *s = &sockets[i];
        if (s->state == TCP_CLOSED) continue;
        if (s->remote_ip   != src_ip)   continue;
        if (s->remote_port != src_port) continue;
        if (s->local_port  != dst_port) continue;

        uint32_t seg_seq = ntohl(hdr->seq);
        uint32_t seg_ack = ntohl(hdr->ack);

        /* RST handling — immediately abort socket in any state */
        if (flags & TCP_FLAG_RST) {
            serial_printf(COM1_BASE, "[TCP] RST received on socket %d — closing\n", i);
            s->state = TCP_CLOSED;
            return;
        }

        if (s->state == TCP_SYN_SENT && (flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
            /* SYN-ACK received — complete the handshake */
            s->ack   = seg_seq + 1;
            s->seq   = seg_ack;
            s->state = TCP_ESTABLISHED;
            tcp_send_seg(s, TCP_FLAG_ACK, 0, 0);
        }
        else if (s->state == TCP_ESTABLISHED) {
            /* Buffer incoming payload if present and advance ack */
            if (plen > 0) {
                if (seg_seq == s->ack || s->ack == 0) {
                    s->ack = seg_seq + plen;
                    for (uint16_t j = 0; j < plen; j++) {
                        uint32_t next = (s->recv_tail + 1) % TCP_RECV_BUF_SIZE;
                        if (next != s->recv_head) {  /* Not full */
                            s->recv_buf[s->recv_tail] = payload[j];
                            s->recv_tail = next;
                        }
                    }
                    /* Send ACK */
                    tcp_send_seg(s, TCP_FLAG_ACK, 0, 0);
                } else if (seg_seq < s->ack) {
                    /* Duplicate/retransmitted segment — send current ACK */
                    tcp_send_seg(s, TCP_FLAG_ACK, 0, 0);
                }
            }

            /* Remote FIN */
            if (flags & TCP_FLAG_FIN) {
                s->ack = seg_seq + plen + 1;
                tcp_send_seg(s, TCP_FLAG_ACK, 0, 0);
                s->state = TCP_CLOSED;
            }
        }
        else if (s->state == TCP_FIN_WAIT && (flags & TCP_FLAG_ACK)) {
            s->state = TCP_CLOSED;
        }
        return;
    }
}
