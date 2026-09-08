#include "image_decoder.h"
#include "../mm.h"
#include "../serial.h"
#include "../fs.h"
#include "http.h"
#include "dns.h"
#include "html.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "netsurf_shim.h"
#include "js/js_shim.h"

#define RGB_COL(r,g,b)  ((uint8_t)(16 + (r)*36 + (g)*6 + (b)))

/* Externs from GUI */
extern void draw_pixel_fb(int x, int y, uint8_t color);

/* ============================================================
 * PALETTE COLOR MAPPER (RGB888 -> VGA 256-color Mode 13h)
 * Fast 6x6x6 color cube + grayscale ramp mapping
 * ============================================================ */
static uint8_t rgb_to_vga(uint8_t r, uint8_t g, uint8_t b) {
    /* Check if it's near-grayscale */
    int diff_rg = (r > g) ? (r - g) : (g - r);
    int diff_gb = (g > b) ? (g - b) : (b - g);
    int diff_rb = (r > b) ? (r - b) : (b - r);

    if (diff_rg < 12 && diff_gb < 12 && diff_rb < 12) {
        int avg = (r + g + b) / 3;
        if (avg < 8) return 0; /* Black */
        if (avg > 248) return 15; /* Bright White */
        int gray_idx = (avg * 24) / 256;
        if (gray_idx > 23) gray_idx = 23;
        return (uint8_t)(232 + gray_idx);
    }

    uint8_t r6 = (uint8_t)((r * 5 + 128) / 255);
    uint8_t g6 = (uint8_t)((g * 5 + 128) / 255);
    uint8_t b6 = (uint8_t)((b * 5 + 128) / 255);
    if (r6 > 5) r6 = 5;
    if (g6 > 5) g6 = 5;
    if (b6 > 5) b6 = 5;

    return RGB_COL(r6, g6, b6);
}

/* ============================================================
 * STB_IMAGE CONFIGURATION & INCLUSION
 * Universal decoder supporting PNG (DEFLATE), JPEG, BMP, GIF, TGA, PSD
 * ============================================================ */
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) malloc(sz)
#define STBI_REALLOC(p,newsz) realloc(p,newsz)
#define STBI_FREE(p) free(p)
#define STBI_MEMCPY(dst,src,sz) memcpy(dst,src,sz)
#define STBI_MEMSET(dst,val,sz) memset(dst,val,sz)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#pragma GCC diagnostic pop

/* ============================================================
 * NETPBM PPM / PGM / PBM FALLBACK DECODER (P6, P5, P4, P3, P2, P1)
 * ============================================================ */
static void ppm_skip_ws_comments(const uint8_t *data, size_t len, size_t *pos) {
    while (*pos < len) {
        if (data[*pos] == '#') {
            while (*pos < len && data[*pos] != '\n') (*pos)++;
        } else if (data[*pos] == ' ' || data[*pos] == '\t' || data[*pos] == '\r' || data[*pos] == '\n') {
            (*pos)++;
        } else {
            break;
        }
    }
}

static int ppm_read_int(const uint8_t *data, size_t len, size_t *pos) {
    ppm_skip_ws_comments(data, len, pos);
    if (*pos >= len) return 0;
    int val = 0;
    while (*pos < len && data[*pos] >= '0' && data[*pos] <= '9') {
        val = val * 10 + (data[*pos] - '0');
        (*pos)++;
    }
    return val;
}

