// src/net/html.c — HTML Parser & Formatter for ArchaOS CLI & GUI Browsers
#include "html.h"
#include "../vga.h"
#include <stdint.h>

static uint32_t str_len(const char *s) { uint32_t n=0; while(*s++) n++; return n; }
static void str_copy(char *d, const char *s, uint32_t n) {
    uint32_t i=0; while (i < n-1 && s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}
static void str_cat(char *d, const char *s, uint32_t max) {
    uint32_t dlen = str_len(d);
    if (dlen >= max-1) return;
    str_copy(d+dlen, s, max-dlen);
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int tag_is(const char *tag, const char *name) {
    while (*tag == ' ' || *tag == '/') tag++;
    while (*name) {
        if (to_lower(*tag) != to_lower(*name)) return 0;
        tag++; name++;
    }
    return (*tag == ' ' || *tag == '>' || *tag == '/' || *tag == '\0');
}

/* ============================================================
 * Resolve relative URLs against base_url
 * ============================================================ */
void html_resolve_url(const char *base_url, const char *rel_url, char *out_url, uint32_t max_len) {
    if (!rel_url || !out_url || max_len == 0) return;

    /* Absolute URL or special schemes (about:, javascript:) */
    if ((rel_url[0] == 'h' && rel_url[1] == 't' && rel_url[2] == 't' && rel_url[3] == 'p') ||
        (rel_url[0] == 'a' && rel_url[1] == 'b' && rel_url[2] == 'o' && rel_url[3] == 'u' && rel_url[4] == 't' && rel_url[5] == ':') ||
        (rel_url[0] == 'j' && rel_url[1] == 'a' && rel_url[2] == 'v' && rel_url[3] == 'a' && rel_url[4] == 's' && rel_url[5] == 'c' && rel_url[6] == 'r' && rel_url[7] == 'i' && rel_url[8] == 'p' && rel_url[9] == 't' && rel_url[10] == ':')) {
        str_copy(out_url, rel_url, max_len);
        return;
    }

    /* Protocol-relative //example.com */
    if (rel_url[0] == '/' && rel_url[1] == '/') {
        out_url[0] = '\0';
        if (base_url && base_url[4] == 's') str_cat(out_url, "https:", max_len);
        else str_cat(out_url, "http:", max_len);
        str_cat(out_url, rel_url, max_len);
        return;
    }

    /* Root-relative /path */
    if (rel_url[0] == '/') {
        out_url[0] = '\0';
        if (base_url) {
            const char *p = base_url;
            if (p[0]=='h' && p[1]=='t' && p[2]=='t' && p[3]=='p') {
                while (*p && *p != ':') p++;
                if (*p == ':') p += 3; /* skip :// */
                while (*p && *p != '/') p++;
                uint32_t host_len = (uint32_t)(p - base_url);
                if (host_len >= max_len) host_len = max_len - 1;
                for (uint32_t i = 0; i < host_len; i++) out_url[i] = base_url[i];
                out_url[host_len] = '\0';
            }
        }
        str_cat(out_url, rel_url, max_len);
        return;
    }

    /* Directory-relative page.html */
    out_url[0] = '\0';
    if (base_url) {
        const char *last_slash = base_url;
        const char *p = base_url;
        while (*p) {
            if (*p == '/') last_slash = p;
            p++;
        }
        if (last_slash > base_url + 7) {
            uint32_t dlen = (uint32_t)(last_slash - base_url + 1);
            if (dlen >= max_len) dlen = max_len - 1;
            for (uint32_t i = 0; i < dlen; i++) out_url[i] = base_url[i];
            out_url[dlen] = '\0';
        } else {
            str_copy(out_url, base_url, max_len);
            str_cat(out_url, "/", max_len);
        }
    }
    str_cat(out_url, rel_url, max_len);
}

/* ============================================================
 * Render HTML to VGA console with numbered links
 * ============================================================ */
void html_render_console(const char *html, const char *base_url, html_page_t *out_page) {
    if (!html) return;
    if (out_page) {
        out_page->link_count = 0;
        out_page->title[0] = '\0';
    }

    const char *p = html;
    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;
    int in_head = 0;
    int in_title = 0;
    __attribute__((unused)) int in_a = 0;
    int cur_color = 0x07; /* White */
    char tag_buf[128];
    int tag_len = 0;
    char current_href[HTML_MAX_URL_LEN];
    current_href[0] = '\0';

    while (*p) {
        /* Skip HTML comments <!-- ... --> */
        if (*p == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            p += 4;
            while (*p && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
            if (*p) p += 3;
            continue;
        }

        if (*p == '<') {
            in_tag = 1;
            tag_len = 0;
            p++;
            continue;
        }

        if (in_tag) {
            if (*p == '>') {
                in_tag = 0;
                tag_buf[tag_len] = '\0';

                int is_close = 0;
                const char *tb = tag_buf;
                while (*tb == ' ') tb++;
                if (*tb == '/') is_close = 1;

                /* Process Tag */
                if (tag_is(tag_buf, "script")) {
                    in_script = !is_close;
                } else if (tag_is(tag_buf, "style")) {
                    in_style = !is_close;
                } else if (tag_is(tag_buf, "head")) {
                    in_head = !is_close;
                } else if (tag_is(tag_buf, "title")) {
                    in_title = !is_close;
                } else if (!in_script && !in_style && !in_head) {
                    if (tag_is(tag_buf, "h1") || tag_is(tag_buf, "h2")) {
                        if (is_close) {
                            vga_print("\n");
                            cur_color = 0x07;
                        } else {
                            vga_print("\n\n");
                            vga_print_color("=== ", 0x0B);
                            cur_color = 0x0F;
                        }
                    } else if (tag_is(tag_buf, "h3") || tag_is(tag_buf, "h4")) {
                        if (is_close) {
                            vga_print("\n");
                            cur_color = 0x07;
                        } else {
                            vga_print("\n");
                            vga_print_color("--- ", 0x0A);
                            cur_color = 0x0E;
                        }
                    } else if (tag_is(tag_buf, "p") || tag_is(tag_buf, "div")) {
                        if (!is_close) vga_print("\n");
                    } else if (tag_is(tag_buf, "br")) {
                        vga_print("\n");
                    } else if (tag_is(tag_buf, "hr")) {
                        vga_print("\n------------------------------------------------------------\n");
                    } else if (tag_is(tag_buf, "li")) {
                        if (!is_close) {
                            vga_print("\n  * ");
                        }
                    } else if (tag_is(tag_buf, "b") || tag_is(tag_buf, "strong")) {
                        cur_color = is_close ? 0x07 : 0x0F;
                    } else if (tag_is(tag_buf, "a")) {
                        if (is_close) {
                            in_a = 0;
                            cur_color = 0x07;
                        } else {
                            in_a = 1;
                            cur_color = 0x0B;
                            /* Extract href="value" */
                            current_href[0] = '\0';
                            const char *href_pos = tag_buf;
                            while (*href_pos) {
                                if ((href_pos[0]=='h'||href_pos[0]=='H') &&
                                    (href_pos[1]=='r'||href_pos[1]=='R') &&
                                    (href_pos[2]=='e'||href_pos[2]=='E') &&
                                    (href_pos[3]=='f'||href_pos[3]=='F') &&
                                    (href_pos[4]=='=')) {
                                    const char *val = href_pos + 5;
                                    char quote = 0;
                                    if (*val == '"' || *val == '\'') { quote = *val; val++; }
                                    int hlen = 0;
                                    while (*val && (quote ? (*val != quote) : (*val != ' ' && *val != '>')) && hlen < HTML_MAX_URL_LEN - 1) {
                                        current_href[hlen++] = *val++;
                                    }
                                    current_href[hlen] = '\0';
                                    break;
                                }
                                href_pos++;
                            }

                            if (out_page && out_page->link_count < HTML_MAX_LINKS && current_href[0]) {
                                html_resolve_url(base_url, current_href, out_page->links[out_page->link_count].url, HTML_MAX_URL_LEN);
                                out_page->link_count++;

                                /* Print [N] index in yellow */
                                char num_buf[8];
                                num_buf[0] = '[';
                                if (out_page->link_count >= 10) {
                                    num_buf[1] = (char)('0' + out_page->link_count / 10);
                                    num_buf[2] = (char)('0' + out_page->link_count % 10);
                                    num_buf[3] = ']'; num_buf[4] = ' '; num_buf[5] = '\0';
                                } else {
                                    num_buf[1] = (char)('0' + out_page->link_count);
                                    num_buf[2] = ']'; num_buf[3] = ' '; num_buf[4] = '\0';
                                }
                                vga_print_color(num_buf, 0x0E);
                            }
                        }
                    }
                }
                p++;
                continue;
            }

            if (tag_len < (int)sizeof(tag_buf) - 1) {
                tag_buf[tag_len++] = *p;
            }
            p++;
            continue;
        }

        /* Skip script, style, and head content */
        if (in_script || in_style || (in_head && !in_title)) {
            p++;
            continue;
        }

        /* Extract Title */
        if (in_title && out_page) {
            uint32_t tlen = str_len(out_page->title);
            if (tlen < HTML_MAX_TITLE - 1 && *p != '\n' && *p != '\r') {
                out_page->title[tlen] = *p;
                out_page->title[tlen + 1] = '\0';
            }
            p++;
            continue;
        }

        /* HTML Entities */
        if (*p == '&') {
            if (p[1]=='n'&&p[2]=='b'&&p[3]=='s'&&p[4]=='p'&&p[5]==';') { vga_print(" "); p += 6; continue; }
            if (p[1]=='a'&&p[2]=='m'&&p[3]=='p'&&p[4]==';')             { vga_print("&"); p += 5; continue; }
            if (p[1]=='l'&&p[2]=='t'&&p[3]==';')                         { vga_print("<"); p += 4; continue; }
            if (p[1]=='g'&&p[2]=='t'&&p[3]==';')                         { vga_print(">"); p += 4; continue; }
            if (p[1]=='q'&&p[2]=='u'&&p[3]=='o'&&p[4]=='t'&&p[5]==';') { vga_print("\""); p += 6; continue; }
            if (p[1]=='#'&&p[2]=='3'&&p[3]=='9'&&p[4]==';')             { vga_print("'"); p += 5; continue; }
        }

        /* Collapse multiple consecutive newlines or spaces */
        if (*p == '\r') { p++; continue; }
        if (*p == '\n') {
            vga_print("\n");
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            continue;
        }

        /* Print regular character */
        char c_str[2];
        c_str[0] = *p++;
        c_str[1] = '\0';
        vga_print_color(c_str, (uint8_t)cur_color);
    }
}
