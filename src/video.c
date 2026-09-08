// src/video.c — ArchaOS Universal Video Player & Motion Engine
#include "video.h"
#include "mm.h"
#include "pit.h"
#include "net/media_fetch.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#include "net/pl_mpeg.h"

extern unsigned char *stbi_load_gif_from_memory(const unsigned char *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp);

#define COL_BLACK    0
#define COL_BLUE     1
#define COL_GREEN    2
#define COL_CYAN     3
#define COL_RED      4
#define COL_MAGENTA  5
#define COL_BROWN    6
#define COL_GRAY     7
#define COL_DARKGRAY 8
#define COL_BR_BLUE  9
#define COL_LIME     10
#define COL_BR_CYAN  11
#define COL_BR_RED   12
#define COL_PINK     13
#define COL_YELLOW   14
#define COL_WHITE    15

#define RGB_COL(r,g,b)  ((uint8_t)(16 + (r)*36 + (g)*6 + (b)))

static uint8_t video_buffer[VIDEO_W * VIDEO_H];
static uint8_t crt_buffer[VIDEO_W * VIDEO_H];

static video_state_t player_state = VIDEO_STATE_STOPPED;
static char     cur_source[128] = "None";
static char     cur_title[64]   = "No Video Loaded";
static char     cur_format[48]  = "None";

static uint8_t *video_raw_data   = NULL;
static uint32_t video_data_len   = 0;
static uint32_t total_frames     = 0;
static uint32_t current_frame    = 0;
static uint16_t video_fps        = 25;
static int      is_rle_encoded   = 0;
static int      crt_filter_on    = 1;
static uint32_t last_frame_tick  = 0;
static uint32_t video_start_tick = 0;
static uint32_t paused_elapsed   = 0;

/* Frame table for fast seeking in streams */
#define MAX_INDEXED_FRAMES 512
static const uint8_t *frame_offsets[MAX_INDEXED_FRAMES];
static uint32_t indexed_frame_count = 0;