static int decode_ppm(const uint8_t *data, size_t len, decoded_image_t *out) {
    if (len < 10 || data[0] != 'P') return -1;
    char type = (char)data[1];
    if (type < '1' || type > '6') return -1;

    size_t pos = 2;
    int width = ppm_read_int(data, len, &pos);
    int height = ppm_read_int(data, len, &pos);
    if (width <= 0 || width > 2048 || height <= 0 || height > 2048) return -1;

    int maxval = 255;
    if (type != '1' && type != '4') {
        maxval = ppm_read_int(data, len, &pos);
        if (maxval <= 0) maxval = 255;
    }
    if (pos < len && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r')) pos++;

    uint8_t *rgb = (uint8_t *)malloc(width * height * 3);
    if (!rgb) return -1;

    if (type == '6') { /* Binary RGB */
        for (int i = 0; i < width * height; i++) {
            if (pos + 2 < len) {
                rgb[i * 3 + 0] = (uint8_t)((data[pos++] * 255) / maxval);
                rgb[i * 3 + 1] = (uint8_t)((data[pos++] * 255) / maxval);
                rgb[i * 3 + 2] = (uint8_t)((data[pos++] * 255) / maxval);
            } else {
                rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = 0;
            }
        }
    } else if (type == '3') { /* ASCII RGB */
        for (int i = 0; i < width * height; i++) {
            int r = ppm_read_int(data, len, &pos);
            int g = ppm_read_int(data, len, &pos);
            int b = ppm_read_int(data, len, &pos);
            rgb[i * 3 + 0] = (uint8_t)((r * 255) / maxval);
            rgb[i * 3 + 1] = (uint8_t)((g * 255) / maxval);
            rgb[i * 3 + 2] = (uint8_t)((b * 255) / maxval);
        }
    } else if (type == '5') { /* Binary Grayscale */
        for (int i = 0; i < width * height; i++) {
            uint8_t gray = (pos < len) ? (uint8_t)((data[pos++] * 255) / maxval) : 0;
            rgb[i * 3 + 0] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = gray;
        }
    } else {
        free(rgb);
        return -1;
    }

    out->width = width;
    out->height = height;
    out->channels = 3;
    out->pixels = rgb;
    out->is_animated = false;
    out->frame_count = 1;
    return 0;
}

/* ============================================================
 * UNIVERSAL OPEN-SOURCE VECTOR SVG RASTERIZER (NanoSVG)
 * ============================================================ */
static int decode_svg(const uint8_t *data, size_t len, decoded_image_t *out) {
    if (!data || len < 4 || !out) return -1;

    /* Quick sniff for SVG XML markers: <?xml or <svg */
    bool is_svg = false;
    size_t check_len = (len < 512) ? len : 512;
    for (size_t i = 0; i + 4 <= check_len; i++) {
        if (data[i] == '<') {
            if (i + 4 <= check_len && memcmp(data + i, "<svg", 4) == 0) { is_svg = true; break; }
            if (i + 5 <= check_len && memcmp(data + i, "<?xml", 5) == 0) { is_svg = true; break; }
        }
    }
    if (!is_svg) return -1;

    /* NanoSVG modifies the input buffer during parse, so create a mutable copy */
    char *svg_buf = (char *)malloc(len + 1);
    if (!svg_buf) return -1;
    memcpy(svg_buf, data, len);
    svg_buf[len] = '\0';

    NSVGimage *image = nsvgParse(svg_buf, "px", 96.0f);
    free(svg_buf);
    if (!image) return -1;

    int orig_w = (int)image->width;
    int orig_h = (int)image->height;
    if (orig_w <= 0 || orig_h <= 0) {
        orig_w = 100;
        orig_h = 100;
    }

    int target_w = orig_w;
    int target_h = orig_h;
    if (target_w > 320) {
        target_h = (target_h * 320) / target_w;
        target_w = 320;
    }
    if (target_h > 240) {
        target_w = (target_w * 240) / target_h;
        target_h = 240;
    }
    if (target_w < 16) target_w = 16;
    if (target_h < 16) target_h = 16;

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return -1;
    }

    size_t rgba_size = (size_t)target_w * target_h * 4;
    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);
        return -1;
    }
    memset(rgba, 0, rgba_size);

    float scale_x = (float)target_w / image->width;
    float scale_y = (float)target_h / image->height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale <= 0.001f) scale = 1.0f;

    nsvgRasterize(rast, image, 0, 0, scale, rgba, target_w, target_h, target_w * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    /* Convert 32-bit RGBA to 24-bit RGB, compositing alpha over white background (255,255,255) */
    size_t rgb_size = (size_t)target_w * target_h * 3;
    uint8_t *rgb = (uint8_t *)malloc(rgb_size);
    if (!rgb) {
        free(rgba);
        return -1;
    }

    for (int i = 0; i < target_w * target_h; i++) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];

        rgb[i * 3 + 0] = (uint8_t)((r * a + 255 * (255 - a)) / 255);
        rgb[i * 3 + 1] = (uint8_t)((g * a + 255 * (255 - a)) / 255);
        rgb[i * 3 + 2] = (uint8_t)((b * a + 255 * (255 - a)) / 255);
    }
    free(rgba);

    out->width = target_w;
    out->height = target_h;
    out->channels = 3;
    out->pixels = rgb;
    out->is_animated = false;
    out->frame_count = 1;

    serial_printf(COM1_BASE, "[ImageDecoder] Decoded SVG: %dx%d (original %dx%d)\n", target_w, target_h, orig_w, orig_h);
    return 0;
}

