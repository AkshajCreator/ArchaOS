/*
 * src/font_engine.h — ArchaOS v0.6 'Nexus' Typography & Font Subsystem
 *
 * Provides a modular, high-performance bitmap font engine supporting four
 * distinct typography aesthetics with full ASCII coverage (32 to 126),
 * 32-bit linear framebuffer drawing (regular, scaled, shadowed), and formatted
 * text specimen previewing.
 *
 * Self-contained, freestanding (-m32 compatible, zero libc dependencies).
 */

#ifndef ARCHA_FONT_ENGINE_H
#define ARCHA_FONT_ENGINE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


#define FONT_STYLE_CLASSIC   0  /* 'Classic IBM BIOS/VGA' (clean 8x8 glyphs) */
#define FONT_STYLE_CYBERPUNK 1  /* 'Cyberpunk Neon' (stylized, angular sci-fi cuts) */
#define FONT_STYLE_MODERN    2  /* 'Modern Sans' (smooth humanist sans-serif aesthetic) */
#define FONT_STYLE_PIXEL     3  /* 'Pixel Mono' (crisp monospaced developer font with dotted zero) */
#define FONT_STYLE_COUNT     4


#define FONT_WIDTH           8
#define FONT_HEIGHT          8
#define FONT_FIRST_CHAR      32   /* ' ' (Space) */
#define FONT_LAST_CHAR       126  /* '~' (Tilde) */
#define FONT_GLYPH_COUNT     95   /* 126 - 32 + 1 */


void        font_engine_init(void);
int         font_get_active(void);
void        font_set_active(int font_id);
const char *font_get_name(int font_id);
int         font_get_count(void);

/* Direct Glyph Access */
const uint8_t *font_get_glyph(int font_id, char c);


void font_draw_char_32(uint32_t *fb, int fb_w, int fb_h, int x, int y, char c, uint32_t color);
void font_draw_string_32(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t color);
void font_draw_string_32_scaled(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t color, int scale);
void font_draw_string_32_shadow(uint32_t *fb, int fb_w, int fb_h, int x, int y, const char *str, uint32_t color, uint32_t shadow_color);


void font_print_preview_card(void (*print_fn)(const char *));

#ifdef __cplusplus
}
#endif

#endif /* ARCHA_FONT_ENGINE_H */
