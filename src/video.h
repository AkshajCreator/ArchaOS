// src/video.h — ArchaOS Universal Video Player & Motion Engine
#ifndef ARCHA_VIDEO_H
#define ARCHA_VIDEO_H

#include <stdint.h>
#include <stddef.h>

#define VIDEO_W 160
#define VIDEO_H 100

typedef enum {
    VIDEO_STATE_STOPPED = 0,
    VIDEO_STATE_PLAYING = 1,
    VIDEO_STATE_PAUSED  = 2
} video_state_t;

/* Standard ArchaOS Video Container Header */
typedef struct __attribute__((packed)) {
    char     magic[4];       /* "AVID" */
    uint16_t width;          /* 160 */
    uint16_t height;         /* 100 */
    uint16_t fps;            /* 25 */
    uint16_t encoding;       /* 0 = Raw bitmap frames, 1 = RLE frames */
    uint32_t total_frames;
    uint32_t data_size;
} avid_header_t;

void          video_init(void);
void          video_step(void);

/* Universal Loading API */
int           video_load_memory(const uint8_t *data, size_t len, const char *source_name);
int           video_load_file(const char *path);
int           video_load_url(const char *url);
int           video_open(const char *url_or_path);

/* Playback Controls */
void          video_play(void);
void          video_pause(void);
void          video_resume(void);
void          video_toggle_play(void);
void          video_stop(void);
void          video_seek_frame(uint32_t frame_idx);
void          video_seek_percent(int percent);

/* Filters & Display */
void          video_set_crt_filter(int enabled);
int           video_get_crt_filter(void);
const uint8_t*video_get_frame_buffer(void);

/* Metadata & Status */
video_state_t video_get_state(void);
const char   *video_get_source(void);
const char   *video_get_title(void);
const char   *video_get_format(void);
uint32_t      video_get_current_frame(void);
uint32_t      video_get_total_frames(void);
uint32_t      video_get_fps(void);
uint32_t      video_get_duration_ms(void);
uint32_t      video_get_elapsed_ms(void);
int           video_is_loaded(void);

/* Status helpers */
const uint8_t*video_get_framebuffer(void);
uint32_t      video_get_elapsed_seconds(void);
uint32_t      video_get_total_seconds(void);

#endif