/* ============================================================
 * UNIVERSAL IMAGE DISPATCHER
 * Automatically identifies & decodes PNG, JPEG, BMP, GIF, SVG, TGA, PSD, PPM
 * ============================================================ */
int image_decode(const uint8_t *data, size_t len, decoded_image_t *out_img) {
    if (!data || len < 4 || !out_img) return -1;
    memset(out_img, 0, sizeof(decoded_image_t));

    /* 1. Try universal stb_image decoder (forced 3 channels = 24-bit RGB) */
    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)data, (int)len, &w, &h, &comp, 3);
    if (pixels && w > 0 && h > 0) {
        out_img->width = w;
        out_img->height = h;
        out_img->channels = 3;
        out_img->pixels = (uint8_t *)pixels;
        out_img->is_animated = false;
        out_img->frame_count = 1;
        return 0;
    }

    /* 2. Try universal open-source NanoSVG vector rasterizer */
    if (decode_svg(data, len, out_img) == 0) {
        return 0;
    }

    /* 3. Fallback to Netpbm PPM / PGM / PBM */
    if (data[0] == 'P' && data[1] >= '1' && data[1] <= '6') {
        return decode_ppm(data, len, out_img);
    }

    return -1;
}

void image_free(decoded_image_t *img) {
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}

/* ============================================================
 * IMAGE CACHE SUBSYSTEM (32 Slots LRU Pool)
 * ============================================================ */
#define IMAGE_CACHE_SLOTS 32
typedef struct {
    char url[256];
    decoded_image_t img;
    uint32_t last_used;
    bool valid;
} image_cache_entry_t;

static image_cache_entry_t img_cache[IMAGE_CACHE_SLOTS];
static uint32_t cache_clock = 0;

void image_decoder_init(void) {
    memset(img_cache, 0, sizeof(img_cache));
    cache_clock = 0;
    stbi_set_flip_vertically_on_load(0);
    serial_printf(COM1_BASE, "[ImageDecoder] Initialized Universal Image Engine (stb_image: PNG/JPEG/BMP/GIF + NanoSVG: SVG)\n");
}

void image_cache_clear(void) {
    for (int i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (img_cache[i].valid) {
            image_free(&img_cache[i].img);
            img_cache[i].valid = false;
        }
    }
}