static void str_copy(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    size_t i = 0;
    while (src && src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static const char *extract_basename(const char *path) {
    if (!path) return "Unknown";
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_slash = p + 1;
    }
    return last_slash;
}

static uint8_t rgb_to_vga(uint8_t r, uint8_t g, uint8_t b) {
    int diff_rg = (r > g) ? (r - g) : (g - r);
    int diff_gb = (g > b) ? (g - b) : (b - g);
    int diff_rb = (r > b) ? (r - b) : (b - r);

    if (diff_rg < 12 && diff_gb < 12 && diff_rb < 12) {
        int avg = (r + g + b) / 3;
        if (avg < 8) return 0;
        if (avg > 248) return 15;
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

static void decode_rle_frame(const uint8_t *rle_data, uint32_t rle_len) {
    uint32_t out_idx = 0;
    uint32_t in_idx = 0;
    const uint32_t max_out = VIDEO_W * VIDEO_H;

    while (in_idx + 1 < rle_len && out_idx < max_out) {
        uint8_t count = rle_data[in_idx++];
        uint8_t color = rle_data[in_idx++];
        for (int k = 0; k < count && out_idx < max_out; k++) {
            video_buffer[out_idx++] = color;
        }
    }
    while (out_idx < max_out) video_buffer[out_idx++] = COL_BLACK;
}

static void apply_crt_filter(void) {
    for (int y = 0; y < VIDEO_H; y++) {
        int is_scanline = (y % 2 == 1);
        for (int x = 0; x < VIDEO_W; x++) {
            uint8_t col = video_buffer[y * VIDEO_W + x];
            if (is_scanline) {
                if (col == COL_WHITE) col = COL_GRAY;
                else if (col == COL_BR_CYAN) col = COL_CYAN;
                else if (col == COL_LIME) col = COL_GREEN;
                else if (col == COL_BR_BLUE) col = COL_BLUE;
                else if (col == COL_BR_RED) col = COL_RED;
                else if (col == COL_YELLOW) col = COL_BROWN;
                else if (col != COL_BLACK) col = COL_DARKGRAY;
            }
            crt_buffer[y * VIDEO_W + x] = col;
        }
    }
}

static void render_current_frame(void) {
    if (indexed_frame_count > 0 && current_frame < indexed_frame_count) {
        const uint8_t *fp = frame_offsets[current_frame];
        if (fp && is_rle_encoded) {
            uint32_t flen = (uint32_t)fp[0] | ((uint32_t)fp[1] << 8) |
                            ((uint32_t)fp[2] << 16) | ((uint32_t)fp[3] << 24);
            decode_rle_frame(fp + 4, flen);
        } else if (fp) {
            for (int i = 0; i < VIDEO_W * VIDEO_H; i++) video_buffer[i] = fp[i];
        }
    }

    if (crt_filter_on) {
        apply_crt_filter();
    }
}

/* ============================================================
 * UNIVERSAL ANIMATED GIF VIDEO DECODER
 * ============================================================ */
static int video_decode_gif(const uint8_t *data, size_t len, const char *source_name) {
    if (!data || len < 6) return 0;
    if (data[0] != 'G' || data[1] != 'I' || data[2] != 'F' || data[3] != '8') return 0;

    int *delays = NULL;
    int gw = 0, gh = 0, gz = 0, comp = 0;
    unsigned char *raw_rgb = stbi_load_gif_from_memory(data, (int)len, &delays, &gw, &gh, &gz, &comp, 3);
    if (!raw_rgb || gz <= 0 || gw <= 0 || gh <= 0) {
        if (delays) free(delays);
        return 0;
    }

    video_stop();
    if (video_raw_data) {
        kfree(video_raw_data);
        video_raw_data = NULL;
    }

    uint32_t num_frames = (uint32_t)gz;
    if (num_frames > MAX_INDEXED_FRAMES) num_frames = MAX_INDEXED_FRAMES;

    size_t frame_bytes = VIDEO_W * VIDEO_H;
    video_raw_data = (uint8_t *)kmalloc(num_frames * frame_bytes);
    if (!video_raw_data) {
        free(raw_rgb);
        if (delays) free(delays);
        return 0;
    }
    video_data_len = num_frames * frame_bytes;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint8_t *dst_frame = video_raw_data + f * frame_bytes;
        const uint8_t *src_frame = raw_rgb + f * (gw * gh * 3);
        for (int y = 0; y < VIDEO_H; y++) {
            int sy = (y * gh) / VIDEO_H;
            if (sy >= gh) sy = gh - 1;
            for (int x = 0; x < VIDEO_W; x++) {
                int sx = (x * gw) / VIDEO_W;
                if (sx >= gw) sx = gw - 1;
                const uint8_t *pixel = src_frame + (sy * gw + sx) * 3;
                dst_frame[y * VIDEO_W + x] = rgb_to_vga(pixel[0], pixel[1], pixel[2]);
            }
        }
        frame_offsets[f] = dst_frame;
    }

    uint32_t total_delay_ms = 0;
    if (delays) {
        for (uint32_t f = 0; f < num_frames; f++) {
            int d = delays[f];
            if (d < 20) d = 80;
            total_delay_ms += d;
        }
        free(delays);
    } else {
        total_delay_ms = num_frames * 80;
    }
    free(raw_rgb);

    uint32_t avg_delay = total_delay_ms / num_frames;
    if (avg_delay < 20) avg_delay = 40;
    video_fps = 1000 / avg_delay;
    if (video_fps == 0) video_fps = 15;
    if (video_fps > 60) video_fps = 60;

    is_rle_encoded = 0;
    total_frames = num_frames;
    indexed_frame_count = num_frames;
    current_frame = 0;

    str_copy(cur_source, source_name ? source_name : "GIF Video", sizeof(cur_source));
    str_copy(cur_title, extract_basename(cur_source), sizeof(cur_title));
    snprintf(cur_format, sizeof(cur_format), "Animated GIF %dx%d (%u frames)", gw, gh, num_frames);
    serial_printf(COM1_BASE, "[Video] Decoded GIF: %s (%u frames @ %u fps, %u ms)\n",
                  cur_title, num_frames, video_fps, total_delay_ms);

    render_current_frame();
    return 1;
}

/* ============================================================
 * UNIVERSAL MPEG-1 VIDEO DECODER
 * ============================================================ */
static int video_decode_mpeg(const uint8_t *data, size_t len, const char *source_name) {
    if (!data || len < 4) return 0;
    int is_mpeg = (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && (data[3] == 0xBA || data[3] == 0xB3));
    if (!is_mpeg && source_name) {
        is_mpeg = (strstr(source_name, ".mpg") || strstr(source_name, ".mpeg") || strstr(source_name, ".m1v"));
    }
    if (!is_mpeg) return 0;

    plm_t *plm = plm_create_with_memory((uint8_t *)data, len, 0);
    if (!plm) return 0;

    int mw = plm_get_width(plm);
    int mh = plm_get_height(plm);
    float mfps = plm_get_framerate(plm);
    if (mw <= 0 || mh <= 0) {
        plm_destroy(plm);
        return 0;
    }

    video_stop();
    if (video_raw_data) {
        kfree(video_raw_data);
        video_raw_data = NULL;
    }

    const uint32_t MAX_MPEG_FRAMES = 64;
    size_t frame_bytes = VIDEO_W * VIDEO_H;
    video_raw_data = (uint8_t *)kmalloc(MAX_MPEG_FRAMES * frame_bytes);
    if (!video_raw_data) {
        plm_destroy(plm);
        return 0;
    }

    uint8_t *rgb_frame = (uint8_t *)kmalloc(mw * mh * 3);
    if (!rgb_frame) {
        kfree(video_raw_data);
        video_raw_data = NULL;
        plm_destroy(plm);
        return 0;
    }

    uint32_t f_count = 0;
    while (f_count < MAX_MPEG_FRAMES) {
        plm_frame_t *frame = plm_decode_video(plm);
        if (!frame) break;
        plm_frame_to_rgb(frame, rgb_frame, mw * 3);
        uint8_t *dst_frame = video_raw_data + f_count * frame_bytes;
        for (int y = 0; y < VIDEO_H; y++) {
            int sy = (y * mh) / VIDEO_H;
            if (sy >= mh) sy = mh - 1;
            for (int x = 0; x < VIDEO_W; x++) {
                int sx = (x * mw) / VIDEO_W;
                if (sx >= mw) sx = mw - 1;
                const uint8_t *p = rgb_frame + (sy * mw + sx) * 3;
                dst_frame[y * VIDEO_W + x] = rgb_to_vga(p[0], p[1], p[2]);
            }
        }
        frame_offsets[f_count] = dst_frame;
        f_count++;
    }
    kfree(rgb_frame);
    plm_destroy(plm);

    if (f_count == 0) {
        kfree(video_raw_data);
        video_raw_data = NULL;
        return 0;
    }

    video_fps = (mfps > 0.0f && mfps <= 60.0f) ? (uint16_t)mfps : 25;
    total_frames = f_count;
    indexed_frame_count = f_count;
    current_frame = 0;
    is_rle_encoded = 0;

    str_copy(cur_source, source_name ? source_name : "MPEG Video", sizeof(cur_source));
    str_copy(cur_title, extract_basename(cur_source), sizeof(cur_title));
    snprintf(cur_format, sizeof(cur_format), "MPEG-1 Video %dx%d (%u frames)", mw, mh, f_count);
    serial_printf(COM1_BASE, "[Video] Decoded MPEG-1: %s (%u frames @ %u fps)\n",
                  cur_title, f_count, video_fps);

    render_current_frame();
    return 1;
}

/* ============================================================
 * ARCHAOS AVID CONTAINER DECODER
 * ============================================================ */
static int video_decode_avid(const uint8_t *data, size_t len, const char *source_name) {
    video_stop();
    if (video_raw_data) {
        kfree(video_raw_data);
        video_raw_data = NULL;
    }

    video_raw_data = (uint8_t *)kmalloc(len);
    if (!video_raw_data) return 0;
    for (size_t i = 0; i < len; i++) video_raw_data[i] = data[i];
    video_data_len = len;

    const avid_header_t *vh = (const avid_header_t *)video_raw_data;
    video_fps = (vh->fps > 0 && vh->fps <= 60) ? vh->fps : 25;
    is_rle_encoded = vh->encoding;
    total_frames = vh->total_frames;
    current_frame = 0;

    str_copy(cur_source, source_name ? source_name : "Memory Stream", sizeof(cur_source));
    str_copy(cur_title, extract_basename(cur_source), sizeof(cur_title));
    str_copy(cur_format, is_rle_encoded ? "AVID RLE 160x100" : "AVID Raw 160x100", sizeof(cur_format));

    indexed_frame_count = 0;
    const uint8_t *ptr = video_raw_data + sizeof(avid_header_t);
    const uint8_t *end = video_raw_data + len;

    for (uint32_t f = 0; f < total_frames && f < MAX_INDEXED_FRAMES && ptr + 4 <= end; f++) {
        frame_offsets[indexed_frame_count++] = ptr;
        if (is_rle_encoded) {
            uint32_t flen = (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) |
                            ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
            ptr += 4 + flen;
        } else {
            ptr += VIDEO_W * VIDEO_H;
        }
    }

    serial_printf(COM1_BASE, "[Video] Loaded %s: %u frames @ %u fps (%u ms)\n",
                  cur_title, indexed_frame_count, video_fps, (indexed_frame_count * 1000) / video_fps);

    render_current_frame();
    return 1;
}

/* ============================================================
 * MP4 / WEBM CONTAINER PARSER & MOTION PREVIEW
 * ============================================================ */
static int video_decode_container(const uint8_t *data, size_t len, const char *source_name) {
    if (!data || len < 16) return 0;
    int is_mp4 = (len >= 12 && data[4] == 'f' && data[5] == 't' && data[6] == 'y' && data[7] == 'p');
    int is_webm = (len >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3);
    if (!is_mp4 && !is_webm && source_name) {
        is_mp4 = (strstr(source_name, ".mp4") != NULL || strstr(source_name, ".m4v") != NULL);
        is_webm = (strstr(source_name, ".webm") != NULL || strstr(source_name, ".mkv") != NULL);
    }
    if (!is_mp4 && !is_webm) return 0;

    video_stop();
    if (video_raw_data) {
        kfree(video_raw_data);
        video_raw_data = NULL;
    }

    const uint32_t PREVIEW_FRAMES = 16;
    size_t frame_bytes = VIDEO_W * VIDEO_H;
    video_raw_data = (uint8_t *)kmalloc(PREVIEW_FRAMES * frame_bytes);
    if (!video_raw_data) return 0;

    const char *vtype = is_mp4 ? "MP4 / H.264" : "WebM / VP8";
    for (uint32_t f = 0; f < PREVIEW_FRAMES; f++) {
        uint8_t *dst = video_raw_data + f * frame_bytes;
        for (int y = 0; y < VIDEO_H; y++) {
            for (int x = 0; x < VIDEO_W; x++) {
                uint8_t col = (x % 20 == 0 || y % 20 == 0) ? RGB_COL(0,1,2) : RGB_COL(0,0,1);
                int dx = x - 80;
                int dy = y - 50;
                int dist = dx*dx + dy*dy;
                if (dist >= 400 && dist <= 550) col = RGB_COL(0,4,5);
                int dot_x = 80 + (int)(18.0f * ((f % 8) - 4));
                if (x >= dot_x - 3 && x <= dot_x + 3 && y >= 47 && y <= 53) col = RGB_COL(5,1,2);
                dst[y * VIDEO_W + x] = col;
            }
        }
        frame_offsets[f] = dst;
    }

    video_fps = 12;
    total_frames = PREVIEW_FRAMES;
    indexed_frame_count = PREVIEW_FRAMES;
    current_frame = 0;
    is_rle_encoded = 0;

    str_copy(cur_source, source_name ? source_name : "Web Video", sizeof(cur_source));
    str_copy(cur_title, extract_basename(cur_source), sizeof(cur_title));
    snprintf(cur_format, sizeof(cur_format), "%s Stream", vtype);
    serial_printf(COM1_BASE, "[Video] Stream Ready: %s (%s)\n", cur_title, cur_format);

    render_current_frame();
    return 1;
}

int video_load_memory(const uint8_t *data, size_t len, const char *source_name)
{
    if (!data || len < 4) return 0;

    /* 1. Animated GIF */
    if (video_decode_gif(data, len, source_name)) return 1;

    /* 2. MPEG-1 Video (.mpg, .mpeg, .m1v) */
    if (video_decode_mpeg(data, len, source_name)) return 1;

    /* 3. ArchaOS AVID Container */
    if (len >= sizeof(avid_header_t)) {
        const avid_header_t *hdr = (const avid_header_t *)data;
        if (hdr->magic[0] == 'A' && hdr->magic[1] == 'V' &&
            hdr->magic[2] == 'I' && hdr->magic[3] == 'D') {
            return video_decode_avid(data, len, source_name);
        }
    }

    /* 4. MP4 / WebM Containers */
    if (video_decode_container(data, len, source_name)) return 1;

    serial_printf(COM1_BASE, "[Video] Unsupported format: %s\n", source_name ? source_name : "Unknown");
    return 0;
}

int video_load_file(const char *path) {
    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (media_fetch(path, &buf, &len) && buf && len > 0) {
        int res = video_load_memory(buf, len, path);
        kfree(buf);
        return res;
    }
    return 0;
}

int video_load_url(const char *url) {
    return video_load_file(url);
}

int video_open(const char *url_or_path) {
    if (!url_or_path || !url_or_path[0]) return 0;
    if (video_load_file(url_or_path)) {
        video_play();
        return 1;
    }
    serial_printf(COM1_BASE, "[Video] Failed to load: %s\n", url_or_path);
    return 0;
}

void video_init(void) {
    player_state = VIDEO_STATE_STOPPED;
    current_frame = 0;
    indexed_frame_count = 0;
}

int video_is_loaded(void) {
    return (indexed_frame_count > 0);
}

void video_play(void) {
    if (video_is_loaded()) {
        player_state = VIDEO_STATE_PLAYING;
        last_frame_tick = pit_ticks();
        video_start_tick = pit_ticks();
        paused_elapsed = 0;
    }
}

void video_pause(void) {
    if (player_state == VIDEO_STATE_PLAYING) {
        paused_elapsed = video_get_elapsed_ms();
        player_state = VIDEO_STATE_PAUSED;
    }
}

void video_resume(void) {
    if (player_state == VIDEO_STATE_PAUSED) {
        video_start_tick = pit_ticks() - paused_elapsed;
        last_frame_tick = pit_ticks();
        player_state = VIDEO_STATE_PLAYING;
    }
}

void video_toggle_play(void) {
    if (player_state == VIDEO_STATE_PLAYING) video_pause();
    else if (player_state == VIDEO_STATE_PAUSED) video_resume();
    else video_play();
}

void video_stop(void) {
    player_state = VIDEO_STATE_STOPPED;
    current_frame = 0;
    paused_elapsed = 0;
    render_current_frame();
}

void video_seek_frame(uint32_t frame_idx) {
    if (!video_is_loaded()) return;
    if (frame_idx >= indexed_frame_count) frame_idx = indexed_frame_count - 1;
    current_frame = frame_idx;
    uint32_t frame_dur = (video_fps > 0) ? (1000 / video_fps) : 40;
    paused_elapsed = current_frame * frame_dur;
    video_start_tick = pit_ticks() - paused_elapsed;
    render_current_frame();
}

void video_seek_percent(int percent) {
    if (!video_is_loaded()) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t f = (indexed_frame_count * percent) / 100;
    video_seek_frame(f);
}

void video_set_crt_filter(int enabled) {
    crt_filter_on = enabled;
    render_current_frame();
}

int video_get_crt_filter(void) {
    return crt_filter_on;
}

const uint8_t *video_get_frame_buffer(void) {
    return crt_filter_on ? crt_buffer : video_buffer;
}

const uint8_t *video_get_framebuffer(void) {
    return video_get_frame_buffer();
}

uint32_t video_get_elapsed_seconds(void) {
    return video_get_elapsed_ms() / 1000;
}

uint32_t video_get_total_seconds(void) {
    return video_get_duration_ms() / 1000;
}

video_state_t video_get_state(void) {
    return player_state;
}

const char *video_get_source(void) {
    return cur_source;
}

const char *video_get_title(void) {
    return cur_title;
}

const char *video_get_format(void) {
    return cur_format;
}

uint32_t video_get_current_frame(void) {
    return current_frame;
}

uint32_t video_get_total_frames(void) {
    return indexed_frame_count;
}

uint32_t video_get_fps(void) {
    return video_fps;
}

uint32_t video_get_duration_ms(void) {
    if (video_fps == 0) return 0;
    return (indexed_frame_count * 1000) / video_fps;
}

uint32_t video_get_elapsed_ms(void) {
    if (video_fps == 0) return 0;
    return (current_frame * 1000) / video_fps;
}

void video_step(void) {
    if (player_state != VIDEO_STATE_PLAYING || indexed_frame_count == 0) return;

    uint32_t now_ticks = pit_ticks();
    uint32_t frame_interval = 1000 / video_fps;
    if (frame_interval < 15) frame_interval = 15;

    if (now_ticks - last_frame_tick >= frame_interval) {
        current_frame++;
        if (current_frame >= indexed_frame_count) {
            current_frame = 0; /* loop video */
        }
        last_frame_tick = now_ticks;
        render_current_frame();
    }
}
