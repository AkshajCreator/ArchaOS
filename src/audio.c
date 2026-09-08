// src/audio.c — ArchaOS Universal Audio Engine & Synthesizer
#include "audio.h"
#include "pit.h"
#include "mm.h"
#include "net/media_fetch.h"
#include "net/stb_vorbis.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "net/minimp3.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Speaker Hardware Control */
static uint8_t  speaker_active = 0;
static uint32_t current_freq   = 0;
static int      sb16_detected  = -1; /* -1 = untested, 0 = no, 1 = yes */

static void sb16_init_check(void) {
    if (sb16_detected != -1) return;
    /* Sound Blaster 16 DSP reset test at base port 0x220 */
    outb(0x226, 1);
    pit_sleep(2);
    outb(0x226, 0);
    pit_sleep(2);
    uint8_t ack = inb(0x22A);
    if (ack == 0xAA) {
        sb16_detected = 1;
        serial_puts(COM1_BASE, "[Audio] Sound Blaster 16 DSP detected at 0x220\n");
    } else {
        sb16_detected = 0;
        serial_puts(COM1_BASE, "[Audio] PC Speaker Universal Synthesis Active\n");
    }
}

void audio_play_freq(uint32_t freq_hz) {
    if (freq_hz == 0) {
        audio_stop();
        return;
    }
    current_freq = freq_hz;
    uint32_t div = 1193180 / freq_hz;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    if ((tmp & 3) != 3) {
        outb(0x61, tmp | 3);
    }
    speaker_active = 1;
}

static uint32_t last_dac_idx = 0;

void audio_stop(void) {
    if (speaker_active) {
        outb(0x61, inb(0x61) & ~3);
        speaker_active = 0;
    }
    current_freq = 0;
    last_dac_idx = 0;
}

/* ============================================================
 * UNIVERSAL AUDIO STATE
 * ============================================================ */

static audio_state_t player_state = AUDIO_STATE_STOPPED;
static char     cur_source[128] = "None";
static char     cur_title[64]   = "No Media Loaded";
static char     cur_format[48]  = "None";
static uint8_t *pcm_samples     = NULL;
static uint32_t pcm_sample_count = 0;
static uint32_t pcm_sample_rate  = 8000;
static uint32_t current_sample_idx = 0;
static uint32_t total_duration_ms  = 0;
static uint32_t track_start_tick   = 0;
static uint32_t paused_elapsed_ms  = 0;
static int      volume_level       = 80; /* 0 to 100% */

/* Real-time Visualizer State */
static uint8_t spec_levels[16] = {0};
static uint8_t spec_peaks[16]  = {0};

/* String helpers */
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

/* ============================================================
 * WAV FILE PARSER
 * ============================================================ */

int audio_parse_wav(const uint8_t *data, size_t len, wav_header_t *out_hdr, const uint8_t **out_pcm_data) {
    if (!data || len < 44 || !out_hdr) return 0;

    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') return 0;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') return 0;

    /* Scan subchunks for fmt and data */
    size_t offset = 12;
    const uint8_t *fmt_chunk = NULL;
    const uint8_t *data_chunk = NULL;
    uint32_t data_bytes = 0;

    while (offset + 8 <= len) {
        char chunk_id[5];
        chunk_id[0] = data[offset];
        chunk_id[1] = data[offset+1];
        chunk_id[2] = data[offset+2];
        chunk_id[3] = data[offset+3];
        chunk_id[4] = '\0';

        uint32_t chunk_sz = (uint32_t)data[offset+4] |
                           ((uint32_t)data[offset+5] << 8) |
                           ((uint32_t)data[offset+6] << 16) |
                           ((uint32_t)data[offset+7] << 24);

        if (chunk_id[0] == 'f' && chunk_id[1] == 'm' && chunk_id[2] == 't' && chunk_id[3] == ' ') {
            fmt_chunk = data + offset + 8;
        } else if (chunk_id[0] == 'd' && chunk_id[1] == 'a' && chunk_id[2] == 't' && chunk_id[3] == 'a') {
            data_chunk = data + offset + 8;
            data_bytes = chunk_sz;
        }

        offset += 8 + chunk_sz;
    }

    if (!fmt_chunk || !data_chunk) return 0;

    out_hdr->audio_format = (uint16_t)fmt_chunk[0] | ((uint16_t)fmt_chunk[1] << 8);
    out_hdr->num_channels = (uint16_t)fmt_chunk[2] | ((uint16_t)fmt_chunk[3] << 8);
    out_hdr->sample_rate  = (uint32_t)fmt_chunk[4] | ((uint32_t)fmt_chunk[5] << 8) |
                            ((uint32_t)fmt_chunk[6] << 16) | ((uint32_t)fmt_chunk[7] << 24);
    out_hdr->bits_per_sample = (uint16_t)fmt_chunk[14] | ((uint16_t)fmt_chunk[15] << 8);
    out_hdr->data_size = data_bytes;

    if (out_pcm_data) *out_pcm_data = data_chunk;
    return 1;
}