decoded_image_t *image_cache_get_or_load(const char *url_or_path, const uint8_t *data, size_t len) {
    if (!url_or_path || !*url_or_path) return NULL;
    cache_clock++;

    /* 1. Check existing cache */
    for (int i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (img_cache[i].valid && strcmp(img_cache[i].url, url_or_path) == 0) {
            img_cache[i].last_used = cache_clock;
            return &img_cache[i].img;
        }
    }

    /* 2. Decode from provided data buffer or local VFS file */
    decoded_image_t new_img;
    int res = -1;

    if (data && len > 0) {
        res = image_decode(data, len, &new_img);
    } else {
        fs_node_t *fnode = fs_resolve(url_or_path);
        if (fnode && fnode->data && fnode->size > 0) {
            res = image_decode((const uint8_t *)fnode->data, fnode->size, &new_img);
        }
    }

    if (res != 0) return NULL;

    /* 3. Find LRU slot */
    int lru_slot = 0;
    uint32_t oldest_time = 0xFFFFFFFF;
    for (int i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (!img_cache[i].valid) {
            lru_slot = i;
            break;
        }
        if (img_cache[i].last_used < oldest_time) {
            oldest_time = img_cache[i].last_used;
            lru_slot = i;
        }
    }

    if (img_cache[lru_slot].valid) {
        image_free(&img_cache[lru_slot].img);
    }

    strncpy(img_cache[lru_slot].url, url_or_path, sizeof(img_cache[lru_slot].url) - 1);
    img_cache[lru_slot].url[sizeof(img_cache[lru_slot].url) - 1] = '\0';
    img_cache[lru_slot].img = new_img;
    img_cache[lru_slot].last_used = cache_clock;
    img_cache[lru_slot].valid = true;

    return &img_cache[lru_slot].img;
}

/* ============================================================
 * UNIVERSAL ON-DEMAND IMAGE FETCHER & CACHING PIPELINE
 * Automatically fetches remote HTTP/HTTPS images or local files,
 * decodes them into RGB pixels using stb_image, and caches them.
 * ============================================================ */
decoded_image_t *image_fetch_and_cache(const char *url_or_path, const char *base_url) {
    if (!url_or_path || !*url_or_path) return NULL;
    cache_clock++;

    /* 1. Fast Cache Hit Check (Direct Key) */
    for (int i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (img_cache[i].valid && strcmp(img_cache[i].url, url_or_path) == 0) {
            img_cache[i].last_used = cache_clock;
            return &img_cache[i].img;
        }
    }

    /* 2. Resolve relative URL against base_url */
    char full_url[256];
    html_resolve_url(base_url, url_or_path, full_url, sizeof(full_url));
    if (!full_url[0]) {
        strncpy(full_url, url_or_path, sizeof(full_url) - 1);
        full_url[sizeof(full_url) - 1] = '\0';
    }

    /* Check cache again with resolved full_url */
    for (int i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (img_cache[i].valid && strcmp(img_cache[i].url, full_url) == 0) {
            img_cache[i].last_used = cache_clock;
            return &img_cache[i].img;
        }
    }

    decoded_image_t new_img;
    memset(&new_img, 0, sizeof(new_img));
    int decode_res = -1;

    /* 3. Check local VFS file */
    fs_node_t *fnode = fs_resolve(url_or_path);
    if (!fnode && full_url[0]) fnode = fs_resolve(full_url);
    if (fnode && fnode->data && fnode->size > 0) {
        decode_res = image_decode((const uint8_t *)fnode->data, fnode->size, &new_img);
    }

    /* 4. Network Fetch (HTTP or HTTPS) */
    if (decode_res != 0 && (strncmp(full_url, "http://", 7) == 0 || strncmp(full_url, "https://", 8) == 0)) {
        char cur_fetch_url[256];
        strncpy(cur_fetch_url, full_url, sizeof(cur_fetch_url) - 1);
        cur_fetch_url[sizeof(cur_fetch_url) - 1] = '\0';

        for (int redir = 0; redir < 3; redir++) {
            bool is_https = (strncmp(cur_fetch_url, "https://", 8) == 0);
            const char *p = is_https ? cur_fetch_url + 8 : cur_fetch_url + 7;
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
                serial_printf(COM1_BASE, "[ImageFetch] DNS failed for %s\n", hostname);
                break;
            }

            serial_printf(COM1_BASE, "[ImageFetch] Fetching (%s) %s:%u%s\n", is_https ? "TLS" : "HTTP", hostname, port, path);

            /* Allocate temporary download buffer (512 KB) */
            uint32_t max_buf = 524288;
            uint8_t *download_buf = (uint8_t *)kmalloc(max_buf);
            if (!download_buf) {
                serial_printf(COM1_BASE, "[ImageFetch] Out of memory allocating download buffer\n");
                break;
            }

            uint32_t out_len = 0;
            int sc = http_get_binary(hostname, hip, port, path, download_buf, max_buf, &out_len);
            serial_printf(COM1_BASE, "[ImageFetch] HTTP status %d, got %u bytes\n", sc, out_len);

            /* Handle redirect */
            if (sc == 301 || sc == 302 || sc == 303 || sc == 307 || sc == 308) {
                char next_loc[256] = "";
                http_get_last_redirect(next_loc, sizeof(next_loc));
                kfree(download_buf);
                if (next_loc[0]) {
                    char next_full[256];
                    html_resolve_url(cur_fetch_url, next_loc, next_full, sizeof(next_full));
                    strncpy(cur_fetch_url, next_full, sizeof(cur_fetch_url) - 1);
                    cur_fetch_url[sizeof(cur_fetch_url) - 1] = '\0';
                    continue;
                }
                break;
            }

            if (sc == 200 && out_len > 0) {
                decode_res = image_decode(download_buf, out_len, &new_img);
                kfree(download_buf);
                if (decode_res == 0) {
                    serial_printf(COM1_BASE, "[ImageFetch] Decoded %s: %dx%d (%d channels)\n",
                                  cur_fetch_url, new_img.width, new_img.height, new_img.channels);
                    break;
                } else {
                    serial_printf(COM1_BASE, "[ImageFetch] Failed to decode image bytes from %s\n", cur_fetch_url);
                }
            } else {
                kfree(download_buf);
            }
            break;
        }
    }

    if (decode_res != 0 || !new_img.pixels) return NULL;

    /* 5. Insert into LRU slot in img_cache */
    int lru_slot = 0;
    uint32_t oldest_time = 0xFFFFFFFF;
    for (int i = 0; i < IMAGE_CACHE_SLOTS; i++) {
        if (!img_cache[i].valid) {
            lru_slot = i;
            break;
        }
        if (img_cache[i].last_used < oldest_time) {
            oldest_time = img_cache[i].last_used;
            lru_slot = i;
        }
    }

    if (img_cache[lru_slot].valid) {
        image_free(&img_cache[lru_slot].img);
    }

    /* Store with url_or_path key so image_cache_get_or_load(src, ...) hits immediately */
    strncpy(img_cache[lru_slot].url, url_or_path, sizeof(img_cache[lru_slot].url) - 1);
    img_cache[lru_slot].url[sizeof(img_cache[lru_slot].url) - 1] = '\0';
    img_cache[lru_slot].img = new_img;
    img_cache[lru_slot].last_used = cache_clock;
    img_cache[lru_slot].valid = true;

    return &img_cache[lru_slot].img;
}

