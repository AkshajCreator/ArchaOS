#ifndef VESA_H
#define VESA_H

#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"



#define VBE_DISPI_IOPORT_INDEX          0x01CE
#define VBE_DISPI_IOPORT_DATA           0x01CF

#define VBE_DISPI_INDEX_ID              0x00
#define VBE_DISPI_INDEX_XRES            0x01
#define VBE_DISPI_INDEX_YRES            0x02
#define VBE_DISPI_INDEX_BPP             0x03
#define VBE_DISPI_INDEX_ENABLE          0x04
#define VBE_DISPI_INDEX_BANK            0x05
#define VBE_DISPI_INDEX_VIRT_WIDTH      0x06
#define VBE_DISPI_INDEX_VIRT_HEIGHT     0x07
#define VBE_DISPI_INDEX_X_OFFSET        0x08
#define VBE_DISPI_INDEX_Y_OFFSET        0x09
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0x0A

/* BGA Hardware Version IDs */
#define VBE_DISPI_ID0                   0xB0C0
#define VBE_DISPI_ID1                   0xB0C1
#define VBE_DISPI_ID2                   0xB0C2
#define VBE_DISPI_ID3                   0xB0C3
#define VBE_DISPI_ID4                   0xB0C4
#define VBE_DISPI_ID5                   0xB0C5

/* BGA Mode Enable Flags */
#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_GETCAPS               0x02
#define VBE_DISPI_8BIT_DAC              0x20
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_NOCLEARMEM            0x80



#define VESA_RES_640_480_W              640
#define VESA_RES_640_480_H              480

#define VESA_RES_800_600_W              800
#define VESA_RES_800_600_H              600

#define VESA_RES_1024_768_W             1024
#define VESA_RES_1024_768_H             768

/* 32-bit ARGB Color Encoding / Extraction Macros */
#ifndef ARGB
#define ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#endif
#ifndef VESA_RGB
#define VESA_RGB(r, g, b) ARGB(0xFF, (r), (g), (b))
#endif
#ifndef RGB
#define RGB(r, g, b)     ARGB(0xFF, (r), (g), (b))
#endif
#define COLOR_A(c)       (((c) >> 24) & 0xFF)
#define COLOR_R(c)       (((c) >> 16) & 0xFF)
#define COLOR_G(c)       (((c) >> 8) & 0xFF)
#define COLOR_B(c)       ((c) & 0xFF)

/* UI Theme Palette Colors */
#define VESA_COLOR_TRANSPARENT 0x00000000
#define VESA_COLOR_BLACK       0xFF000000
#define VESA_COLOR_WHITE       0xFFFFFFFF
#define VESA_COLOR_DARK_BG     0xFF0F172A
#define VESA_COLOR_CARD_BG     0xFF1E293B
#define VESA_COLOR_CARD_BORDER 0xFF38BDF8
#define VESA_COLOR_NEON_CYAN   0xFF00F0FF
#define VESA_COLOR_NEON_BLUE   0xFF3B82F6
#define VESA_COLOR_NEON_GREEN  0xFF10B981
#define VESA_COLOR_NEON_AMBER  0xFFF59E0B
#define VESA_COLOR_NEON_RED    0xFFEF4444
#define VESA_COLOR_NEON_PURPLE 0xFF8B5CF6
#define VESA_COLOR_TEXT_WHITE  0xFFF8FAFC
#define VESA_COLOR_TEXT_MUTED  0xFF94A3B8



typedef struct {
    int       active;          /* 1 if VESA graphics mode is active */
    int       width;           /* Active horizontal resolution */
    int       height;          /* Active vertical resolution */
    int       bpp;             /* Bits per pixel (usually 32) */
    int       pitch;           /* Scanline pitch in bytes */
    uint32_t  phys_base;       /* Physical VRAM MMIO base address */
    uint32_t *lfb;             /* Virtual address of frontbuffer (MMIO aperture) */
    uint32_t *backbuffer;      /* 32-bit ARGB offscreen compositor backbuffer */
    size_t    backbuffer_size; /* Allocated backbuffer size in bytes */
    int       bga_detected;    /* 1 if Bochs Graphics Adapter detected */
    uint16_t  bga_version;     /* Negotiated BGA version (e.g. 0xB0C5) */
    int       multiboot_fb;    /* 1 if Multiboot Framebuffer detected */
} vesa_driver_t;



/* Hardware Detection & Driver Query */
int            vesa_init(multiboot_info_t *mbi);
int            vesa_is_available(void);
int            vesa_is_active(void);
vesa_driver_t *vesa_get_driver(void);
int            vesa_get_width(void);
int            vesa_get_height(void);
uint32_t      *vesa_get_backbuffer(void);
uint32_t      *vesa_get_lfb(void);

/* Mode Switching */
int            vesa_set_mode(int width, int height, int bpp);
void           vesa_set_text_mode(void);

/* True-Color 32-bit ARGB Compositor Primitives */
void           vesa_clear(uint32_t argb);
void           vesa_put_pixel(int x, int y, uint32_t argb);
uint32_t       vesa_get_pixel(int x, int y);
void           vesa_fill_rect(int x, int y, int w, int h, uint32_t argb);
void           vesa_fill_rect_alpha(int x, int y, int w, int h, uint32_t argb, uint8_t alpha);
void           vesa_draw_rect(int x, int y, int w, int h, uint32_t argb);
void           vesa_draw_line(int x0, int y0, int x1, int y1, uint32_t argb);
void           vesa_draw_gradient_v(int x, int y, int w, int h, uint32_t top_color, uint32_t bot_color);
void           vesa_draw_gradient_h(int x, int y, int w, int h, uint32_t left_color, uint32_t right_color);
void           vesa_draw_circle(int cx, int cy, int radius, uint32_t argb);
void           vesa_fill_circle(int cx, int cy, int radius, uint32_t argb);
void           vesa_fill_circle_alpha(int cx, int cy, int radius, uint32_t argb, uint8_t alpha);

/* Typography & String Rendering (bridges to font_engine) */
void           vesa_draw_char(int x, int y, char c, uint32_t color);
void           vesa_draw_string(int x, int y, const char *str, uint32_t color);
void           vesa_draw_string_scaled(int x, int y, const char *str, uint32_t color, int scale);
void           vesa_draw_string_shadow(int x, int y, const char *str, uint32_t color, uint32_t shadow_color);

/* Double-Buffering Fast Blit */
void           vesa_flip(void);

/* Interactive Showcase Demo & Resolution Test */
void           vesa_demo(void);
void           vesa_show_resolution_test(int width, int height);

#endif /* VESA_H */
