// src/net/http.h — Minimal HTTP/1.1 client
#ifndef HTTP_H
#define HTTP_H
#include <stdint.h>

/* Perform an HTTP GET. host_ip is already-resolved IPv4 (host byte order).
 * hostname: virtual host for Host: header (can be NULL to use IP).
 * Writes response body into out_buf (null-terminated).
 * Returns HTTP status code, or 0 on network error. */
int http_get(const char *hostname, uint32_t host_ip, uint16_t port, const char *path,
             char *out_buf, uint32_t max_len);

/* Perform binary HTTP/HTTPS GET, returns status and written body byte length in out_len */
int http_get_binary(const char *hostname, uint32_t host_ip, uint16_t port, const char *path,
                    uint8_t *out_buf, uint32_t max_len, uint32_t *out_len);

/* Perform an HTTP POST with a JSON body.
 * extra_headers: optional additional headers (can be NULL).
 * Returns HTTP status code, or 0 on network error. */
int http_post(const char *hostname, uint32_t host_ip, uint16_t port, const char *path,
              const char *body, const char *extra_headers,
              char *out_buf, uint32_t max_len);

/* Retrieve the Location header from the most recent 3xx redirect */
void http_get_last_redirect(char *out, uint32_t max_len);

#endif
