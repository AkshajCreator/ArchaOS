// src/audio.h — ArchaOS Universal Audio Engine & Synthesizer
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    AUDIO_STATE_STOPPED = 0,
    AUDIO_STATE_PLAYING = 1,
    AUDIO_STATE_PAUSED  = 2
} audio_state_t;

/* Standard RIFF WAV Header */
typedef struct __attribute__((packed)) {
    char     riff_id[4];        /* "RIFF" */
    uint32_t riff_size;
    char     wave_id[4];        /* "WAVE" */
    char     fmt_id[4];         /* "fmt " */
    uint32_t fmt_size;          /* 16 for PCM */
    uint16_t audio_format;      /* 1 = PCM */
    uint16_t num_channels;      /* 1 = Mono, 2 = Stereo */
    uint32_t sample_rate;       /* e.g. 8000, 22050, 44100 */
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;   /* 8 or 16 */
    char     data_id[4];        /* "data" */
    uint32_t data_size;
} wav_header_t;

/* Universal Audio API */
void          audio_init(void);
void          audio_step(uint32_t now_ticks);

/* Loading arbitrary media from memory, VFS, or URL */
int           audio_load_memory(const uint8_t *data, size_t len, const char *source_name);
int           audio_load_file(const char *path);
int           audio_load_url(const char *url);
int           audio_open(const char *url_or_path);

/* Playback Controls */
void          audio_play(void);
void          audio_pause(void);
void          audio_resume(void);
void          audio_toggle_play(void);
void          audio_stop(void);
void          audio_seek_percent(int percent);
void          audio_seek_ms(uint32_t target_ms);

/* Volume & Hardware */
void          audio_set_volume(int vol_percent);
int           audio_get_volume(void);
void          audio_play_freq(uint32_t freq_hz);

/* Metadata & Status */
audio_state_t audio_get_state(void);
const char   *audio_get_source(void);
const char   *audio_get_title(void);
const char   *audio_get_format(void);
uint32_t      audio_get_duration_ms(void);
uint32_t      audio_get_elapsed_ms(void);
int           audio_is_loaded(void);

/* Real-Time Visualizer Engine (computed directly from current audio buffer) */
void          audio_get_spectrum(uint8_t *bars, int num_bars);
void          audio_get_waveform(int8_t *samples, int num_samples);

int           audio_parse_wav(const uint8_t *data, size_t len, wav_header_t *out_hdr, const uint8_t **out_pcm_data);

#endif
