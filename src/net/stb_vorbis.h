#ifndef STB_VORBIS_H
#define STB_VORBIS_H
#include <stdint.h>
#include <stddef.h>

/* Full decode (malloc-heavy — use only for small files) */
int stb_vorbis_decode_memory(const uint8_t *mem, int len, int *channels, int *sample_rate, short **output);

/* Streaming / frame-by-frame API — preferred for large files */
typedef struct stb_vorbis stb_vorbis;
typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;
typedef struct {
    char *alloc_buffer;
    int   alloc_buffer_length_in_bytes;
} stb_vorbis_alloc;

stb_vorbis *stb_vorbis_open_memory(const unsigned char *data, int len, int *error, const stb_vorbis_alloc *alloc_buffer);
stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
int  stb_vorbis_get_frame_short_interleaved(stb_vorbis *f, int num_c, short *buffer, int num_shorts);
void stb_vorbis_close(stb_vorbis *f);

#endif
