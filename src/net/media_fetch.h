// src/net/media_fetch.h — Universal Media Download & VFS Loading
#ifndef MEDIA_FETCH_H
#define MEDIA_FETCH_H

#include <stdint.h>
#include <stddef.h>

/* Fetch audio/video/media data from either a local VFS path or an HTTP/HTTPS URL.
 * Allocates *out_buf via kmalloc, caller must free with kfree(*out_buf) when done.
 * Returns 1 on success, 0 on failure. */
int media_fetch(const char *url_or_path, uint8_t **out_buf, uint32_t *out_len);

/* Resolve URL or Wikipedia File: link into direct media URL */
void media_resolve_url(const char *url, char *out_resolved, size_t max_len);

#endif
