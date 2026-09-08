// src/net/browse.c — Interactive Text-Mode Web Browser (Lynx-style) for ArchaOS
#include "browse.h"
#include "html.h"
#include "http.h"
#include "dns.h"
#include "net.h"
#include "../vga.h"
#include <stdint.h>

static uint32_t str_len(const char *s) { uint32_t n=0; while(*s++)n++; return n; }
static void str_copy(char *d, const char *s, uint32_t n) {
    uint32_t i=0; while (i < n-1 && s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}
static void str_cat(char *d, const char *s, uint32_t max) {
    uint32_t dlen = str_len(d);
    if (dlen >= max-1) return;
    str_copy(d+dlen, s, max-dlen);
}

#define HIST_MAX 16
static char history[HIST_MAX][HTML_MAX_URL_LEN];
static int  hist_count = 0;
static int  hist_pos   = -1;

static int fetch_page(const char *url, char *out_buf, uint32_t max_len) {
    while (*url == ' ') url++;
    int is_https = 0;
    const char *hoststart = url;
    uint16_t port = 80;

    if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' && url[4]=='s' && url[5]==':' && url[6]=='/' && url[7]=='/') {
        is_https = 1;
        port = 443;
        hoststart = url + 8;
    } else if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' && url[4]==':' && url[5]=='/' && url[6]=='/') {
        is_https = 0;
        port = 80;
        hoststart = url + 7;
    }

    const char *slash = hoststart;
    while (*slash && *slash != '/' && *slash != ':') slash++;
    char hostname[64]; int hn = (int)(slash - hoststart);
    if (hn > 63) hn = 63;
    for (int i = 0; i < hn; i++) hostname[i] = hoststart[i];
    hostname[hn] = '\0';

    if (*slash == ':') {
        port = 0;
        slash++;
        while (*slash >= '0' && *slash <= '9') {
            port = port * 10 + (*slash - '0');
            slash++;
        }
    }
    const char *path = *slash == '/' ? slash : "/";

    uint32_t hip = 0;
    if (!dns_resolve(hostname, &hip)) {
        vga_print_color("[Browser] Error: Could not resolve hostname '", 0x0C);
        vga_print_color(hostname, 0x0C);
        vga_print_color("'\n", 0x0C);
        return 0;
    }

    if (is_https) {
        vga_print_color("[TLS 1.2] Fetching HTTPS ", 0x0B);
    } else {
        vga_print_color("[HTTP] Fetching ", 0x0B);
    }
    vga_print_color(url, 0x0E);
    vga_print_color("...\n\n", 0x0B);

    int sc = http_get(hostname, hip, port, path, out_buf, max_len);
    if (sc == 0) {
        vga_print_color("[Browser] Error: Connection failed.\n", 0x0C);
        return 0;
    }
    return sc;
}

void cmd_browse(const char *initial_url) {
    if (!net_if.up) {
        vga_print_color("Network not configured. Run DHCP first.\n", 0x0C);
        return;
    }

    char current_url[HTML_MAX_URL_LEN];
    if (initial_url && initial_url[0]) {
        while (*initial_url == ' ') initial_url++;
        if (initial_url[0] != 'h') {
            str_copy(current_url, "https://", sizeof(current_url));
            str_cat(current_url, initial_url, sizeof(current_url));
        } else {
            str_copy(current_url, initial_url, sizeof(current_url));
        }
    } else {
        str_copy(current_url, "https://icanhazip.com/", sizeof(current_url));
    }

    hist_count = 1;
    hist_pos = 0;
    str_copy(history[0], current_url, sizeof(history[0]));

    static char html_buf[16384];
    static html_page_t page;

    while (1) {
        vga_clear();
        vga_print_color("================================================================================\n", 0x03);
        vga_print_color(" ArchaOS Web Explorer -> ", 0x0B);
        vga_print_color(current_url, 0x0E);
        vga_print("\n");
        vga_print_color("================================================================================\n\n", 0x03);

        int sc = fetch_page(current_url, html_buf, sizeof(html_buf));
        if (sc > 0) {
            html_render_console(html_buf, current_url, &page);
        }

        vga_print("\n");
        vga_print_color("--------------------------------------------------------------------------------\n", 0x08);
        vga_print_color(" [#]: Open Link | [b]: Back | [r]: Reload | [u <url>]: Go to URL | [q]: Quit\n", 0x0A);

        char nav[128];
        vga_get_input("browse> ", nav, sizeof(nav));

        /* Parse navigation command */
        const char *np = nav;
        while (*np == ' ') np++;

        if (np[0] == 'q' || np[0] == 'Q') {
            vga_print("Exiting browser...\n\n");
            break;
        } else if (np[0] == 'b' || np[0] == 'B') {
            if (hist_pos > 0) {
                hist_pos--;
                str_copy(current_url, history[hist_pos], sizeof(current_url));
            } else {
                vga_print_color("[Browser] Already at oldest history page.\n", 0x0C);
            }
        } else if (np[0] == 'r' || np[0] == 'R') {
            /* Reload current */
            continue;
        } else if (np[0] == 'u' && (np[1] == ' ' || np[1] == '\0')) {
            const char *new_u = np + 1;
            while (*new_u == ' ') new_u++;
            if (*new_u) {
                if (new_u[0] != 'h') {
                    str_copy(current_url, "https://", sizeof(current_url));
                    str_cat(current_url, new_u, sizeof(current_url));
                } else {
                    str_copy(current_url, new_u, sizeof(current_url));
                }
                if (hist_pos < HIST_MAX - 1) {
                    hist_pos++;
                    str_copy(history[hist_pos], current_url, sizeof(history[0]));
                    hist_count = hist_pos + 1;
                }
            }
        } else if (np[0] >= '0' && np[0] <= '9') {
            int link_num = 0;
            while (*np >= '0' && *np <= '9') {
                link_num = link_num * 10 + (*np - '0');
                np++;
            }
            if (link_num >= 1 && link_num <= page.link_count) {
                str_copy(current_url, page.links[link_num - 1].url, sizeof(current_url));
                if (hist_pos < HIST_MAX - 1) {
                    hist_pos++;
                    str_copy(history[hist_pos], current_url, sizeof(history[0]));
                    hist_count = hist_pos + 1;
                }
            } else {
                vga_print_color("[Browser] Invalid link number.\n", 0x0C);
            }
        }
    }
}