/* ============================================================
 * BILINEAR SCALING & FRAMEBUFFER BLITTER
 * ============================================================ */
void image_scale_blit(const decoded_image_t *img, int dst_x, int dst_y, int dst_w, int dst_h,
                      int clip_x, int clip_y, int clip_w, int clip_h) {
    if (!img || !img->pixels || dst_w <= 0 || dst_h <= 0) return;

    int src_w = img->width;
    int src_h = img->height;

    for (int dy = 0; dy < dst_h; dy++) {
        int py = dst_y + dy;
        if (py < clip_y || py >= clip_y + clip_h) continue;

        int src_y = (dy * src_h) / dst_h;
        if (src_y >= src_h) src_y = src_h - 1;
        const uint8_t *src_row = img->pixels + src_y * src_w * 3;

        for (int dx = 0; dx < dst_w; dx++) {
            int px = dst_x + dx;
            if (px < clip_x || px >= clip_x + clip_w) continue;

            int src_x = (dx * src_w) / dst_w;
            if (src_x >= src_w) src_x = src_w - 1;

            uint8_t r = src_row[src_x * 3 + 0];
            uint8_t g = src_row[src_x * 3 + 1];
            uint8_t b = src_row[src_x * 3 + 2];

            uint8_t vga_color = rgb_to_vga(r, g, b);
            draw_pixel_fb(px, py, vga_color);
        }
    }
}
