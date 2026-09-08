// src/net/media_fetch.c — Universal Media Fetcher & URL Resolver
#include "media_fetch.h"
#include "../fs.h"
#include "../mm.h"
#include "dns.h"
#include "http.h"
#include "html.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static int str_len(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void str_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    while (src && src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int __attribute__((unused)) str_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static const char *str_find(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && (*h == *n || (*h >= 'A' && *h <= 'Z' && *h + 32 == *n) ||
                                       (*n >= 'A' && *n <= 'Z' && *n + 32 == *h))) {
            h++; n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

void media_resolve_url(const char *url, char *out_resolved, size_t max_len)
{
    if (!url || !out_resolved || max_len == 0) return;

    /* If it's a protocol-relative URL: //upload.wikimedia.org/... */
    if (url[0] == '/' && url[1] == '/') {
        str_copy(out_resolved, "https:", max_len);
        size_t cur = str_len(out_resolved);
        str_copy(out_resolved + cur, url, max_len - cur);
        return;
    }

    /* Check for Wikipedia File: links */
    const char *fpos = str_find(url, "File:");
    if (fpos) {
        const char *fname = fpos + 5;
        str_copy(out_resolved, "https://commons.wikimedia.org/wiki/Special:FilePath/", max_len);
        size_t cur = str_len(out_resolved);
        str_copy(out_resolved + cur, fname, max_len - cur);
        return;
    }

    str_copy(out_resolved, url, max_len);
}

int media_fetch(const char *url_or_path, uint8_t **out_buf, uint32_t *out_len)
{
    if (!url_or_path || !out_buf || !out_len) return 0;
    *out_buf = NULL;
    *out_len = 0;

    char resolved[256];
    media_resolve_url(url_or_path, resolved, sizeof(resolved));

    /* 1. Try VFS if local path or local cache */
    if (resolved[0] == '/' || (!str_find(resolved, "://") && !str_find(resolved, "www."))) {
        fs_node_t *fnode = fs_resolve(resolved);
        if (fnode && fnode->type == FS_FILE && fnode->data && fnode->size > 0) {
            uint8_t *copy = (uint8_t *)kmalloc(fnode->size + 1);
            if (copy) {
                for (size_t i = 0; i < fnode->size; i++) copy[i] = fnode->data[i];
                copy[fnode->size] = '\0';
                *out_buf = copy;
                *out_len = (uint32_t)fnode->size;
                serial_printf(COM1_BASE, "[MediaFetch] Loaded from VFS: %s (%u bytes)\n", resolved, *out_len);
                return 1;
            }
        }
    }

    /* 2. HTTP / HTTPS Network Download */
    if (str_find(resolved, "http://") || str_find(resolved, "https://")) {
        char cur_url[256];
        str_copy(cur_url, resolved, sizeof(cur_url));

        for (int redir = 0; redir < 4; redir++) {
            bool is_https = (str_find(cur_url, "https://") == cur_url);
            const char *p = is_https ? cur_url + 8 : cur_url + 7;
            char hostname[128];
            uint16_t port = is_https ? 443 : 80;
            int hi = 0;
            while (*p && *p != '/' && *p != ':' && hi < (int)sizeof(hostname) - 1) {
                hostname[hi++] = *p++;
            }
            hostname[hi] = '\0';
            if (*p == ':') {
                p++;
                port = 0;
                while (*p >= '0' && *p <= '9') {
                    port = port * 10 + (*p - '0');
                    p++;
                }
            }
            const char *path = (*p == '/') ? p : "/";

            uint32_t hip = 0;
            if (!dns_resolve(hostname, &hip)) {
                serial_printf(COM1_BASE, "[MediaFetch] DNS resolution failed for %s\n", hostname);
                break;
            }

            serial_printf(COM1_BASE, "[MediaFetch] Fetching %s (%s:%u%s)\n", is_https ? "HTTPS" : "HTTP", hostname, port, path);

            /* Allocate 512 KB buffer for media payload */
            uint32_t max_buf = 524288;
            uint8_t *download_buf = (uint8_t *)kmalloc(max_buf);
            if (!download_buf) {
                serial_printf(COM1_BASE, "[MediaFetch] Out of memory for media download buffer\n");
                break;
            }

            uint32_t dl_len = 0;
            int sc = http_get_binary(hostname, hip, port, path, download_buf, max_buf, &dl_len);
            serial_printf(COM1_BASE, "[MediaFetch] HTTP status %d, length %u\n", sc, dl_len);

            /* Check redirects */
            if (sc == 301 || sc == 302 || sc == 303 || sc == 307 || sc == 308) {
                char next_loc[256] = "";
                http_get_last_redirect(next_loc, sizeof(next_loc));
                kfree(download_buf);
                if (next_loc[0]) {
                    char next_full[256];
                    html_resolve_url(cur_url, next_loc, next_full, sizeof(next_full));
                    str_copy(cur_url, next_full, sizeof(cur_url));
                    continue;
                }
                break;
            }

            if (sc == 200 && dl_len > 0) {
                *out_buf = download_buf;
                *out_len = dl_len;
                return 1;
            }

            kfree(download_buf);
            break;
        }
    }

    return 0;
}