/* ============================================================
 * UNIVERSAL MEDIA LOADER (WAV, OGG Vorbis, AU, RAW PCM)
 * ============================================================ */

static void format_snprintf(char *buf, size_t sz, const char *fmt_name, uint32_t rate, int ch, uint32_t kb) {
    (void)kb;
    /* Simple formatter without heavy dependencies */
    char r_str[16];
    int r_len = 0;
    uint32_t r_tmp = rate;
    while (r_tmp > 0) { r_str[r_len++] = '0' + (r_tmp % 10); r_tmp /= 10; }
    for (int i = 0; i < r_len / 2; i++) { char c = r_str[i]; r_str[i] = r_str[r_len - 1 - i]; r_str[r_len - 1 - i] = c; }
    r_str[r_len] = '\0';

    size_t pos = 0;
    while (*fmt_name && pos < sz - 1) buf[pos++] = *fmt_name++;
    if (pos < sz - 2) { buf[pos++] = ' '; }
    for (int i = 0; r_str[i] && pos < sz - 1; i++) buf[pos++] = r_str[i];
    if (pos < sz - 4) { buf[pos++] = 'H'; buf[pos++] = 'z'; buf[pos++] = ' '; }
    if (ch == 1 && pos < sz - 5) { buf[pos++] = 'M'; buf[pos++] = 'o'; buf[pos++] = 'n'; buf[pos++] = 'o'; buf[pos++] = ' '; }
    else if (pos < sz - 7) { buf[pos++] = 'S'; buf[pos++] = 't'; buf[pos++] = 'e'; buf[pos++] = 'r'; buf[pos++] = 'e'; buf[pos++] = 'o'; buf[pos++] = ' '; }
    buf[pos] = '\0';
}

