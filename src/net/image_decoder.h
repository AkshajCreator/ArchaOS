#ifndef ARCHAOS_IMAGE_DECODER_H
#define ARCHAOS_IMAGE_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;
    int height;
    int channels; /* 3 = RGB, 4 = RGBA, 1 = Grayscale */
    uint8_t *pixels; /* 24-bit RGB (3 bytes per pixel) or 32-bit RGBA (4 bytes per pixel) */
    bool is_animated;
    int frame_count;
} decoded_image_t;

/* Initialize image decoder subsystem */
void image_decoder_init(void);

/* Universal decoder: automatically detects format (PNG, JPEG, BMP, GIF, PPM, TGA) from magic bytes */
int image_decode(const uint8_t *data, size_t len, decoded_image_t *out_img);

/* Free allocated decoded image buffer */
void image_free(decoded_image_t *img);

/* Image Cache Subsystem */
decoded_image_t *image_cache_get_or_load(const char *url_or_path, const uint8_t *data, size_t len);
void image_cache_clear(void);

/* Universal on-demand network/local image fetcher & cache */
decoded_image_t *image_fetch_and_cache(const char *url_or_path, const char *base_url);

/* Scale and blit decoded image directly to framebuffer */
void image_scale_blit(const decoded_image_t *img, int dst_x, int dst_y, int dst_w, int dst_h,
                      int clip_x, int clip_y, int clip_w, int clip_h);

#ifdef __cplusplus
}
#endif

#endif /* ARCHAOS_IMAGE_DECODER_H */
