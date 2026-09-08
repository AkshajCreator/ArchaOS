// src/net/html.h — Streaming HTML Parser & Formatter for ArchaOS CLI & GUI Browsers
#ifndef HTML_H
#define HTML_H

#include <stdint.h>
#include <stddef.h>

#define HTML_MAX_LINKS    32
#define HTML_MAX_URL_LEN  128
#define HTML_MAX_TITLE    64

typedef struct {
    char url[HTML_MAX_URL_LEN];
    char text[64];
} html_link_t;

typedef struct {
    char        title[HTML_MAX_TITLE];
    html_link_t links[HTML_MAX_LINKS];
    int         link_count;
} html_page_t;

/* Render HTML text to VGA console with numbered links, populating page metadata */
void html_render_console(const char *html_source, const char *base_url, html_page_t *out_page);

/* Resolve a relative URL (e.g. "/about" or "page2.html") against base_url */
void html_resolve_url(const char *base_url, const char *rel_url, char *out_url, uint32_t max_len);

#endif /* HTML_H */