int audio_load_memory(const uint8_t *data, size_t len, const char *source_name)
{
    if (!data || len == 0) return 0;
    sb16_init_check();

    audio_stop();
    if (pcm_samples) {
        kfree(pcm_samples);
        pcm_samples = NULL;
    }
    pcm_sample_count = 0;
    current_sample_idx = 0;

    str_copy(cur_source, source_name ? source_name : "Memory Buffer", sizeof(cur_source));
    str_copy(cur_title, extract_basename(cur_source), sizeof(cur_title));

    /* 1. Check for OGG Vorbis (Magic: OggS)
     * Use frame-by-frame streaming decode with a static pre-allocated setup buffer.
     * stb_vorbis normally uses alloca() for temp buffers during frame decode which
     * can overflow the kernel stack (no guard pages). Passing stb_vorbis_alloc tells
     * it to use our buffer instead, keeping all memory usage deterministic. */
    if (len >= 4 && data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S') {
        /* Static 256KB setup/temp buffer — enough for any standard Vorbis file */
        static char vorbis_setup_buf[256 * 1024];
        stb_vorbis_alloc alloc;
        alloc.alloc_buffer = vorbis_setup_buf;
        alloc.alloc_buffer_length_in_bytes = sizeof(vorbis_setup_buf);

        int error = 0;
        stb_vorbis *v = stb_vorbis_open_memory(data, (int)len, &error, &alloc);
        if (v) {
            stb_vorbis_info info = stb_vorbis_get_info(v);
            int channels    = info.channels    > 0 ? info.channels    : 1;
            int sample_rate = info.sample_rate > 0 ? (int)info.sample_rate : 8000;

            /* Allocate PCM output buffer: 512KB mono 8-bit = ~11s at 44100Hz or 64s at 8kHz */
            const uint32_t MAX_PCM = 512 * 1024;
            pcm_samples = (uint8_t *)kmalloc(MAX_PCM);
            if (!pcm_samples) {
                stb_vorbis_close(v);
                serial_printf(COM1_BASE, "[Audio] OGG: out of memory for PCM buffer\n");
                return 0;
            }

            /* Static frame buffer: 4096 samples * 2 channels * 2 bytes = 16KB */
            static short frame_buf[4096 * 2];
            uint32_t out_count = 0;

            for (;;) {
                int n = stb_vorbis_get_frame_short_interleaved(v, channels,
                            frame_buf, 4096 * channels);
                if (n <= 0) break;

                /* Convert interleaved 16-bit shorts -> 8-bit unsigned mono PCM */
                for (int i = 0; i < n && out_count < MAX_PCM; i++) {
                    int s;
                    if (channels == 1) {
                        s = frame_buf[i];
                    } else {
                        s = ((int)frame_buf[i * 2] + (int)frame_buf[i * 2 + 1]) / 2;
                    }
                    pcm_samples[out_count++] = (uint8_t)((s >> 8) + 128);
                }
                if (out_count >= MAX_PCM) break;
            }
            stb_vorbis_close(v);

            if (out_count > 0) {
                pcm_sample_count  = out_count;
                pcm_sample_rate   = (uint32_t)sample_rate;
                total_duration_ms = (pcm_sample_count * 1000) / pcm_sample_rate;
                format_snprintf(cur_format, sizeof(cur_format), "OGG Vorbis",
                                pcm_sample_rate, channels, (uint32_t)(len / 1024));
                serial_printf(COM1_BASE, "[Audio] OGG decoded: %u samples @ %u Hz, %u ms, %dch\n",
                              pcm_sample_count, pcm_sample_rate, total_duration_ms, channels);
                return 1;
            }

            kfree(pcm_samples);
            pcm_samples = NULL;
            serial_printf(COM1_BASE, "[Audio] OGG: decoded 0 samples (setup err=%d)\n", error);
        } else {
            serial_printf(COM1_BASE, "[Audio] OGG: stb_vorbis_open_memory failed err=%d\n", error);
        }
        return 0;  /* OggS detected but decode failed — do not treat as raw PCM */
    }

    /* 2. Check for MP3 / MPEG Audio */
    int is_mp3 = (len >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3');
    if (!is_mp3) {
        for (size_t i = 0; i + 2 < len && i < 1024; i++) {
            if (data[i] == 0xFF && ((data[i+1] & 0xE0) == 0xE0)) {
                is_mp3 = 1;
                break;
            }
        }
    }
    if (is_mp3 || (source_name && (str_find(source_name, ".mp3") || str_find(source_name, ".MP3")))) {
        static mp3dec_t mp3;
        mp3dec_init(&mp3);
        mp3dec_frame_info_t info;
        static short pcm_frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

        const uint32_t MAX_PCM = 512 * 1024;
        pcm_samples = (uint8_t *)kmalloc(MAX_PCM);
        if (pcm_samples) {
            const uint8_t *input = data;
            int input_left = (int)len;
            uint32_t out_count = 0;
            int sample_rate = 44100;
            int channels = 2;

            while (input_left > 0 && out_count < MAX_PCM) {
                int samples = mp3dec_decode_frame(&mp3, input, input_left, pcm_frame, &info);
                if (info.frame_bytes <= 0) break;
                input += info.frame_bytes;
                input_left -= info.frame_bytes;
                if (samples > 0) {
                    if (info.hz > 0) sample_rate = info.hz;
                    if (info.channels > 0) channels = info.channels;
                    for (int i = 0; i < samples && out_count < MAX_PCM; i++) {
                        int s;
                        if (channels == 1) {
                            s = pcm_frame[i];
                        } else {
                            s = ((int)pcm_frame[i * 2] + (int)pcm_frame[i * 2 + 1]) / 2;
                        }
                        pcm_samples[out_count++] = (uint8_t)((s >> 8) + 128);
                    }
                }
            }

            if (out_count > 0) {
                pcm_sample_count = out_count;
                pcm_sample_rate = (uint32_t)sample_rate;
                total_duration_ms = (pcm_sample_count * 1000) / pcm_sample_rate;
                format_snprintf(cur_format, sizeof(cur_format), "MP3 Audio", pcm_sample_rate, channels, (uint32_t)(len / 1024));
                serial_printf(COM1_BASE, "[Audio] Decoded MP3: %u samples @ %u Hz (%u ms, %dch)\n",
                              pcm_sample_count, pcm_sample_rate, total_duration_ms, channels);
                return 1;
            }

            kfree(pcm_samples);
            pcm_samples = NULL;
        }
    }

    /* 3. Check for RIFF WAVE PCM */
    wav_header_t wav_hdr;
    const uint8_t *raw_pcm = NULL;
    if (audio_parse_wav(data, len, &wav_hdr, &raw_pcm) && raw_pcm) {
        pcm_sample_rate = (wav_hdr.sample_rate > 0) ? wav_hdr.sample_rate : 8000;
        uint32_t bytes = wav_hdr.data_size;
        if (bytes > len - (raw_pcm - data)) bytes = len - (raw_pcm - data);

        if (wav_hdr.bits_per_sample == 16) {
            uint32_t shorts_count = bytes / 2;
            if (wav_hdr.num_channels == 2) {
                pcm_sample_count = shorts_count / 2;
                pcm_samples = (uint8_t *)kmalloc(pcm_sample_count);
                if (!pcm_samples) return 0;
                const int16_t *s16 = (const int16_t *)raw_pcm;
                for (uint32_t i = 0; i < pcm_sample_count; i++) {
                    int mixed = ((int)s16[i * 2] + (int)s16[i * 2 + 1]) / 2;
                    pcm_samples[i] = (uint8_t)((mixed >> 8) + 128);
                }
            } else {
                pcm_sample_count = shorts_count;
                pcm_samples = (uint8_t *)kmalloc(pcm_sample_count);
                if (!pcm_samples) return 0;
                const int16_t *s16 = (const int16_t *)raw_pcm;
                for (uint32_t i = 0; i < pcm_sample_count; i++) {
                    pcm_samples[i] = (uint8_t)((s16[i] >> 8) + 128);
                }
            }
        } else if (wav_hdr.bits_per_sample == 24) {
            uint32_t total_samples = bytes / 3;
            if (wav_hdr.num_channels == 2) {
                pcm_sample_count = total_samples / 2;
                pcm_samples = (uint8_t *)kmalloc(pcm_sample_count);
                if (!pcm_samples) return 0;
                for (uint32_t i = 0; i < pcm_sample_count; i++) {
                    int32_t s0 = (int32_t)(((uint32_t)raw_pcm[i*6+1] << 16) | ((uint32_t)raw_pcm[i*6+2] << 24));
                    int32_t s1 = (int32_t)(((uint32_t)raw_pcm[i*6+4] << 16) | ((uint32_t)raw_pcm[i*6+5] << 24));
                    int mixed = ((s0 >> 16) + (s1 >> 16)) / 2;
                    pcm_samples[i] = (uint8_t)((mixed >> 8) + 128);
                }
            } else {
                pcm_sample_count = total_samples;
                pcm_samples = (uint8_t *)kmalloc(pcm_sample_count);
                if (!pcm_samples) return 0;
                for (uint32_t i = 0; i < pcm_sample_count; i++) {
                    int32_t s = (int32_t)(((uint32_t)raw_pcm[i*3+1] << 16) | ((uint32_t)raw_pcm[i*3+2] << 24));
                    pcm_samples[i] = (uint8_t)((s >> 24) + 128);
                }
            }
        } else {
            /* 8-bit unsigned PCM */
            if (wav_hdr.num_channels == 2) {
                pcm_sample_count = bytes / 2;
                pcm_samples = (uint8_t *)kmalloc(pcm_sample_count);
                if (!pcm_samples) return 0;
                for (uint32_t i = 0; i < pcm_sample_count; i++) {
                    pcm_samples[i] = (uint8_t)(((int)raw_pcm[i * 2] + (int)raw_pcm[i * 2 + 1]) / 2);
                }
            } else {
                pcm_sample_count = bytes;
                pcm_samples = (uint8_t *)kmalloc(pcm_sample_count);
                if (!pcm_samples) return 0;
                for (uint32_t i = 0; i < pcm_sample_count; i++) {
                    pcm_samples[i] = raw_pcm[i];
                }
            }
        }

        total_duration_ms = (pcm_sample_count * 1000) / pcm_sample_rate;
        format_snprintf(cur_format, sizeof(cur_format), "RIFF WAV", pcm_sample_rate, wav_hdr.num_channels, (uint32_t)(len / 1024));
        serial_printf(COM1_BASE, "[Audio] Decoded WAV: %u samples @ %u Hz (%u ms)\n",
                      pcm_sample_count, pcm_sample_rate, total_duration_ms);
        return 1;
    }

    /* 4. Check for Sun/NeXT AU Audio (.snd) */
    if (len >= 24 && data[0] == '.' && data[1] == 's' && data[2] == 'n' && data[3] == 'd') {
        uint32_t offset = ((uint32_t)data[4]<<24) | ((uint32_t)data[5]<<16) | ((uint32_t)data[6]<<8) | data[7];
        uint32_t dsize  = ((uint32_t)data[8]<<24) | ((uint32_t)data[9]<<16) | ((uint32_t)data[10]<<8) | data[11];
        uint32_t enc    = ((uint32_t)data[12]<<24) | ((uint32_t)data[13]<<16) | ((uint32_t)data[14]<<8) | data[15];
        uint32_t rate   = ((uint32_t)data[16]<<24) | ((uint32_t)data[17]<<16) | ((uint32_t)data[18]<<8) | data[19];
        uint32_t ch     = ((uint32_t)data[20]<<24) | ((uint32_t)data[21]<<16) | ((uint32_t)data[22]<<8) | data[23];
        if (offset < len) {
            if (dsize == 0xFFFFFFFF || offset + dsize > len) dsize = len - offset;
            if (rate == 0) rate = 8000;
            if (ch == 0) ch = 1;

            pcm_samples = (uint8_t *)kmalloc(dsize);
            if (pcm_samples) {
                const uint8_t *payload = data + offset;
                if (enc == 1) {
                    /* 8-bit ISDN u-law */
                    for (uint32_t i = 0; i < dsize; i++) {
                        uint8_t u = ~payload[i];
                        int sign = (u & 0x80) ? -1 : 1;
                        int exponent = (u >> 4) & 0x07;
                        int mantissa = u & 0x0F;
                        int sample = sign * ((1 << (exponent + 3)) * (mantissa + 16) - 132);
                        pcm_samples[i] = (uint8_t)((sample >> 8) + 128);
                    }
                } else if (enc == 2) {
                    /* 8-bit linear signed */
                    for (uint32_t i = 0; i < dsize; i++) {
                        pcm_samples[i] = (uint8_t)((int8_t)payload[i] + 128);
                    }
                } else if (enc == 3) {
                    /* 16-bit linear big endian */
                    uint32_t shorts = dsize / 2;
                    for (uint32_t i = 0; i < shorts; i++) {
                        int16_t s = (int16_t)(((uint16_t)payload[i*2] << 8) | payload[i*2 + 1]);
                        pcm_samples[i] = (uint8_t)((s >> 8) + 128);
                    }
                    dsize = shorts;
                } else {
                    for (uint32_t i = 0; i < dsize; i++) pcm_samples[i] = payload[i];
                }
                pcm_sample_count = dsize;
                pcm_sample_rate = rate;
                total_duration_ms = (pcm_sample_count * 1000) / pcm_sample_rate;
                format_snprintf(cur_format, sizeof(cur_format), "AU Audio", pcm_sample_rate, ch, (uint32_t)(len / 1024));
                serial_printf(COM1_BASE, "[Audio] Decoded AU: %u samples @ %u Hz (%u ms)\n",
                              pcm_sample_count, pcm_sample_rate, total_duration_ms);
                return 1;
            }
        }
    }

    /* 5. Only treat as Raw PCM if user specifically requested a .raw or .pcm file */
    if (source_name && (str_find(source_name, ".raw") || str_find(source_name, ".pcm"))) {
        pcm_sample_rate = 8000;
        pcm_sample_count = len;
        pcm_samples = (uint8_t *)kmalloc(len);
        if (!pcm_samples) return 0;
        for (size_t i = 0; i < len; i++) pcm_samples[i] = data[i];
        total_duration_ms = (pcm_sample_count * 1000) / pcm_sample_rate;
        format_snprintf(cur_format, sizeof(cur_format), "Raw PCM", pcm_sample_rate, 1, (uint32_t)(len / 1024));
        return 1;
    }

    serial_printf(COM1_BASE, "[Audio] Unsupported format for %s (magic: %02X %02X %02X %02X)\n",
                  source_name ? source_name : "Media",
                  data[0], data[1], data[2], data[3]);
    str_copy(cur_format, "Unsupported Format", sizeof(cur_format));
    return 0;
}

int audio_load_file(const char *path) {
    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (media_fetch(path, &buf, &len) && buf && len > 0) {
        int res = audio_load_memory(buf, len, path);
        kfree(buf);
        return res;
    }
    return 0;
}

int audio_load_url(const char *url) {
    return audio_load_file(url);
}

int audio_open(const char *url_or_path) {
    if (!url_or_path || !url_or_path[0]) return 0;

    if (audio_load_file(url_or_path)) {
        audio_play();
        return 1;
    }

    serial_printf(COM1_BASE, "[Audio] Failed to load: %s\n", url_or_path);
    return 0;
}

/* ============================================================
 * PLAYBACK CONTROLS
 * ============================================================ */

void audio_init(void) {
    audio_stop();
    player_state = AUDIO_STATE_STOPPED;
    current_sample_idx = 0;
    sb16_init_check();
}

int audio_is_loaded(void) {
    return (pcm_samples != NULL && pcm_sample_count > 0);
}

void audio_play(void) {
    if (audio_is_loaded()) {
        player_state = AUDIO_STATE_PLAYING;
        track_start_tick = pit_ticks();
        paused_elapsed_ms = 0;
    }
}

void audio_pause(void) {
    if (player_state == AUDIO_STATE_PLAYING) {
        paused_elapsed_ms = audio_get_elapsed_ms();
        audio_stop();
        player_state = AUDIO_STATE_PAUSED;
    }
}

void audio_resume(void) {
    if (player_state == AUDIO_STATE_PAUSED) {
        track_start_tick = pit_ticks() - paused_elapsed_ms;
        player_state = AUDIO_STATE_PLAYING;
    }
}

void audio_toggle_play(void) {
    if (player_state == AUDIO_STATE_PLAYING) audio_pause();
    else if (player_state == AUDIO_STATE_PAUSED) audio_resume();
    else audio_play();
}

void audio_seek_ms(uint32_t target_ms) {
    if (!audio_is_loaded()) return;
    if (target_ms > total_duration_ms) target_ms = total_duration_ms;
    current_sample_idx = (target_ms * pcm_sample_rate) / 1000;
    if (current_sample_idx >= pcm_sample_count) current_sample_idx = pcm_sample_count - 1;
    track_start_tick = pit_ticks() - target_ms;
    paused_elapsed_ms = target_ms;
}

void audio_seek_percent(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t target_ms = (total_duration_ms * percent) / 100;
    audio_seek_ms(target_ms);
}

void audio_set_volume(int vol_percent) {
    if (vol_percent < 0) vol_percent = 0;
    if (vol_percent > 100) vol_percent = 100;
    volume_level = vol_percent;
    if (volume_level == 0) audio_stop();
}

int audio_get_volume(void) {
    return volume_level;
}

audio_state_t audio_get_state(void) {
    return player_state;
}

const char *audio_get_source(void) {
    return cur_source;
}

const char *audio_get_title(void) {
    return cur_title;
}

const char *audio_get_format(void) {
    return cur_format;
}

uint32_t audio_get_duration_ms(void) {
    return total_duration_ms;
}

uint32_t audio_get_elapsed_ms(void) {
    if (player_state == AUDIO_STATE_PLAYING) {
        uint32_t now = pit_ticks();
        if (now >= track_start_tick) {
            uint32_t el = now - track_start_tick;
            return (el > total_duration_ms) ? total_duration_ms : el;
        }
    } else if (player_state == AUDIO_STATE_PAUSED) {
        return paused_elapsed_ms;
    }
    return 0;
}

/* ============================================================
 * AUDIO STEP ENGINE (Speaker Output & Pitch Tracking)
 * ============================================================ */

void audio_step(uint32_t now_ticks) {
    if (player_state != AUDIO_STATE_PLAYING || !pcm_samples || pcm_sample_count == 0) return;

    uint32_t elapsed_ms = now_ticks - track_start_tick;
    current_sample_idx = (elapsed_ms * pcm_sample_rate) / 1000;

    if (current_sample_idx >= pcm_sample_count) {
        audio_stop();
        player_state = AUDIO_STATE_STOPPED;
        current_sample_idx = 0;
        return;
    }

    if (volume_level == 0) {
        audio_stop();
        return;
    }

    /* Sound Blaster 16 Direct 8-bit DAC Output (stream up to 512 elapsed samples) */
    if (sb16_detected == 1) {
        if (current_sample_idx < last_dac_idx) last_dac_idx = 0;
        uint32_t to_write = current_sample_idx - last_dac_idx;
        if (to_write > 512) to_write = 512;
        while (to_write > 0 && last_dac_idx < pcm_sample_count) {
            uint8_t smp = pcm_samples[last_dac_idx++];
            for (int spin = 0; spin < 50 && (inb(0x22C) & 0x80); spin++) {}
            outb(0x22C, 0x10); /* DSP direct DAC command */
            for (int spin = 0; spin < 50 && (inb(0x22C) & 0x80); spin++) {}
            outb(0x22C, smp);
            to_write--;
        }
        last_dac_idx = current_sample_idx;
    }

    /* Pitch & Formant Zero-Crossing Frequency Tracker for PC Speaker */
    const int win = 128;
    uint32_t idx = current_sample_idx;
    if (idx + win > pcm_sample_count) idx = (pcm_sample_count > win) ? pcm_sample_count - win : 0;

    int zc = 0;
    int sum_amp = 0;
    for (int i = 0; i < win - 1 && (idx + i + 1 < pcm_sample_count); i++) {
        int s0 = (int)pcm_samples[idx + i] - 128;
        int s1 = (int)pcm_samples[idx + i + 1] - 128;
        if ((s0 >= 0 && s1 < 0) || (s0 < 0 && s1 >= 0)) zc++;
        if (s0 < 0) s0 = -s0;
        sum_amp += s0;
    }

    int avg_amp = sum_amp / win;
    if (avg_amp < 3) {
        /* Silence or pause in speech */
        audio_stop();
    } else {
        uint32_t freq = (zc * pcm_sample_rate) / (2 * win);
        if (freq < 110) freq = 110;
        if (freq > 3200) freq = 3200;
        audio_play_freq(freq);
    }
}

/* ============================================================
 * REAL-TIME VISUALIZER (Spectrum Analyzer & Waveform)
 * ============================================================ */

void audio_get_spectrum(uint8_t *bars, int num_bars) {
    if (!bars || num_bars <= 0) return;
    if (num_bars > 16) num_bars = 16;

    if (player_state != AUDIO_STATE_PLAYING || !pcm_samples || volume_level == 0) {
        for (int i = 0; i < num_bars; i++) {
            if (spec_levels[i] > 3) spec_levels[i] -= 3; else spec_levels[i] = 0;
            if (spec_peaks[i] > 1) spec_peaks[i] -= 1; else spec_peaks[i] = 0;
            bars[i] = spec_levels[i];
        }
        return;
    }

    /* Compute actual energy across 16 frequency bands from current PCM audio window */
    const int win = 128;
    uint32_t idx = current_sample_idx;
    if (idx + win > pcm_sample_count) idx = (pcm_sample_count > win) ? pcm_sample_count - win : 0;

    int band_size = win / num_bars;
    if (band_size < 2) band_size = 2;

    for (int b = 0; b < num_bars; b++) {
        int sum = 0;
        int start = idx + b * band_size;
        for (int i = 0; i < band_size && (start + i < (int)pcm_sample_count); i++) {
            int val = (int)pcm_samples[start + i] - 128;
            if (val < 0) val = -val;
            sum += val;
        }

        int target = (sum * 90) / (band_size * 64);
        if (target > 95) target = 95;
        target = (target * volume_level) / 100;

        if (spec_levels[b] < target) spec_levels[b] += (target - spec_levels[b]) / 2 + 1;
        else if (spec_levels[b] > target) spec_levels[b] -= 3;

        if (spec_levels[b] > spec_peaks[b]) spec_peaks[b] = spec_levels[b];
        else if (spec_peaks[b] > 0) spec_peaks[b]--;

        bars[b] = spec_levels[b];
    }
}

void audio_get_waveform(int8_t *samples, int num_samples) {
    if (!samples || num_samples <= 0) return;

    if (player_state != AUDIO_STATE_PLAYING || !pcm_samples || volume_level == 0) {
        for (int i = 0; i < num_samples; i++) samples[i] = 0;
        return;
    }

    uint32_t idx = current_sample_idx;
    for (int i = 0; i < num_samples; i++) {
        if (idx + i < pcm_sample_count) {
            int val = ((int)pcm_samples[idx + i] - 128) * volume_level / 100;
            if (val > 127) val = 127;
            if (val < -128) val = -128;
            samples[i] = (int8_t)val;
        } else {
            samples[i] = 0;
        }
    }
}
