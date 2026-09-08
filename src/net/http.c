// src/net/http.c — Unified HTTP/1.1 and HTTPS Client for ArchaOS
#include "http.h"
#include "net.h"
#include "tcp.h"
#include "tls.h"
#include "../serial.h"
#include <stdint.h>
#include <string.h>

static uint32_t str_len(const char *s) { uint32_t n=0; while(*s++)n++; return n; }
static void str_copy(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}
static void str_cat(char *d, const char *s, uint32_t max)
{
    uint32_t dlen = str_len(d);
    if (dlen >= max - 1) return;
    str_copy(d + dlen, s, max - dlen);
}
static void u32_to_str(uint32_t v, char *out)
{
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    char tmp[12]; int pos=0;
    while (v) { tmp[pos++] = (char)('0' + v%10); v/=10; }
    int i; for(i=0;i<pos;i++) out[i]=tmp[pos-1-i]; out[i]='\0';
}

static uint32_t parse_hex(const char *s, const char **endptr)
{
    uint32_t val = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (1) {
        char c = *s;
        if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
        else break;
        s++;
    }
    if (endptr) *endptr = s;
    return val;
}

static uint32_t http_dechunk(const char *in, uint32_t in_len, char *out, uint32_t max_out)
{
    const char *p = in;
    const char *end = in + in_len;
    uint32_t out_len = 0;

    while (p < end && out_len < max_out - 1) {
        while (p < end && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')) p++;
        if (p >= end) break;

        const char *next_p = p;
        uint32_t chunk_sz = parse_hex(p, &next_p);
        if (next_p == p) {
            while (p < end && out_len < max_out - 1) out[out_len++] = *p++;
            break;
        }
        if (chunk_sz == 0) {
            break;
        }

        p = next_p;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') p++;

        uint32_t copy_cnt = chunk_sz;
        if (copy_cnt > (uint32_t)(end - p)) copy_cnt = (uint32_t)(end - p);
        if (copy_cnt > max_out - 1 - out_len) copy_cnt = max_out - 1 - out_len;

        for (uint32_t i = 0; i < copy_cnt; i++) out[out_len++] = p[i];
        p += copy_cnt;

        while (p < end && (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')) p++;
    }
    out[out_len] = '\0';
    return out_len;
}

static int find_header(const char *hdr, uint32_t hdr_len, const char *name, const char **val_out) {
    uint32_t nlen = str_len(name);
    if (nlen == 0 || hdr_len < nlen) return 0;
    for (uint32_t i = 0; i + nlen <= hdr_len; i++) {
        int match = 1;
        for (uint32_t j = 0; j < nlen; j++) {
            char c1 = hdr[i + j];
            char c2 = name[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
            if (c2 >= 'A' && c2 <= 'Z') c2 += ('a' - 'A');
            if (c1 != c2) { match = 0; break; }
        }
        if (match) {
            const char *p = hdr + i + nlen;
            while (*p == ' ') p++;
            if (val_out) *val_out = p;
            return 1;
        }
    }
    return 0;
}

static char http_last_redirect[256] = "";

void http_get_last_redirect(char *out, uint32_t max_len) {
    if (!out || max_len == 0) return;
    str_copy(out, http_last_redirect, max_len);
}

/* ============================================================
 * Build HTTP/HTTPS request, send via TCP or TLS, extract response body
 * ============================================================ */
static int http_request(const char *hostname, uint32_t host_ip, uint16_t port,
                        const char *method, const char *path,
                        const char *body, const char *extra_headers,
                        char *out_buf, uint32_t max_len, uint32_t *out_body_len)
{
    int is_tls = (port == 443);
    int sock;

    if (is_tls) {
        sock = tls_connect(host_ip, port, hostname);
    } else {
        sock = tcp_connect(host_ip, port);
    }
    if (sock < 0) return 0;

    /* Build request */
    static char req[8192];
    req[0] = '\0';
    str_cat(req, method, sizeof(req));
    str_cat(req, " ", sizeof(req));

    /* Encode raw spaces in path to guarantee valid HTTP/1.1 request line */
    char safe_path[1024];
    int sp = 0;
    for (int i = 0; path[i] && sp < (int)sizeof(safe_path) - 4; i++) {
        if (path[i] == ' ') {
            safe_path[sp++] = '%';
            safe_path[sp++] = '2';
            safe_path[sp++] = '0';
        } else {
            safe_path[sp++] = path[i];
        }
    }
    safe_path[sp] = '\0';
    str_cat(req, safe_path, sizeof(req));
    str_cat(req, " HTTP/1.1\r\nHost: ", sizeof(req));
    if (hostname && str_len(hostname) > 0) {
        str_cat(req, hostname, sizeof(req));
    } else {
        char ip_str[16]; ip_to_str(host_ip, ip_str);
        str_cat(req, ip_str, sizeof(req));
    }
    str_cat(req, "\r\nUser-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:130.0) Gecko/20100101 Firefox/130.0 ArchaOS/4.0\r\n", sizeof(req));
    if (extra_headers && strstr(extra_headers, "Accept:")) {
        /* Custom Accept header provided */
    } else {
        str_cat(req, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/png,image/jpeg,image/*;q=0.8,*/*;q=0.7\r\n", sizeof(req));
    }
    str_cat(req, "Accept-Language: en-US,en;q=0.5\r\n", sizeof(req));
    str_cat(req, "Upgrade-Insecure-Requests: 1\r\n", sizeof(req));
    str_cat(req, "Sec-Fetch-Dest: document\r\n", sizeof(req));
    str_cat(req, "Sec-Fetch-Mode: navigate\r\n", sizeof(req));
    str_cat(req, "Sec-Fetch-Site: none\r\n", sizeof(req));
    str_cat(req, "Sec-Fetch-User: ?1\r\n", sizeof(req));
    str_cat(req, "Connection: close\r\n", sizeof(req));
    if (extra_headers) str_cat(req, extra_headers, sizeof(req));
    if (body && str_len(body) > 0) {
        char clen[16]; u32_to_str(str_len(body), clen);
        str_cat(req, "Content-Type: application/json\r\nContent-Length: ", sizeof(req));
        str_cat(req, clen, sizeof(req));
        str_cat(req, "\r\n", sizeof(req));
    }
    str_cat(req, "\r\n", sizeof(req));
    if (body) str_cat(req, body, sizeof(req));

    if (is_tls) {
        if (!tls_send(sock, req, (uint16_t)str_len(req))) {
            tls_close(sock);
            return 0;
        }
    } else {
        tcp_send(sock, req, (uint16_t)str_len(req));
    }

    /* Receive response */
    static char resp[262144];
    uint32_t total = 0;
    uint16_t got;

    uint16_t first_chunk = (uint16_t)(sizeof(resp) > 32768 ? 32768 : sizeof(resp) - 1);
    if (is_tls) {
        got = tls_recv(sock, resp, first_chunk, 5000);
    } else {
        got = tcp_recv(sock, resp, first_chunk, 5000);
    }

    if (got > 0) {
        total += got;
        resp[total] = '\0';
        while (total < sizeof(resp) - 1) {
            extern void gui_cooperative_pump(const char *msg);
            extern volatile int g_net_cancel_requested;
            char progress_msg[32];
            char kb_buf[16];
            str_copy(progress_msg, "Downloading (", sizeof(progress_msg));
            u32_to_str(total / 1024, kb_buf);
            str_cat(progress_msg, kb_buf, sizeof(progress_msg));
            str_cat(progress_msg, " KB)...", sizeof(progress_msg));
            gui_cooperative_pump(progress_msg);
            if (g_net_cancel_requested) goto recv_done;

            /* Check if response headers completed */
            const char *hdr_end = 0;
            for (uint32_t i = 0; i + 3 < total; i++) {
                if (resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n') {
                    hdr_end = resp + i + 4;
                    break;
                }
                if (resp[i]=='\n'&&resp[i+1]=='\n') {
                    hdr_end = resp + i + 2;
                    break;
                }
            }

            if (hdr_end) {
                uint32_t cur_hdr_len = (uint32_t)(hdr_end - resp);
                uint32_t cur_body_len = total - cur_hdr_len;

                /* Check Content-Length */
                const char *clp = 0;
                if (find_header(resp, cur_hdr_len, "content-length:", &clp)) {
                    uint32_t clen = 0;
                    while (*clp >= '0' && *clp <= '9') {
                        clen = clen * 10 + (*clp - '0');
                        clp++;
                    }
                    if (cur_body_len >= clen) goto recv_done;
                }

                /* Check chunked completion */
                if (cur_body_len >= 5 && (resp[total-5]=='0' && resp[total-4]=='\r' && resp[total-3]=='\n' && resp[total-2]=='\r' && resp[total-1]=='\n')) {
                    goto recv_done;
                }
            }

            uint32_t rem = sizeof(resp) - 1 - total;
            uint16_t next_chunk = (uint16_t)(rem > 32768 ? 32768 : rem);
            if (is_tls) {
                got = tls_recv(sock, resp + total, next_chunk, 3000);
            } else {
                got = tcp_recv(sock, resp + total, next_chunk, 3000);
            }
            if (got == 0) break;
            total += got;
            resp[total] = '\0';
        }
    }
recv_done:
    resp[total] = '\0';

    if (is_tls) {
        tls_close(sock);
    } else {
        tcp_close(sock);
    }

    /* Extract status code from "HTTP/1.1 200 OK" */
    int status = 0;
    if (total >= 12 && resp[0]=='H' && resp[5]=='1') {
        status = (resp[9]-'0')*100 + (resp[10]-'0')*10 + (resp[11]-'0');
    }

    /* Skip headers — find "\r\n\r\n" or "\n\n" */
    const char *body_start = resp;
    for (uint32_t i = 0; i + 3 < total; i++) {
        if (resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n') {
            body_start = resp + i + 4;
            break;
        }
        if (resp[i]=='\n'&&resp[i+1]=='\n') {
            body_start = resp + i + 2;
            break;
        }
    }

    /* Check if response is chunked */
    int is_chunked = 0;
    uint32_t hdr_len = (uint32_t)(body_start - resp);
    const char *te_val = 0;
    if (find_header(resp, hdr_len, "transfer-encoding:", &te_val) && te_val) {
        /* Check if chunked appears in the header value before line end */
        const char *p = te_val;
        while (*p && *p != '\r' && *p != '\n') {
            if ((p[0]=='c'||p[0]=='C') && (p[1]=='h'||p[1]=='H') && (p[2]=='u'||p[2]=='U') &&
                (p[3]=='n'||p[3]=='N') && (p[4]=='k'||p[4]=='K') && (p[5]=='e'||p[5]=='E') &&
                (p[6]=='d'||p[6]=='D')) {
                is_chunked = 1;
                break;
            }
            p++;
        }
    }

    uint32_t blen = (uint32_t)(resp + total - body_start);
    http_last_redirect[0] = '\0';
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const char *lp = 0;
        if (find_header(resp, hdr_len, "location:", &lp)) {
            int llen = 0;
            while (*lp && *lp != '\r' && *lp != '\n' && llen < (int)sizeof(http_last_redirect) - 1) {
                http_last_redirect[llen++] = *lp++;
            }
            http_last_redirect[llen] = '\0';
        }
    }

    uint32_t final_blen = 0;
    if (is_chunked) {
        final_blen = http_dechunk(body_start, blen, out_buf, max_len);
    } else {
        if (blen >= max_len) blen = max_len - 1;
        for (uint32_t i = 0; i < blen; i++) out_buf[i] = body_start[i];
        out_buf[blen] = '\0';
        final_blen = blen;
    }
    if (out_body_len) *out_body_len = final_blen;

    serial_printf(COM1_BASE, "[HTTP] total=%u status=%d body_len=%u (chunked=%d) redirect='%s'\n",
                  total, status, final_blen, is_chunked, http_last_redirect);

    return status ? status : 200;
}

int http_get(const char *hostname, uint32_t host_ip, uint16_t port, const char *path,
             char *out_buf, uint32_t max_len)
{
    return http_request(hostname, host_ip, port, "GET", path, 0, 0, out_buf, max_len, 0);
}

int http_get_binary(const char *hostname, uint32_t host_ip, uint16_t port, const char *path,
                    uint8_t *out_buf, uint32_t max_len, uint32_t *out_len)
{
    return http_request(hostname, host_ip, port, "GET", path, 0,
                        "Accept: image/png,image/jpeg,image/*;q=0.9,*/*;q=0.8\r\n",
                        (char *)out_buf, max_len, out_len);
}

int http_post(const char *hostname, uint32_t host_ip, uint16_t port, const char *path,
              const char *body, const char *extra_headers,
              char *out_buf, uint32_t max_len)
{
    return http_request(hostname, host_ip, port, "POST", path, body, extra_headers, out_buf, max_len, 0);
}
