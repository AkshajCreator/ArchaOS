// src/gui.c — ArchaOS v0.5 Desktop & Program Suite
// VGA Mode 13h: 320x200, 256 colors, linear framebuffer at 0xA0000
// 100% Double-Buffered: 0% Tearing, 0% Flickering, 0% Mouse Trails!

#include "gui.h"
#include "idt.h"
#include "pit.h"
#include "mm.h"
#include "kernel.h"
#include "fs.h"
#include "vga.h"
#include "shellext.h"
#include "micropython/mp_core.h"
#include "tcc/tcc_core.h"
#include "net/net.h"
#include "net/dns.h"
#include "net/http.h"
#include "net/html.h"
#include "net/ns_engine.h"
#include "net/image_decoder.h"
#include "audio.h"
#include "video.h"
#include "net/nim.h"
#include "ai.h"
#include "net/media_fetch.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * PORT I/O & VGA MODE SWITCHING
 * ============================================================ */

static inline void outb(uint16_t port, uint8_t val)
{ asm volatile("outb %0,%1"::"a"(val),"Nd"(port)); }

static inline uint8_t inb(uint16_t port)
{ uint8_t r; asm volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

#define SCREEN_W   320
#define SCREEN_H   200
#define FB_ADDR    0xA0000

/* 64 KB Off-screen Backbuffer for Double Buffering */
static uint8_t backbuffer[SCREEN_W * SCREEN_H];

void vga_set_mode13h(void)
{
    inb(0x3DA);
    outb(0x3C2, 0x63);

    static const uint8_t SEQ[]  = { 0x03, 0x01, 0x0F, 0x00, 0x0E };
    static const uint8_t CRTC[] = {
        0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
        0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,
        0xFF
    };
    static const uint8_t GC[]   = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
    static const uint8_t AC[]   = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x41,0x00,0x0F,0x00,0x00
    };

    for (int i = 0; i < 5; i++)  { outb(0x3C4, i); outb(0x3C5, SEQ[i]); }
    outb(0x3D4, 0x03); outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & ~0x80);
    for (int i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, CRTC[i]); }
    for (int i = 0; i < 9; i++)  { outb(0x3CE, i); outb(0x3CF, GC[i]); }
    inb(0x3DA);
    for (int i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, AC[i]); }
    outb(0x3C0, 0x20);
}

void vga_font_backup_save(uint8_t *font_buf)
{
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);
    uint8_t *p2 = (uint8_t *)0xA0000;
    for (int i = 0; i < 256 * 32; i++) font_buf[i] = p2[i];
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
}

static uint8_t font_backup[256 * 32];

void vga_set_text_mode(void)
{
    inb(0x3DA); outb(0x3C2, 0x67);
    static const uint8_t SEQ[]  = { 0x03,0x00,0x03,0x00,0x02 };
    static const uint8_t CRTC[] = {
        0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,
        0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,
        0xFF
    };
    static const uint8_t GC[]   = { 0x00,0x00,0x00,0x00,0x00,0x10,0x0E,0x00,0xFF };
    static const uint8_t AC[]   = {
        /* 1:1 palette mapping: AC[i] = i so all 16 colors → DAC[0-15] */
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x0C,0x00,0x0F,0x00,0x00   /* Mode, Overscan, ColorEnable, HPan, ColorSel */
    };
    for (int i = 0; i < 5; i++)  { outb(0x3C4, i); outb(0x3C5, SEQ[i]); }
    outb(0x3D4, 0x03); outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & ~0x80);
    for (int i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, CRTC[i]); }
    for (int i = 0; i < 9; i++)  { outb(0x3CE, i); outb(0x3CF, GC[i]); }
    inb(0x3DA);
    for (int i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, AC[i]); }
    outb(0x3C0, 0x20);
}

void vga_restore_text_palette(void)
{
    static const uint8_t cga[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };
    for (int i = 0; i < 16; i++) {
        uint8_t r = cga[i][0], g = cga[i][1], b = cga[i][2];
        outb(0x3C8, (uint8_t)i);
        outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b);
    }
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0D);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0E);
}

void vga_restore_font_and_text(uint8_t *font_buf)
{
    /* ---- Set up write access to plane 2 (font plane) ---- */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);  /* Map Mask: write to plane 2 ONLY */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);  /* Memory Mode: extended + sequential */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);  /* Read Map Select: plane 2 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);  /* GC Mode: write mode 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);  /* Misc: A000h base, no odd/even chain */

    uint8_t *p2 = (uint8_t *)0xA0000;
    for (int i = 0; i < 256 * 32; i++) p2[i] = font_buf[i];

    /* ---- Restore SEQ + GC for text mode (odd/even, B800h) ---- */
    outb(0x3C4, 0x02); outb(0x3C5, 0x03);  /* Map Mask: planes 0+1 (char + attr) */
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);  /* Memory Mode: extended + odd/even */
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);  /* Read Map Select: plane 0 */
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);  /* GC Mode: odd/even */
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);  /* Misc: B800h, alpha/text mode */
}

static void __attribute__((unused)) set_dac_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    outb(0x3C8, idx); outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b);
}

static inline void fast_memcpy_dw(uint32_t *dst, const uint32_t *src, uint32_t count)
{
    asm volatile(
        "cld; rep movsl"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

/* Ultra-Fast 32-bit Frame Flip to VGA Memory 0xA0000 */
static void gui_flip(void)
{
    uint32_t *dst = (uint32_t *)FB_ADDR;
    uint32_t *src = (uint32_t *)backbuffer;
    fast_memcpy_dw(dst, src, (SCREEN_W * SCREEN_H) / 4);
}

/* ============================================================
 * PALETTE & COLORS
 * ============================================================ */

static void set_palette(void)
{
    outb(0x3C8, 0);
    static const uint8_t cga[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, cga[i][0]); outb(0x3C9, cga[i][1]); outb(0x3C9, cga[i][2]);
    }
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                uint8_t vr = (uint8_t)(r * 63 / 5);
                uint8_t vg = (uint8_t)(g * 63 / 5);
                uint8_t vb = (uint8_t)(b * 63 / 5);
                outb(0x3C9, vr); outb(0x3C9, vg); outb(0x3C9, vb);
            }
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(i * 63 / 23);
        outb(0x3C9, v); outb(0x3C9, v); outb(0x3C9, v);
    }
}

#define RGB(r,g,b)  ((uint8_t)(16 + (r)*36 + (g)*6 + (b)))
#define GRAY(n)     ((uint8_t)(232 + (n)))

#define COL_BLACK      0
#define COL_WHITE      15
#define COL_DARK_GRAY  GRAY(4)
#define COL_MID_GRAY   GRAY(12)
#define COL_LIGHT_GRAY GRAY(20)
#define COL_BLUE       RGB(0,0,4)
#define COL_RED        RGB(4,0,0)
#define COL_GREEN      RGB(0,4,0)
#define COL_TEAL       RGB(0,3,3)
#define COL_YELLOW     RGB(5,5,0)
#define COL_AMBER      RGB(5,3,0)
#define COL_WINBG      GRAY(22)
#define COL_ORANGE     RGB(5,2,0)
#define COL_PURPLE     RGB(3,0,4)
#define COL_CYAN       RGB(0,4,5)
#define COL_PINK       RGB(5,0,3)
#define COL_LIME       RGB(2,5,0)

static uint8_t gui_desktop_color = RGB(0,3,3); /* Default Dark Teal */
static int gui_wallpaper_type = 0; /* 0: Solid, 1: Stars, 2: Grid, 3: Sunset */

static inline uint8_t cmos_read(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
static inline uint8_t bcd_to_bin(uint8_t val) { return (val & 0x0F) + ((val >> 4) * 10); }

static void rtc_read(uint8_t *h, uint8_t *m, uint8_t *s) {
    uint8_t sec  = cmos_read(0x00);
    uint8_t min  = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t regB = cmos_read(0x0B);
    if (!(regB & 0x04)) {
        sec  = bcd_to_bin(sec); min  = bcd_to_bin(min); hour = bcd_to_bin(hour);
    }
    *s = sec; *m = min; *h = hour;
}

/* ============================================================
 * PRNG RANDOM NUMBER GENERATOR
 * ============================================================ */

static uint32_t prng_state = 12345;

static void seed_rand(void)
{
    uint8_t h, m, s;
    rtc_read(&h, &m, &s);
    prng_state = pit_ticks() + (s * 1000) + (m * 60000) + 1337;
}

static uint32_t rand_next(void)
{
    prng_state = prng_state * 1103515245 + 12345;
    return (prng_state / 65536) % 32768;
}

/* ============================================================
 * DRAWING PRIMITIVES (Render to Backbuffer)
 * ============================================================ */

static inline void put_pixel(int x, int y, uint8_t color)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
        backbuffer[y * SCREEN_W + x] = color;
}

void draw_pixel_fb(int x, int y, uint8_t color)
{
    put_pixel(x, y, color);
}

void fill_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int r = y; r < y + h; r++)
        for (int c = x; c < x + w; c++)
            put_pixel(c, r, color);
}

void draw_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int c = x; c < x + w; c++) { put_pixel(c, y, color); put_pixel(c, y+h-1, color); }
    for (int r = y; r < y + h; r++) { put_pixel(x, r, color); put_pixel(x+w-1, r, color); }
}

void draw_hline(int x, int y, int w, uint8_t color)
{ for (int c = x; c < x+w; c++) put_pixel(c, y, color); }

/* ============================================================
 * FULL ASCII 5x7 FONT
 * ============================================================ */

static const uint8_t FONT[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' (32) */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' (58) */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' (59) */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' (60) */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' (61) */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' (62) */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' (63) */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' (64) */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' (65) */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' (66) */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' (67) */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' (68) */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' (69) */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' (70) */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' (71) */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' (72) */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' (73) */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' (74) */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' (75) */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' (76) */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' (77) */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' (78) */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' (79) */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' (80) */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' (81) */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' (82) */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' (83) */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' (84) */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 'u' (85) */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' (86) */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' (87) */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' (88) */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' (89) */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' (90) */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' (91) */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' (92) */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' (93) */
    {0x08,0x08,0x2A,0x1C,0x08}  /* '~' (94) */
};

static void draw_char(int x, int y, char c, uint8_t fg, uint8_t bg)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = FONT[(int)c - 32];
    for (int col = 0; col < 5; col++) {
        for (int r = 0; r < 7; r++) {
            uint8_t bit = (glyph[col] >> r) & 1;
            if (bit) put_pixel(x + col, y + r, fg);
            else if (bg != 0xFF) put_pixel(x + col, y + r, bg);
        }
    }
}

void draw_str(int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    while (*s) { draw_char(x, y, *s++, fg, bg); x += 6; }
}

void draw_str_clip(int x, int y, const char *s, uint8_t fg, uint8_t bg, int clip_x)
{
    while (*s && x + 5 < clip_x) { draw_char(x, y, *s++, fg, bg); x += 6; }
}

void draw_char_scale(int x, int y, char c, uint8_t fg, uint8_t bg, int scale)
{
    if (scale <= 1) { draw_char(x, y, c, fg, bg); return; }
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = FONT[(int)c - 32];
    for (int col = 0; col < 5; col++) {
        for (int r = 0; r < 7; r++) {
            uint8_t bit = (glyph[col] >> r) & 1;
            if (bit) {
                fill_rect(x + col * scale, y + r * scale, scale, scale, fg);
            } else if (bg != 0xFF) {
                fill_rect(x + col * scale, y + r * scale, scale, scale, bg);
            }
        }
    }
}

void draw_str_scale(int x, int y, const char *s, uint8_t fg, uint8_t bg, int scale)
{
    int adv = 6 * scale;
    while (*s) {
        draw_char_scale(x, y, *s++, fg, bg, scale);
        x += adv;
    }
}

static int str_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) {
    while(*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static __attribute__((unused)) const char* str_chr(const char *s, char c) {
    while (*s) { if (*s == c) return s; s++; }
    return NULL;
}

static int str_ncmp(const char *a, const char *b, int n) {
    int i = 0;
    while (i < n && a[i] && (a[i] == b[i])) { i++; }
    if (i == n) return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int mini_atoi(const char *s) {
    while (*s && (*s < '0' || *s > '9')) s++;
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

__attribute__((unused)) static void str_cat(char *dst, const char *src, int max) {
    int len = str_len(dst);
    if (len >= max - 1) return;
    str_cpy(dst + len, src, max - len);
}

/* ============================================================
 * PROGRAM SUITE & WINDOW MANAGEMENT
 * ============================================================ */

typedef enum {
    APP_CALC = 1,
    APP_PAINTER,
    APP_MINESWEEPER,
    APP_FILEMAN,
    APP_NOTEPAD,
    APP_CPANEL,
    APP_TASKMAN,
    APP_IMGVIEW,
    APP_SNAKE,
    APP_CLI,
    APP_CODESTUDIO,
    APP_CONSOLE,
    APP_BROWSER,
    APP_AI_CHAT
} app_type_t;

typedef struct {
    int x, y, w, h;
    int saved_x, saved_y, saved_w, saved_h;
    const char *title;
    const char *short_title;
    app_type_t app;
    int visible;    /* 1: open, 0: closed */
    int minimized;  /* 1: minimized to ProcessBar, 0: on desktop */
    int focused;
    int maximized;
    int split_state;
    uint32_t open_tick;  /* tick when window was opened (for open animation) */
} gui_window_t;

#define MAX_WINDOWS 24
static gui_window_t windows[MAX_WINDOWS];
static int win_count = 0;
static int focused_win = -1;

/* ============================================================
 * TOAST / STATUS NOTIFICATION (replaces modal alert dialogs)
 * ============================================================ */
#define TOAST_OK    0
#define TOAST_ERR   1
#define TOAST_INFO  2

static int    toast_visible   = 0;
static const char *toast_msg  = "";
static int    toast_type      = TOAST_OK;
static uint32_t toast_expire  = 0;   /* pit_ticks() when toast should disappear */

static void show_toast(const char *msg, int type) {
    toast_msg     = msg;
    toast_type    = type;
    toast_visible = 1;
    toast_expire  = pit_ticks() + 2500; /* show for 2.5 seconds */
}

/* Backwards-compat alias — non-error alerts now go through toast */
static void __attribute__((unused)) show_alert(const char *msg) { show_toast(msg, TOAST_ERR); }

/* Forward Declarations */
static void redraw_all_frame(int mx, int my);
static void draw_mouse_cursor(int mx, int my);
static char scancode_to_ascii(uint8_t sc, int shift);
static void open_associated_file(fs_node_t *f);

/* Interactive Desktop Modal Input Dialog */
static void gui_prompt_user_input(const char *prompt, char *out_buf, size_t max_len) {
    out_buf[0] = '\0';
    int len = 0;
    int dlg_w = 210, dlg_h = 65;
    int dlg_x = (80 * 8 - dlg_w) / 2;
    int dlg_y = (25 * 16 - dlg_h) / 2;
    int shift_down = 0;

    irq_kbd_fired = 0;

    uint32_t start_ticks = pit_ticks();
    while (pit_ticks() - start_ticks < 60000) { /* 60 sec timeout */
        redraw_all_frame(mouse_x, mouse_y);

        fill_rect(dlg_x, dlg_y, dlg_w, dlg_h, GRAY(20));
        draw_rect(dlg_x, dlg_y, dlg_w, dlg_h, COL_WHITE);
        fill_rect(dlg_x + 1, dlg_y + 1, dlg_w - 2, 12, COL_BLUE);
        draw_str(dlg_x + 4, dlg_y + 3, "Input Required", COL_WHITE, COL_BLUE);

        draw_str_clip(dlg_x + 6, dlg_y + 18, prompt, COL_WHITE, GRAY(20), dlg_x + dlg_w - 6);
        fill_rect(dlg_x + 6, dlg_y + 34, dlg_w - 12, 14, COL_WHITE);
        draw_rect(dlg_x + 6, dlg_y + 34, dlg_w - 12, 14, COL_BLACK);

        draw_str(dlg_x + 8, dlg_y + 37, out_buf, COL_BLACK, COL_WHITE);

        if ((pit_ticks() / 400) % 2 == 0) {
            int cx = dlg_x + 8 + len * 6;
            if (cx < dlg_x + dlg_w - 14) fill_rect(cx, dlg_y + 37, 5, 7, COL_BLACK);
        }

        draw_mouse_cursor(mouse_x, mouse_y);
        gui_flip();
        uint8_t kbd_status = inb(0x64);
        if (!irq_kbd_fired && (kbd_status & 0x01) && !(kbd_status & 0x20)) {
            last_scancode = inb(0x60);
            irq_kbd_fired = 1;
        }

        if (irq_kbd_fired) {
            uint8_t sc = last_scancode;
            irq_kbd_fired = 0;
            if (sc == 0x2A || sc == 0x36) shift_down = 1;
            else if (sc == 0xAA || sc == 0xB6) shift_down = 0;
            else if (sc == 0x1C) {
                if (pit_ticks() - start_ticks >= 300) break; /* Enter key submits input after 300ms grace period */
            } else if (sc == 0x0E && len > 0) { /* Backspace */
                out_buf[--len] = '\0';
            } else {
                char ch = scancode_to_ascii(sc, shift_down);
                if (ch && ch >= 32 && ch <= 126 && len < (int)max_len - 1) {
                    out_buf[len++] = ch;
                    out_buf[len] = '\0';
                }
            }
        }
        pit_sleep(20);
    }
    irq_kbd_fired = 0;
    redraw_all_frame(mouse_x, mouse_y);
}

/* Calculator State */
static int calc_val = 0;
static int calc_acc = 0;
static char calc_op = 0;
static char calc_display[16] = "0";

/* Painter State — canvas is 140x65 logical pixels */
#define PAINT_CW 140
#define PAINT_CH 65
static uint8_t paint_canvas[PAINT_CW * PAINT_CH];
static uint8_t paint_color = COL_BLACK;
static int paint_brush_size = 1; /* 1: Pencil (1px), 3: Brush (3px) */
static int paint_eraser = 0;     /* 1: Eraser mode */
static int paint_fill   = 0;     /* 1: Flood-fill mode */
static int prev_paint_x = -1, prev_paint_y = -1;

/* ============================================================
 * FLOOD FILL (BFS, iterative — no recursion = no stack overflow)
 * ============================================================ */
#define FILL_STACK_CAP (PAINT_CW * PAINT_CH)
static uint16_t fill_stack[FILL_STACK_CAP];  /* packed x|y */
static void paint_flood_fill(int sx, int sy, uint8_t fill_col) {
    if (sx < 0 || sx >= PAINT_CW || sy < 0 || sy >= PAINT_CH) return;
    uint8_t target = paint_canvas[sy * PAINT_CW + sx];
    if (target == fill_col) return;
    int top = 0;
    fill_stack[top++] = (uint16_t)((sx << 8) | sy);
    while (top > 0) {
        uint16_t v = fill_stack[--top];
        int x = (v >> 8) & 0xFF, y = v & 0xFF;
        if (x < 0 || x >= PAINT_CW || y < 0 || y >= PAINT_CH) continue;
        if (paint_canvas[y * PAINT_CW + x] != target) continue;
        paint_canvas[y * PAINT_CW + x] = fill_col;
        if (top < FILL_STACK_CAP - 4) {
            if (x > 0)          fill_stack[top++] = (uint16_t)(((x-1) << 8) | y);
            if (x < PAINT_CW-1) fill_stack[top++] = (uint16_t)(((x+1) << 8) | y);
            if (y > 0)          fill_stack[top++] = (uint16_t)(( x    << 8) | (y-1));
            if (y < PAINT_CH-1) fill_stack[top++] = (uint16_t)(( x    << 8) | (y+1));
        }
    }
}

/* Sound Function Prototypes */
static void sound_win(void);
static void sound_lose(void);

/* ============================================================
 * SNAKE GAME STATE
 * ============================================================ */
#define SNAKE_GRID_W  22
#define SNAKE_GRID_H  14
#define SNAKE_CELL    6          /* px per cell */
#define SNAKE_MAX_LEN (SNAKE_GRID_W * SNAKE_GRID_H)

static int8_t  snake_x[SNAKE_MAX_LEN], snake_y[SNAKE_MAX_LEN];
static int     snake_len    = 0;
static int     snake_dir    = 1;   /* 0=up 1=right 2=down 3=left */
static int     snake_next_dir = 1;
static int8_t  snake_food_x = 0, snake_food_y = 0;
static int     snake_dead   = 0;
static int     snake_score  = 0;
static uint32_t snake_last_move = 0;
#define SNAKE_SPEED_MS 180

static void snake_place_food(void) {
    for (int tries = 0; tries < 100; tries++) {
        int8_t fx = (int8_t)(rand_next() % SNAKE_GRID_W);
        int8_t fy = (int8_t)(rand_next() % SNAKE_GRID_H);
        int hit = 0;
        for (int i = 0; i < snake_len; i++)
            if (snake_x[i] == fx && snake_y[i] == fy) { hit = 1; break; }
        if (!hit) { snake_food_x = fx; snake_food_y = fy; return; }
    }
}

static void snake_reset(void) {
    seed_rand();
    snake_len   = 4;
    snake_dir   = 1;
    snake_next_dir = 1;
    snake_dead  = 0;
    snake_score = 0;
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = (int8_t)(SNAKE_GRID_W / 2 - i);
        snake_y[i] = (int8_t)(SNAKE_GRID_H / 2);
    }
    snake_place_food();
    snake_last_move = pit_ticks();
}

static void snake_step(void) {
    if (snake_dead) return;
    snake_dir = snake_next_dir;
    int8_t nx = snake_x[0], ny = snake_y[0];
    if      (snake_dir == 0) ny--;
    else if (snake_dir == 1) nx++;
    else if (snake_dir == 2) ny++;
    else                     nx--;
    /* Wall collision */
    if (nx < 0 || nx >= SNAKE_GRID_W || ny < 0 || ny >= SNAKE_GRID_H) { snake_dead = 1; sound_lose(); return; }
    /* Self collision */
    for (int i = 0; i < snake_len - 1; i++)
        if (snake_x[i] == nx && snake_y[i] == ny) { snake_dead = 1; sound_lose(); return; }
    /* Ate food */
    int ate = (nx == snake_food_x && ny == snake_food_y);
    /* Shift body */
    int new_len = ate ? snake_len + 1 : snake_len;
    if (new_len > SNAKE_MAX_LEN) new_len = SNAKE_MAX_LEN;
    for (int i = new_len - 1; i > 0; i--) { snake_x[i] = snake_x[i-1]; snake_y[i] = snake_y[i-1]; }
    snake_x[0] = nx; snake_y[0] = ny;
    if (ate) { snake_len = new_len; snake_score += 10; snake_place_food(); sound_win(); }
    else      snake_len = new_len;
}

/* ============================================================
 * SCREENSAVER STATE
 * ============================================================ */
#define SAVER_IDLE_MS 25000    /* 25 seconds of idle → screensaver */
static uint32_t last_activity_tick = 0;
static int      screensaver_active = 0;



static void draw_paint_spot(int cx, int cy, uint8_t color) {
    int radius = paint_eraser ? 3 : (paint_brush_size == 3 ? 1 : 0);
    uint8_t draw_col = paint_eraser ? COL_WHITE : color;

    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < PAINT_CW - 2 && py >= 0 && py < PAINT_CH - 2)
                paint_canvas[py * PAINT_CW + px] = draw_col;
        }
}

static void draw_paint_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    int e2;
    while (1) {
        draw_paint_spot(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 <  dy) { err += dx; y0 += sy; }
    }
}

/* Sound Function Prototypes */
static void sound_click(void);
static void sound_win_open(void);
static void sound_win_close(void);
static void sound_win(void);
static void sound_lose(void);

/* Minesweeper State */
static uint8_t mine_grid[8][8];
static uint8_t mine_revealed[8][8]; /* 0: unrevealed, 1: revealed, 2: flag */
static int mine_game_over = 0;
static int mine_win = 0;
static uint32_t mine_lost_ticks = 0;

static void minesweeper_check_win(void)
{
    if (mine_game_over) return;
    int unrevealed_safe = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (mine_grid[r][c] != 9 && mine_revealed[r][c] != 1)
                unrevealed_safe++;

    if (unrevealed_safe == 0 && !mine_win) {
        mine_win = 1;
        sound_win();
    }
}

static void minesweeper_reset(void)
{
    seed_rand();
    for (int r=0; r<8; r++)
        for (int c=0; c<8; c++) { mine_grid[r][c] = 0; mine_revealed[r][c] = 0; }
    mine_game_over = 0;
    mine_win = 0;
    mine_lost_ticks = 0;

    int placed = 0;
    while (placed < 8) {
        int r = rand_next() % 8;
        int c = rand_next() % 8;
        if (mine_grid[r][c] != 9) { mine_grid[r][c] = 9; placed++; }
    }
    for (int r=0; r<8; r++)
        for (int c=0; c<8; c++) {
            if (mine_grid[r][c] == 9) continue;
            int cnt = 0;
            for (int dr=-1; dr<=1; dr++)
                for (int dc=-1; dc<=1; dc++) {
                    int nr = r+dr, nc = c+dc;
                    if (nr>=0 && nr<8 && nc>=0 && nc<8 && mine_grid[nr][nc]==9) cnt++;
                }
            mine_grid[r][c] = cnt;
        }
}

/* Interactive Notepad State */
static char notepad_buf[512] = "";
static int notepad_len = 0;
static char notepad_filename[32] = "note.txt";

/* Painter & Image Viewer File State */
static char paint_filename[32] = "art.bmp";
static char imgview_filename[32] = "art.bmp";

/* File Manager State */
static fs_node_t *fileman_cur_dir = NULL;
static int        fileman_selected = -1;
static int        fileman_new_cnt  = 1;

/* Function Prototypes */
static void sound_click(void);
static void sound_win_open(void);
static void sound_win_close(void);
static void sound_win(void);
static void sound_lose(void);
static void sound_tone(uint32_t freq_hz, uint32_t ms);
static void sound_error(void);
static void sound_clear(void);
static void sound_open(void);
static void open_window(int idx);

/* 7x7 Pixel Program Icons */
static void draw_app_icon(int x, int y, app_type_t app)
{
    static const uint8_t ICONS[16][7][7] = {
        /* Calc */
        { {1,1,1,1,1,1,1},{1,0,0,0,0,0,1},{1,0,1,0,1,0,1},{1,0,0,0,0,0,1},{1,0,1,0,1,0,1},{1,0,0,0,0,0,1},{1,1,1,1,1,1,1} },
        /* Painter */
        { {0,1,1,1,1,1,0},{1,0,0,0,0,0,1},{1,0,1,0,1,0,1},{1,0,0,1,0,0,1},{1,0,1,0,0,0,1},{1,0,0,0,0,0,1},{0,1,1,1,1,1,0} },
        /* Minesweeper */
        { {0,0,1,0,1,0,0},{0,1,1,1,1,1,0},{1,1,1,0,1,1,1},{0,1,0,1,0,1,0},{1,1,1,0,1,1,1},{0,1,1,1,1,1,0},{0,0,1,0,1,0,0} },
        /* FileMan */
        { {0,1,1,1,0,0,0},{1,0,0,0,1,1,1},{1,0,0,0,0,0,1},{1,0,0,0,0,0,1},{1,0,0,0,0,0,1},{1,0,0,0,0,0,1},{1,1,1,1,1,1,1} },
        /* Notepad */
        { {1,1,1,1,1,0,0},{1,0,0,0,1,1,0},{1,0,0,0,0,1,1},{1,0,1,1,1,0,1},{1,0,1,1,1,0,1},{1,0,0,0,0,0,1},{1,1,1,1,1,1,1} },
        /* CPanel */
        { {0,0,1,1,1,0,0},{0,1,0,0,0,1,0},{1,0,1,1,1,0,1},{1,0,1,0,1,0,1},{1,0,1,1,1,0,1},{0,1,0,0,0,1,0},{0,0,1,1,1,0,0} },
        /* TaskMan */
        { {1,1,1,1,1,1,1},{1,0,0,0,0,1,1},{1,0,0,0,1,0,1},{1,0,0,1,0,0,1},{1,0,1,0,0,0,1},{1,1,0,0,0,0,1},{1,1,1,1,1,1,1} },
        /* ImgView */
        { {1,1,1,1,1,1,1},{1,0,0,1,0,0,1},{1,0,0,0,0,0,1},{1,0,1,0,0,1,1},{1,1,0,1,1,0,1},{1,0,0,0,0,0,1},{1,1,1,1,1,1,1} },
        /* Snake */
        { {0,1,1,1,1,0,0},{1,0,0,0,1,0,0},{0,1,1,0,1,0,0},{0,0,1,0,1,1,1},{0,0,1,0,0,0,1},{0,0,1,1,1,1,1},{0,0,0,0,0,0,0} },
        /* CLI */
        { {0,0,0,0,0,0,0},{1,1,0,0,0,0,0},{0,0,1,1,0,0,0},{1,1,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,1,1,1,1},{0,0,0,0,0,0,0} },
        /* AppStudio */
        { {0,0,1,0,1,0,0},{0,1,0,0,0,1,0},{1,0,0,0,0,0,1},{0,1,0,0,0,1,0},{0,0,1,0,1,0,0},{0,0,0,0,0,0,0},{1,1,1,1,1,1,1} },
        /* Console */
        { {0,0,0,0,0,0,0},{1,1,0,0,0,0,0},{0,0,1,1,0,0,0},{1,1,0,0,0,0,0},{0,0,0,0,0,0,0},{0,0,0,0,1,1,0},{0,0,0,0,0,0,0} },
        /* Browser (Globe) */
        { {0,1,1,1,1,1,0},{1,0,1,1,0,0,1},{1,1,1,0,0,0,1},{1,0,0,1,0,0,1},{1,0,0,0,1,1,1},{1,0,0,1,1,0,1},{0,1,1,1,1,1,0} },
        /* Audio Player (Music Note) */
        { {0,0,0,1,1,1,1},{0,0,0,0,0,1,0},{0,0,0,0,0,1,0},{0,0,0,0,0,1,0},{0,1,1,0,0,1,0},{1,1,1,1,1,1,0},{0,1,1,0,0,0,0} },
        /* Video Player (Film & Play) */
        { {1,1,1,1,1,1,1},{1,0,1,0,1,0,1},{1,1,1,1,1,1,1},{1,0,1,0,0,0,1},{1,0,1,1,0,0,1},{1,0,1,0,0,0,1},{1,1,1,1,1,1,1} },
        /* AI Assistant (Chat Spark) */
        { {0,1,1,1,1,1,0},{1,1,0,1,0,1,1},{1,0,1,1,1,0,1},{1,0,0,1,0,0,1},{1,1,1,1,1,1,1},{1,1,0,0,0,0,0},{1,0,0,0,0,0,0} }
    };

    uint8_t fg = COL_BLACK;
    if (app == APP_CALC) fg = COL_BLUE;
    else if (app == APP_PAINTER) fg = COL_RED;
    else if (app == APP_MINESWEEPER) fg = COL_GREEN;
    else if (app == APP_FILEMAN) fg = COL_YELLOW;
    else if (app == APP_CPANEL) fg = COL_TEAL;
    else if (app == APP_TASKMAN) fg = COL_GREEN;
    else if (app == APP_IMGVIEW) fg = RGB(5,0,5);
    else if (app == APP_SNAKE)   fg = COL_LIME;
    else if (app == APP_CLI)     fg = COL_LIME;
    else if (app == APP_CODESTUDIO) fg = COL_CYAN;
    else if (app == APP_CONSOLE) fg = COL_LIME;
    else if (app == APP_BROWSER) fg = COL_CYAN;
    else if (app == APP_AI_CHAT)     fg = COL_LIME;

    int idx = (int)app - 1;
    if (idx < 0 || idx >= 16) return;

    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 7; c++)
            if (ICONS[idx][r][c]) put_pixel(x + c, y + r, fg);
}

/* System Configuration file `archaos.conf` Parsing */
static void load_archaos_conf(void) {
    fs_node_t *f = fs_resolve("archaos.conf");
    if (f && f->data) {
        if (f->data[0] == '1') gui_desktop_color = RGB(0,1,4); /* Win31 */
        else if (f->data[0] == '2') gui_desktop_color = RGB(0,3,0); /* Matrix */
        else if (f->data[0] == '3') gui_desktop_color = RGB(5,3,0); /* Amber */
        else gui_desktop_color = RGB(0,3,3); /* Teal */

        if (f->size > 2) {
            gui_wallpaper_type = f->data[2] - '0';
            if (gui_wallpaper_type < 0 || gui_wallpaper_type > 3) gui_wallpaper_type = 0;
        }
    }
}

static void save_archaos_conf(void) {
    char buf[4];
    buf[0] = '0';
    if (gui_desktop_color == RGB(0,1,4)) buf[0] = '1';
    else if (gui_desktop_color == RGB(0,3,0)) buf[0] = '2';
    else if (gui_desktop_color == RGB(5,3,0)) buf[0] = '3';
    buf[1] = ' ';
    buf[2] = '0' + gui_wallpaper_type;
    buf[3] = '\0';
    fs_write("archaos.conf", buf, 3);
}

static void win_init_all(void)
{
    win_count = 0;

    windows[0] = (gui_window_t){ 15, 15, 110, 115, 15, 15, 110, 115, "Calculator", "Calc", APP_CALC, 0, 0, 0, 0, 0, 0 };
    windows[1] = (gui_window_t){ 135, 15, 165, 105, 135, 15, 165, 105, "Painter", "Painter", APP_PAINTER, 0, 0, 0, 0, 0, 0 };
    windows[2] = (gui_window_t){ 15, 65, 110, 105, 15, 65, 110, 105, "Minesweeper", "Mine", APP_MINESWEEPER, 0, 0, 0, 0, 0, 0 };
    windows[3] = (gui_window_t){ 130, 50, 150, 110, 130, 50, 150, 110, "File Manager", "Files", APP_FILEMAN, 0, 0, 0, 0, 0, 0 };
    windows[4] = (gui_window_t){ 35, 30, 150, 110, 35, 30, 150, 110, "Notepad", "Notepad", APP_NOTEPAD, 0, 0, 0, 0, 0, 0 };
    windows[5] = (gui_window_t){ 150, 25, 145, 115, 150, 25, 145, 115, "Control Panel", "CPanel", APP_CPANEL, 0, 0, 0, 0, 0, 0 };
    windows[6] = (gui_window_t){ 40, 20, 160, 135, 40, 20, 160, 135, "Task Manager", "TaskMan", APP_TASKMAN, 0, 0, 0, 0, 0, 0 };
    windows[7] = (gui_window_t){ 120, 35, 155, 120, 120, 35, 155, 120, "Image Viewer", "ImgView", APP_IMGVIEW, 0, 0, 0, 0, 0, 0 };
    windows[8]  = (gui_window_t){ 60,  20, 160, 120, 60,  20, 160, 120, "Snake",        "Snake",     APP_SNAKE,      0, 0, 0, 0, 0, 0 };
    windows[9]  = (gui_window_t){ 15,  12, 290, 168, 15,  12, 290, 168, "Terminal",     "CLI",       APP_CLI,        0, 0, 0, 0, 0, 0 };
    windows[10] = (gui_window_t){ 35,  10, 245, 145, 35,  10, 245, 145, "App Studio",   "AppStudio", APP_CODESTUDIO, 0, 0, 0, 0, 0, 0 };
    windows[11] = (gui_window_t){ 70,  30, 180, 130, 70,  30, 180, 130, "App Output",   "Console",   APP_CONSOLE,    0, 0, 0, 0, 0, 0 };
    windows[12] = (gui_window_t){ 4,   14, 312, 172, 4,   14, 312, 172, "Web Browser",  "Browser",   APP_BROWSER,    0, 0, 0, 0, 0, 0 };
    windows[13] = (gui_window_t){ 36,  18, 220, 148, 36,  18, 220, 148, "AI Assistant", "AI Chat",   APP_AI_CHAT,    0, 0, 0, 0, 0, 0 };

    win_count = 14;
    focused_win = -1;
    fileman_cur_dir = fs_root();
    load_archaos_conf();
    minesweeper_reset();
    snake_reset();
    last_activity_tick = pit_ticks();
    screensaver_active = 0;
    for (int i=0; i<PAINT_CW*PAINT_CH; i++) paint_canvas[i] = COL_WHITE;
}

#include "net/js/js_engine.h"

/* ============================================================
 * WEB BROWSER APP — Graphical Mode 13h Web Browser & JS Engine
 * ============================================================ */
static char gui_to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static __attribute__((unused)) int gui_tag_match(const char *tag, const char *name) {
    while (*tag == ' ' || *tag == '/') tag++;
    while (*name) {
        if (gui_to_lower(*tag) != gui_to_lower(*name)) return 0;
        tag++; name++;
    }
    return (*tag == ' ' || *tag == '>' || *tag == '/' || *tag == '\0');
}

static __attribute__((unused)) void get_html_attr(const char *tag, const char *attr_name, char *out_val, int max_len) {
    out_val[0] = '\0';
    int nlen = str_len(attr_name);
    const char *p = tag;
    while (*p) {
        int match = 1;
        for (int i = 0; i < nlen; i++) {
            if (gui_to_lower(p[i]) != gui_to_lower(attr_name[i])) { match = 0; break; }
        }
        if (match && (p[nlen] == '=' || p[nlen] == ' ')) {
            const char *val = p + nlen;
            while (*val == ' ' || *val == '=') val++;
            char quote = 0;
            if (*val == '"' || *val == '\'') { quote = *val; val++; }
            int vlen = 0;
            while (*val && (quote ? (*val != quote) : (*val != ' ' && *val != '>')) && vlen < max_len - 1) {
                out_val[vlen++] = *val++;
            }
            out_val[vlen] = '\0';
            return;
        }
        p++;
    }
}

static __attribute__((unused)) uint8_t hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}


static __attribute__((unused)) void css_get_property(const char *style_str, const char *prop_name, char *out_val, int max_len) {
    out_val[0] = '\0';
    if (!style_str || !prop_name) return;
    int plen = str_len(prop_name);
    const char *p = style_str;
    while (*p) {
        while (*p == ' ' || *p == ';' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        int match = 1;
        for (int i = 0; i < plen; i++) {
            if (gui_to_lower(p[i]) != gui_to_lower(prop_name[i])) { match = 0; break; }
        }
        if (match && (p[plen] == ':' || p[plen] == ' ' || p[plen] == '\t')) {
            const char *v = p + plen;
            while (*v == ' ' || *v == ':' || *v == '\t') v++;
            int vi = 0;
            while (*v && *v != ';' && *v != '"' && *v != '\'' && vi < max_len - 1) {
                out_val[vi++] = *v++;
            }
            while (vi > 0 && (out_val[vi - 1] == ' ' || out_val[vi - 1] == '\t')) vi--;
            out_val[vi] = '\0';
            return;
        }
        while (*p && *p != ';') p++;
    }
}

typedef struct {
    int x, y, w, h;
    char url[128];
    char onclick[128];
} gui_link_t;

typedef struct {
    int x, y, w, h;
    char name[32];
    char value[64];
    char placeholder[32];
    int is_submit;
    char onclick[128];
} gui_input_t;

#define BROWSER_FOCUS_NONE       -2
#define BROWSER_FOCUS_URL        -1
#define BROWSER_FOCUS_INPUT_BASE  0
#define BROWSER_FOCUS_LINK_BASE   100

#define MAX_BROWSER_TABS 4

typedef struct {
    char url[128];
    char title[64];
    char html[196608];
    char history[16][128];
    int  hist_count;
    int  hist_pos;
    int  scroll_y;
    int  nav_focus;
    int  input_focus;
    int  is_loading;
    uint8_t bg_color;
} browser_tab_t;

static browser_tab_t browser_tabs[MAX_BROWSER_TABS];
static int browser_tab_count = 1;
static int browser_active_tab = 0;
static char browser_status[48] = "Ready";
static int  browser_url_focus = 0;
static int  browser_url_cursor = 0;
static int  browser_url_scroll = 0;
static int  browser_loaded = 0;

#define browser_url         (browser_tabs[browser_active_tab].url)
#define browser_html        (browser_tabs[browser_active_tab].html)
#define browser_history     (browser_tabs[browser_active_tab].history)
#define browser_hist_count  (browser_tabs[browser_active_tab].hist_count)
#define browser_hist_pos    (browser_tabs[browser_active_tab].hist_pos)
#define browser_scroll_y    (browser_tabs[browser_active_tab].scroll_y)
#define browser_nav_focus   (browser_tabs[browser_active_tab].nav_focus)
#define browser_input_focus (browser_tabs[browser_active_tab].input_focus)

static const char *showcase_html =
"<html><head><title>Firefox Home</title>"
"<style>"
"body { background: #ffffff; color: #1e293b; }"
".hero { text-align: center; margin: 6px 0 4px 0; }"
".search-box { background: #f8fafc; border: 1px solid #cbd5e1; padding: 6px; margin: 4px 8px; text-align: center; }"
".card { background: #f8fafc; border: 1px solid #e2e8f0; padding: 4px; margin: 4px; }"
".shortcuts { text-align: center; margin: 4px 0; }"
"</style>"
"</head>"
"<body style=\"background: #ffffff; color: #1e293b;\">"
"<div class=\"hero\">"
"<center>"
"<img src=\"archaos_logo.bmp\" alt=\"Logo\" style=\"width: 36px; height: 16px;\" />"
"<p style=\"font-weight: bold; color: #0284c7; margin: 3px 0 1px 0;\">Firefox for ArchaOS</p>"
"<p style=\"color: #64748b; margin: 1px 0 4px 0;\">Fast, private, and open-source web browser</p>"
"</center>"
"</div>"
"<div class=\"search-box\">"
"<center>"
"<form action=\"https://lite.duckduckgo.com/lite/\">"
"<input type=\"text\" name=\"q\" placeholder=\"Search the web with DuckDuckGo...\" style=\"background: #ffffff; color: #0f172a; border: 1px solid #94a3b8; width: 170px;\" /> "
"<input type=\"submit\" value=\"Search\" style=\"background: #0284c7; color: #ffffff; border: 1px solid #0369a1;\" />"
"</form>"
"</center>"
"</div>"
"<div class=\"shortcuts\">"
"<center>"
"<p style=\"color: #64748b; font-weight: bold; margin-bottom: 2px;\">Quick Launch Shortcuts</p>"
"<p>"
"<a href=\"https://lite.duckduckgo.com/lite/\" style=\"color: #ea580c; font-weight: bold;\">[DuckDuckGo]</a> &nbsp; "
"<a href=\"https://en.m.wikipedia.org/wiki/Main_Page\" style=\"color: #7c3aed; font-weight: bold;\">[Wikipedia]</a> &nbsp; "
"<a href=\"about:media\" style=\"color: #16a34a; font-weight: bold;\">[Media]</a> &nbsp; "
"<a href=\"http://wttr.in/?1\" style=\"color: #0891b2; font-weight: bold;\">[Weather]</a>"
"</p>"
"<p>"
"<a href=\"/audio/linus.ogg\" style=\"color: #16a34a; font-weight: bold;\">[Linus Pronunciation (.ogg)]</a> &nbsp; "
"<a href=\"/audio/theme.wav\" style=\"color: #2563eb; font-weight: bold;\">[Theme (.wav)]</a>"
"</p>"
"</center>"
"</div>"
"<hr />"
"<div class=\"card\">"
"<p style=\"color: #0f172a; font-weight: bold;\">Web Graphics & Media Engine</p>"
"<p style=\"color: #475569;\">Universal stb_image decoder & HTML5 video stream:</p>"
"<img src=\"archaos_logo.bmp\" alt=\"ArchaOS System Desktop Logo\" />"
"<video poster=\"video_thumb.bmp\" src=\"http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4\">ArchaOS Showcase Video</video>"
"</div>"
"<div class=\"card\">"
"<p style=\"color: #0f172a; font-weight: bold;\">JavaScript Subsystem</p>"
"<p style=\"color: #475569;\">Live Duktape engine with DOM event handling:</p>"
"<center>"
"<button onclick=\"alert('Hello from ArchaOS JavaScript Engine!')\" style=\"background: #0284c7; color: #ffffff; border: 1px solid #0369a1;\">Test JavaScript Alert</button>"
"</center>"
"</div>"
"</body></html>";

static const char *media_html =
"<html><head><title>In-Browser Media Player</title>"
"<style>"
"body { background: #ffffff; color: #1e293b; font-family: sans-serif; }"
".navbar { background: #f1f5f9; border: 1px solid #cbd5e1; padding: 2px; margin: 2px; }"
".card { background: #f8fafc; border: 1px solid #e2e8f0; padding: 4px; margin: 4px; }"
"</style>"
"</head>"
"<body>"
"<header class=\"navbar\">"
"<a href=\"about:home\" style=\"color: #0284c7; font-weight: bold;\">[Home]</a> &nbsp; "
"<a href=\"about:media\" style=\"color: #16a34a; font-weight: bold;\">[Media]</a> &nbsp; "
"<a href=\"https://en.m.wikipedia.org/wiki/Main_Page\" style=\"color: #7c3aed; font-weight: bold;\">[Wikipedia]</a>"
"</header>"
"<div class=\"card\">"
"<p style=\"color: #0f172a; font-weight: bold; margin: 2px;\">ArchaOS In-Browser Media Player</p>"
"<p style=\"color: #64748b; font-size: 8px; margin: 2px;\">Single-click universal playback for web and local media streams. Supported formats: OGG Vorbis, MP3, WAV, AU, Animated GIF, and MPEG-1.</p>"
"</div>"
"<div class=\"card\">"
"<p style=\"color: #0369a1; font-weight: bold; margin: 2px;\">Single-Click Web &amp; Wikipedia Playback</p>"
"<p style=\"color: #334155; font-size: 8px; margin: 2px;\">When browsing Wikipedia or any webpage, clicking any audio pronunciation, speech recording, or video clip immediately plays it in the browser's docked player bar without leaving the page.</p>"
"</div>"
"<div class=\"card\">"
"<p style=\"color: #059669; font-weight: bold; margin: 2px;\">Direct Media Navigation</p>"
"<p style=\"color: #334155; font-size: 8px; margin: 2px;\">Type or paste any media URL (HTTP, HTTPS, or local path) into the address bar to stream and play inside this tab.</p>"
"</div>"
"</body></html>";

static void show_browser_alert(const char *msg) {
    if (msg) show_toast(msg, TOAST_INFO);
}

static void browser_push_history(const char *url) {
    if (!url || !url[0]) return;
    if (browser_hist_pos >= 0 && browser_hist_pos < browser_hist_count) {
        if (str_cmp(browser_history[browser_hist_pos], url) == 0) return;
    }
    if (browser_hist_pos < 15) {
        browser_hist_pos++;
        str_cpy(browser_history[browser_hist_pos], url, sizeof(browser_history[0]));
        browser_hist_count = browser_hist_pos + 1;
    }
}

static void browser_fetch(const char *url);
static void browser_fetch_internal(const char *url, int record_history);
static int browser_is_audio_media(const char *url, const char *text);
static int browser_is_video_media(const char *url, const char *text);

/* In-Browser Native Media Playback Engine */
static bool browser_media_active = false;
static bool browser_media_is_video = false;
static char browser_media_url[256] = "";
static char browser_media_title[64] = "";

static const char *extract_basename_str(const char *path) {
    if (!path) return "Media";
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last[0] ? last : "Media";
}

static void browser_play_in_browser_media(const char *url_or_path, int is_video) {
    if (!url_or_path || !url_or_path[0]) return;
    char resolved[256];
    media_resolve_url(url_or_path, resolved, sizeof(resolved));

    browser_media_active = true;
    browser_media_is_video = is_video ? true : false;
    str_cpy(browser_media_url, resolved, sizeof(browser_media_url));

    if (is_video) {
        video_open(resolved);
        video_play();
        str_cpy(browser_media_title, video_get_title(), sizeof(browser_media_title));
        snprintf(browser_status, sizeof(browser_status), "Playing Video: %s", browser_media_title);
        serial_printf(COM1_BASE, "[BROWSER_MEDIA] In-browser video active: %s (%s)\n", browser_media_title, resolved);
    } else {
        audio_open(resolved);
        audio_play();
        str_cpy(browser_media_title, audio_get_title(), sizeof(browser_media_title));
        snprintf(browser_status, sizeof(browser_status), "Playing Audio: %s (%s)", browser_media_title, audio_get_format());
        serial_printf(COM1_BASE, "[BROWSER_MEDIA] In-browser audio active: %s (%s, %s)\n", browser_media_title, resolved, audio_get_format());
    }
}

static void browser_generate_inbrowser_media_page(const char *resolved, int is_video) {
    const char *bname = extract_basename_str(resolved);
    if (is_video) {
        snprintf(browser_html, sizeof(browser_html),
            "<html><head><title>Video: %s</title>"
            "<style>body { background: #0f172a; color: #f8fafc; text-align: center; font-family: sans-serif; }"
            ".card { background: #1e293b; border: 1px solid #38bdf8; padding: 6px; margin: 6px auto; max-width: 250px; }</style>"
            "</head><body>"
            "<div class=\"card\">"
            "<p style=\"color: #38bdf8; font-weight: bold; margin: 2px;\">%s</p>"
            "<p style=\"color: #94a3b8; font-size: 8px; margin: 1px 0 4px 0;\">Format: %s &bull; Native Decoder</p>"
            "<video src=\"%s\" poster=\"video_thumb.bmp\">%s</video>"
            "<p style=\"color: #4ade80; font-size: 8px; margin: 3px 0 1px 0;\">[ Playing inside ArchaOS Browser ]</p>"
            "</div></body></html>",
            bname, bname, video_get_format(), resolved, bname);
    } else {
        snprintf(browser_html, sizeof(browser_html),
            "<html><head><title>Audio: %s</title>"
            "<style>body { background: #0f172a; color: #f8fafc; text-align: center; font-family: sans-serif; }"
            ".card { background: #1e293b; border: 1px solid #38bdf8; padding: 6px; margin: 6px auto; max-width: 250px; }</style>"
            "</head><body>"
            "<div class=\"card\">"
            "<p style=\"color: #38bdf8; font-weight: bold; margin: 2px;\">%s</p>"
            "<p style=\"color: #94a3b8; font-size: 8px; margin: 1px 0 4px 0;\">Format: %s &bull; Open-Source Universal Audio</p>"
            "<audio src=\"%s\">%s</audio>"
            "<p style=\"color: #4ade80; font-size: 8px; margin: 3px 0 1px 0;\">[ Playing inside ArchaOS Browser ]</p>"
            "</div></body></html>",
            bname, bname, audio_get_format(), resolved, bname);
    }
    str_cpy(browser_tabs[browser_active_tab].title, bname, sizeof(browser_tabs[0].title));
    str_cpy(browser_url, resolved, sizeof(browser_url));
    browser_loaded = 1;
    browser_scroll_y = 0;
    browser_input_focus = -1;
    ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
    redraw_all_frame(160, 100);
}

static void browser_pop_history_back(void) {
    if (browser_hist_pos > 0) {
        browser_hist_pos--;
        browser_fetch_internal(browser_history[browser_hist_pos], 0);
    }
}

static void browser_pop_history_fwd(void) {
    if (browser_hist_pos + 1 < browser_hist_count) {
        browser_hist_pos++;
        browser_fetch_internal(browser_history[browser_hist_pos], 0);
    }
}

static void browser_tab_init_defaults(int idx) {
    if (idx < 0 || idx >= MAX_BROWSER_TABS) return;
    browser_tab_t *t = &browser_tabs[idx];
    memset(t, 0, sizeof(browser_tab_t));
    str_cpy(t->url, "about:home", sizeof(t->url));
    str_cpy(t->title, "Home", sizeof(t->title));
    str_cpy(t->html, showcase_html, sizeof(t->html));
    str_cpy(t->history[0], "about:home", 128);
    t->hist_count = 1;
    t->hist_pos = 0;
    t->scroll_y = 0;
    t->nav_focus = BROWSER_FOCUS_URL;
    t->input_focus = -1;
    t->is_loading = 0;
    t->bg_color = COL_WHITE;
}

static void browser_new_tab(const char *url) {
    if (browser_tab_count >= MAX_BROWSER_TABS) {
        show_toast("Max 4 tabs open", TOAST_INFO);
        return;
    }
    int new_idx = browser_tab_count;
    browser_tab_init_defaults(new_idx);
    browser_tab_count++;
    browser_active_tab = new_idx;
    if (url && url[0] && str_cmp(url, "about:home") != 0) {
        browser_fetch(url);
    } else {
        browser_fetch_internal("about:home", 0);
    }
    sound_click();
    redraw_all_frame(160, 100);
}

static void browser_close_tab(int idx) {
    if (idx < 0 || idx >= browser_tab_count) return;
    if (browser_tab_count <= 1) {
        browser_tab_init_defaults(0);
        browser_fetch_internal("about:home", 0);
        sound_click();
        redraw_all_frame(160, 100);
        return;
    }

    for (int i = idx; i < browser_tab_count - 1; i++) {
        browser_tabs[i] = browser_tabs[i + 1];
    }
    browser_tab_count--;
    if (browser_active_tab >= browser_tab_count) {
        browser_active_tab = browser_tab_count - 1;
    }
    browser_tab_t *cur = &browser_tabs[browser_active_tab];
    ns_engine_parse_html(cur->html, str_len(cur->html), cur->url);
    sound_click();
    redraw_all_frame(160, 100);
}

static void browser_switch_tab(int idx) {
    if (idx < 0 || idx >= browser_tab_count || idx == browser_active_tab) return;
    browser_active_tab = idx;
    browser_tab_t *cur = &browser_tabs[browser_active_tab];
    ns_engine_parse_html(cur->html, str_len(cur->html), cur->url);
    sound_click();
    redraw_all_frame(160, 100);
}

static void url_encode(const char *src, char *dst, int max_len) {
    if (!src || !dst || max_len <= 0) return;
    int d = 0;
    for (int i = 0; src[i] && d < max_len - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == ' ') {
            dst[d++] = '+';
        } else if ((c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.' || c == '~') {
            dst[d++] = c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            dst[d++] = '%';
            dst[d++] = hex[(c >> 4) & 0x0F];
            dst[d++] = hex[c & 0x0F];
        }
    }
    dst[d] = '\0';
}

static void browser_submit_search(const char *query) {
    if (!query || !query[0]) {
        str_cpy(browser_status, "Please enter a search query", sizeof(browser_status));
        return;
    }
    char enc[192];
    url_encode(query, enc, sizeof(enc));
    char search_url[256];
    if (strstr(browser_url, "google")) {
        snprintf(search_url, sizeof(search_url), "https://www.google.com/search?q=%s", enc);
    } else if (strstr(browser_url, "wikipedia.org")) {
        snprintf(search_url, sizeof(search_url), "https://en.m.wikipedia.org/w/index.php?search=%s", enc);
    } else {
        snprintf(search_url, sizeof(search_url), "https://lite.duckduckgo.com/lite/?q=%s", enc);
    }
    browser_fetch(search_url);
}

static void browser_fetch_internal(const char *url, int record_history) {
    const char *u_check = url ? url : "";
    while (*u_check == ' ') u_check++;

    /* In-Browser Native Media Playback for Direct Media URLs */
    if (browser_is_audio_media(u_check, "")) {
        char resolved[256];
        media_resolve_url(u_check, resolved, sizeof(resolved));
        if (record_history) browser_push_history(u_check);
        browser_play_in_browser_media(resolved, 0);
        browser_generate_inbrowser_media_page(resolved, 0);
        return;
    }

    if (browser_is_video_media(u_check, "")) {
        char resolved[256];
        media_resolve_url(u_check, resolved, sizeof(resolved));
        if (record_history) browser_push_history(u_check);
        browser_play_in_browser_media(resolved, 1);
        browser_generate_inbrowser_media_page(resolved, 1);
        return;
    }

    if (str_cmp(u_check, "about:media") == 0) {
        if (record_history) browser_push_history("about:media");
        str_cpy(browser_url, "about:media", sizeof(browser_url));
        str_cpy(browser_html, media_html, sizeof(browser_html));
        str_cpy(browser_tabs[browser_active_tab].title, "Media", sizeof(browser_tabs[0].title));
        str_cpy(browser_status, "Ready - Media Showcase", sizeof(browser_status));
        browser_loaded = 1;
        browser_scroll_y = 0;
        browser_nav_focus = BROWSER_FOCUS_URL;
        browser_input_focus = -1;
        ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
        redraw_all_frame(160, 100);
        return;
    }
    if (u_check[0] == '\0' ||
        (u_check[0] == 'a' && u_check[1] == 'b' && u_check[2] == 'o' && u_check[3] == 'u' && u_check[4] == 't' && u_check[5] == ':') ||
        (u_check[0] == 'h' && u_check[1] == 't' && u_check[2] == 't' && u_check[3] == 'p' && (str_cmp(u_check, "http://archaos.local/") == 0 || str_cmp(u_check, "http://archaos.local") == 0 || str_cmp(u_check, "http://home.local/") == 0 || str_cmp(u_check, "http://home.local") == 0))) {
        if (record_history) browser_push_history(url[0] ? url : "about:home");
        str_cpy(browser_url, url[0] ? url : "about:home", sizeof(browser_url));
        str_cpy(browser_html, showcase_html, sizeof(browser_html));
        str_cpy(browser_tabs[browser_active_tab].title, "Home", sizeof(browser_tabs[0].title));
        str_cpy(browser_status, "Ready - Home", sizeof(browser_status));
        browser_loaded = 1;
        browser_scroll_y = 0;
        browser_nav_focus = BROWSER_FOCUS_URL;
        browser_input_focus = -1;
        ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
        redraw_all_frame(160, 100);
        return;
    }

    if (!net_if.up) {
        str_cpy(browser_status, "Network down", sizeof(browser_status));
        str_cpy(browser_html, "<html><body style=\"background:#ffffff; color:#1c1b22;\"><center><h1 style=\"color:#ef4444;\">Network Offline</h1><p>Please connect or configure network interface.</p><p><a href=\"about:home\" style=\"color:#0284c7;\">Return to Firefox Home</a></p></center></body></html>", sizeof(browser_html));
        browser_loaded = 1;
        ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
        return;
    }

    if (record_history) browser_push_history(url);
    browser_loaded = 0;
    browser_html[0] = '\0';

    char cur_url[256];
    const char *raw_u = url ? url : "";
    while (*raw_u == ' ') raw_u++;
    if (str_ncmp(raw_u, "http://", 7) != 0 && str_ncmp(raw_u, "https://", 8) != 0) {
        const char *dot = raw_u;
        while (*dot && *dot != '.') dot++;
        if (*dot == '\0') {
            browser_submit_search(raw_u);
            return;
        }
        snprintf(cur_url, sizeof(cur_url), "https://%s", raw_u);
    } else {
        str_cpy(cur_url, raw_u, sizeof(cur_url));
    }

    /* DuckDuckGo redirect unwrapping: /l/?uddg=<url_encoded_target> */
    const char *uddg = strstr(cur_url, "uddg=");
    if (uddg) {
        uddg += 5;
        char dec[256];
        int dlen = 0;
        while (*uddg && *uddg != '&' && dlen < (int)sizeof(dec) - 1) {
            if (*uddg == '%' && uddg[1] && uddg[2]) {
                static const char *h = "0123456789ABCDEFabcdef";
                int hi = -1, lo = -1;
                for (int i = 0; h[i]; i++) {
                    if (h[i] == uddg[1]) hi = (i < 16 ? i : i - 6);
                    if (h[i] == uddg[2]) lo = (i < 16 ? i : i - 6);
                }
                if (hi >= 0 && lo >= 0) {
                    dec[dlen++] = (char)((hi << 4) | lo);
                    uddg += 3;
                    continue;
                }
            }
            dec[dlen++] = *uddg++;
        }
        dec[dlen] = '\0';
        if (dlen > 0) {
            str_cpy(cur_url, dec, sizeof(cur_url));
        }
    }

    if (str_cmp(cur_url, "https://duckduckgo.com") == 0 || str_cmp(cur_url, "https://duckduckgo.com/") == 0 ||
        str_cmp(cur_url, "http://duckduckgo.com") == 0 || str_cmp(cur_url, "http://duckduckgo.com/") == 0 ||
        str_cmp(cur_url, "https://lite.duckduckgo.com") == 0 || str_cmp(cur_url, "https://lite.duckduckgo.com/") == 0) {
        str_cpy(cur_url, "https://lite.duckduckgo.com/lite/", sizeof(cur_url));
    } else if (str_cmp(cur_url, "https://html.duckduckgo.com") == 0 || str_cmp(cur_url, "https://html.duckduckgo.com/") == 0) {
        str_cpy(cur_url, "https://html.duckduckgo.com/html/", sizeof(cur_url));
    } else if (str_cmp(cur_url, "http://google.com") == 0 || str_cmp(cur_url, "http://google.com/") == 0 ||
               str_cmp(cur_url, "http://www.google.com") == 0 || str_cmp(cur_url, "http://www.google.com/") == 0 ||
               str_cmp(cur_url, "https://google.com") == 0 || str_cmp(cur_url, "https://google.com/") == 0 ||
               str_cmp(cur_url, "https://www.google.com") == 0 || str_cmp(cur_url, "https://www.google.com/") == 0) {
        str_cpy(cur_url, "https://www.google.com/", sizeof(cur_url));
    } else if (strstr(cur_url, "en.wikipedia.org/") && !strstr(cur_url, "en.m.wikipedia.org/")) {
        char m_url[256];
        const char *p = strstr(cur_url, "en.wikipedia.org/");
        int prefix_len = (int)(p - cur_url);
        if (prefix_len > 120) prefix_len = 120;
        for (int i = 0; i < prefix_len; i++) m_url[i] = cur_url[i];
        m_url[prefix_len] = '\0';
        str_cat(m_url, "en.m.wikipedia.org/", sizeof(m_url));
        str_cat(m_url, p + 17, sizeof(m_url));
        str_cpy(cur_url, m_url, sizeof(cur_url));
    }

    for (int hop = 0; hop < 5; hop++) {
        str_cpy(browser_status, hop > 0 ? "Redirecting..." : "Connecting...", sizeof(browser_status));
        str_cpy(browser_url, cur_url, sizeof(browser_url));
        redraw_all_frame(160, 100);

        const char *u = cur_url;
        while (*u == ' ') u++;
        int is_https = 0;
        const char *hoststart = u;
        uint16_t port = 80;

        if (u[0]=='h' && u[1]=='t' && u[2]=='t' && u[3]=='p' && u[4]=='s' && u[5]==':' && u[6]=='/' && u[7]=='/') {
            is_https = 1;
            port = 443;
            hoststart = u + 8;
        } else if (u[0]=='h' && u[1]=='t' && u[2]=='t' && u[3]=='p' && u[4]==':' && u[5]=='/' && u[6]=='/') {
            is_https = 0;
            port = 80;
            hoststart = u + 7;
        }

        const char *slash = hoststart;
        while (*slash && *slash != '/' && *slash != ':') slash++;
        char hostname[64]; int hn = (int)(slash - hoststart);
        if (hn > 63) hn = 63;
        for (int i = 0; i < hn; i++) hostname[i] = hoststart[i];
        hostname[hn] = '\0';

        if (*slash == ':') {
            port = 0;
            slash++;
            while (*slash >= '0' && *slash <= '9') {
                port = port * 10 + (*slash - '0');
                slash++;
            }
        }
        const char *path = *slash == '/' ? slash : "/";

        uint32_t hip = 0;
        if (!dns_resolve(hostname, &hip)) {
            snprintf(browser_status, sizeof(browser_status), "DNS Failed: %s", hostname);
            snprintf(browser_html, sizeof(browser_html),
                     "<html><head><title>Server Not Found</title></head>"
                     "<body style=\"background:#ffffff; color:#1e293b;\"><center><br />"
                     "<h1 style=\"color:#ef4444;\">Server Not Found</h1>"
                     "<p style=\"color:#334155;\">We can't connect to the server at <b>%s</b>.</p>"
                     "<p style=\"color:#64748b;\">Check the address for typos or verify your internet connection.</p><br />"
                     "<p><a href=\"%s\" style=\"color:#0284c7; font-weight:bold;\">[ Try Again ]</a> &nbsp; "
                     "<a href=\"about:home\" style=\"color:#16a34a; font-weight:bold;\">[ Return to Home ]</a></p>"
                     "</center></body></html>",
                     hostname, cur_url);
            browser_loaded = 1;
            ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
            redraw_all_frame(160, 100);
            return;
        }

        if (is_https) str_cpy(browser_status, "[TLS] Handshake...", sizeof(browser_status));
        else str_cpy(browser_status, "HTTP Fetching...", sizeof(browser_status));
        redraw_all_frame(160, 100);

        int sc = http_get(hostname, hip, port, path, browser_html, sizeof(browser_html));
        if (sc == 0) {
            snprintf(browser_status, sizeof(browser_status), "Connect failed: %s", hostname);
            snprintf(browser_html, sizeof(browser_html),
                     "<html><head><title>Connection Error</title></head>"
                     "<body style=\"background:#ffffff; color:#1e293b;\"><center><br />"
                     "<h1 style=\"color:#ef4444;\">Connection Error</h1>"
                     "<p style=\"color:#334155;\">Unable to connect to <b>%s</b> on port %d.</p><br />"
                     "<p><a href=\"%s\" style=\"color:#0284c7; font-weight:bold;\">[ Retry ]</a> &nbsp; "
                     "<a href=\"about:home\" style=\"color:#16a34a; font-weight:bold;\">[ Return to Home ]</a></p>"
                     "</center></body></html>",
                     hostname, (int)port, cur_url);
            browser_loaded = 1;
            ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
            redraw_all_frame(160, 100);
            return;
        }

        /* Check for 301/302/303/307/308 redirect */
        if (sc == 301 || sc == 302 || sc == 303 || sc == 307 || sc == 308) {
            char next_loc[256] = "";
            http_get_last_redirect(next_loc, sizeof(next_loc));
            if (next_loc[0]) {
                char target_url[256];
                html_resolve_url(cur_url, next_loc, target_url, sizeof(target_url));
                str_cpy(cur_url, target_url, sizeof(cur_url));
                browser_html[0] = '\0';
                continue; /* Auto-follow redirect loop */
            }
        }

        /* Success */
        /* Check if response is a raw binary image (PNG, JPEG, BMP, GIF) */
        uint8_t *bdata = (uint8_t *)browser_html;
        int is_direct_img = 0;
        if ((bdata[0] == 0x89 && bdata[1] == 'P' && bdata[2] == 'N' && bdata[3] == 'G') ||
            (bdata[0] == 0xFF && bdata[1] == 0xD8 && bdata[2] == 0xFF) ||
            (bdata[0] == 'B' && bdata[1] == 'M') ||
            (bdata[0] == 'G' && bdata[1] == 'I' && bdata[2] == 'F' && bdata[3] == '8')) {
            is_direct_img = 1;
        }

        if (is_direct_img) {
            image_fetch_and_cache(cur_url, NULL);
            snprintf(browser_html, sizeof(browser_html),
                     "<html><head><title>Image (%s)</title></head><body style='background:#111;text-align:center;'><center><br><img src='%s' alt='%s'></center></body></html>",
                     hostname, cur_url, hostname);
        }

        str_cpy(browser_status, is_https ? "[TLS 1.2] 200 OK" : "200 OK - Ready", sizeof(browser_status));
        str_cpy(browser_url, cur_url, sizeof(browser_url));
        browser_loaded = 1;
        browser_scroll_y = 0;
        browser_input_focus = -1;
        ns_engine_parse_html(browser_html, str_len(browser_html), browser_url);
        const char *t = ns_engine_get_title();
        if (t && t[0]) {
            str_cpy(browser_tabs[browser_active_tab].title, t, sizeof(browser_tabs[0].title));
        } else {
            str_cpy(browser_tabs[browser_active_tab].title, hostname, sizeof(browser_tabs[0].title));
        }
        redraw_all_frame(160, 100);
        break;
    }

    js_set_alert_callback(show_browser_alert);
    js_set_navigate_callback(browser_fetch);
}

static void browser_fetch(const char *url) {
    browser_fetch_internal(url, 1);
}

static void draw_browser(gui_window_t *w)
{
    /* Initialize default tab if needed */
    if (!browser_loaded || browser_html[0] == '\0') {
        browser_tab_init_defaults(0);
        browser_fetch_internal("about:home", 0);
    }

    /* Row 1: Firefox Tab Bar */
    int tab_bar_y = w->y + 13;
    fill_rect(w->x + 2, tab_bar_y, w->w - 4, 12, GRAY(4));
    draw_rect(w->x + 2, tab_bar_y, w->w - 4, 12, GRAY(2));

    int tab_area_w = w->w - 24;
    int tab_w = tab_area_w / browser_tab_count;
    if (tab_w > 90) tab_w = 90;
    if (tab_w < 45) tab_w = 45;

    for (int i = 0; i < browser_tab_count; i++) {
        int tx = w->x + 4 + i * (tab_w + 2);
        int is_active = (i == browser_active_tab);
        if (is_active) {
            fill_rect(tx, tab_bar_y + 1, tab_w, 11, GRAY(20));
            draw_rect(tx, tab_bar_y + 1, tab_w, 11, GRAY(12));
            draw_str(tx + 3, tab_bar_y + 2, "o", RGB(0,2,5), GRAY(20));
            draw_str_clip(tx + 11, tab_bar_y + 2, browser_tabs[i].title, COL_BLACK, GRAY(20), tx + tab_w - 11);
            draw_str(tx + tab_w - 9, tab_bar_y + 2, "x", GRAY(10), GRAY(20));
        } else {
            fill_rect(tx, tab_bar_y + 2, tab_w, 10, GRAY(7));
            draw_rect(tx, tab_bar_y + 2, tab_w, 10, GRAY(5));
            draw_str(tx + 3, tab_bar_y + 3, "o", GRAY(14), GRAY(7));
            draw_str_clip(tx + 11, tab_bar_y + 3, browser_tabs[i].title, GRAY(18), GRAY(7), tx + tab_w - 11);
            draw_str(tx + tab_w - 9, tab_bar_y + 3, "x", GRAY(12), GRAY(7));
        }
    }

    /* [ + ] New Tab Button */
    int plus_x = w->x + 4 + browser_tab_count * (tab_w + 2);
    fill_rect(plus_x, tab_bar_y + 2, 12, 10, GRAY(9));
    draw_rect(plus_x, tab_bar_y + 2, 12, 10, GRAY(6));
    draw_str(plus_x + 3, tab_bar_y + 3, "+", COL_WHITE, GRAY(9));

    /* Row 2: Navigation & Pill Address Bar */
    int nav_y = w->y + 25;
    fill_rect(w->x + 2, nav_y, w->w - 4, 14, GRAY(20));
    draw_rect(w->x + 2, nav_y, w->w - 4, 14, GRAY(10));

    /* [ < ] Back Button */
    int back_en = (browser_hist_pos > 0);
    fill_rect(w->x + 4, nav_y + 2, 12, 10, back_en ? GRAY(23) : GRAY(16));
    draw_rect(w->x + 4, nav_y + 2, 12, 10, back_en ? GRAY(8) : GRAY(14));
    draw_str(w->x + 7, nav_y + 3, "<", back_en ? COL_BLACK : GRAY(12), back_en ? GRAY(23) : GRAY(16));

    /* [ > ] Forward Button */
    int fwd_en = (browser_hist_pos < browser_hist_count - 1 && browser_hist_count > 0);
    fill_rect(w->x + 18, nav_y + 2, 12, 10, fwd_en ? GRAY(23) : GRAY(16));
    draw_rect(w->x + 18, nav_y + 2, 12, 10, fwd_en ? GRAY(8) : GRAY(14));
    draw_str(w->x + 21, nav_y + 3, ">", fwd_en ? COL_BLACK : GRAY(12), fwd_en ? GRAY(23) : GRAY(16));

    /* [ ⟳ ] Reload Button */
    fill_rect(w->x + 32, nav_y + 2, 12, 10, GRAY(23));
    draw_rect(w->x + 32, nav_y + 2, 12, 10, GRAY(8));
    draw_str(w->x + 35, nav_y + 3, "R", COL_BLACK, GRAY(23));

    /* Rounded Pill Address Bar */
    int url_w = w->w - 84;
    int is_https = (str_ncmp(browser_url, "https://", 8) == 0);
    fill_rect(w->x + 46, nav_y + 2, url_w, 10, COL_WHITE);
    draw_rect(w->x + 46, nav_y + 2, url_w, 10, (browser_nav_focus == BROWSER_FOCUS_URL || browser_url_focus) ? RGB(0,2,5) : GRAY(12));
    /* Security Badge */
    draw_str(w->x + 48, nav_y + 3, is_https ? "S" : "*", is_https ? RGB(0,4,1) : GRAY(10), COL_WHITE);
    
    int max_url_chars = (url_w - 22) / 6;
    if (max_url_chars < 5) max_url_chars = 5;
    if (browser_url_cursor < browser_url_scroll) browser_url_scroll = browser_url_cursor;
    if (browser_url_cursor > browser_url_scroll + max_url_chars) browser_url_scroll = browser_url_cursor - max_url_chars;
    if (browser_url_scroll < 0) browser_url_scroll = 0;

    int ulen = str_len(browser_url);
    const char *visible_url = (browser_url_scroll < ulen) ? (browser_url + browser_url_scroll) : "";
    draw_str_clip(w->x + 56, nav_y + 3, visible_url, COL_BLACK, COL_WHITE, w->x + 46 + url_w - 12);

    /* Text Cursor in URL bar when focused */
    if (browser_nav_focus == BROWSER_FOCUS_URL || browser_url_focus) {
        int cur_x = w->x + 56 + (browser_url_cursor - browser_url_scroll) * 6;
        if (cur_x >= w->x + 56 && cur_x <= w->x + 46 + url_w - 12) {
            fill_rect(cur_x, nav_y + 2, 1, 9, RGB(0,2,5));
        }
    }
    
    /* Bookmark Star */
    draw_str(w->x + 46 + url_w - 9, nav_y + 3, "*", RGB(5,4,0), COL_WHITE);

    /* [ Go ] Button */
    int go_x = w->x + 46 + url_w + 3;
    fill_rect(go_x, nav_y + 2, 26, 10, RGB(0,2,4));
    draw_rect(go_x, nav_y + 2, 26, 10, RGB(0,1,3));
    draw_str(go_x + 7, nav_y + 3, "Go", COL_WHITE, RGB(0,2,4));

    /* Row 3: Bookmarks Toolbar */
    int bm_y = w->y + 39;
    fill_rect(w->x + 2, bm_y, w->w - 4, 11, GRAY(22));
    draw_hline(w->x + 2, bm_y + 10, w->w - 4, GRAY(13));
    draw_str(w->x + 4, bm_y + 2, "*", RGB(5,4,0), GRAY(22));
    draw_str(w->x + 12, bm_y + 2, "Home", RGB(0,2,4), GRAY(22));
    draw_str(w->x + 40, bm_y + 2, "|", GRAY(16), GRAY(22));
    draw_str(w->x + 46, bm_y + 2, "DuckDuckGo", RGB(0,2,4), GRAY(22));
    draw_str(w->x + 114, bm_y + 2, "|", GRAY(16), GRAY(22));
    draw_str(w->x + 120, bm_y + 2, "Wikipedia", RGB(0,2,4), GRAY(22));
    draw_str(w->x + 182, bm_y + 2, "|", GRAY(16), GRAY(22));
    draw_str(w->x + 188, bm_y + 2, "Media", RGB(0,2,4), GRAY(22));

    /* Row 4: Web Viewport */
    int view_y = w->y + 50;
    int view_h = w->h - (browser_media_active ? 77 : 63);

    /* Render HTML Viewport via NetSurf Engine */
    ns_engine_render(w->x + 2, view_y, w->w - 4, view_h, browser_scroll_y, browser_nav_focus);

    /* Focus Ring on URL bar */
    if (browser_nav_focus == BROWSER_FOCUS_URL) {
        draw_rect(w->x + 45, nav_y + 1, url_w + 2, 12, COL_YELLOW);
    }

    /* Scrollbar Indicator */
    int sb_track_h = view_h - 4;
    int sb_x = w->x + w->w - 6;
    fill_rect(sb_x, view_y + 2, 4, sb_track_h, GRAY(21));
    draw_rect(sb_x, view_y + 2, 4, sb_track_h, GRAY(15));
    int thumb_h = 16;
    int max_scroll = ns_engine_get_content_height() - view_h;
    if (max_scroll < 1) max_scroll = 1;
    int thumb_y = view_y + 2 + (browser_scroll_y * (sb_track_h - thumb_h)) / max_scroll;
    if (thumb_y > view_y + 2 + sb_track_h - thumb_h) thumb_y = view_y + 2 + sb_track_h - thumb_h;
    if (thumb_y < view_y + 2) thumb_y = view_y + 2;
    fill_rect(sb_x, thumb_y, 4, thumb_h, GRAY(10));

    /* Row 4.5: Docked In-Browser Media Player Bar */
    if (browser_media_active) {
        int mb_y = w->y + w->h - 26;
        int mb_x = w->x + 2;
        int mb_w = w->w - 4;
        fill_rect(mb_x, mb_y, mb_w, 14, RGB(0,1,2));
        draw_rect(mb_x, mb_y, mb_w, 14, browser_media_is_video ? RGB(0,3,5) : RGB(0,4,2));

        /* Badge */
        draw_str(mb_x + 3, mb_y + 3, browser_media_is_video ? "[Vid]" : "[Aud]", browser_media_is_video ? RGB(0,4,5) : RGB(0,5,2), RGB(0,1,2));

        /* Play/Pause button */
        bool playing = browser_media_is_video ? (video_get_state() == VIDEO_STATE_PLAYING) : (audio_get_state() == AUDIO_STATE_PLAYING);
        fill_rect(mb_x + 36, mb_y + 2, 12, 10, RGB(0,2,4));
        draw_rect(mb_x + 36, mb_y + 2, 12, 10, GRAY(12));
        draw_str(mb_x + 39, mb_y + 3, playing ? "||" : ">", COL_WHITE, RGB(0,2,4));

        /* Stop button */
        fill_rect(mb_x + 50, mb_y + 2, 12, 10, RGB(2,1,1));
        draw_rect(mb_x + 50, mb_y + 2, 12, 10, GRAY(12));
        draw_str(mb_x + 53, mb_y + 3, "#", COL_WHITE, RGB(2,1,1));

        /* Title */
        draw_str_clip(mb_x + 66, mb_y + 3, browser_media_title, COL_WHITE, RGB(0,1,2), mb_x + mb_w - 75);

        /* Mini live spectrum visualizer */
        if (playing && !browser_media_is_video) {
            uint8_t spec[16];
            audio_get_spectrum(spec, 16);
            for (int si = 0; si < 5; si++) {
                int bh = (spec[si * 3] * 7) / 32 + 1;
                fill_rect(mb_x + mb_w - 72 + si * 4, mb_y + 10 - bh, 3, bh, RGB(0,5,2));
            }
        }

        /* Dual Timecode: elapsed / total duration */
        uint32_t elap_s = (browser_media_is_video ? video_get_elapsed_ms() : audio_get_elapsed_ms()) / 1000;
        uint32_t dur_s  = (browser_media_is_video ? video_get_duration_ms() : audio_get_duration_ms()) / 1000;
        char tc[24];
        if (dur_s > 0) {
            snprintf(tc, sizeof(tc), "%02u:%02u/%02u:%02u", elap_s / 60, elap_s % 60, dur_s / 60, dur_s % 60);
        } else {
            snprintf(tc, sizeof(tc), "%02u:%02u", elap_s / 60, elap_s % 60);
        }
        draw_str(mb_x + mb_w - 105, mb_y + 3, tc, GRAY(18), RGB(0,1,2));

        /* Fullscreen button [FS] */
        fill_rect(mb_x + mb_w - 28, mb_y + 2, 12, 10, RGB(0,2,4));
        draw_rect(mb_x + mb_w - 28, mb_y + 2, 12, 10, GRAY(10));
        draw_str(mb_x + mb_w - 27, mb_y + 3, "FS", COL_WHITE, RGB(0,2,4));

        /* Close button [x] */
        fill_rect(mb_x + mb_w - 14, mb_y + 2, 11, 10, RGB(2,0,0));
        draw_rect(mb_x + mb_w - 14, mb_y + 2, 11, 10, GRAY(8));
        draw_str(mb_x + mb_w - 11, mb_y + 3, "x", COL_WHITE, RGB(2,0,0));

        /* Scrubber Bar with Buffer Gauge */
        uint32_t dur = browser_media_is_video ? video_get_duration_ms() : audio_get_duration_ms();
        uint32_t elap = browser_media_is_video ? video_get_elapsed_ms() : audio_get_elapsed_ms();
        int sw = mb_w - 20;
        int sp = (dur > 0) ? (int)(((uint32_t)elap * (uint32_t)sw) / dur) : 0;
        if (sp > sw) sp = sw;
        draw_hline(mb_x + 10, mb_y + 12, sw, GRAY(6));
        /* Buffer gauge indicator */
        int buf_w = (sw * 9) / 10;
        draw_hline(mb_x + 10, mb_y + 12, buf_w, GRAY(10));
        if (sp > 0) draw_hline(mb_x + 10, mb_y + 12, sp, browser_media_is_video ? RGB(0,4,5) : RGB(0,5,2));

        /* Hover Tooltip on Scrubber */
        if (mouse_y >= mb_y + 10 && mouse_y <= mb_y + 15 && mouse_x >= mb_x + 10 && mouse_x <= mb_x + 10 + sw) {
            uint32_t hov_s = (dur_s > 0) ? (uint32_t)(((mouse_x - (mb_x + 10)) * dur_s) / sw) : 0;
            char hov_tc[12];
            snprintf(hov_tc, sizeof(hov_tc), "%02u:%02u", hov_s / 60, hov_s % 60);
            int tip_x = mouse_x - 14;
            if (tip_x < mb_x + 2) tip_x = mb_x + 2;
            if (tip_x + 32 > mb_x + mb_w) tip_x = mb_x + mb_w - 32;
            fill_rect(tip_x, mb_y - 8, 30, 9, RGB(0,1,3));
            draw_rect(tip_x, mb_y - 8, 30, 9, COL_WHITE);
            draw_str(tip_x + 2, mb_y - 7, hov_tc, COL_WHITE, RGB(0,1,3));
        }
    }

    /* Row 5: Status Bar */
    int sb_y = w->y + w->h - 12;
    fill_rect(w->x + 2, sb_y, w->w - 4, 10, GRAY(20));
    draw_rect(w->x + 2, sb_y, w->w - 4, 10, GRAY(10));
    draw_str(w->x + 6, sb_y + 2, browser_status, COL_BLACK, GRAY(20));
}

/* ============================================================
 * DRAW APP CONTENTS
 * ============================================================ */

static void draw_calculator(gui_window_t *w)
{
    fill_rect(w->x + 8, w->y + 16, w->w - 16, 12, COL_WHITE);
    draw_rect(w->x + 8, w->y + 16, w->w - 16, 12, COL_BLACK);
    draw_str(w->x + w->w - 20 - str_len(calc_display)*6, w->y + 18, calc_display, COL_BLACK, COL_WHITE);

    static const char *btns[4][4] = {
        {"7","8","9","/"},
        {"4","5","6","*"},
        {"1","2","3","-"},
        {"0","C","=","+"}
    };
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int bx = w->x + 10 + c * 23;
            int by = w->y + 34 + r * 18;
            fill_rect(bx, by, 20, 15, GRAY(18));
            draw_rect(bx, by, 20, 15, GRAY(8));
            draw_str(bx + 7, by + 4, btns[r][c], COL_BLACK, GRAY(18));
        }
}

/* ============================================================
 * PAINTER — Fully Dynamic, Window-Size-Aware
 *
 * Layout (relative to window):
 *   Toolbar:  y+13..y+23   (10px tall, action buttons left, color swatches right)
 *   Canvas:   y+25..y+h-14 (fills the window body)
 *   Status:   y+h-12..y+h  (filename + current tool)
 * ============================================================ */

/* Returns the painter canvas rect in screen coords given the window */
static void paint_canvas_rect(gui_window_t *w, int *cx, int *cy, int *cw, int *ch) {
    *cx = w->x + 4;
    *cy = w->y + 25;
    *cw = w->w - 8;
    *ch = w->h - 38;
    if (*cw < 10) *cw = 10;
    if (*ch < 10) *ch = 10;
}

/* Palette colors available in painter */
static const uint8_t PAINT_PALETTE[12] = {
    COL_BLACK, COL_WHITE, COL_RED, COL_GREEN, COL_BLUE,
    COL_YELLOW, COL_TEAL, COL_ORANGE, COL_PURPLE, COL_CYAN,
    COL_PINK,  COL_LIME
};
#define PAINT_PALETTE_COUNT 12

static void draw_painter(gui_window_t *w)
{
    int toolbar_y = w->y + 13;

    /* ---- Toolbar Background ---- */
    fill_rect(w->x + 2, toolbar_y, w->w - 4, 11, GRAY(16));

    /* Tool buttons: Clr Sav Opn | Pnc Bsh Ers Fil */
    static const char *tbtns[7] = { "Clr","Sav","Opn","Pnc","Bsh","Ers","Fil" };
    for (int i = 0; i < 3; i++) {
        int bx = w->x + 4 + i * 26;
        fill_rect(bx, toolbar_y + 1, 24, 9, GRAY(20));
        draw_rect(bx, toolbar_y + 1, 24, 9, GRAY(8));
        draw_str(bx + 2, toolbar_y + 2, tbtns[i], COL_BLACK, GRAY(20));
    }
    /* Separator */
    for (int dy=0; dy<9; dy++) put_pixel(w->x + 82, toolbar_y + 1 + dy, GRAY(8));

    int tool_sel_pnc = (paint_brush_size == 1 && !paint_eraser && !paint_fill);
    int tool_sel_bsh = (paint_brush_size == 3 && !paint_eraser && !paint_fill);
    int tool_sel_ers = paint_eraser;
    int tool_sel_fil = paint_fill;

    int tbx[4] = { w->x + 85, w->x + 111, w->x + 137, w->x + 163 };
    int tact[4] = { tool_sel_pnc, tool_sel_bsh, tool_sel_ers, tool_sel_fil };
    for (int i = 0; i < 4; i++) {
        if (tbx[i] + 24 > w->x + w->w - 2) break;
        uint8_t bg = tact[i] ? GRAY(10) : GRAY(20);
        fill_rect(tbx[i], toolbar_y + 1, 24, 9, bg);
        draw_rect(tbx[i], toolbar_y + 1, 24, 9, tact[i] ? COL_TEAL : GRAY(8));
        draw_str(tbx[i] + 2, toolbar_y + 2, tbtns[3 + i], COL_BLACK, bg);
    }

    /* ---- Canvas ---- */
    int cx, cy, cw, ch;
    paint_canvas_rect(w, &cx, &cy, &cw, &ch);
    fill_rect(cx, cy, cw, ch, COL_WHITE);
    draw_rect(cx, cy, cw, ch, COL_BLACK);

    /* Blit canvas — scale logical canvas to the actual display rect */
    int draw_w = cw - 2, draw_h = ch - 2;
    for (int row = 0; row < draw_h; row++) {
        int src_row = (row * PAINT_CH) / draw_h;
        if (src_row >= PAINT_CH) src_row = PAINT_CH - 1;
        for (int col = 0; col < draw_w; col++) {
            int src_col = (col * PAINT_CW) / draw_w;
            if (src_col >= PAINT_CW) src_col = PAINT_CW - 1;
            put_pixel(cx + 1 + col, cy + 1 + row, paint_canvas[src_row * PAINT_CW + src_col]);
        }
    }

    /* ---- Color Palette Row (bottom) ---- */
    int pal_y = w->y + w->h - 12;
    int swatch_w = (w->w - 8) / PAINT_PALETTE_COUNT;
    if (swatch_w < 4) swatch_w = 4;
    for (int i = 0; i < PAINT_PALETTE_COUNT; i++) {
        int px = w->x + 4 + i * swatch_w;
        uint8_t border = (paint_color == PAINT_PALETTE[i] && !paint_eraser) ? COL_WHITE : COL_BLACK;
        fill_rect(px, pal_y, swatch_w - 1, 9, PAINT_PALETTE[i]);
        draw_rect(px, pal_y, swatch_w - 1, 9, border);
    }

    /* ---- Status bar: filename / current color indicator ---- */
    int stat_y = w->y + w->h - 12;
    (void)stat_y; /* palette already at stat_y; overlaps intentionally */
}

static void draw_minesweeper(gui_window_t *w)
{
    fill_rect(w->x + 8, w->y + 16, w->w - 16, 14, GRAY(18));
    draw_str(w->x + 12, w->y + 20, "08", COL_RED, GRAY(18));
    const char *face = mine_game_over ? "X(" : (mine_win ? ":-D" : ":-)");
    draw_str(w->x + (w->w/2) - 6, w->y + 20, face, COL_BLACK, GRAY(18));

    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int gx = w->x + 12 + c * 11;
            int gy = w->y + 34 + r * 8;
            if (mine_revealed[r][c] == 1) {
                fill_rect(gx, gy, 10, 7, GRAY(20));
                if (mine_grid[r][c] == 9) draw_char(gx + 2, gy, '*', COL_RED, GRAY(20));
                else if (mine_grid[r][c] > 0) {
                    char nstr[2] = {'0' + mine_grid[r][c], '\0'};
                    draw_str(gx + 2, gy, nstr, COL_BLUE, GRAY(20));
                }
            } else if (mine_revealed[r][c] == 2) {
                fill_rect(gx, gy, 10, 7, GRAY(14));
                draw_rect(gx, gy, 10, 7, GRAY(8));
                draw_char(gx + 3, gy, '!', COL_RED, GRAY(14));
            } else {
                fill_rect(gx, gy, 10, 7, GRAY(14));
                draw_rect(gx, gy, 10, 7, GRAY(8));
            }
        }
}

static void draw_fileman(gui_window_t *w)
{
    int body_y = w->y + 29;
    int body_w = w->w - 16;
    int body_h = w->h - 37;
    fill_rect(w->x + 8, body_y, body_w, body_h, COL_WHITE);
    draw_rect(w->x + 8, body_y, body_w, body_h, COL_BLACK);

    /* Action Toolbar: [..] [+File] [+Dir] [Edit] [Props] */
    int tby = w->y + 15;
    fill_rect(w->x + 8, tby, 16, 11, GRAY(18));
    draw_rect(w->x + 8, tby, 16, 11, GRAY(8));
    draw_str(w->x + 10, tby + 2, "..", COL_BLACK, GRAY(18));

    fill_rect(w->x + 26, tby, 34, 11, GRAY(18));
    draw_rect(w->x + 26, tby, 34, 11, GRAY(8));
    draw_str(w->x + 28, tby + 2, "+File", COL_BLACK, GRAY(18));

    fill_rect(w->x + 62, tby, 28, 11, GRAY(18));
    draw_rect(w->x + 62, tby, 28, 11, GRAY(8));
    draw_str(w->x + 64, tby + 2, "+Dir", COL_BLACK, GRAY(18));

    fill_rect(w->x + 92, tby, 26, 11, GRAY(18));
    draw_rect(w->x + 92, tby, 26, 11, GRAY(8));
    draw_str(w->x + 94, tby + 2, "Edit", COL_BLACK, GRAY(18));

    fill_rect(w->x + 120, tby, 30, 11, GRAY(18));
    draw_rect(w->x + 120, tby, 30, 11, GRAY(8));
    draw_str(w->x + 122, tby + 2, "Props", COL_BLACK, GRAY(18));

    fs_node_t *dir = fileman_cur_dir ? fileman_cur_dir : fs_root();
    int max_items = (body_h - 6) / 9;
    if (dir) {
        for (int i = 0; i < dir->child_count && i < max_items; i++) {
            fs_node_t *c = dir->children[i];
            uint8_t rbg = (fileman_selected == i) ? RGB(0,2,4) : COL_WHITE;
            uint8_t rfg = (fileman_selected == i) ? COL_WHITE : COL_BLACK;
            uint8_t tfg = (fileman_selected == i) ? COL_YELLOW : COL_DARK_GRAY;

            if (fileman_selected == i) {
                fill_rect(w->x + 9, body_y + 2 + i * 9, body_w - 2, 9, rbg);
            }

            draw_str(w->x + 12, body_y + 3 + i * 9, c->type == FS_DIR ? "[DIR]" : "[FILE]", tfg, rbg);
            draw_str_clip(w->x + 52, body_y + 3 + i * 9, c->name, rfg, rbg, w->x + w->w - 10);
        }
    }
}

static void draw_notepad(gui_window_t *w)
{
    /* Action Bar: [Clr] [Sav] [Opn] File: note.txt */
    fill_rect(w->x + 6, w->y + 16, 26, 12, GRAY(18));
    draw_rect(w->x + 6, w->y + 16, 26, 12, GRAY(8));
    draw_str(w->x + 8, w->y + 18, "Clr", COL_BLACK, GRAY(18));

    fill_rect(w->x + 34, w->y + 16, 26, 12, GRAY(18));
    draw_rect(w->x + 34, w->y + 16, 26, 12, GRAY(8));
    draw_str(w->x + 36, w->y + 18, "Sav", COL_BLACK, GRAY(18));

    fill_rect(w->x + 62, w->y + 16, 26, 12, GRAY(18));
    draw_rect(w->x + 62, w->y + 16, 26, 12, GRAY(8));
    draw_str(w->x + 64, w->y + 18, "Opn", COL_BLACK, GRAY(18));

    draw_str_clip(w->x + 92, w->y + 18, notepad_filename, COL_BLUE, COL_WINBG, w->x + w->w - 6);

    /* Text Canvas Area */
    int tx = w->x + 8, ty = w->y + 30, tw = w->w - 16, th = w->h - 38;
    fill_rect(tx, ty, tw, th, COL_WHITE);
    draw_rect(tx, ty, tw, th, COL_BLACK);

    /* Render Notepad Text */
    int cur_x = tx + 4, cur_y = ty + 4;
    for (int i = 0; i < notepad_len; i++) {
        char ch = notepad_buf[i];
        if (ch == '\n') {
            cur_x = tx + 4;
            cur_y += 9;
            if (cur_y > ty + th - 10) break;
        } else {
            draw_char(cur_x, cur_y, ch, COL_BLACK, COL_WHITE);
            cur_x += 6;
            if (cur_x > tx + tw - 8) {
                cur_x = tx + 4;
                cur_y += 9;
                if (cur_y > ty + th - 10) break;
            }
        }
    }

    /* Cursor */
    if ((pit_ticks() / 500) % 2 == 0 && w->focused) {
        fill_rect(cur_x, cur_y, 5, 7, COL_BLACK);
    }
}

/* ============================================================
 * AI ASSISTANT CHAT APPLICATION (APP_AI_CHAT)
 * ============================================================ */
static char ai_chat_input[128] = "";
static int  ai_chat_input_len = 0;
static char ai_chat_status[48] = "Ready";

static void ai_chat_send_message(void) {
    if (ai_chat_input_len == 0) return;
    char prompt[128];
    str_cpy(prompt, ai_chat_input, sizeof(prompt));
    ai_chat_input[0] = '\0';
    ai_chat_input_len = 0;

    str_cpy(ai_chat_status, "Thinking...", sizeof(ai_chat_status));
    sound_click();
    redraw_all_frame(mouse_x, mouse_y);

    int online_ok = 0;
    if (net_if.up) {
        static char ai_resp[4096];
        if (nim_query(prompt, ai_resp, sizeof(ai_resp)) && ai_resp[0]) {
            str_cpy(ai_chat_status, "Ready", sizeof(ai_chat_status));
            sound_tone(523, 20);
            online_ok = 1;
        }
    }
    if (!online_ok) {
        char resp[128];
        ai_get_response(prompt, resp, sizeof(resp));
        nim_history_add("user", prompt);
        nim_history_add("assistant", resp);
        str_cpy(ai_chat_status, "Ready (Offline)", sizeof(ai_chat_status));
        sound_tone(523, 20);
    }
}

static void draw_ai_chat(gui_window_t *w)
{
    fill_rect(w->x + 1, w->y + 12, w->w - 2, w->h - 13, GRAY(4));

    /* Header Bar */
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "ArchaOS AI Copilot [%s]", ai_chat_status);
    draw_str_clip(w->x + 6, w->y + 14, hdr, COL_WHITE, GRAY(4), w->x + w->w - 6);

    /* Chat Transcript View */
    int tx = w->x + 5, ty = w->y + 24, tw = w->w - 10, th = w->h - 44;
    fill_rect(tx, ty, tw, th, RGB(0, 0, 1));
    draw_rect(tx, ty, tw, th, GRAY(8));

    int hist_cnt = nim_history_count();
    if (hist_cnt == 0) {
        draw_str(tx + 8, ty + 12, "AI Assistant initialized.", GRAY(14), RGB(0, 0, 1));
        draw_str(tx + 8, ty + 24, "Type a question below to chat.", GRAY(10), RGB(0, 0, 1));
        draw_str(tx + 8, ty + 36, "Context is remembered across turns!", COL_CYAN, RGB(0, 0, 1));
    } else {
        int cur_y = ty + 4;
        int start_h = (hist_cnt > 6) ? hist_cnt - 6 : 0;
        for (int i = start_h; i < hist_cnt && cur_y + 10 < ty + th; i++) {
            const nim_message_t *m = nim_history_get(i);
            if (!m) continue;
            int is_user = (m->role[0] == 'u');
            uint8_t badge_col = is_user ? COL_CYAN : COL_LIME;
            draw_str(tx + 4, cur_y, is_user ? "You:" : "AI:", badge_col, RGB(0, 0, 1));
            draw_str_clip(tx + 30, cur_y, m->content, is_user ? COL_WHITE : GRAY(18), RGB(0, 0, 1), tx + tw - 6);
            cur_y += 12;
        }
    }

    /* Input Bar */
    int iy = w->y + w->h - 17;
    int iw = w->w - 75;
    fill_rect(tx, iy, iw, 12, COL_WHITE);
    draw_rect(tx, iy, iw, 12, COL_BLACK);
    draw_str_clip(tx + 3, iy + 2, ai_chat_input, COL_BLACK, COL_WHITE, tx + iw - 8);

    /* Blinking cursor */
    if ((pit_ticks() / 500) % 2 == 0 && w->focused) {
        int cx = tx + 3 + str_len(ai_chat_input) * 6;
        if (cx < tx + iw - 6) fill_rect(cx, iy + 2, 4, 8, COL_BLUE);
    }

    /* [Send] Button */
    fill_rect(tx + iw + 2, iy, 34, 12, COL_BLUE);
    draw_rect(tx + iw + 2, iy, 34, 12, GRAY(2));
    draw_str(tx + iw + 5, iy + 2, "Send", COL_WHITE, COL_BLUE);

    /* [Clr] Button */
    fill_rect(tx + iw + 38, iy, 26, 12, GRAY(14));
    draw_rect(tx + iw + 38, iy, 26, 12, GRAY(2));
    draw_str(tx + iw + 41, iy + 2, "Clr", COL_BLACK, GRAY(14));
}

static void handle_ai_chat_click(gui_window_t *w, int mx, int my) {
    int tx = w->x + 5;
    int iy = w->y + w->h - 17;
    int iw = w->w - 75;

    /* Send Button */
    if (mx >= tx + iw + 2 && mx <= tx + iw + 36 && my >= iy && my <= iy + 12) {
        ai_chat_send_message();
    }
    /* Clear Button */
    else if (mx >= tx + iw + 38 && mx <= tx + iw + 64 && my >= iy && my <= iy + 12) {
        nim_history_clear();
        sound_clear();
    }
}

/* Browser Audio & Video Media Link Detection */
static int str_contains_case(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    int hlen = str_len(haystack);
    int nlen = str_len(needle);
    if (nlen > hlen) return 0;
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            char ch = haystack[i + j];
            char cn = needle[j];
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
            if (cn >= 'A' && cn <= 'Z') cn = (char)(cn + 32);
            if (ch != cn) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static int browser_is_audio_media(const char *url, const char *text) {
    if (!url) return 0;
    if (str_contains_case(url, ".ogg") || str_contains_case(url, ".oga") ||
        str_contains_case(url, ".wav") || str_contains_case(url, ".mp3") ||
        str_contains_case(url, ".au")  || str_contains_case(url, ".snd") ||
        str_contains_case(url, ".aiff")|| str_contains_case(url, ".pcm") ||
        str_contains_case(url, ".flac")|| str_contains_case(url, ".aac") ||
        str_contains_case(url, ".m4a") || str_contains_case(url, ".mid")) {
        return 1;
    }
    if (text && (str_contains_case(text, "pronunciation") ||
                 str_contains_case(text, "listen") ||
                 str_contains_case(text, "audio") ||
                 str_contains_case(text, "play audio") ||
                 str_contains_case(text, "speech") ||
                 str_contains_case(text, "sound") ||
                 str_contains_case(text, "voice") ||
                 str_contains_case(text, "ogg") ||
                 str_contains_case(text, "mp3") ||
                 str_contains_case(text, "wav"))) {
        return 1;
    }
    return 0;
}

static int browser_is_video_media(const char *url, const char *text) {
    if (!url) return 0;
    if (str_contains_case(url, ".mp4") || str_contains_case(url, ".webm") ||
        str_contains_case(url, ".ogv") || str_contains_case(url, ".avi") ||
        str_contains_case(url, ".vid") || str_contains_case(url, ".mpg") ||
        str_contains_case(url, ".mpeg")|| str_contains_case(url, ".m1v") ||
        str_contains_case(url, ".gif") || str_contains_case(url, ".mov") ||
        str_contains_case(url, ".mkv")) {
        return 1;
    }
    if (text && (str_contains_case(text, "video") || str_contains_case(text, "watch") || str_contains_case(text, "clip") || str_contains_case(text, "motion"))) return 1;
    return 0;
}

static void draw_cpanel(gui_window_t *w)
{
    draw_str(w->x + 10, w->y + 18, "ArchaOS v0.5 Settings", COL_BLACK, COL_WINBG);
    draw_hline(w->x + 8, w->y + 28, w->w - 16, GRAY(10));

    draw_str(w->x + 10, w->y + 33, "Themes:", COL_BLACK, COL_WINBG);
    static const char *tbtn[4] = {"Teal","Win31","Matrix","Amber"};
    for (int i=0; i<4; i++) {
        int tx = w->x + 10 + (i%2)*68;
        int ty = w->y + 44 + (i/2)*16;
        uint8_t active = 0;
        if (i == 0 && gui_desktop_color == RGB(0,3,3)) active = 1;
        else if (i == 1 && gui_desktop_color == RGB(0,1,4)) active = 1;
        else if (i == 2 && gui_desktop_color == RGB(0,3,0)) active = 1;
        else if (i == 3 && gui_desktop_color == RGB(5,3,0)) active = 1;
        uint8_t bg = active ? GRAY(10) : GRAY(18);
        fill_rect(tx, ty, 64, 12, bg);
        draw_rect(tx, ty, 64, 12, active ? COL_TEAL : GRAY(8));
        draw_str(tx + 4, ty + 2, tbtn[i], COL_BLACK, bg);
    }

    draw_str(w->x + 10, w->y + 79, "Wallpapers:", COL_BLACK, COL_WINBG);
    static const char *wpbtn[4] = {"Solid","Stars","Grid","Sunset"};
    for (int i=0; i<4; i++) {
        int wx = w->x + 10 + (i%2)*68;
        int wy = w->y + 90 + (i/2)*16;
        uint8_t active = (gui_wallpaper_type == i);
        uint8_t bg = active ? GRAY(10) : GRAY(18);
        fill_rect(wx, wy, 64, 12, bg);
        draw_rect(wx, wy, 64, 12, active ? COL_TEAL : GRAY(8));
        draw_str(wx + 4, wy + 2, wpbtn[i], COL_BLACK, bg);
    }
}

/* ============================================================
 * PC SPEAKER UI SOUND EFFECTS
 * ============================================================ */

static void sound_tone(uint32_t freq_hz, uint32_t ms) {
    if (freq_hz == 0) return;
    uint32_t div = 1193180 / freq_hz;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));
    outb(0x61, inb(0x61) | 3);
    pit_sleep(ms);
    outb(0x61, inb(0x61) & ~3);
}

static void sound_click(void)    { sound_tone(900,  4); }
static void sound_win_open(void) { sound_tone(600, 10); sound_tone(900, 14); }
static void sound_win_close(void){ sound_tone(700, 10); sound_tone(450, 14); }
static void sound_win(void)      { sound_tone(523, 25); sound_tone(659, 25); sound_tone(784, 45); }
static void sound_lose(void)     { sound_tone(350, 35); sound_tone(250, 55); }
static void sound_save(void)     { sound_tone(750, 8); sound_tone(1050, 10); }
static void sound_open(void)     { sound_tone(500, 8); sound_tone(700, 10); sound_tone(900, 12); }
static void sound_error(void)    { sound_tone(400, 25); sound_tone(300, 35); }
static void sound_color_pick(void) { sound_tone(1100, 5); }
static void sound_clear(void)    { sound_tone(600, 8); sound_tone(400, 8); }
static void sound_mine_flag(void){ sound_tone(800, 6); sound_tone(1000, 6); }
static void sound_calc(void)     { sound_tone(1200, 5); }

static void draw_taskman(gui_window_t *w)
{
    draw_str(w->x + 8, w->y + 16, "SYSTEM PERFORMANCE", COL_BLACK, COL_WINBG);
    draw_hline(w->x + 8, w->y + 25, w->w - 16, GRAY(10));

    mm_stats_t s = mm_stats();
    char buf[32];

    draw_str(w->x + 8, w->y + 30, "Heap Used:", COL_BLACK, COL_WINBG);
    itoa((int)(s.used / 1024), buf, 10);
    draw_str(w->x + 72, w->y + 30, buf, COL_BLUE, COL_WINBG);
    draw_str(w->x + 72 + str_len(buf)*6, w->y + 30, " KB /", COL_BLACK, COL_WINBG);
    itoa((int)(s.total / 1024), buf, 10);
    draw_str(w->x + 115, w->y + 30, buf, COL_BLACK, COL_WINBG);
    draw_str(w->x + 115 + str_len(buf)*6, w->y + 30, " KB", COL_BLACK, COL_WINBG);

    /* Memory Usage Bar */
    int bar_w = w->w - 20;
    fill_rect(w->x + 10, w->y + 40, bar_w, 8, GRAY(18));
    draw_rect(w->x + 10, w->y + 40, bar_w, 8, GRAY(8));
    int fill_w = (s.total > 0) ? (int)((s.used * (bar_w - 2)) / s.total) : 0;
    if (fill_w > 0) fill_rect(w->x + 11, w->y + 41, fill_w, 6, COL_GREEN);

    /* System Uptime */
    draw_str(w->x + 8, w->y + 52, "Uptime:", COL_BLACK, COL_WINBG);
    uint32_t ticks = pit_ticks();
    itoa((int)(ticks / 1000), buf, 10);
    draw_str(w->x + 55, w->y + 52, buf, COL_BLACK, COL_WINBG);
    draw_str(w->x + 55 + str_len(buf)*6, w->y + 52, " sec", COL_BLACK, COL_WINBG);

    /* Process Table */
    draw_str(w->x + 8, w->y + 64, "ACTIVE PROCESSES", COL_BLACK, COL_WINBG);
    draw_hline(w->x + 8, w->y + 73, w->w - 16, GRAY(10));

    int py = w->y + 77;
    for (int i = 0; i < win_count; i++) {
        if (py + 10 > w->y + w->h - 5) break;
        if (windows[i].visible) {
            uint8_t fg = (i == focused_win) ? COL_BLUE : COL_BLACK;
            draw_app_icon(w->x + 10, py, windows[i].app);
            draw_str_clip(w->x + 20, py, windows[i].title, fg, COL_WINBG, w->x + w->w - 35);
            fill_rect(w->x + w->w - 30, py, 22, 9, GRAY(18));
            draw_rect(w->x + w->w - 30, py, 22, 9, GRAY(8));
            draw_str(w->x + w->w - 28, py + 1, "Kill", COL_RED, GRAY(18));
            py += 11;
        }
    }
}

/* ============================================================
 * IMAGE VIEWER — Full Controls + Synthwave Landscape
 * ============================================================ */
static void draw_imgview(gui_window_t *w)
{
    /* Toolbar: [Prev] [Next] [Zoom+] [Zoom-] [Clr] */
    int tb_y = w->y + 13;
    fill_rect(w->x + 2, tb_y, w->w - 4, 11, GRAY(16));

    static const char *iv_btns[] = { "Sav", "Opn", "Rst" };
    for (int i = 0; i < 3; i++) {
        int bx = w->x + 4 + i * 30;
        fill_rect(bx, tb_y + 1, 28, 9, GRAY(20));
        draw_rect(bx, tb_y + 1, 28, 9, GRAY(8));
        draw_str(bx + 4, tb_y + 2, iv_btns[i], COL_BLACK, GRAY(20));
    }

    /* Filename label on right side of toolbar */
    draw_str_clip(w->x + 100, tb_y + 2, imgview_filename, COL_BLUE, GRAY(16), w->x + w->w - 4);

    /* Canvas area */
    int cx = w->x + 4;
    int cy = w->y + 26;
    int cw = w->w - 8;
    int ch = w->h - 38;

    if (cw < 8) cw = 8;
    if (ch < 8) ch = 8;

    fill_rect(cx, cy, cw, ch, GRAY(2));
    draw_rect(cx, cy, cw, ch, GRAY(10));

    int ix = cx + 1, iy = cy + 1, iw = cw - 2, ih = ch - 2;

    /* Check if we have canvas data; if so, render it scaled */
    int has_data = 0;
    for (int k = 0; k < PAINT_CW * PAINT_CH; k++) {
        if (paint_canvas[k] != COL_WHITE) { has_data = 1; break; }
    }

    if (has_data) {
        /* Scale-blit the paint canvas into the viewer */
        for (int row = 0; row < ih; row++) {
            int src_row = (row * PAINT_CH) / ih;
            if (src_row >= PAINT_CH) src_row = PAINT_CH - 1;
            for (int col = 0; col < iw; col++) {
                int src_col = (col * PAINT_CW) / iw;
                if (src_col >= PAINT_CW) src_col = PAINT_CW - 1;
                put_pixel(ix + col, iy + row, paint_canvas[src_row * PAINT_CW + src_col]);
            }
        }
    } else {
        /* Render Retro Synthwave / Mountain Sunset Landscape */
        int horizon = ih * 6 / 10;
        for (int y = 0; y < ih; y++) {
            for (int x = 0; x < iw; x++) {
                uint8_t color;
                if (y < horizon) {
                    /* Sky Gradient (Dark Purple to Orange/Yellow) */
                    int sky_step = (y * 5) / horizon;
                    if (sky_step == 0) color = RGB(1, 0, 3);      /* Dark Violet */
                    else if (sky_step == 1) color = RGB(2, 0, 4); /* Purple */
                    else if (sky_step == 2) color = RGB(4, 0, 3); /* Magenta */
                    else if (sky_step == 3) color = RGB(5, 1, 1); /* Dark Orange */
                    else color = RGB(5, 3, 0);                   /* Amber Sun Glow */

                    /* Draw Sun in Sky */
                    int sun_cx = iw / 2;
                    int sun_cy = horizon - 8;
                    int dx = x - sun_cx;
                    int dy = y - sun_cy;
                    if (dx * dx + dy * dy <= 144) {
                        if ((y % 3) != 0 || y < sun_cy) {
                            color = RGB(5, 5, 0); /* Bright Yellow Sun */
                        }
                    }
                    /* Mountain silhouette */
                    int mh1 = (horizon * 3) / 5;
                    int mt1 = (iw / 2) - 20;
                    int mt2 = (iw / 2) + 20;
                    int mslope = (x < mt1) ? ((x * horizon) / (mt1 > 0 ? mt1 : 1)) / 4
                                           : (x > mt2) ? (((iw - x) * horizon) / ((iw - mt2) > 0 ? (iw - mt2) : 1)) / 4
                                           : mh1;
                    if (y > mslope && y >= horizon - 12) {
                        color = (y > mslope + 2) ? RGB(1,0,2) : RGB(3,1,4);
                    }
                } else {
                    /* Synthwave Grid Horizon Ground */
                    color = RGB(0, 0, 2); /* Deep Blue Ground */
                    if (y == horizon) {
                        color = RGB(5, 2, 0); /* Horizon Line */
                    } else if ((y - horizon) % 6 == 0) {
                        color = RGB(0, 4, 5); /* Cyan Grid Line Horizontal */
                    } else {
                        int dist = (y - horizon) + 1;
                        int lx = (iw / 2) + ((x - (iw / 2)) * 8) / dist;
                        if ((lx % 12) == 0) {
                            color = RGB(0, 4, 5); /* Cyan Grid Line Vertical Perspective */
                        }
                    }
                }
                put_pixel(ix + x, iy + y, color);
            }
        }
    }

    /* Caption bar at bottom */
    fill_rect(w->x + 4, w->y + w->h - 12, w->w - 8, 10, GRAY(6));
    draw_str_clip(w->x + 8, w->y + w->h - 11, imgview_filename, COL_WHITE, GRAY(6), w->x + w->w - 8);
}

/* ============================================================
 * DRAW SNAKE
 * ============================================================ */
static void draw_snake(gui_window_t *w)
{
    int avail_w = w->w - 8;
    int avail_h = w->h - 28;
    int cw = avail_w / SNAKE_GRID_W;
    int ch = avail_h / SNAKE_GRID_H;
    int cell = (cw < ch) ? cw : ch;
    if (cell < 2) cell = 2;

    int gw = SNAKE_GRID_W * cell;
    int gh = SNAKE_GRID_H * cell;
    int gx = w->x + 4 + (avail_w - gw) / 2;
    int gy = w->y + 14 + (avail_h - gh) / 2;

    /* Background grid */
    fill_rect(gx, gy, gw, gh, RGB(0,1,0));
    for (int r = 0; r < SNAKE_GRID_H; r++)
        for (int c = 0; c < SNAKE_GRID_W; c++)
            draw_rect(gx + c*cell, gy + r*cell, cell, cell, RGB(0,2,0));

    /* Food (red flashing apple) */
    uint8_t food_col = (pit_ticks() / 200) & 1 ? COL_RED : RGB(5,2,0);
    int sz = (cell - 2 > 0) ? (cell - 2) : 1;
    fill_rect(gx + snake_food_x * cell + 1, gy + snake_food_y * cell + 1, sz, sz, food_col);

    /* Snake body */
    for (int i = snake_len - 1; i >= 0; i--) {
        uint8_t seg_col = (i == 0) ? COL_LIME : RGB(0,4,0);
        fill_rect(gx + snake_x[i]*cell + 1, gy + snake_y[i]*cell + 1, sz, sz, seg_col);
    }

    /* Score bar */
    int score_y = w->y + w->h - 12;
    fill_rect(w->x + 4, score_y, w->w - 8, 10, GRAY(4));
    draw_str(w->x + 6, score_y + 1, "Score:", COL_WHITE, GRAY(4));
    char sbuf[12]; itoa(snake_score, sbuf, 10);
    draw_str(w->x + 44, score_y + 1, sbuf, COL_LIME, GRAY(4));

    if (snake_dead) {
        /* GAME OVER overlay */
        int bw = (gw - 20 > 70) ? (gw - 20) : 70;
        fill_rect(gx + (gw - bw)/2, gy + gh/2 - 10, bw, 20, COL_RED);
        draw_str(gx + (gw - bw)/2 + (bw - 54)/2, gy + gh/2 - 7, "GAME OVER", COL_WHITE, COL_RED);
        draw_str(gx + (gw - bw)/2 + (bw - 66)/2, gy + gh/2 + 3, "[Click/ENTER]", GRAY(12), COL_RED);
    }
}

/* ============================================================
 * TERMINAL / CLI APP STATE
 * ============================================================ */
#define CLI_LINES_MAX 128
#define CLI_LINE_LEN  80
static char    cli_lines[CLI_LINES_MAX][CLI_LINE_LEN];
static uint8_t cli_line_colors[CLI_LINES_MAX];
static int     cli_line_count = 0;
static int     cli_scroll = 0;

static char cli_input[CLI_LINE_LEN] = "";
static int  cli_input_len = 0;
static int  cli_cursor = 0;

#define CLI_HIST_MAX 20
static char cli_history[CLI_HIST_MAX][CLI_LINE_LEN];
static int  cli_hist_cnt = 0;
static int  cli_hist_idx = 0;

static void cli_add_line(const char *text, uint8_t color) {
    if (!text) return;
    if (cli_line_count < CLI_LINES_MAX) {
        str_cpy(cli_lines[cli_line_count], text, CLI_LINE_LEN);
        cli_line_colors[cli_line_count] = color;
        cli_line_count++;
    } else {
        for (int i = 0; i < CLI_LINES_MAX - 1; i++) {
            str_cpy(cli_lines[i], cli_lines[i + 1], CLI_LINE_LEN);
            cli_line_colors[i] = cli_line_colors[i + 1];
        }
        str_cpy(cli_lines[CLI_LINES_MAX - 1], text, CLI_LINE_LEN);
        cli_line_colors[CLI_LINES_MAX - 1] = color;
    }
}

static void cli_add_text(const char *text, uint8_t color, int max_cols) {
    if (!text || !text[0]) return;
    if (max_cols < 20) max_cols = 45;
    if (max_cols >= CLI_LINE_LEN) max_cols = CLI_LINE_LEN - 1;

    int i = 0;
    while (text[i]) {
        char linebuf[CLI_LINE_LEN];
        int li = 0;
        
        while (text[i] && text[i] != '\n' && li < max_cols) {
            if (text[i] != '\r') {
                linebuf[li++] = text[i];
            }
            i++;
        }
        
        /* If we hit max_cols without hitting newline, try to wrap cleanly at last space */
        if (li >= max_cols && text[i] && text[i] != '\n') {
            int last_space = -1;
            for (int s = li - 1; s >= li - 12 && s >= 0; s--) {
                if (linebuf[s] == ' ') {
                    last_space = s;
                    break;
                }
            }
            if (last_space > 0) {
                int rewind = li - (last_space + 1);
                i -= rewind;
                li = last_space;
            }
        }
        
        linebuf[li] = '\0';
        cli_add_line(linebuf, color);
        
        if (text[i] == '\n') i++;
    }
}

static void cli_init_welcome(void) {
    if (cli_line_count == 0) {
        cli_add_line("ArchaOS v0.5 \"Monolith\" Terminal", COL_LIME);
        cli_add_line("Type \"new\" to learn what's new and \"general\" for full docs.", GRAY(14));
        cli_add_line("Type \"help\" for all commands.", GRAY(10));
        cli_add_line("", COL_WHITE);
    }
}

static void cli_history_save(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    if (cli_hist_cnt > 0 && str_cmp(cli_history[cli_hist_cnt - 1], cmd) == 0) return;
    if (cli_hist_cnt < CLI_HIST_MAX) {
        str_cpy(cli_history[cli_hist_cnt++], cmd, CLI_LINE_LEN);
    } else {
        for (int i = 0; i < CLI_HIST_MAX - 1; i++) {
            str_cpy(cli_history[i], cli_history[i + 1], CLI_LINE_LEN);
        }
        str_cpy(cli_history[CLI_HIST_MAX - 1], cmd, CLI_LINE_LEN);
    }
}

static void cli_execute(gui_window_t *w) {
    int max_cols = (w->w - 14) / 6;
    if (max_cols < 20) max_cols = 45;

    /* Add prompt echo line */
    char pbuf[CLI_LINE_LEN + 10];
    str_cpy(pbuf, "Arc/> ", sizeof(pbuf));
    str_cpy(pbuf + 6, cli_input, CLI_LINE_LEN);
    cli_add_line(pbuf, COL_LIME);

    if (cli_input_len > 0) {
        cli_history_save(cli_input);
        
        if (str_cmp(cli_input, "clear") == 0 || str_cmp(cli_input, "cls") == 0) {
            cli_line_count = 0;
            cli_scroll = 0;
        } else {
            shellext_capture_start();
            shell_exec(cli_input);
            shellext_capture_stop();

            const char *cap = shellext_get_captured();
            if (cap && cap[0]) {
                cli_add_text(cap, COL_WHITE, max_cols);
            }
        }
    }

    cli_input[0] = '\0';
    cli_input_len = 0;
    cli_cursor = 0;
    cli_hist_idx = cli_hist_cnt;
    cli_scroll = 0;
}

static void draw_cli(gui_window_t *w)
{
    cli_init_welcome();

    int body_x = w->x + 3, body_y = w->y + 13;
    int body_w = w->w - 6, body_h = w->h - 16;

    /* Terminal Screen — Pure Black BG, Teal Border */
    fill_rect(body_x, body_y, body_w, body_h, COL_BLACK);
    draw_rect(body_x, body_y, body_w, body_h, COL_TEAL);

    int max_rows = (body_h - 6) / 9;
    if (max_rows < 1) max_rows = 1;

    /* Total logical lines = history lines + 1 active prompt line */
    int total_lines = cli_line_count + 1;
    
    int start_line = 0;
    if (total_lines > max_rows) {
        start_line = (total_lines - max_rows) - cli_scroll;
        if (start_line < 0) start_line = 0;
        if (start_line > total_lines - max_rows) start_line = total_lines - max_rows;
    }

    for (int r = 0; r < max_rows; r++) {
        int line_idx = start_line + r;
        int py = body_y + 3 + r * 9;

        if (line_idx < cli_line_count) {
            /* Render history output line */
            draw_str_clip(body_x + 4, py, cli_lines[line_idx], cli_line_colors[line_idx], COL_BLACK, body_x + body_w - 4);
        } else if (line_idx == cli_line_count) {
            /* Render active prompt line */
            draw_str(body_x + 4, py, "Arc/> ", COL_GREEN, COL_BLACK);
            draw_str_clip(body_x + 40, py, cli_input, COL_WHITE, COL_BLACK, body_x + body_w - 8);

            /* Blinking cursor */
            if ((pit_ticks() / 350) % 2 == 0 && w->focused) {
                int cur_x = body_x + 40 + cli_cursor * 6;
                if (cur_x < body_x + body_w - 6) {
                    fill_rect(cur_x, py + 7, 5, 2, COL_GREEN); /* Solid underscore cursor */
                }
            }
        }
    }
}

/* ============================================================
 * APP STUDIO / CODEABLE APPS STATE (ArchaOS, Python, C)
 * ============================================================ */
#define CODE_MAX_LINES 32
#define CODE_LINE_MAX_LEN 128
static char code_lines[CODE_MAX_LINES][CODE_LINE_MAX_LEN];
static int  code_line_cnt = 1;
static char code_cur_line[CODE_LINE_MAX_LEN] = "";
static int  code_cur_len = 0;

static int code_cursor_line = 0;
static int code_cursor_col  = 0;
static int code_scroll_offset = 0;
static char code_output[64] = "Status: Ready to build";

typedef enum { LANG_ARCHAOS = 0, LANG_PYTHON, LANG_C } code_lang_t;
static code_lang_t code_lang = LANG_PYTHON;
static int         code_show_docs = 0;

typedef struct {
    char name[16];
    int val;
    char str_val[32];
    int is_str;
} var_t;

#define MAX_VARS 32
static var_t env_vars[MAX_VARS];
static int   env_var_cnt = 0;

static void var_set_int(const char *name, int val) {
    for (int i = 0; i < env_var_cnt; i++) {
        if (str_cmp(env_vars[i].name, name) == 0) {
            env_vars[i].val = val;
            env_vars[i].is_str = 0;
            return;
        }
    }
    if (env_var_cnt < MAX_VARS) {
        str_cpy(env_vars[env_var_cnt].name, name, 16);
        env_vars[env_var_cnt].val = val;
        env_vars[env_var_cnt].is_str = 0;
        env_var_cnt++;
    }
}

static int var_get_int(const char *name) {
    for (int i = 0; i < env_var_cnt; i++) {
        if (str_cmp(env_vars[i].name, name) == 0) return env_vars[i].val;
    }
    return mini_atoi(name);
}

/* ============================================================
 * STRING VARIABLE SUPPORT
 * ============================================================ */

static void var_set_str(const char *name, const char *val) {
    for (int i = 0; i < env_var_cnt; i++) {
        if (str_cmp(env_vars[i].name, name) == 0) {
            str_cpy(env_vars[i].str_val, val, 32);
            env_vars[i].val = mini_atoi(val);
            env_vars[i].is_str = 1;
            return;
        }
    }
    if (env_var_cnt < MAX_VARS) {
        str_cpy(env_vars[env_var_cnt].name, name, 16);
        str_cpy(env_vars[env_var_cnt].str_val, val, 32);
        env_vars[env_var_cnt].val = mini_atoi(val);
        env_vars[env_var_cnt].is_str = 1;
        env_var_cnt++;
    }
}

static const char *var_get_str(const char *name) {
    for (int i = 0; i < env_var_cnt; i++) {
        if (str_cmp(env_vars[i].name, name) == 0 && env_vars[i].is_str)
            return env_vars[i].str_val;
    }
    return 0;
}

static void console_add_line(const char *text);

/* ============================================================
 * EXPRESSION EVALUATOR — recursive descent parser
 * Precedence (low to high): or, and, not, comparison, +/-, *, /, %, unary-, atom
 * ============================================================ */

static const char *expr_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int expr_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int expr_is_alnum(char c) {
    return expr_is_alpha(c) || (c >= '0' && c <= '9');
}
static int expr_is_digit(char c) { return c >= '0' && c <= '9'; }

static int eval_expr_full(const char **pp);

static int eval_atom(const char **pp) {
    const char *p = expr_ws(*pp);

    if (*p == '(') {
        p++;
        int v = eval_expr_full(&p);
        p = expr_ws(p);
        if (*p == ')') p++;
        *pp = p;
        return v;
    }

    if (*p == '-') {
        p++;
        *pp = p;
        return -eval_atom(pp);
    }

    if (*p == '"' || *p == '\'') {
        char q = *p++; char sval[64]; int si = 0;
        while (*p && *p != q && si < 63) sval[si++] = *p++;
        if (*p == q) p++;
        sval[si] = '\0';
        *pp = p;
        return mini_atoi(sval);
    }

    if (expr_is_digit(*p)) {
        int v = 0;
        while (expr_is_digit(*p)) v = v * 10 + (*p++ - '0');
        *pp = p;
        return v;
    }

    if (expr_is_alpha(*p)) {
        char name[16]; int ni = 0;
        while (expr_is_alnum(*p) && ni < 15) name[ni++] = *p++;
        name[ni] = '\0';
        *pp = p;

        if (str_cmp(name, "True") == 0) return 1;
        if (str_cmp(name, "False") == 0) return 0;

        p = expr_ws(p);
        if (*p == '(' && str_cmp(name, "len") == 0) {
            p++;
            p = expr_ws(p);
            if (*p == '"' || *p == '\'') {
                char q = *p++; int slen = 0;
                while (*p && *p != q) { slen++; p++; }
                if (*p == q) p++;
                p = expr_ws(p);
                if (*p == ')') p++;
                *pp = p;
                return slen;
            } else {
                char arg[16]; int ai = 0;
                while (expr_is_alnum(*p) && ai < 15) arg[ai++] = *p++;
                arg[ai] = '\0';
                p = expr_ws(p);
                if (*p == ')') p++;
                *pp = p;
                const char *sv = var_get_str(arg);
                return sv ? str_len(sv) : 0;
            }
        }

        const char *sv = var_get_str(name);
        if (sv) return mini_atoi(sv);
        return var_get_int(name);
    }

    *pp = p;
    return 0;
}

static int eval_mul(const char **pp) {
    int v = eval_atom(pp);
    for (;;) {
        const char *p = expr_ws(*pp);
        if (*p == '*')                     { *pp = p + 1; v *= eval_atom(pp); }
        else if (*p == '/' && p[1] == '/') { *pp = p + 2; int d = eval_atom(pp); if (d) v /= d; }
        else if (*p == '/' && p[1] != '/') { *pp = p + 1; int d = eval_atom(pp); if (d) v /= d; }
        else if (*p == '%')                { *pp = p + 1; int d = eval_atom(pp); if (d) v %= d; }
        else break;
    }
    return v;
}

static int eval_add(const char **pp) {
    int v = eval_mul(pp);
    for (;;) {
        const char *p = expr_ws(*pp);
        if (*p == '+')      { *pp = p + 1; v += eval_mul(pp); }
        else if (*p == '-') { *pp = p + 1; v -= eval_mul(pp); }
        else break;
    }
    return v;
}

static int eval_cmp(const char **pp) {
    int v = eval_add(pp);
    const char *p = expr_ws(*pp);
    if (p[0] == '=' && p[1] == '=') { *pp = p + 2; return v == eval_add(pp); }
    if (p[0] == '!' && p[1] == '=') { *pp = p + 2; return v != eval_add(pp); }
    if (p[0] == '<' && p[1] == '=') { *pp = p + 2; return v <= eval_add(pp); }
    if (p[0] == '>' && p[1] == '=') { *pp = p + 2; return v >= eval_add(pp); }
    if (p[0] == '<')                { *pp = p + 1; return v <  eval_add(pp); }
    if (p[0] == '>')                { *pp = p + 1; return v >  eval_add(pp); }
    return v;
}

static int eval_expr_full(const char **pp) {
    const char *p = expr_ws(*pp);
    if (str_ncmp(p, "not ", 4) == 0) {
        *pp = p + 4;
        return !eval_expr_full(pp);
    }
    int v = eval_cmp(pp);
    for (;;) {
        p = expr_ws(*pp);
        if (str_ncmp(p, "and ", 4) == 0)    { *pp = p + 4; int r = eval_cmp(pp); v = v && r; }
        else if (str_ncmp(p, "or ", 3) == 0) { *pp = p + 3; int r = eval_cmp(pp); v = v || r; }
        else break;
    }
    return v;
}

static int eval_expr_s(const char *s) {
    const char *p = s;
    return eval_expr_full(&p);
}

/* ============================================================
 * PYTHON MULTI-LINE INTERPRETER
 * Processes all lines at once with indentation-based block tracking.
 * ============================================================ */

static int py_indent(const char *line) {
    int n = 0;
    while (line[n] == ' ') n++;
    return n;
}

static int py_block_end(char lines[][CODE_LINE_MAX_LEN], int count, int start, int base_indent) {
    for (int i = start; i < count; i++) {
        if (!lines[i][0]) continue;
        int ind = 0;
        while (lines[i][ind] == ' ') ind++;
        if (!lines[i][ind]) continue;
        if (ind <= base_indent) return i;
    }
    return count;
}

static void py_do_print(const char *args) {
    char out[120]; int oi = 0;
    const char *p = args;
    int first_arg = 1;

    while (*p && *p != ')') {
        p = expr_ws(p);
        if (*p == ')' || !*p) break;

        if (!first_arg && oi < 119) out[oi++] = ' ';
        first_arg = 0;

        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q && oi < 118) {
                if (*p == '\\' && p[1] == 'n') {
                    out[oi] = '\0'; console_add_line(out); oi = 0; p += 2; continue;
                }
                if (*p == '\\' && p[1] == 't') { out[oi++] = ' '; if (oi < 118) out[oi++] = ' '; p += 2; continue; }
                out[oi++] = *p++;
            }
            if (*p == q) p++;
        } else {
            const char *expr_start = p;
            char vname[16]; int vi = 0;
            while (expr_is_alnum(*p) && vi < 15) vname[vi++] = *p++;
            vname[vi] = '\0';

            const char *after = expr_ws(p);
            int is_simple = (*after == ',' || *after == ')' || *after == '\0');

            if (is_simple && vi > 0) {
                const char *sv = var_get_str(vname);
                if (sv) {
                    while (*sv && oi < 118) out[oi++] = *sv++;
                } else {
                    int val = var_get_int(vname);
                    char num[16]; itoa(val, num, 10);
                    int ni = 0; while (num[ni] && oi < 118) out[oi++] = num[ni++];
                }
            } else {
                p = expr_start;
                int val = eval_expr_full(&p);
                char num[16]; itoa(val, num, 10);
                int ni = 0; while (num[ni] && oi < 118) out[oi++] = num[ni++];
            }
        }
        p = expr_ws(p);
        if (*p == ',') p++;
    }
    out[oi] = '\0';
    console_add_line(out);
}

static int py_is_assign(const char *line) {
    const char *p = line;
    while (*p && (*p == ' ' || *p == '\t' || !expr_is_alpha(*p))) {
        if (*p == '=' || *p == '(' || *p == '#' || *p == '"' || *p == '\'') return 0;
        p++;
    }
    if (!expr_is_alpha(*p)) return 0;
    while (expr_is_alnum(*p)) p++;
    while (*p == ' ') p++;
    return (*p == '=' && p[1] != '=');
}

static void py_exec_block(char lines[][CODE_LINE_MAX_LEN], int count, int start, int end) {
    int pc = start;
    while (pc < end) {
        const char *raw = lines[pc];
        if (!raw[0]) { pc++; continue; }
        int indent = py_indent(raw);
        const char *L = raw + indent;
        if (!*L || *L == '#') { pc++; continue; }
        while (*L && (*L == ' ' || *L == '\t')) L++;

        if (py_is_assign(L)) {
            const char *p = L;
            while (*p && !expr_is_alpha(*p)) p++;
            char vn[16]; int vi = 0;
            while (expr_is_alnum(*p) && vi < 15) vn[vi++] = *p++;
            vn[vi] = '\0';
            p = expr_ws(p);
            if (*p == '=') p++;
            p = expr_ws(p);
            if (str_ncmp(p, "input(", 6) == 0) {
                p += 6; p = expr_ws(p);
                char prompt[64] = "Enter input: ";
                if (*p == '"' || *p == '\'') {
                    char q = *p++; int pi = 0;
                    while (*p && *p != q && pi < 63) prompt[pi++] = *p++;
                    prompt[pi] = '\0';
                }
                char in_val[64];
                gui_prompt_user_input(prompt[0] ? prompt : "Enter input: ", in_val, sizeof(in_val));
                char log_line[128]; str_cpy(log_line, prompt, 128);
                int lli = str_len(log_line), ivi = 0;
                while (in_val[ivi] && lli < 127) log_line[lli++] = in_val[ivi++];
                log_line[lli] = '\0';
                console_add_line(log_line);
                var_set_str(vn, in_val);
            } else if (*p == '"' || *p == '\'') {
                char q = *p++; char sv[32]; int si = 0;
                while (*p && *p != q && si < 31) {
                    if (*p == '\\' && p[1] == 'n') { sv[si++] = '\n'; p += 2; continue; }
                    sv[si++] = *p++;
                }
                sv[si] = '\0';
                var_set_str(vn, sv);
                var_set_int(vn, mini_atoi(sv));
            } else {
                int val = eval_expr_full(&p);
                var_set_int(vn, val);
                char num_buf[16]; itoa(val, num_buf, 10);
                var_set_str(vn, num_buf);
            }
            pc++; continue;
        }

        /* ---- for VAR in range(...): ---- */
        if (str_ncmp(L, "for ", 4) == 0) {
            const char *p = L + 4;
            char var[16]; int vi = 0;
            while (*p && *p != ' ' && vi < 15) var[vi++] = *p++;
            var[vi] = '\0';
            p = expr_ws(p);
            if (str_ncmp(p, "in range(", 9) == 0) {
                p += 9;
                int rstart = 0, rend = 0, rstep = 1;
                rend = eval_expr_full(&p);
                p = expr_ws(p);
                if (*p == ',') {
                    rstart = rend; p++;
                    rend = eval_expr_full(&p);
                    p = expr_ws(p);
                    if (*p == ',') { p++; rstep = eval_expr_full(&p); if (!rstep) rstep = 1; }
                }
                int bs = pc + 1, be = py_block_end(lines, count, bs, indent);
                if (rstep > 0) {
                    for (int i = rstart; i < rend; i += rstep) {
                        var_set_int(var, i);
                        py_exec_block(lines, count, bs, be);
                    }
                } else {
                    for (int i = rstart; i > rend; i += rstep) {
                        var_set_int(var, i);
                        py_exec_block(lines, count, bs, be);
                    }
                }
                pc = be; continue;
            }
            pc++; continue;
        }

        /* ---- while COND: ---- */
        if (str_ncmp(L, "while ", 6) == 0) {
            const char *cs = L + 6;
            char cond[64]; int ci = 0;
            while (*cs && *cs != ':' && ci < 63) cond[ci++] = *cs++;
            cond[ci] = '\0';
            while (ci > 0 && cond[ci-1] == ' ') cond[--ci] = '\0';
            int bs = pc + 1, be = py_block_end(lines, count, bs, indent);
            int safety = 100000;
            while (eval_expr_s(cond) && safety-- > 0)
                py_exec_block(lines, count, bs, be);
            pc = be; continue;
        }

        /* ---- if / elif / else chain ---- */
        if (str_ncmp(L, "if ", 3) == 0) {
            int executed = 0;
            const char *cs = L + 3;
            char cond[64]; int ci = 0;
            while (*cs && *cs != ':' && ci < 63) cond[ci++] = *cs++;
            cond[ci] = '\0';
            while (ci > 0 && cond[ci-1] == ' ') cond[--ci] = '\0';
            int bs = pc + 1, be = py_block_end(lines, count, bs, indent);
            if (eval_expr_s(cond)) { py_exec_block(lines, count, bs, be); executed = 1; }
            pc = be;
            while (pc < end) {
                if (!lines[pc][0]) { pc++; continue; }
                int ni = py_indent(lines[pc]);
                if (ni != indent) break;
                const char *nL = lines[pc] + ni;
                if (str_ncmp(nL, "elif ", 5) == 0) {
                    cs = nL + 5; ci = 0;
                    while (*cs && *cs != ':' && ci < 63) cond[ci++] = *cs++;
                    cond[ci] = '\0';
                    while (ci > 0 && cond[ci-1] == ' ') cond[--ci] = '\0';
                    bs = pc + 1; be = py_block_end(lines, count, bs, indent);
                    if (!executed && eval_expr_s(cond)) { py_exec_block(lines, count, bs, be); executed = 1; }
                    pc = be;
                } else if (str_ncmp(nL, "else", 4) == 0 && (nL[4] == ':' || nL[4] == '\0' || nL[4] == ' ')) {
                    bs = pc + 1; be = py_block_end(lines, count, bs, indent);
                    if (!executed) py_exec_block(lines, count, bs, be);
                    pc = be; break;
                } else break;
            }
            continue;
        }

        /* Skip orphaned elif/else */
        if (str_ncmp(L, "elif ", 5) == 0 ||
            (str_ncmp(L, "else", 4) == 0 && (L[4] == ':' || L[4] == '\0' || L[4] == ' '))) {
            int bs = pc + 1, be = py_block_end(lines, count, bs, indent);
            pc = be; continue;
        }

        /* ---- input(...) without assignment ---- */
        if (str_ncmp(L, "input(", 6) == 0) {
            const char *p = L + 6; p = expr_ws(p);
            char prompt[64] = "Enter input: ";
            if (*p == '"' || *p == '\'') {
                char q = *p++; int pi = 0;
                while (*p && *p != q && pi < 63) prompt[pi++] = *p++;
                prompt[pi] = '\0';
            }
            char in_val[64];
            gui_prompt_user_input(prompt[0] ? prompt : "Enter input: ", in_val, sizeof(in_val));
            pc++; continue;
        }

        /* ---- print(...) ---- */
        if (str_ncmp(L, "print(", 6) == 0) { py_do_print(L + 6); pc++; continue; }

        /* ---- beep(freq, dur) ---- */
        if (str_ncmp(L, "beep(", 5) == 0) {
            const char *p = L + 5;
            int freq = eval_expr_full(&p); p = expr_ws(p); if (*p == ',') p++;
            int dur = eval_expr_full(&p);
            if (freq > 0 && dur > 0) {
                char bcmd[32];
                int bi = 0;
                const char *b = "beep "; while (*b && bi < 31) bcmd[bi++] = *b++;
                char fb[12]; itoa(freq, fb, 10);
                for (int k = 0; fb[k] && bi < 31; k++) bcmd[bi++] = fb[k];
                if (bi < 31) bcmd[bi++] = ' ';
                itoa(dur, fb, 10);
                for (int k = 0; fb[k] && bi < 31; k++) bcmd[bi++] = fb[k];
                bcmd[bi] = '\0';
                shell_exec(bcmd);
            }
            pc++; continue;
        }

        /* ---- sleep(ms) ---- */
        if (str_ncmp(L, "sleep(", 6) == 0) {
            int ms = eval_expr_s(L + 6);
            if (ms > 0) pit_sleep((uint32_t)ms);
            pc++; continue;
        }

        /* ---- sys_exec("cmd") / system("cmd") ---- */
        if (str_ncmp(L, "sys_exec(", 9) == 0 || str_ncmp(L, "system(", 7) == 0) {
            const char *p = (str_ncmp(L, "sys_exec(", 9) == 0) ? L + 9 : L + 7;
            char cmd[64]; int ci = 0;
            if (*p == '"' || *p == '\'') { char q = *p++; while (*p && *p != q && ci < 63) cmd[ci++] = *p++; }
            else { while (*p && *p != ')' && ci < 63) cmd[ci++] = *p++; }
            cmd[ci] = '\0';
            shell_exec(cmd);
            pc++; continue;
        }



        /* ---- Unknown Python line → error in console, NOT shell ---- */
        {
            char em[80]; str_cpy(em, "Error: ", 80);
            int ei = 7; while (*L && ei < 78) em[ei++] = *L++;
            em[ei] = '\0';
            console_add_line(em);
        }
        pc++;
    }
}

static void py_run_program(char lines[][CODE_LINE_MAX_LEN], int total) {
    env_var_cnt = 0;
    py_exec_block(lines, total, 0, total);
}

/* ============================================================
 * C MULTI-LINE INTERPRETER
 * Processes all lines at once with brace-based block tracking.
 * ============================================================ */

static int c_has_open_brace(const char *line) {
    int len = str_len(line);
    while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t')) len--;
    return len > 0 && line[len-1] == '{';
}

static int c_brace_end(char lines[][CODE_LINE_MAX_LEN], int count, int start) {
    int depth = 0;
    for (int i = start; i < count; i++) {
        for (const char *p = lines[i]; *p; p++) {
            if (*p == '{') depth++;
            if (*p == '}') { depth--; if (depth <= 0) return i; }
        }
    }
    return count;
}

static void c_find_body(char lines[][CODE_LINE_MAX_LEN], int count, int pc, int *bs, int *be) {
    if (c_has_open_brace(lines[pc])) {
        *bs = pc + 1;
        *be = c_brace_end(lines, count, pc);
    } else if (pc + 1 < count) {
        const char *nx = lines[pc + 1];
        while (*nx == ' ' || *nx == '\t') nx++;
        if (*nx == '{') {
            *bs = pc + 2;
            *be = c_brace_end(lines, count, pc + 1);
        } else {
            *bs = pc + 1;
            *be = pc + 2;
        }
    } else {
        *bs = pc + 1;
        *be = pc + 1;
    }
}

static void c_do_printf(const char *args) {
    char fmt[80]; int fi = 0;
    const char *p = args;

    if (*p == '"') {
        p++;
        while (*p && *p != '"' && fi < 79) {
            if (*p == '\\' && p[1] == 'n') { fmt[fi++] = '\n'; p += 2; continue; }
            if (*p == '\\' && p[1] == 't') { fmt[fi++] = '\t'; p += 2; continue; }
            if (*p == '\\' && p[1] == '\\') { fmt[fi++] = '\\'; p += 2; continue; }
            fmt[fi++] = *p++;
        }
        if (*p == '"') p++;
    }
    fmt[fi] = '\0';

    int av[8]; int ac = 0;
    while (*p && *p != ')' && *p != ';') {
        if (*p == ',') {
            p++; p = expr_ws(p);
            if (*p && *p != ')' && *p != ';' && ac < 8)
                av[ac++] = eval_expr_full(&p);
        } else p++;
    }

    char out[120]; int oi = 0; int ai = 0;
    for (int i = 0; fmt[i] && oi < 118; i++) {
        if (fmt[i] == '%' && fmt[i+1] == 'd' && ai < ac) {
            char num[16]; itoa(av[ai++], num, 10);
            int ni = 0; while (num[ni] && oi < 118) out[oi++] = num[ni++];
            i++;
        } else if (fmt[i] == '\n') {
            out[oi] = '\0';
            if (oi > 0) console_add_line(out);
            oi = 0;
        } else {
            out[oi++] = fmt[i];
        }
    }
    if (oi > 0) { out[oi] = '\0'; console_add_line(out); }
}

static void c_exec_block(char lines[][CODE_LINE_MAX_LEN], int count, int start, int end) {
    int pc = start;
    while (pc < end) {
        const char *raw = lines[pc];
        if (!raw[0]) { pc++; continue; }
        const char *L = raw;
        while (*L == ' ' || *L == '\t') L++;
        if (!*L || *L == '{' || *L == '}') { pc++; continue; }
        if (str_ncmp(L, "//", 2) == 0 || str_ncmp(L, "#include", 8) == 0 ||
            str_ncmp(L, "int main", 8) == 0 || str_ncmp(L, "return", 6) == 0) { pc++; continue; }

        /* ---- printf(...); ---- */
        if (str_ncmp(L, "printf(", 7) == 0) { c_do_printf(L + 7); pc++; continue; }
        if (str_ncmp(L, "puts(", 5) == 0) {
            const char *p = L + 5; char msg[80]; int mi = 0;
            if (*p == '"') { p++; while (*p && *p != '"' && mi < 79) msg[mi++] = *p++; }
            msg[mi] = '\0'; console_add_line(msg); pc++; continue;
        }

        /* ---- for (...) { ... } ---- */
        if (str_ncmp(L, "for ", 4) == 0 || str_ncmp(L, "for(", 4) == 0) {
            const char *p = L + 3;
            while (*p == ' ' || *p == '(') p++;
            if (str_ncmp(p, "int ", 4) == 0) p += 4;
            p = expr_ws(p);
            char var[16]; int vi = 0;
            while (expr_is_alnum(*p) && vi < 15) var[vi++] = *p++;
            var[vi] = '\0';
            p = expr_ws(p);
            if (*p == '=') { p++; var_set_int(var, eval_expr_full(&p)); }
            while (*p && *p != ';') p++;
            if (*p == ';') p++;
            p = expr_ws(p);
            char cond[64]; int ci = 0;
            while (*p && *p != ';' && ci < 63) cond[ci++] = *p++;
            cond[ci] = '\0';
            while (ci > 0 && cond[ci-1] == ' ') cond[--ci] = '\0';
            if (*p == ';') p++;
            p = expr_ws(p);
            char incvar[16]; int ivi = 0;
            while (expr_is_alnum(*p) && ivi < 15) incvar[ivi++] = *p++;
            incvar[ivi] = '\0';
            p = expr_ws(p);
            int inc_mode = 0, inc_val = 1;
            if (*p == '+' && p[1] == '+') inc_mode = 0;
            else if (*p == '-' && p[1] == '-') inc_mode = 1;
            else if (*p == '+' && p[1] == '=') { inc_mode = 2; p += 2; inc_val = eval_expr_full(&p); }
            else if (*p == '-' && p[1] == '=') { inc_mode = 3; p += 2; inc_val = eval_expr_full(&p); }

            int bs, be;
            c_find_body(lines, end, pc, &bs, &be);

            int safety = 100000;
            while (eval_expr_s(cond) && safety-- > 0) {
                c_exec_block(lines, count, bs, be);
                int cur = var_get_int(incvar);
                if (inc_mode == 0) var_set_int(incvar, cur + 1);
                else if (inc_mode == 1) var_set_int(incvar, cur - 1);
                else if (inc_mode == 2) var_set_int(incvar, cur + inc_val);
                else if (inc_mode == 3) var_set_int(incvar, cur - inc_val);
            }
            pc = be;
            if (pc < end) { const char *t = lines[pc]; while (*t == ' ' || *t == '\t') t++; if (*t == '}') pc++; }
            continue;
        }

        /* ---- while (...) { ... } ---- */
        if (str_ncmp(L, "while ", 6) == 0 || str_ncmp(L, "while(", 6) == 0) {
            const char *p = L + 5;
            while (*p == ' ' || *p == '(') p++;
            char cond[64]; int ci = 0; int pd = 0;
            while (*p && ci < 63) {
                if (*p == '(') pd++;
                if (*p == ')') { if (pd == 0) break; pd--; }
                cond[ci++] = *p++;
            }
            cond[ci] = '\0';
            while (ci > 0 && cond[ci-1] == ' ') cond[--ci] = '\0';
            int bs, be;
            c_find_body(lines, end, pc, &bs, &be);
            int safety = 100000;
            while (eval_expr_s(cond) && safety-- > 0) c_exec_block(lines, count, bs, be);
            pc = be;
            if (pc < end) { const char *t = lines[pc]; while (*t == ' ' || *t == '\t') t++; if (*t == '}') pc++; }
            continue;
        }

        /* ---- if (...) { ... } [else { ... }] ---- */
        if (str_ncmp(L, "if ", 3) == 0 || str_ncmp(L, "if(", 3) == 0) {
            const char *p = L + 2; while (*p == ' ' || *p == '(') p++;
            char cond[64]; int ci = 0; int pd = 0;
            while (*p && ci < 63) {
                if (*p == '(') pd++;
                if (*p == ')') { if (pd == 0) break; pd--; }
                cond[ci++] = *p++;
            }
            cond[ci] = '\0';
            while (ci > 0 && cond[ci-1] == ' ') cond[--ci] = '\0';
            int bs, be;
            c_find_body(lines, end, pc, &bs, &be);
            int did = 0;
            if (eval_expr_s(cond)) { c_exec_block(lines, count, bs, be); did = 1; }
            pc = be;
            if (pc < end) { const char *t = lines[pc]; while (*t == ' ' || *t == '\t') t++; if (*t == '}') pc++; }
            /* Check for } else { on brace line or standalone else */
            if (pc < end) {
                const char *t = lines[pc]; while (*t == ' ' || *t == '\t') t++;
                int is_else = 0;
                if (*t == '}') {
                    const char *after = t + 1; while (*after == ' ') after++;
                    if (str_ncmp(after, "else", 4) == 0) is_else = 1;
                }
                if (str_ncmp(t, "else", 4) == 0) is_else = 1;
                if (is_else) {
                    c_find_body(lines, end, pc, &bs, &be);
                    if (!did) c_exec_block(lines, count, bs, be);
                    pc = be;
                    if (pc < end) { const char *t2 = lines[pc]; while (*t2 == ' ' || *t2 == '\t') t2++; if (*t2 == '}') pc++; }
                }
            }
            continue;
        }

        /* ---- Variable decl/assign: [int] VAR = EXPR; or VAR++; or VAR += EXPR; ---- */
        {
            const char *p = L;
            if (str_ncmp(p, "int ", 4) == 0) p += 4;
            else if (str_ncmp(p, "float ", 6) == 0) p += 6;
            else if (str_ncmp(p, "char ", 5) == 0) p += 5;
            p = expr_ws(p);
            if (expr_is_alpha(*p)) {
                char vn[16]; int vi = 0;
                while (expr_is_alnum(*p) && vi < 15) vn[vi++] = *p++;
                vn[vi] = '\0';
                p = expr_ws(p);
                if (*p == '=' && p[1] != '=') {
                    p++; p = expr_ws(p);
                    if (*p == '"') { p++; char sv[32]; int si = 0; while (*p && *p != '"' && si < 31) sv[si++] = *p++; sv[si] = '\0'; var_set_str(vn, sv); }
                    else { var_set_int(vn, eval_expr_full(&p)); }
                    pc++; continue;
                }
                if (*p == '+' && p[1] == '+') { var_set_int(vn, var_get_int(vn) + 1); pc++; continue; }
                if (*p == '-' && p[1] == '-') { var_set_int(vn, var_get_int(vn) - 1); pc++; continue; }
                if (*p == '+' && p[1] == '=') { p += 2; var_set_int(vn, var_get_int(vn) + eval_expr_full(&p)); pc++; continue; }
                if (*p == '-' && p[1] == '=') { p += 2; var_set_int(vn, var_get_int(vn) - eval_expr_full(&p)); pc++; continue; }
                if (*p == '*' && p[1] == '=') { p += 2; var_set_int(vn, var_get_int(vn) * eval_expr_full(&p)); pc++; continue; }
            }
        }

        /* ---- beep(freq, dur); ---- */
        if (str_ncmp(L, "beep(", 5) == 0) {
            const char *p = L + 5;
            int freq = eval_expr_full(&p); p = expr_ws(p); if (*p == ',') p++;
            int dur = eval_expr_full(&p);
            if (freq > 0 && dur > 0) {
                char bcmd[32];
                int bi = 0;
                const char *b = "beep "; while (*b && bi < 31) bcmd[bi++] = *b++;
                char fb[12]; itoa(freq, fb, 10);
                for (int k = 0; fb[k] && bi < 31; k++) bcmd[bi++] = fb[k];
                if (bi < 31) bcmd[bi++] = ' ';
                itoa(dur, fb, 10);
                for (int k = 0; fb[k] && bi < 31; k++) bcmd[bi++] = fb[k];
                bcmd[bi] = '\0';
                shell_exec(bcmd);
            }
            pc++; continue;
        }

        /* ---- sleep(ms); ---- */
        if (str_ncmp(L, "sleep(", 6) == 0) { int ms = eval_expr_s(L + 6); if (ms > 0) pit_sleep((uint32_t)ms); pc++; continue; }

        /* ---- system("cmd"); ---- */
        if (str_ncmp(L, "system(", 7) == 0) {
            const char *p = L + 7; char cmd[64]; int ci = 0;
            if (*p == '"') { p++; while (*p && *p != '"' && ci < 63) cmd[ci++] = *p++; }
            cmd[ci] = '\0'; shell_exec(cmd); pc++; continue;
        }

        pc++;
    }
}

static void c_run_program(char lines[][CODE_LINE_MAX_LEN], int total) {
    env_var_cnt = 0;
    c_exec_block(lines, total, 0, total);
}

static void codestudio_load_demo(void) {
    code_line_cnt = 0;
    if (code_lang == LANG_ARCHAOS) {
        str_cpy(code_lines[code_line_cnt++], "echo === ArchaOS App ===", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "beep 880 150", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "fortune", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "neofetch", CODE_LINE_MAX_LEN);
        str_cpy(code_output, "Loaded demo (ArchaOS Script)", 64);
    } else if (code_lang == LANG_PYTHON) {
        str_cpy(code_lines[code_line_cnt++], "# ArchaOS Micro-Python App", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "print(\"Hello from Python!\")", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "beep(880, 150)", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "sys_exec(\"neofetch\")", CODE_LINE_MAX_LEN);
        str_cpy(code_output, "Loaded demo (MicroPython)", 64);
    } else if (code_lang == LANG_C) {
        str_cpy(code_lines[code_line_cnt++], "// ArchaOS Mini-C App", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "int main() {", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "  printf(\"Hello from C!\\n\");", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "  beep(880, 150);", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "  system(\"fortune\");", CODE_LINE_MAX_LEN);
        str_cpy(code_lines[code_line_cnt++], "}", CODE_LINE_MAX_LEN);
        str_cpy(code_output, "Loaded demo (Mini-C)", 64);
    }
    code_cur_len = 0;
    code_cur_line[0] = '\0';
    code_cursor_line = 0;
    code_cursor_col = 0;
    code_scroll_offset = 0;
}

/* ============================================================
 * CONSOLE OUTPUT APP STATE (APP_CONSOLE)
 * ============================================================ */
#define CONSOLE_LINES_MAX 64
#define CONSOLE_LINE_LEN 80
static char console_lines[CONSOLE_LINES_MAX][CONSOLE_LINE_LEN];
static int  console_line_cnt = 0;

static void console_clear(void) {
    console_line_cnt = 0;
    for (int i = 0; i < CONSOLE_LINES_MAX; i++) console_lines[i][0] = '\0';
}

static void console_add_line(const char *text) {
    if (!text || !text[0]) return;
    if (console_line_cnt < CONSOLE_LINES_MAX) {
        str_cpy(console_lines[console_line_cnt++], text, CONSOLE_LINE_LEN);
    } else {
        for (int i = 0; i < CONSOLE_LINES_MAX - 1; i++) {
            str_cpy(console_lines[i], console_lines[i+1], CONSOLE_LINE_LEN);
        }
        str_cpy(console_lines[CONSOLE_LINES_MAX - 1], text, CONSOLE_LINE_LEN);
    }
    redraw_all_frame(mouse_x, mouse_y);
}

static void draw_console(gui_window_t *w)
{
    int body_x = w->x + 4, body_y = w->y + 14;
    int body_w = w->w - 8, body_h = w->h - 18;

    fill_rect(body_x, body_y, body_w, body_h, COL_BLACK);
    draw_rect(body_x, body_y, body_w, body_h, COL_LIME);

    int max_rows = (body_h - 6) / 9;
    if (max_rows > CONSOLE_LINES_MAX) max_rows = CONSOLE_LINES_MAX;
    int start_row = (console_line_cnt > max_rows) ? (console_line_cnt - max_rows) : 0;

    for (int i = start_row; i < console_line_cnt; i++) {
        draw_str_clip(body_x + 4, body_y + 4 + (i - start_row) * 9, console_lines[i], COL_LIME, COL_BLACK, body_x + body_w - 4);
    }
}

static void codestudio_run(void) {
    open_window(11);
    console_clear();

    str_cpy(code_output, "Status: Running...", 64);
    redraw_all_frame(mouse_x, mouse_y);

    if (code_lang == LANG_ARCHAOS) console_add_line("=== ArchaOS Output ===");
    else if (code_lang == LANG_PYTHON) console_add_line("=== Python Output ===");
    else if (code_lang == LANG_C) console_add_line("=== C Output ===");

    int total = code_line_cnt;
    if (total == 0 || (total == 1 && code_lines[0][0] == '\0')) {
        show_toast("No code to run!", TOAST_ERR);
        sound_error();
        return;
    }

    shellext_capture_start();

    /* Assemble full program string */
    static char code_buf[4096];
    int cbi = 0;
    for (int i = 0; i < total; i++) {
        const char *cl = code_lines[i];
        while (*cl && cbi < 4094) code_buf[cbi++] = *cl++;
        code_buf[cbi++] = '\n';
    }
    code_buf[cbi] = '\0';
    (void)code_buf;

    if (code_lang == LANG_ARCHAOS) {
        for (int i = 0; i < total; i++)
            if (code_lines[i][0]) shell_exec(code_lines[i]);
    } else if (code_lang == LANG_PYTHON) {
        py_run_program(code_lines, total);
    } else if (code_lang == LANG_C) {
        c_run_program(code_lines, total);
    }

    shellext_capture_stop();

    /* Capture any VGA output from shell_exec calls */
    const char *cap = shellext_get_captured();
    if (cap && cap[0]) {
        char linebuf[80];
        int li = 0;
        for (int i = 0; cap[i]; i++) {
            if (cap[i] == '\n' || li >= 79) {
                linebuf[li] = '\0';
                if (li > 0) console_add_line(linebuf);
                li = 0;
            } else if (cap[i] != '\r') {
                linebuf[li++] = cap[i];
            }
        }
        if (li > 0) {
            linebuf[li] = '\0';
            console_add_line(linebuf);
        }
    }

    show_toast("App Output Displayed!", TOAST_OK);
    str_cpy(code_output, "Status: Program Finished", 64);
    sound_save();
}

static void codestudio_copy(void) {
    static char buf[2048];
    int bi = 0;
    for (int i = 0; i < code_line_cnt; i++) {
        const char *l = code_lines[i];
        while (*l && bi < 2046) buf[bi++] = *l++;
        if (bi < 2046 && i < code_line_cnt - 1) buf[bi++] = '\n';
    }
    buf[bi] = '\0';
    clipboard_copy(buf);
    show_toast("Copied code to Serial COM1!", TOAST_OK);
}

static void codestudio_paste(void) {
    clipboard_check_serial_input();
    const char *p = clipboard_paste();
    if (!p || !*p) {
        show_toast("Clipboard empty!", TOAST_INFO);
        return;
    }
    
    char lines[64][CODE_LINE_MAX_LEN];
    int l_cnt = 0;
    int li = 0;
    while (*p && l_cnt < 64) {
        if (*p == '\n' || li >= CODE_LINE_MAX_LEN - 1) {
            lines[l_cnt][li] = '\0';
            l_cnt++;
            li = 0;
        } else if (*p != '\r') {
            lines[l_cnt][li++] = *p;
        }
        p++;
    }
    if (li > 0 && l_cnt < 64) {
        lines[l_cnt][li] = '\0';
        l_cnt++;
    }

    if (code_line_cnt <= 0) {
        code_line_cnt = 1;
        code_lines[0][0] = '\0';
    }

    int add = l_cnt;
    if (code_line_cnt + add > CODE_MAX_LINES) {
        add = CODE_MAX_LINES - code_line_cnt;
    }

    if (add > 0) {
        for (int k = code_line_cnt - 1; k >= code_cursor_line; k--) {
            if (k + add < CODE_MAX_LINES) {
                str_cpy(code_lines[k + add], code_lines[k], CODE_LINE_MAX_LEN);
            }
        }
        for (int i = 0; i < add; i++) {
            str_cpy(code_lines[code_cursor_line + i], lines[i], CODE_LINE_MAX_LEN);
        }
        code_line_cnt += add;
        code_cursor_line += add;
        if (code_cursor_line >= code_line_cnt) {
            code_cursor_line = code_line_cnt - 1;
        }
        code_cursor_col = str_len(code_lines[code_cursor_line]);
    }

    show_toast("Pasted code from Clipboard!", TOAST_OK);
}

static char __attribute__((unused)) *code_get_line_ptr(int idx) {
    if (idx < 0) return code_lines[0];
    if (idx < CODE_MAX_LINES) return code_lines[idx];
    return code_lines[CODE_MAX_LINES - 1];
}

static int __attribute__((unused)) code_get_line_len(int idx) {
    return str_len(code_get_line_ptr(idx));
}

static void draw_codestudio(gui_window_t *w)
{
    /* Action Bar: [Run] [Clr] [Cpy] [Pst] [Tab] [Demo] [Lang] [Docs] ext */
    int tb_y = w->y + 13;
    fill_rect(w->x + 4, tb_y, 20, 11, GRAY(18));
    draw_rect(w->x + 4, tb_y, 20, 11, GRAY(8));
    draw_str(w->x + 6, tb_y + 2, "Run", COL_GREEN, GRAY(18));

    fill_rect(w->x + 26, tb_y, 20, 11, GRAY(18));
    draw_rect(w->x + 26, tb_y, 20, 11, GRAY(8));
    draw_str(w->x + 28, tb_y + 2, "Clr", COL_BLACK, GRAY(18));

    fill_rect(w->x + 48, tb_y, 20, 11, GRAY(18));
    draw_rect(w->x + 48, tb_y, 20, 11, GRAY(8));
    draw_str(w->x + 50, tb_y + 2, "Cpy", COL_YELLOW, GRAY(18));

    fill_rect(w->x + 70, tb_y, 20, 11, GRAY(18));
    draw_rect(w->x + 70, tb_y, 20, 11, GRAY(8));
    draw_str(w->x + 72, tb_y + 2, "Pst", COL_CYAN, GRAY(18));

    fill_rect(w->x + 92, tb_y, 20, 11, GRAY(18));
    draw_rect(w->x + 92, tb_y, 20, 11, GRAY(8));
    draw_str(w->x + 94, tb_y + 2, "Tab", COL_WHITE, GRAY(18));

    fill_rect(w->x + 114, tb_y, 26, 11, GRAY(18));
    draw_rect(w->x + 114, tb_y, 26, 11, GRAY(8));
    draw_str(w->x + 116, tb_y + 2, "Demo", COL_BLUE, GRAY(18));

    /* [Lang: ...] */
    const char *lstr = (code_lang == LANG_ARCHAOS) ? "Sh" : (code_lang == LANG_PYTHON ? "Py" : "C");
    fill_rect(w->x + 142, tb_y, 22, 11, GRAY(18));
    draw_rect(w->x + 142, tb_y, 22, 11, GRAY(8));
    draw_str(w->x + 144, tb_y + 2, lstr, COL_YELLOW, GRAY(18));

    /* [Docs] */
    fill_rect(w->x + 166, tb_y, 26, 11, code_show_docs ? COL_TEAL : GRAY(18));
    draw_rect(w->x + 166, tb_y, 26, 11, GRAY(8));
    draw_str(w->x + 168, tb_y + 2, "Docs", code_show_docs ? COL_WHITE : COL_BLACK, code_show_docs ? COL_TEAL : GRAY(18));

    const char *ext = (code_lang == LANG_ARCHAOS) ? "app.sh" : (code_lang == LANG_PYTHON ? "main.py" : "main.c");
    draw_str_clip(w->x + 196, tb_y + 2, ext, COL_WHITE, COL_WINBG, w->x + w->w - 4);

    /* Main Display Panel */
    int ed_y = w->y + 26, ed_w = w->w - 8, ed_h = w->h - 38;
    fill_rect(w->x + 4, ed_y, ed_w, ed_h, COL_WHITE);
    draw_rect(w->x + 4, ed_y, ed_w, ed_h, COL_BLACK);

    if (code_show_docs) {
        /* Render In-Built Interactive Documentation */
        fill_rect(w->x + 5, ed_y + 1, ed_w - 2, 11, COL_BLUE);
        if (code_lang == LANG_ARCHAOS) {
            draw_str(w->x + 8, ed_y + 2, "ArchaOS Script Docs", COL_WHITE, COL_BLUE);
            draw_str(w->x + 8, ed_y + 14, "Syntax: command arg1 arg2", COL_BLACK, COL_WHITE);
            draw_str(w->x + 8, ed_y + 24, "Print: echo <msg> | cat <file>", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 34, "Audio: beep <freq_hz> <ms>", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 44, "Time:  sleep <ms>", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 54, "Files: > file | >> file | pipe |", COL_DARK_GRAY, COL_WHITE);
        } else if (code_lang == LANG_PYTHON) {
            draw_str(w->x + 8, ed_y + 2, "Micro-Python Docs", COL_WHITE, COL_BLUE);
            draw_str(w->x + 8, ed_y + 14, "Syntax: print(\"text\")", COL_BLACK, COL_WHITE);
            draw_str(w->x + 8, ed_y + 24, "Audio:  beep(freq_hz, ms)", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 34, "Time:   sleep(ms)", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 44, "Shell:  sys_exec(\"command\")", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 54, "Vars:   x = 10 | y = \"text\"", COL_DARK_GRAY, COL_WHITE);
        } else if (code_lang == LANG_C) {
            draw_str(w->x + 8, ed_y + 2, "Mini-C Compiler Docs", COL_WHITE, COL_BLUE);
            draw_str(w->x + 8, ed_y + 14, "Syntax: int main() { ... }", COL_BLACK, COL_WHITE);
            draw_str(w->x + 8, ed_y + 24, "Print:  printf(\"text\\n\");", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 34, "Audio:  beep(freq_hz, ms);", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 44, "Time:   sleep(ms);", COL_DARK_GRAY, COL_WHITE);
            draw_str(w->x + 8, ed_y + 54, "Shell:  system(\"command\");", COL_DARK_GRAY, COL_WHITE);
        }
    } else {
        /* Render Code Editor */
        int max_visible = (ed_h - 6) / 9;
        if (max_visible < 1) max_visible = 1;
        int total_lines = code_line_cnt > 0 ? code_line_cnt : 1;

        if (code_scroll_offset > total_lines - max_visible) {
            code_scroll_offset = total_lines - max_visible;
        }
        if (code_scroll_offset < 0) code_scroll_offset = 0;

        // Ensure cursor is visible
        if (code_cursor_line < code_scroll_offset) {
            code_scroll_offset = code_cursor_line;
        } else if (code_cursor_line >= code_scroll_offset + max_visible) {
            code_scroll_offset = code_cursor_line - max_visible + 1;
        }

        int start_line = code_scroll_offset;
        int end_line = start_line + max_visible;
        if (end_line > total_lines) end_line = total_lines;

        int line_y = ed_y + 3;
        for (int i = start_line; i < end_line; i++) {
            char *lstr = code_lines[i];
            uint8_t fg = (i == code_cursor_line) ? COL_BLACK : COL_BLUE;
            draw_str_clip(w->x + 8, line_y, lstr, fg, COL_WHITE, w->x + w->w - 8);

            if (i == code_cursor_line && (pit_ticks() / 400) % 2 == 0 && w->focused) {
                int cx = w->x + 8 + code_cursor_col * 6;
                if (cx < w->x + w->w - 10) fill_rect(cx, line_y, 5, 7, COL_BLACK);
            }
            line_y += 9;
        }
    }

    /* Output status bar at bottom */
    int stat_y = w->y + w->h - 12;
    fill_rect(w->x + 4, stat_y, w->w - 8, 10, GRAY(4));
    draw_str_clip(w->x + 6, stat_y + 1, code_output, COL_YELLOW, GRAY(4), w->x + w->w - 6);
}

/* ============================================================
 * SCREENSAVER — Starfield warp
 * ============================================================ */
#define NUM_STARS 60
typedef struct { int x, y, z; } star_t;
static star_t stars[NUM_STARS];

static void saver_init(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x = (int)(rand_next() % 320) - 160;
        stars[i].y = (int)(rand_next() % 200) - 100;
        stars[i].z = (int)(rand_next() % 128) + 1;
    }
}

static void draw_screensaver(void) {
    fill_rect(0, 0, 320, 200, COL_BLACK);
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].z -= 2;
        if (stars[i].z <= 0) {
            stars[i].x = (int)(rand_next() % 320) - 160;
            stars[i].y = (int)(rand_next() % 200) - 100;
            stars[i].z = 127;
        }
        int sx = 160 + (stars[i].x * 128) / stars[i].z;
        int sy = 100 + (stars[i].y * 128) / stars[i].z;
        if (sx < 0 || sx >= 320 || sy < 0 || sy >= 200) continue;
        int bright = (128 - stars[i].z) / 18;
        if (bright < 0) bright = 0;
        if (bright > 5) bright = 5;
        uint8_t col = RGB(bright, bright, bright);
        put_pixel(sx, sy, col);
        if (bright > 2) put_pixel(sx+1, sy, col);
    }
    /* Dim "ArchaOS" watermark */
    draw_str(108, 96, "ArchaOS", GRAY(4), COL_BLACK);
    draw_str(94, 104, "Press any key...", GRAY(3), COL_BLACK);
}

/* ============================================================
 * DRAW WINDOW & DESKTOP
 * ============================================================ */

#define TASKBAR_Y  188
#define TASKBAR_H  12
#define COL_TASKBAR GRAY(3)

static int start_menu_open = 0;
static int start_menu_selected_idx = 0;

/* Window open animation flash — draws a highlight border that fades */
static void draw_win_open_flash(gui_window_t *w)
{
    uint32_t age = pit_ticks() - w->open_tick;
    if (age > 200) return; /* 200ms animation */
    /* 3 concentric bright borders that fade with age */
    int n = (int)(3 - (age * 3) / 200);
    for (int i = 0; i <= n; i++) {
        draw_rect(w->x - i, w->y - i, w->w + i*2, w->h + i*2, COL_TEAL);
    }
}

static void draw_window(int idx)
{
    gui_window_t *w = &windows[idx];
    if (!w->visible || w->minimized) return;

    uint8_t title_bg = w->focused ? COL_BLUE : COL_DARK_GRAY;
    uint8_t border   = w->focused ? COL_TEAL : GRAY(8);

    /* Multi-Tier Drop Shadow */
    if (!w->maximized) {
        fill_rect(w->x + 4, w->y + 4, w->w, w->h, GRAY(1));
        fill_rect(w->x + 2, w->y + 2, w->w, w->h, GRAY(3));
    }
    /* Body */
    fill_rect(w->x, w->y, w->w, w->h, COL_WINBG);
    /* Title bar */
    fill_rect(w->x, w->y, w->w, 12, title_bg);

    /* Icon on Left of Title Bar */
    draw_app_icon(w->x + 3, w->y + 3, w->app);
    draw_str_clip(w->x + 13, w->y + 2, w->title, COL_WHITE, title_bg, w->x + w->w - 34);

    /* Controls: Minimize _, Split |, Maximize =, Close X */
    fill_rect(w->x + w->w - 32, w->y + 2, 7, 8, GRAY(14));
    draw_char(w->x + w->w - 31, w->y + 2, '_', COL_BLACK, GRAY(14));

    fill_rect(w->x + w->w - 24, w->y + 2, 7, 8, GRAY(14));
    draw_char(w->x + w->w - 23, w->y + 2, '|', COL_BLACK, GRAY(14));

    fill_rect(w->x + w->w - 16, w->y + 2, 7, 8, GRAY(14));
    draw_char(w->x + w->w - 15, w->y + 2, '=', COL_BLACK, GRAY(14));

    fill_rect(w->x + w->w - 8, w->y + 2, 7, 8, COL_RED);
    draw_char(w->x + w->w - 7, w->y + 2, 'X', COL_WHITE, COL_RED);

    /* Border & Active Window Glow */
    draw_rect(w->x, w->y, w->w, w->h, border);
    if (w->focused && !w->maximized) {
        draw_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 2, RGB(0,2,4));
    }

    /* Render App Contents */
    switch (w->app) {
        case APP_CALC:       draw_calculator(w); break;
        case APP_PAINTER:    draw_painter(w); break;
        case APP_MINESWEEPER:draw_minesweeper(w); break;
        case APP_FILEMAN:    draw_fileman(w); break;
        case APP_NOTEPAD:    draw_notepad(w); break;
        case APP_CPANEL:     draw_cpanel(w); break;
        case APP_TASKMAN:    draw_taskman(w); break;
        case APP_IMGVIEW:    draw_imgview(w); break;
        case APP_SNAKE:      draw_snake(w); break;
        case APP_CLI:        draw_cli(w); break;
        case APP_CODESTUDIO: draw_codestudio(w); break;
        case APP_CONSOLE:    draw_console(w); break;
        case APP_BROWSER:    draw_browser(w); break;
        case APP_AI_CHAT:    draw_ai_chat(w); break;
        default: break;
    }

    /* Window open flash animation */
    draw_win_open_flash(w);
}

/* ============================================================
 * CONTEXT MENU & DESKTOP ICONS STATE
 * ============================================================ */
static int ctx_menu_open = 0;
static int ctx_menu_x = 0, ctx_menu_y = 0;
static int selected_desktop_icon = -1;
static int selected_desktop_file = -1;

static void draw_context_menu(void)
{
    if (!ctx_menu_open) return;
    int mw = 100, mh = 80;
    int mx = ctx_menu_x, my = ctx_menu_y;
    if (mx + mw > SCREEN_W) mx = SCREEN_W - mw;
    if (my + mh > TASKBAR_Y) my = TASKBAR_Y - mh;

    /* Shadow & body */
    fill_rect(mx + 2, my + 2, mw, mh, GRAY(2));
    fill_rect(mx, my, mw, mh, COL_WINBG);
    draw_rect(mx, my, mw, mh, COL_BLACK);

    static const char *ctx_items[6] = {
        "New Note", "Terminal CLI", "Snake Game", "App Studio", "Control Panel", "Exit GUI"
    };

    for (int i = 0; i < 6; i++) {
        int iy = my + 3 + i * 12;
        draw_str(mx + 8, iy + 1, ctx_items[i], COL_BLACK, COL_WINBG);
        if (i < 5) draw_hline(mx + 4, iy + 11, mw - 8, GRAY(18));
    }
}

static void draw_start_menu(void)
{
    if (!start_menu_open) return;
    int mx = 2, my = 26, mw = 115, mh = 160;
    fill_rect(mx + 2, my + 2, mw, mh, GRAY(2));
    fill_rect(mx, my, mw, mh, COL_WINBG);
    draw_rect(mx, my, mw, mh, COL_BLACK);

    /* Intricate Vertical ArchaOS Banner */
    fill_rect(mx, my, 18, mh, COL_BLUE);
    draw_rect(mx, my, 18, mh, COL_TEAL);
    draw_char(mx + 6, my + 160, 'A', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 148, 'R', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 136, 'C', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 124, 'H', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 112, 'A', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 100, 'O', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 88,  'S', COL_WHITE, COL_BLUE);

    static const char *menu_items[13] = {
        "Calculator", "Painter", "Minesweeper", "File Manager",
        "Notepad", "Control Panel", "Task Manager", "Image Viewer",
        "Snake", "Terminal", "App Studio", "Web Browser",
        "AI Assistant"
    };

    static const app_type_t menu_apps[13] = {
        APP_CALC, APP_PAINTER, APP_MINESWEEPER, APP_FILEMAN,
        APP_NOTEPAD, APP_CPANEL, APP_TASKMAN, APP_IMGVIEW,
        APP_SNAKE, APP_CLI, APP_CODESTUDIO, APP_BROWSER,
        APP_AI_CHAT
    };

    for (int i = 0; i < 13; i++) {
        app_type_t app_id = menu_apps[i];
        int item_y = my + 3 + i * 12;
        if (i == start_menu_selected_idx) {
            fill_rect(mx + 19, item_y - 1, mw - 20, 11, COL_BLUE);
            draw_app_icon(mx + 21, item_y + 1, app_id);
            draw_str(mx + 31, item_y + 1, menu_items[i], COL_WHITE, COL_BLUE);
        } else {
            draw_app_icon(mx + 21, item_y + 1, app_id);
            draw_str(mx + 31, item_y + 1, menu_items[i], COL_BLACK, COL_WINBG);
        }
        if (i < 12) draw_hline(mx + 20, item_y + 11, mw - 22, GRAY(18));
    }
}

static void draw_processbar(void)
{
    fill_rect(0, TASKBAR_Y, SCREEN_W, TASKBAR_H, COL_TASKBAR);
    draw_hline(0, TASKBAR_Y, SCREEN_W, GRAY(15));

    /* Start Button with ArchaOS Logo */
    fill_rect(2, TASKBAR_Y + 1, 54, 10, start_menu_open ? GRAY(12) : GRAY(8));
    draw_rect(2, TASKBAR_Y + 1, 54, 10, GRAY(14));
    draw_str(5, TASKBAR_Y + 3, "ArchaOS", COL_GREEN, start_menu_open ? GRAY(12) : GRAY(8));

    /* Count Open Windows (visible == 1) */
    int open_cnt = 0;
    for (int i = 0; i < win_count; i++) if (windows[i].visible) open_cnt++;

    if (open_cnt > 0) {
        int avail_w = 205; /* space from x=58 to x=263 */
        int btn_w = avail_w / open_cnt;
        if (btn_w > 75) btn_w = 75;
        int bx = 58;

        for (int i = 0; i < win_count; i++) {
            if (windows[i].visible) {
                uint8_t bbg = (i == focused_win && !windows[i].minimized) ? GRAY(14) : GRAY(6);
                fill_rect(bx, TASKBAR_Y + 1, btn_w - 2, 10, bbg);
                draw_rect(bx, TASKBAR_Y + 1, btn_w - 2, 10, GRAY(14));

                /* Draw Program Icon */
                draw_app_icon(bx + 2, TASKBAR_Y + 3, windows[i].app);

                /* If width allows text, draw title */
                if (btn_w >= 55) {
                    draw_str_clip(bx + 11, TASKBAR_Y + 3, windows[i].title, COL_WHITE, bbg, bx + btn_w - 4);
                } else if (btn_w >= 32) {
                    draw_str_clip(bx + 11, TASKBAR_Y + 3, windows[i].short_title, COL_WHITE, bbg, bx + btn_w - 4);
                }

                bx += btn_w;
            }
        }
    }

    /* Clock */
    uint8_t h, m, s;
    rtc_read(&h, &m, &s);
    char buf[9];
    buf[0] = '0' + h/10; buf[1] = '0' + h%10; buf[2] = ':';
    buf[3] = '0' + m/10; buf[4] = '0' + m%10; buf[5] = ':';
    buf[6] = '0' + s/10; buf[7] = '0' + s%10; buf[8] = '\0';
    draw_str(SCREEN_W - 52, TASKBAR_Y + 3, buf, COL_WHITE, COL_TASKBAR);
}

static void get_desktop_slot_pos(int slot_idx, int *out_x, int *out_y)
{
    /* 6 columns across desktop (0..5), 3 items per column (0..2) */
    int col = (slot_idx / 3) % 6;
    int row = slot_idx % 3;
    if (out_x) *out_x = 8 + col * 52;
    if (out_y) *out_y = 16 + row * 48;
}

static void draw_desktop_icons(void)
{
    /* Render clickable app shortcuts seamlessly on desktop across all 6 columns */
    int slot = 0;
    for (int i = 0; i < win_count; i++) {
        int ix, iy;
        get_desktop_slot_pos(slot++, &ix, &iy);
        
        if (selected_desktop_icon == i) {
            fill_rect(ix - 2, iy - 2, 44, 28, RGB(0,2,4));
            draw_rect(ix - 2, iy - 2, 44, 28, COL_WHITE);
        }

        draw_app_icon(ix + 18, iy + 2, windows[i].app);
        /* Draw text with transparent background (0xFF) */
        uint8_t text_bg = (selected_desktop_icon == i) ? RGB(0,2,4) : 0xFF;
        draw_str_clip(ix, iy + 14, windows[i].short_title, COL_WHITE, text_bg, ix + 42);
    }

    /* Render User Files from VFS root seamlessly packed into remaining slots */
    fs_node_t *root = fs_root();
    if (root) {
        for (int i = 0; i < root->child_count && slot < 18; i++) {
            fs_node_t *f = root->children[i];
            if (!f || f->type != FS_FILE) continue;
            if (f->name[0] == '.' || str_cmp(f->name, "tmp_pipe") == 0) continue;

            int ix, iy;
            get_desktop_slot_pos(slot++, &ix, &iy);

            if (selected_desktop_file == i) {
                fill_rect(ix - 2, iy - 2, 44, 28, RGB(0,2,4));
                draw_rect(ix - 2, iy - 2, 44, 28, COL_WHITE);
            }

            int nlen = str_len(f->name);
            int is_wav = (nlen >= 4 && str_cmp(f->name + nlen - 4, ".wav") == 0);
            int is_vid = (nlen >= 4 && str_cmp(f->name + nlen - 4, ".vid") == 0);
            int is_py  = (nlen >= 3 && str_cmp(f->name + nlen - 3, ".py") == 0);

            if (is_wav || is_vid) {
                fill_rect(ix + 18, iy + 2, 8, 8, is_vid ? RGB(0,3,5) : RGB(0,5,2));
                draw_rect(ix + 18, iy + 2, 8, 8, COL_WHITE);
                draw_char(ix + 20, iy + 2, '>', COL_WHITE, is_vid ? RGB(0,3,5) : RGB(0,5,2));
            } else if (is_py) {
                fill_rect(ix + 18, iy + 2, 8, 8, RGB(5,4,0));
                draw_rect(ix + 18, iy + 2, 8, 8, COL_WHITE);
                draw_char(ix + 20, iy + 2, 'P', COL_WHITE, RGB(5,4,0));
            } else {
                fill_rect(ix + 18, iy + 2, 8, 8, COL_WHITE);
                draw_rect(ix + 18, iy + 2, 8, 8, GRAY(10));
                draw_char(ix + 20, iy + 2, '=', COL_BLACK, COL_WHITE);
            }

            uint8_t text_bg = (selected_desktop_file == i) ? RGB(0,2,4) : 0xFF;
            draw_str_clip(ix, iy + 14, f->name, COL_WHITE, text_bg, ix + 42);
        }
    }
}

static void draw_desktop(void)
{
    fill_rect(0, 0, SCREEN_W, TASKBAR_Y, gui_desktop_color);

    if (gui_wallpaper_type == 1) {
        /* Starfield / Cosmic */
        for (int i = 0; i < 40; i++) {
            int sx = (i * 37 + 13) % SCREEN_W;
            int sy = (i * 29 + 7) % TASKBAR_Y;
            put_pixel(sx, sy, (i % 2 == 0) ? COL_WHITE : COL_YELLOW);
        }
    } else if (gui_wallpaper_type == 2) {
        /* Grid Matrix */
        for (int x = 0; x < SCREEN_W; x += 16)
            for (int y = 0; y < TASKBAR_Y; y += 16)
                draw_rect(x, y, 16, 16, RGB(0,2,0));
    } else if (gui_wallpaper_type == 3) {
        /* Sunset Lines */
        for (int y = 0; y < TASKBAR_Y; y += 8)
            draw_hline(0, y, SCREEN_W, RGB((y*5)/TASKBAR_Y, 1, 1));
    } else {
        /* Solid Teal with subtle grid */
        for (int y = 0; y < TASKBAR_Y; y += 16)
            draw_hline(0, y, SCREEN_W, RGB(0,1,3));
    }

    draw_desktop_icons();

    /* Clean Solid Black "ArchaOS v0.5" Watermark */
    draw_str(SCREEN_W - 74, TASKBAR_Y - 14, "ArchaOS v0.5", COL_BLACK, gui_desktop_color);
}

/* ============================================================
 * TOAST NOTIFICATION RENDERING (replaces error dialog)
 * ============================================================ */
static void draw_toast(void)
{
    if (!toast_visible) return;
    if (pit_ticks() >= toast_expire) { toast_visible = 0; return; }

    int msg_len = str_len(toast_msg);
    int tw = msg_len * 6 + 14;
    if (tw > 220) tw = 220;
    int tx = (SCREEN_W - tw) / 2;
    int ty = TASKBAR_Y - 18;

    /* Shadow */
    fill_rect(tx + 2, ty + 2, tw, 12, GRAY(1));
    /* Body */
    uint8_t bg;
    if (toast_type == TOAST_ERR)   bg = RGB(4,0,0);
    else if (toast_type == TOAST_INFO) bg = RGB(0,2,4);
    else                            bg = RGB(0,3,1);
    fill_rect(tx, ty, tw, 12, bg);
    draw_rect(tx, ty, tw, 12, COL_WHITE);

    /* Icon glyph */
    char icon = (toast_type == TOAST_ERR) ? '!' : (toast_type == TOAST_INFO) ? 'i' : '+';
    draw_char(tx + 3, ty + 2, icon, COL_WHITE, bg);
    draw_str_clip(tx + 11, ty + 2, toast_msg, COL_WHITE, bg, tx + tw - 2);
}

int gui_cursor_type = 0; /* 0: Arrow, 1: Hand, 2: I-Beam, 3: Spinner */
volatile int g_net_cancel_requested = 0;

/* Mouse Pointer Graphic (Arrow) */
static const uint8_t MOUSE_ARROW[11][11] = {
    {1,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,1,1,1,1,0,0,0},
    {1,2,1,2,1,0,0,0,0,0,0},
    {1,1,0,1,1,0,0,0,0,0,0},
};

/* Hand / Link Pointer Cursor */
static const uint8_t MOUSE_HAND[12][11] = {
    {0,0,0,1,1,0,0,0,0,0,0},
    {0,0,1,2,2,1,0,0,0,0,0},
    {0,0,1,2,2,1,0,0,0,0,0},
    {0,0,1,2,2,1,1,1,0,0,0},
    {0,1,1,2,2,1,2,2,1,1,0},
    {1,2,1,2,2,1,2,2,1,2,1},
    {1,2,2,2,2,2,2,2,1,2,1},
    {0,1,2,2,2,2,2,2,2,2,1},
    {0,1,2,2,2,2,2,2,2,1,0},
    {0,0,1,2,2,2,2,2,2,1,0},
    {0,0,1,2,2,2,2,2,1,0,0},
    {0,0,0,1,1,1,1,1,0,0,0}
};

/* Text I-Beam Cursor */
static const uint8_t MOUSE_IBEAM[11][7] = {
    {1,1,1,1,1,1,1},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {0,0,0,1,0,0,0},
    {1,1,1,1,1,1,1}
};

/* Animated 8-Phase Circular Loading Spinner */
static const uint8_t SPINNER_SHAPE[8][8][8] = {
    { /* 0 */
        {0,0,2,2,2,0,0,0},
        {0,2,3,3,3,2,0,0},
        {2,3,0,0,0,3,2,0},
        {2,3,0,0,0,0,2,0},
        {2,3,0,0,0,0,2,0},
        {2,3,0,0,0,1,2,0},
        {0,2,1,1,1,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 1 */
        {0,0,2,2,2,0,0,0},
        {0,2,1,3,3,2,0,0},
        {2,0,0,0,0,3,2,0},
        {2,0,0,0,0,3,2,0},
        {2,0,0,0,0,3,2,0},
        {2,1,0,0,0,1,2,0},
        {0,2,1,1,1,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 2 */
        {0,0,2,2,2,0,0,0},
        {0,2,1,1,3,2,0,0},
        {2,1,0,0,0,3,2,0},
        {2,0,0,0,0,3,2,0},
        {2,0,0,0,0,3,2,0},
        {2,0,0,0,0,1,2,0},
        {0,2,1,1,1,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 3 */
        {0,0,2,2,2,0,0,0},
        {0,2,1,1,1,2,0,0},
        {2,1,0,0,0,1,2,0},
        {2,1,0,0,0,3,2,0},
        {2,0,0,0,0,3,2,0},
        {2,0,0,0,0,3,2,0},
        {0,2,1,1,3,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 4 */
        {0,0,2,2,2,0,0,0},
        {0,2,1,1,1,2,0,0},
        {2,1,0,0,0,1,2,0},
        {2,1,0,0,0,1,2,0},
        {2,1,0,0,0,1,2,0},
        {2,0,0,0,0,3,2,0},
        {0,2,3,3,3,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 5 */
        {0,0,2,2,2,0,0,0},
        {0,2,1,1,1,2,0,0},
        {2,3,0,0,0,1,2,0},
        {2,3,0,0,0,1,2,0},
        {2,3,0,0,0,1,2,0},
        {2,3,0,0,0,0,2,0},
        {0,2,3,1,1,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 6 */
        {0,0,2,2,2,0,0,0},
        {0,2,3,1,1,2,0,0},
        {2,3,0,0,0,1,2,0},
        {2,3,0,0,0,0,2,0},
        {2,3,0,0,0,0,2,0},
        {2,3,0,0,0,0,2,0},
        {0,2,1,1,1,2,0,0},
        {0,0,2,2,2,0,0,0}
    },
    { /* 7 */
        {0,0,2,2,2,0,0,0},
        {0,2,3,3,1,2,0,0},
        {2,3,0,0,0,1,2,0},
        {2,3,0,0,0,0,2,0},
        {2,3,0,0,0,0,2,0},
        {2,3,0,0,0,1,2,0},
        {0,2,1,1,1,2,0,0},
        {0,0,2,2,2,0,0,0}
    }
};

static void draw_mouse_cursor(int mx, int my)
{
    if (gui_cursor_type == 1) { /* Hand */
        for (int r = 0; r < 12; r++)
            for (int c = 0; c < 11; c++) {
                uint8_t v = MOUSE_HAND[r][c];
                if (v == 1) put_pixel(mx + c - 3, my + r, COL_BLACK);
                else if (v == 2) put_pixel(mx + c - 3, my + r, COL_WHITE);
            }
    } else if (gui_cursor_type == 2) { /* I-Beam */
        for (int r = 0; r < 11; r++)
            for (int c = 0; c < 7; c++) {
                uint8_t v = MOUSE_IBEAM[r][c];
                if (v == 1) put_pixel(mx + c - 3, my + r - 5, COL_BLACK);
            }
    } else if (gui_cursor_type == 3) { /* Spinning Loader */
        int frame = (int)((pit_ticks() / 3) % 8);
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                uint8_t v = SPINNER_SHAPE[frame][r][c];
                if (v == 1) put_pixel(mx + c, my + r, COL_BLACK);
                else if (v == 2) put_pixel(mx + c, my + r, GRAY(10));
                else if (v == 3) put_pixel(mx + c, my + r, RGB(0,4,5)); /* Glowing Cyan */
            }
    } else { /* Default Arrow */
        for (int r = 0; r < 11; r++)
            for (int c = 0; c < 11; c++) {
                uint8_t v = MOUSE_ARROW[r][c];
                if (v == 1) put_pixel(mx + c, my + r, COL_BLACK);
                else if (v == 2) put_pixel(mx + c, my + r, COL_WHITE);
            }
    }
}

void gui_cooperative_pump(const char *status_msg)
{
    if (status_msg && status_msg[0]) {
        str_cpy(browser_status, status_msg, sizeof(browser_status));
    }
    gui_cursor_type = 3; /* Spinner */

    /* Check keyboard for Escape (Cancel) */
    uint8_t kbd_status = inb(0x64);
    if ((kbd_status & 0x01) && !(kbd_status & 0x20)) {
        uint8_t sc = inb(0x60);
        if (sc == 0x01) { /* Escape */
            g_net_cancel_requested = 1;
            str_cpy(browser_status, "Cancelled", sizeof(browser_status));
            sound_click();
        }
    }

    /* Redraw frame periodically */
    static uint32_t last_pump_tick = 0;
    uint32_t now = pit_ticks();
    if (now - last_pump_tick >= 1) {
        last_pump_tick = now;
        redraw_all_frame(mouse_x, mouse_y);
    }
}

static void redraw_all_frame(int mx, int my)
{
    if (screensaver_active) {
        draw_screensaver();
        draw_mouse_cursor(mx, my);
        gui_flip();
        return;
    }

    draw_desktop();
    for (int i = win_count - 1; i >= 0; i--)
        if (i != focused_win) draw_window(i);

    if (focused_win >= 0 && focused_win < win_count)
        draw_window(focused_win);

    draw_start_menu();
    draw_processbar();
    draw_context_menu();
    draw_toast();           /* Toast overlays instead of blocking alert */
    draw_mouse_cursor(mx, my);

    gui_flip();
}

/* Open file with associated app */
static void open_associated_file(fs_node_t *f) {
    if (!f || f->type != FS_FILE) return;

    int len = str_len(f->name);
    /* Text files -> Notepad */
    if (len >= 2 && (str_cmp(f->name + len - 4, ".txt") == 0 ||
                     (len >= 5 && str_cmp(f->name + len - 5, ".conf") == 0) ||
                     str_cmp(f->name + len - 2, ".c") == 0 ||
                     str_cmp(f->name + len - 2, ".h") == 0 ||
                     (len >= 4 && str_cmp(f->name + len - 4, ".asm") == 0) ||
                     str_cmp(f->name + len - 3, ".md") == 0)) {
        str_cpy(notepad_filename, f->name, 32);

        notepad_len = 0;
        if (f->data) {
            for (size_t k = 0; k < f->size && k < 500; k++) notepad_buf[k] = f->data[k];
            notepad_len = (int)(f->size < 500 ? f->size : 500);
        }
        notepad_buf[notepad_len] = '\0';
        windows[4].visible = 1;
        windows[4].minimized = 0;
        if (focused_win >= 0 && focused_win < win_count) windows[focused_win].focused = 0;
        focused_win = 4;
        windows[4].focused = 1;
        windows[4].open_tick = pit_ticks();
        sound_win_open();
    }
    /* Image files -> Image Viewer */
    else if (len >= 4 && (str_cmp(f->name + len - 4, ".bmp") == 0 ||
                          str_cmp(f->name + len - 4, ".png") == 0 ||
                          str_cmp(f->name + len - 4, ".jpg") == 0 ||
                          str_cmp(f->name + len - 4, ".img") == 0)) {
        str_cpy(imgview_filename, f->name, 32);
        str_cpy(paint_filename,   f->name, 32);

        if (f->data) {
            size_t sz = f->size < (size_t)(PAINT_CW * PAINT_CH) ? f->size : (size_t)(PAINT_CW * PAINT_CH);
            for (size_t k = 0; k < sz; k++) paint_canvas[k] = (uint8_t)f->data[k];
        }

        windows[7].visible = 1;
        windows[7].minimized = 0;
        windows[7].open_tick = pit_ticks();
        if (focused_win >= 0 && focused_win < win_count) windows[focused_win].focused = 0;
        focused_win = 7;
        windows[7].focused = 1;
        sound_win_open();
    }
    /* Audio & Video files -> Web Browser In-Browser Media Player */
    else if (len >= 4 && (str_cmp(f->name + len - 4, ".ogg") == 0 ||
                          str_cmp(f->name + len - 4, ".mp3") == 0 ||
                          str_cmp(f->name + len - 4, ".wav") == 0 ||
                          str_cmp(f->name + len - 4, ".gif") == 0 ||
                          str_cmp(f->name + len - 4, ".vid") == 0 ||
                          str_cmp(f->name + len - 4, ".mpg") == 0 ||
                          str_cmp(f->name + len - 4, ".mp4") == 0)) {
        windows[12].visible = 1;
        windows[12].minimized = 0;
        if (focused_win >= 0 && focused_win < win_count) windows[focused_win].focused = 0;
        focused_win = 12;
        windows[12].focused = 1;
        windows[12].open_tick = pit_ticks();
        sound_win_open();
        browser_fetch(f->name);
    }
    else {
        show_toast("Unknown file type!", TOAST_ERR);
        sound_error();
    }
}

static int caps_lock_state = 0;

/* Shift & Caps Lock Key Scancode Conversion Table */
static char scancode_to_ascii(uint8_t sc, int shift) {
    if (sc >= 128) return 0;
    static const char map_lower[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    static const char map_upper[128] = {
        0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
        '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    };
    char lower_c = map_lower[sc];
    if (lower_c >= 'a' && lower_c <= 'z') {
        int is_upper = shift ^ caps_lock_state;
        return is_upper ? map_upper[sc] : map_lower[sc];
    }
    return shift ? map_upper[sc] : map_lower[sc];
}

/* ============================================================
 * HELPER: open a window by index, set focus, play sound
 * ============================================================ */
static void open_window(int idx) {
    if (idx < 0 || idx >= win_count) return;
    windows[idx].visible = 1;
    windows[idx].minimized = 0;
    windows[idx].open_tick = pit_ticks();
    if (focused_win >= 0 && focused_win < win_count)
        windows[focused_win].focused = 0;
    focused_win = idx;
    windows[idx].focused = 1;
    sound_win_open();
}

static void init_vfs_media_assets(void) {
    /* Create a 48x24 24-bit uncompressed BMP: archaos_logo.bmp */
    static uint8_t bmp[54 + 48 * 24 * 3];
    uint32_t w = 48, h = 24;
    uint32_t row_bytes = (w * 3 + 3) & ~3;
    uint32_t img_size = row_bytes * h;
    uint32_t file_size = 54 + img_size;

    memset(bmp, 0, sizeof(bmp));
    /* BMP Header */
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = (uint8_t)(file_size);
    bmp[3] = (uint8_t)(file_size >> 8);
    bmp[4] = (uint8_t)(file_size >> 16);
    bmp[5] = (uint8_t)(file_size >> 24);
    bmp[10] = 54; /* offset */

    /* DIB Header */
    bmp[14] = 40; /* DIB size */
    bmp[18] = (uint8_t)(w); bmp[19] = (uint8_t)(w >> 8);
    bmp[22] = (uint8_t)(h); bmp[23] = (uint8_t)(h >> 8);
    bmp[26] = 1;  /* planes */
    bmp[28] = 24; /* bpp */
    bmp[34] = (uint8_t)(img_size);
    bmp[35] = (uint8_t)(img_size >> 8);

    /* Fill pixels: Dark Slate background with stylized cyan & orange logo */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = bmp + 54 + y * row_bytes;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *p = row + x * 3;
            if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                p[0] = 0x80; p[1] = 0x60; p[2] = 0x20;
            } else if (x >= 6 && x <= 18 && y >= 5 && y <= 18) {
                p[0] = 0x0c; p[1] = 0x58; p[2] = 0xea; /* Firefox orange #ea580c */
            } else if (x >= 22 && x <= 42 && y >= 5 && y <= 18) {
                p[0] = 0xc7; p[1] = 0x84; p[2] = 0x02; /* ArchaOS cyan #0284c7 */
            } else {
                p[0] = 0x28; p[1] = 0x1e; p[2] = 0x12; /* Dark slate */
            }
        }
    }
    fs_write("archaos_logo.bmp", (const char *)bmp, 54 + img_size);

    /* 2. Create 120x48 24-bit Video Thumbnail: video_thumb.bmp */
    static uint8_t tbmp[54 + 120 * 48 * 3];
    uint32_t tw = 120, th = 48;
    uint32_t trow_bytes = (tw * 3 + 3) & ~3;
    uint32_t timg_size = trow_bytes * th;
    uint32_t tfile_size = 54 + timg_size;

    memset(tbmp, 0, sizeof(tbmp));
    tbmp[0] = 'B'; tbmp[1] = 'M';
    tbmp[2] = (uint8_t)(tfile_size);
    tbmp[3] = (uint8_t)(tfile_size >> 8);
    tbmp[4] = (uint8_t)(tfile_size >> 16);
    tbmp[5] = (uint8_t)(tfile_size >> 24);
    tbmp[10] = 54;
    tbmp[14] = 40;
    tbmp[18] = (uint8_t)(tw); tbmp[19] = (uint8_t)(tw >> 8);
    tbmp[22] = (uint8_t)(th); tbmp[23] = (uint8_t)(th >> 8);
    tbmp[26] = 1;
    tbmp[28] = 24;
    tbmp[34] = (uint8_t)(timg_size);
    tbmp[35] = (uint8_t)(timg_size >> 8);

    /* Paint Big Buck Bunny sunny meadow scene */
    for (uint32_t y = 0; y < th; y++) {
        uint8_t *row = tbmp + 54 + y * trow_bytes;
        for (uint32_t x = 0; x < tw; x++) {
            uint8_t *p = row + x * 3;
            if (y >= 26) {
                int dx = (int)x - 92, dy = (int)y - 38;
                if (dx * dx + dy * dy <= 64) {
                    p[0] = 0x50; p[1] = 0xee; p[2] = 0xff; /* Golden Sun #ffee50 */
                } else {
                    p[0] = 0xf8; p[1] = 0xd8; p[2] = (uint8_t)(0x40 + (y - 26) * 3); /* Blue sky */
                }
            } else if (y >= 14) {
                int hill_h = 18 + ((int)(x * 7) % 5);
                if ((int)y <= hill_h) {
                    p[0] = 0x40; p[1] = 0xaa; p[2] = 0x20; /* Grass hills */
                } else {
                    p[0] = 0xf0; p[1] = 0xcc; p[2] = 0x80;
                }
            } else {
                if ((x % 11 == 0 && y == 7) || (x % 17 == 0 && y == 9)) {
                    p[0] = 0x30; p[1] = 0x30; p[2] = 0xf0; /* Red meadow flowers */
                } else {
                    p[0] = 0x22; p[1] = 0x70; p[2] = 0x15; /* Lush green meadow */
                }
            }
        }
    }
    fs_write("video_thumb.bmp", (const char *)tbmp, 54 + timg_size);
}

/* ============================================================
 * MAIN GUI LOOP
 * ============================================================ */

int gui_active = 0;

void gui_enter(void)
{
    gui_active = 1;
    vga_font_backup_save(font_backup);
    vga_set_mode13h();
    set_palette();
    /* mouse_init() already called at boot in idt_init() — no need to repeat */

    init_vfs_media_assets();
    win_init_all();
    int mx = mouse_x, my = mouse_y;
    int prev_mx = -1, prev_my = -1;
    uint8_t prev_lclick = 0, prev_rclick = 0;

    int dragging_win = -1;
    int drag_off_x = 0, drag_off_y = 0;
    int shift_down = 0;

    redraw_all_frame(mx, my);
    uint32_t last_clock = pit_ticks();

    while (1)
    {
        uint8_t current_exit_flag = 0;
        uint32_t now_ticks = pit_ticks();

        /* Screensaver Idle Check */
        if (!screensaver_active && (now_ticks - last_activity_tick >= SAVER_IDLE_MS)) {
            screensaver_active = 1;
            saver_init();
            redraw_all_frame(mouse_x, mouse_y);
        }

        /* Snake Timer Step */
        if (focused_win >= 0 && windows[focused_win].app == APP_SNAKE &&
            windows[focused_win].visible && !windows[focused_win].minimized && !snake_dead) {
            if (now_ticks - snake_last_move >= SNAKE_SPEED_MS) {
                snake_step();
                snake_last_move = now_ticks;
                redraw_all_frame(mouse_x, mouse_y);
            }
        }

        /* Periodic re-draw for clock tick and toast expiry */
        if (now_ticks - last_clock >= 1000 || (toast_visible && now_ticks >= toast_expire)) {
            last_clock = now_ticks;
            redraw_all_frame(mouse_x, mouse_y);
        }

        /* 3-Second Auto Reset for Minesweeper on Loss */
        if (mine_game_over && mine_lost_ticks > 0 && now_ticks - mine_lost_ticks >= 3000) {
            minesweeper_reset();
            redraw_all_frame(mouse_x, mouse_y);
        }

        /* Audio Player Step */
        if (audio_get_state() == AUDIO_STATE_PLAYING) {
            audio_step(now_ticks);
            if (windows[12].visible && !windows[12].minimized) {
                static uint32_t last_audio_draw = 0;
                if (now_ticks - last_audio_draw >= 40) {
                    last_audio_draw = now_ticks;
                    redraw_all_frame(mouse_x, mouse_y);
                }
            }
        }

        /* Video Player Step (~25 fps) */
        if (video_get_state() == VIDEO_STATE_PLAYING) {
            static uint32_t last_video_step = 0;
            if (now_ticks - last_video_step >= 40) {
                last_video_step = now_ticks;
                video_step();
                if (windows[12].visible && !windows[12].minimized) {
                    redraw_all_frame(mouse_x, mouse_y);
                }
            }
        }

        /* Cooperative poll for mouse data if available in PS/2 controller.
         * Uses mouse_poll_hw() (not irq12_handler) to avoid sending a
         * spurious PIC EOI when called outside of an actual hardware IRQ. */
        uint8_t ps2_st = inb(0x64);
        if ((ps2_st & 0x01) && (ps2_st & 0x20)) {
            extern void mouse_poll_hw(void);
            mouse_poll_hw();
        }

        mx = mouse_x;
        my = mouse_y;
        uint8_t lclick = mouse_left_click;
        uint8_t rclick = mouse_right_click;

        /* Activity detection resets screensaver timer */
        if (mx != prev_mx || my != prev_my || lclick != prev_lclick || rclick != prev_rclick) {
            if (screensaver_active) {
                screensaver_active = 0;
                redraw_all_frame(mx, my);
            }
            last_activity_tick = now_ticks;
        }

        /* Right-Click Handling: Minesweeper Flagging OR Desktop Context Menu */
        if (rclick && !prev_rclick) {
            int win_rclick = -1;
            if (focused_win >= 0 && focused_win < win_count) {
                gui_window_t *w = &windows[focused_win];
                if (w->visible && !w->minimized &&
                    mx >= w->x && mx < w->x + w->w &&
                    my >= w->y && my < w->y + w->h) {
                    win_rclick = focused_win;
                }
            }
            if (win_rclick < 0) {
                for (int i = 0; i < win_count; i++) {
                    if (i == focused_win) continue;
                    gui_window_t *w = &windows[i];
                    if (w->visible && !w->minimized &&
                        mx >= w->x && mx < w->x + w->w &&
                        my >= w->y && my < w->y + w->h) {
                        win_rclick = i;
                        break;
                    }
                }
            }

            if (win_rclick >= 0) {
                /* Right-clicked inside an active window */
                gui_window_t *w = &windows[win_rclick];
                if (w->app == APP_MINESWEEPER) {
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c++) {
                            int gx = w->x + 12 + c * 11;
                            int gy = w->y + 34 + r * 8;
                            if (mx >= gx && mx < gx + 10 && my >= gy && my < gy + 7) {
                                if (mine_revealed[r][c] == 0) { mine_revealed[r][c] = 2; sound_mine_flag(); }
                                else if (mine_revealed[r][c] == 2) { mine_revealed[r][c] = 0; sound_click(); }
                            }
                        }
                    redraw_all_frame(mx, my);
                }
            } else if (my < TASKBAR_Y) {
                /* Right-clicked on empty desktop area */
                ctx_menu_open = 1; ctx_menu_x = mx; ctx_menu_y = my; sound_click(); redraw_all_frame(mx, my);
            }
        }

        /* Left-Click Mouse Event Processing */
        if (mx != prev_mx || my != prev_my || lclick != prev_lclick)
        {
            if (lclick && !prev_lclick)
            {
                /* Dismiss Toast on Click */
                if (toast_visible) toast_visible = 0;

                /* Context Menu Click Processing */
                if (ctx_menu_open) {
                    int mw = 100, mh = 80;
                    int cx = ctx_menu_x, cy = ctx_menu_y;
                    if (cx + mw > SCREEN_W) cx = SCREEN_W - mw;
                    if (cy + mh > TASKBAR_Y) cy = TASKBAR_Y - mh;

                    if (mx >= cx && mx <= cx + mw && my >= cy && my <= cy + mh) {
                        int citem = (my - (cy + 3)) / 12;
                        ctx_menu_open = 0;
                        if (citem == 0) { open_window(4); notepad_len = 0; notepad_buf[0] = '\0'; } /* New Note */
                        else if (citem == 1) { open_window(9); }                                     /* Terminal CLI */
                        else if (citem == 2) { open_window(8); snake_reset(); }                     /* Snake */
                        else if (citem == 3) { open_window(10); }                                    /* App Studio */
                        else if (citem == 4) { open_window(5); }                                     /* Control Panel */
                        else if (citem == 5) { current_exit_flag = 1; }                              /* Exit GUI */
                        sound_click();
                    } else {
                        ctx_menu_open = 0;
                    }
                    if (current_exit_flag) break;
                }

                /* Start Button Click */
                if (my >= TASKBAR_Y && mx >= 2 && mx <= 56) {
                    start_menu_open = !start_menu_open;
                    sound_click();
                }
                /* ProcessBar Window Tab Click */
                else if (my >= TASKBAR_Y && mx >= 58 && mx < 263) {
                    int open_cnt = 0;
                    for (int i = 0; i < win_count; i++) if (windows[i].visible) open_cnt++;

                    if (open_cnt > 0) {
                        int btn_w = 205 / open_cnt;
                        if (btn_w > 75) btn_w = 75;
                        int idx = (mx - 58) / btn_w;

                        int cur = 0;
                        for (int i = 0; i < win_count; i++) {
                            if (windows[i].visible) {
                                if (cur == idx) {
                                    if (focused_win == i && !windows[i].minimized) {
                                        windows[i].minimized = 1;
                                        sound_win_close();
                                    } else {
                                        if (focused_win >= 0 && focused_win < win_count)
                                            windows[focused_win].focused = 0;
                                        focused_win = i;
                                        windows[i].focused = 1;
                                        windows[i].minimized = 0;
                                        sound_click();
                                    }
                                    break;
                                }
                                cur++;
                            }
                        }
                    }
                }
                /* Start Menu Item Click */
                else if (start_menu_open && mx >= 2 && mx <= 117 && my >= 26 && my < 26 + 160) {
                    int item = (my - 26) / 12;
                    start_menu_open = 0;
                    if (item >= 0 && item < 13) {
                        static const int menu_to_win[13] = {
                            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13
                        };
                        int win_idx = menu_to_win[item];
                        open_window(win_idx);
                        if (item == 2) minesweeper_reset();
                        if (item == 8) snake_reset();
                    }
                }
                else
                {
                    start_menu_open = 0;

                    /* Check Window Clicks — search focused window first, then z-order */
                    int clicked = -1;
                    /* First check focused window for priority */
                    if (focused_win >= 0 && focused_win < win_count) {
                        gui_window_t *w = &windows[focused_win];
                        if (w->visible && !w->minimized &&
                            mx >= w->x && mx < w->x + w->w &&
                            my >= w->y && my < w->y + w->h) {
                            clicked = focused_win;
                        }
                    }
                    /* Then check other windows front-to-back */
                    if (clicked < 0) {
                        for (int i = 0; i < win_count; i++) {
                            if (i == focused_win) continue;
                            gui_window_t *w = &windows[i];
                            if (w->visible && !w->minimized &&
                                mx >= w->x && mx < w->x + w->w &&
                                my >= w->y && my < w->y + w->h) {
                                clicked = i; break;
                            }
                        }
                    }

                    if (clicked < 0) {
                        int icon_clicked = -1;
                        int slot = 0;
                        for (int i = 0; i < win_count; i++) {
                            int ix, iy;
                            get_desktop_slot_pos(slot++, &ix, &iy);
                            if (mx >= ix - 2 && mx <= ix + 42 && my >= iy - 2 && my <= iy + 28) {
                                icon_clicked = i;
                                break;
                            }
                        }

                        int file_clicked = -1;
                        fs_node_t *root = fs_root();
                        if (icon_clicked < 0 && root) {
                            for (int i = 0; i < root->child_count && slot < 18; i++) {
                                fs_node_t *f = root->children[i];
                                if (!f || f->type != FS_FILE) continue;
                                if (f->name[0] == '.' || str_cmp(f->name, "tmp_pipe") == 0) continue;
                                int ix, iy;
                                get_desktop_slot_pos(slot++, &ix, &iy);
                                if (mx >= ix - 2 && mx <= ix + 42 && my >= iy - 2 && my <= iy + 28) {
                                    file_clicked = i;
                                    break;
                                }
                            }
                        }

                        if (icon_clicked >= 0) {
                            selected_desktop_file = -1;
                            if (selected_desktop_icon == icon_clicked) {
                                open_window(icon_clicked);
                                if (icon_clicked == 2) minesweeper_reset();
                                if (icon_clicked == 8) snake_reset();
                            } else {
                                selected_desktop_icon = icon_clicked;
                                sound_click();
                            }
                        } else if (file_clicked >= 0 && root) {
                            selected_desktop_icon = -1;
                            if (selected_desktop_file == file_clicked) {
                                open_associated_file(root->children[file_clicked]);
                                selected_desktop_file = -1;
                            } else {
                                selected_desktop_file = file_clicked;
                                sound_click();
                            }
                        } else {
                            selected_desktop_icon = -1;
                            selected_desktop_file = -1;
                        }
                    }

                    if (clicked >= 0) {
                        if (focused_win >= 0 && focused_win < win_count)
                            windows[focused_win].focused = 0;
                        focused_win = clicked;
                        windows[focused_win].focused = 1;

                        gui_window_t *w = &windows[clicked];

                        /* Close button X */
                        if (mx >= w->x + w->w - 8 && mx <= w->x + w->w - 1 && my >= w->y + 2 && my <= w->y + 10) {
                            w->visible = 0;
                            sound_win_close();
                        }
                        /* Maximize button = */
                        else if (mx >= w->x + w->w - 16 && mx <= w->x + w->w - 9 && my >= w->y + 2 && my <= w->y + 10) {
                            sound_click();
                            if (!w->maximized) {
                                w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                                w->x = 0; w->y = 0; w->w = SCREEN_W; w->h = TASKBAR_Y;
                                w->maximized = 1; w->split_state = 0;
                            } else {
                                w->x = w->saved_x; w->y = w->saved_y; w->w = w->saved_w; w->h = w->saved_h;
                                w->maximized = 0;
                            }
                        }
                        /* Split Screen button | */
                        else if (mx >= w->x + w->w - 24 && mx <= w->x + w->w - 17 && my >= w->y + 2 && my <= w->y + 10) {
                            sound_click();
                            if (w->split_state == 0) {
                                w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                                w->x = 0; w->y = 0; w->w = 160; w->h = TASKBAR_Y;
                                w->split_state = 1; w->maximized = 0;
                            } else if (w->split_state == 1) {
                                w->x = 160; w->y = 0; w->w = 160; w->h = TASKBAR_Y;
                                w->split_state = 2;
                            } else {
                                w->x = w->saved_x; w->y = w->saved_y; w->w = w->saved_w; w->h = w->saved_h;
                                w->split_state = 0;
                            }
                        }
                        /* Minimize button _ */
                        else if (mx >= w->x + w->w - 32 && mx <= w->x + w->w - 25 && my >= w->y + 2 && my <= w->y + 10) {
                            w->minimized = 1;
                            sound_win_close();
                        }
                        /* Title bar drag and double-click maximize */
                        else if (my >= w->y && my < w->y + 12) {
                            static uint32_t last_title_click = 0;
                            static int last_title_win = -1;
                            uint32_t now_clk = pit_ticks();
                            if (now_clk - last_title_click < 350 && last_title_win == clicked) {
                                sound_click();
                                if (!w->maximized) {
                                    w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                                    w->x = 0; w->y = 0; w->w = SCREEN_W; w->h = TASKBAR_Y;
                                    w->maximized = 1; w->split_state = 0;
                                    show_toast("Maximized", TOAST_INFO);
                                } else {
                                    w->x = w->saved_x; w->y = w->saved_y; w->w = w->saved_w; w->h = w->saved_h;
                                    w->maximized = 0;
                                    show_toast("Restored", TOAST_INFO);
                                }
                                last_title_click = 0;
                                last_title_win = -1;
                            } else {
                                last_title_click = now_clk;
                                last_title_win = clicked;
                                if (!w->maximized) {
                                    dragging_win = clicked;
                                    drag_off_x = mx - w->x;
                                    drag_off_y = my - w->y;
                                }
                            }
                        }
                        /* ====================================================
                         * APP INTERACTIONS
                         * ==================================================== */
                        else if (w->app == APP_CALC) {
                            for (int r = 0; r < 4; r++)
                                for (int c = 0; c < 4; c++) {
                                    int bx = w->x + 10 + c * 23;
                                    int by = w->y + 34 + r * 18;
                                    if (mx >= bx && mx < bx + 20 && my >= by && my < by + 15) {
                                        static const char bch[4][4] = {{'7','8','9','/'},{'4','5','6','*'},{'1','2','3','-'},{'0','C','=','+'}};
                                        char ch = bch[r][c];
                                        sound_calc();
                                        if (ch >= '0' && ch <= '9') {
                                            calc_val = calc_val * 10 + (ch - '0');
                                            itoa(calc_val, calc_display, 10);
                                        } else if (ch == 'C') {
                                            calc_val = 0; calc_acc = 0; calc_op = 0;
                                            calc_display[0] = '0'; calc_display[1] = '\0';
                                        } else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
                                            calc_acc = calc_val; calc_val = 0; calc_op = ch;
                                        } else if (ch == '=') {
                                            if (calc_op == '+') calc_acc += calc_val;
                                            else if (calc_op == '-') calc_acc -= calc_val;
                                            else if (calc_op == '*') calc_acc *= calc_val;
                                            else if (calc_op == '/' && calc_val != 0) calc_acc /= calc_val;
                                            else if (calc_op == '/' && calc_val == 0) {
                                                show_toast("Division by zero!", TOAST_ERR);
                                                sound_error();
                                                calc_val = 0; calc_acc = 0; calc_op = 0;
                                                calc_display[0] = '0'; calc_display[1] = '\0';
                                            }
                                            calc_val = calc_acc;
                                            itoa(calc_acc, calc_display, 10);
                                        }
                                    }
                                }
                        }
                        else if (w->app == APP_PAINTER) {
                            int tb_y = w->y + 13;
                            /* Clear */
                            if (mx >= w->x + 4 && mx <= w->x + 28 && my >= tb_y && my <= tb_y + 11) {
                                for (int i=0; i<PAINT_CW*PAINT_CH; i++) paint_canvas[i] = COL_WHITE;
                                show_toast("Canvas cleared.", TOAST_INFO);
                                sound_clear();
                            }
                            /* Save */
                            else if (mx >= w->x + 30 && mx <= w->x + 54 && my >= tb_y && my <= tb_y + 11) {
                                fs_write(paint_filename, (const char*)paint_canvas, PAINT_CW * PAINT_CH);
                                show_toast("Saved!", TOAST_OK);
                                sound_save();
                            }
                            /* Open */
                            else if (mx >= w->x + 56 && mx <= w->x + 80 && my >= tb_y && my <= tb_y + 11) {
                                fs_node_t *f = fs_resolve(paint_filename);
                                if (f && f->data) {
                                    size_t sz = f->size < (size_t)(PAINT_CW * PAINT_CH) ? f->size : (size_t)(PAINT_CW * PAINT_CH);
                                    for (size_t i = 0; i < sz; i++) paint_canvas[i] = (uint8_t)f->data[i];
                                    show_toast("Opened!", TOAST_OK);
                                    sound_open();
                                } else {
                                    show_toast("File not found!", TOAST_ERR);
                                    sound_error();
                                }
                            }
                            /* Pencil */
                            else if (mx >= w->x + 85 && mx <= w->x + 109 && my >= tb_y && my <= tb_y + 11) {
                                paint_brush_size = 1; paint_eraser = 0; paint_fill = 0; sound_click();
                            }
                            /* Brush */
                            else if (mx >= w->x + 111 && mx <= w->x + 135 && my >= tb_y && my <= tb_y + 11) {
                                paint_brush_size = 3; paint_eraser = 0; paint_fill = 0; sound_click();
                            }
                            /* Eraser */
                            else if (mx >= w->x + 137 && mx <= w->x + 161 && my >= tb_y && my <= tb_y + 11) {
                                paint_eraser = 1; paint_fill = 0; sound_click();
                            }
                            /* Fill */
                            else if (mx >= w->x + 163 && mx <= w->x + 187 && my >= tb_y && my <= tb_y + 11) {
                                paint_fill = 1; paint_eraser = 0; sound_click();
                            }
                            /* Color Palette */
                            else {
                                int pal_y = w->y + w->h - 12;
                                if (my >= pal_y && my < pal_y + 9) {
                                    int swatch_w = (w->w - 8) / PAINT_PALETTE_COUNT;
                                    if (swatch_w < 4) swatch_w = 4;
                                    int pidx = (mx - (w->x + 4)) / swatch_w;
                                    if (pidx >= 0 && pidx < PAINT_PALETTE_COUNT) {
                                        paint_color = PAINT_PALETTE[pidx];
                                        paint_eraser = 0;
                                        sound_color_pick();
                                    }
                                    prev_paint_x = -1; prev_paint_y = -1;
                                }
                            }
                        }
                        else if (w->app == APP_MINESWEEPER) {
                            if (!mine_game_over && !mine_win) {
                                for (int r = 0; r < 8; r++)
                                    for (int c = 0; c < 8; c++) {
                                        int gx = w->x + 12 + c * 11;
                                        int gy = w->y + 34 + r * 8;
                                        if (mx >= gx && mx < gx + 10 && my >= gy && my < gy + 7) {
                                            if (mine_revealed[r][c] == 0) {
                                                mine_revealed[r][c] = 1;
                                                if (mine_grid[r][c] == 9) {
                                                    mine_game_over = 1;
                                                    mine_lost_ticks = pit_ticks();
                                                    sound_lose();
                                                } else {
                                                    sound_click();
                                                    minesweeper_check_win();
                                                }
                                            }
                                        }
                                    }
                            } else {
                                /* Click smiley face area to reset */
                                int face_x = w->x + (w->w/2) - 6;
                                if (mx >= face_x && mx < face_x + 20 && my >= w->y + 18 && my < w->y + 30) {
                                    minesweeper_reset();
                                    sound_win_open();
                                }
                            }
                        }
                        else if (w->app == APP_FILEMAN) {
                            int tby = w->y + 15;
                            /* Up [..] button */
                            if (mx >= w->x + 8 && mx <= w->x + 24 && my >= tby && my <= tby + 11) {
                                if (fileman_cur_dir && fileman_cur_dir->parent)
                                    fileman_cur_dir = fileman_cur_dir->parent;
                                fileman_selected = -1;
                                sound_click();
                            }
                            /* [+File] create new file */
                            else if (mx >= w->x + 26 && mx <= w->x + 60 && my >= tby && my <= tby + 11) {
                                char fname[32];
                                str_cpy(fname, "file", 32);
                                char numbuf[8]; itoa(fileman_new_cnt++, numbuf, 10);
                                int i=4, j=0; while(numbuf[j]) fname[i++] = numbuf[j++];
                                fname[i++] = '.'; fname[i++] = 't'; fname[i++] = 'x'; fname[i++] = 't'; fname[i] = '\0';
                                if (fs_touch(fname)) {
                                    show_toast("Created new file!", TOAST_OK);
                                    sound_save();
                                } else {
                                    show_toast("File limit reached!", TOAST_ERR);
                                    sound_error();
                                }
                            }
                            /* [+Dir] create new folder */
                            else if (mx >= w->x + 62 && mx <= w->x + 90 && my >= tby && my <= tby + 11) {
                                char dname[32];
                                str_cpy(dname, "folder", 32);
                                char numbuf[8]; itoa(fileman_new_cnt++, numbuf, 10);
                                int i=6, j=0; while(numbuf[j]) dname[i++] = numbuf[j++];
                                dname[i] = '\0';
                                if (fs_mkdir(dname)) {
                                    show_toast("Created folder!", TOAST_OK);
                                    sound_save();
                                } else {
                                    show_toast("Folder limit reached!", TOAST_ERR);
                                    sound_error();
                                }
                            }
                            /* [Edit] open selected file in Notepad */
                            else if (mx >= w->x + 92 && mx <= w->x + 118 && my >= tby && my <= tby + 11) {
                                fs_node_t *dir = fileman_cur_dir ? fileman_cur_dir : fs_root();
                                if (dir && fileman_selected >= 0 && fileman_selected < dir->child_count) {
                                    fs_node_t *c = dir->children[fileman_selected];
                                    if (c->type == FS_FILE) open_associated_file(c);
                                    else { show_toast("Cannot edit folder!", TOAST_ERR); sound_error(); }
                                } else { show_toast("Select a file first!", TOAST_INFO); sound_click(); }
                            }
                            /* [Props] show file/folder properties */
                            else if (mx >= w->x + 120 && mx <= w->x + 150 && my >= tby && my <= tby + 11) {
                                fs_node_t *dir = fileman_cur_dir ? fileman_cur_dir : fs_root();
                                if (dir && fileman_selected >= 0 && fileman_selected < dir->child_count) {
                                    fs_node_t *c = dir->children[fileman_selected];
                                    char pbuf[64];
                                    str_cpy(pbuf, c->type == FS_DIR ? "[DIR] " : "[FILE] ", 64);
                                    int pi = str_len(pbuf);
                                    str_cpy(pbuf + pi, c->name, 64 - pi);
                                    pi = str_len(pbuf);
                                    str_cpy(pbuf + pi, " (", 64 - pi);
                                    pi = str_len(pbuf);
                                    char sbuf[16]; itoa((int)c->size, sbuf, 10);
                                    str_cpy(pbuf + pi, sbuf, 64 - pi);
                                    pi = str_len(pbuf);
                                    str_cpy(pbuf + pi, " B)", 64 - pi);
                                    show_toast(pbuf, TOAST_INFO);
                                    sound_click();
                                } else { show_toast("Select an item first!", TOAST_INFO); sound_click(); }
                            }
                            /* Directory Listing Item Clicks */
                            else if (my >= w->y + 29 && my <= w->y + w->h - 8) {
                                int item_idx = (my - (w->y + 29)) / 9;
                                fs_node_t *dir = fileman_cur_dir ? fileman_cur_dir : fs_root();
                                if (dir && item_idx >= 0 && item_idx < dir->child_count) {
                                    if (fileman_selected == item_idx) {
                                        /* Double click / already selected: open */
                                        fs_node_t *c = dir->children[item_idx];
                                        fileman_selected = -1;
                                        if (c->type == FS_DIR) { fileman_cur_dir = c; sound_click(); }
                                        else { open_associated_file(c); }
                                    } else {
                                        fileman_selected = item_idx;
                                        sound_click();
                                    }
                                } else {
                                    fileman_selected = -1;
                                }
                            }
                        }
                        else if (w->app == APP_NOTEPAD) {
                            /* Clear button */
                            if (mx >= w->x + 6 && mx <= w->x + 34 && my >= w->y + 16 && my <= w->y + 28) {
                                notepad_len = 0;
                                notepad_buf[0] = '\0';
                                sound_clear();
                                show_toast("Cleared.", TOAST_INFO);
                            }
                            /* Save button */
                            else if (mx >= w->x + 36 && mx <= w->x + 62 && my >= w->y + 16 && my <= w->y + 28) {
                                fs_write(notepad_filename, notepad_buf, (size_t)notepad_len);
                                show_toast("Saved!", TOAST_OK);
                                sound_save();
                            }
                            /* Open button */
                            else if (mx >= w->x + 64 && mx <= w->x + 90 && my >= w->y + 16 && my <= w->y + 28) {
                                fs_node_t *f = fs_resolve(notepad_filename);
                                if (f && f->data) {
                                    notepad_len = 0;
                                    for (size_t i = 0; i < f->size && i < 500; i++) notepad_buf[i] = f->data[i];
                                    notepad_len = (int)(f->size < 500 ? f->size : 500);
                                    notepad_buf[notepad_len] = '\0';
                                    show_toast("Opened!", TOAST_OK);
                                    sound_open();
                                } else {
                                    show_toast("File not found!", TOAST_ERR);
                                    sound_error();
                                }
                            }
                        }
                        else if (w->app == APP_CPANEL) {
                            if (my >= w->y + 44 && my <= w->y + 76) {
                                int tidx = (mx - (w->x + 10)) / 68 + ((my - (w->y + 44)) / 16) * 2;
                                if (tidx == 0) gui_desktop_color = RGB(0,3,3); /* Teal */
                                else if (tidx == 1) gui_desktop_color = RGB(0,1,4); /* Win31 Navy */
                                else if (tidx == 2) gui_desktop_color = RGB(0,3,0); /* Matrix Green */
                                else if (tidx == 3) gui_desktop_color = RGB(5,3,0); /* Amber Gold */
                                save_archaos_conf();
                                sound_click();
                            }
                            else if (my >= w->y + 90 && my <= w->y + 122) {
                                int widx = (mx - (w->x + 10)) / 68 + ((my - (w->y + 90)) / 16) * 2;
                                if (widx >= 0 && widx < 4) {
                                    gui_wallpaper_type = widx;
                                    save_archaos_conf();
                                    sound_click();
                                }
                            }
                        }
                        else if (w->app == APP_TASKMAN) {
                            int py = w->y + 77;
                            for (int i = 0; i < win_count; i++) {
                                if (py + 10 > w->y + w->h - 5) break;
                                if (windows[i].visible) {
                                    if (mx >= w->x + w->w - 32 && mx <= w->x + w->w - 4 &&
                                        my >= py && my <= py + 10) {
                                        if (i != focused_win) {
                                            windows[i].visible = 0;
                                            sound_win_close();
                                        } else {
                                            /* Can't kill Task Manager itself */
                                            show_toast("Can't kill Task Manager!", TOAST_ERR);
                                            sound_error();
                                        }
                                    }
                                    py += 11;
                                }
                            }
                        }
                        else if (w->app == APP_IMGVIEW) {
                            int tb_y = w->y + 13;
                            /* Sav */
                            if (mx >= w->x + 4 && mx <= w->x + 32 && my >= tb_y && my <= tb_y + 11) {
                                /* Save currently viewed image (canvas) */
                                fs_write(imgview_filename, (const char*)paint_canvas, PAINT_CW * PAINT_CH);
                                show_toast("Image saved!", TOAST_OK);
                                sound_save();
                            }
                            /* Opn */
                            else if (mx >= w->x + 34 && mx <= w->x + 62 && my >= tb_y && my <= tb_y + 11) {
                                fs_node_t *f = fs_resolve(imgview_filename);
                                if (f && f->data) {
                                    size_t sz = f->size < (size_t)(PAINT_CW * PAINT_CH) ? f->size : (size_t)(PAINT_CW * PAINT_CH);
                                    for (size_t i = 0; i < sz; i++) paint_canvas[i] = (uint8_t)f->data[i];
                                    show_toast("Image opened!", TOAST_OK);
                                    sound_open();
                                } else {
                                    show_toast("File not found!", TOAST_ERR);
                                    sound_error();
                                }
                            }
                            /* Rst (Reset / show demo landscape) */
                            else if (mx >= w->x + 64 && mx <= w->x + 92 && my >= tb_y && my <= tb_y + 11) {
                                for (int i=0; i<PAINT_CW*PAINT_CH; i++) paint_canvas[i] = COL_WHITE;
                                show_toast("View reset.", TOAST_INFO);
                                sound_clear();
                            }
                        }
                        else if (w->app == APP_SNAKE) {
                            if (snake_dead) {
                                snake_reset();
                                sound_win_open();
                            }
                        }
                        else if (w->app == APP_CODESTUDIO) {
                            int tb_y = w->y + 13;
                            /* [Run] */
                            if (mx >= w->x + 4 && mx <= w->x + 24 && my >= tb_y && my <= tb_y + 11) {
                                codestudio_run();
                            }
                            /* [Clr] */
                            else if (mx >= w->x + 26 && mx <= w->x + 46 && my >= tb_y && my <= tb_y + 11) {
                                code_line_cnt = 1;
                                code_lines[0][0] = '\0';
                                code_cursor_line = 0; code_cursor_col = 0; code_scroll_offset = 0;
                                str_cpy(code_output, "Cleared", 64);
                                sound_clear();
                            }
                            /* [Cpy] */
                            else if (mx >= w->x + 48 && mx <= w->x + 68 && my >= tb_y && my <= tb_y + 11) {
                                codestudio_copy();
                                sound_click();
                            }
                            /* [Pst] */
                            else if (mx >= w->x + 70 && mx <= w->x + 90 && my >= tb_y && my <= tb_y + 11) {
                                codestudio_paste();
                                sound_click();
                            }
                            /* [Tab] */
                            else if (mx >= w->x + 92 && mx <= w->x + 112 && my >= tb_y && my <= tb_y + 11) {
                                char *lptr = code_lines[code_cursor_line];
                                int llen = str_len(lptr);
                                if (llen + 4 < CODE_LINE_MAX_LEN - 1) {
                                    for (int k = llen; k >= code_cursor_col; k--) lptr[k + 4] = lptr[k];
                                    for (int k = 0; k < 4; k++) lptr[code_cursor_col++] = ' ';
                                    sound_click();
                                }
                            }
                            /* [Demo] */
                            else if (mx >= w->x + 114 && mx <= w->x + 140 && my >= tb_y && my <= tb_y + 11) {
                                codestudio_load_demo();
                                sound_open();
                            }
                            /* [Lang] */
                            else if (mx >= w->x + 142 && mx <= w->x + 164 && my >= tb_y && my <= tb_y + 11) {
                                code_lang = (code_lang + 1) % 3;
                                codestudio_load_demo();
                                sound_click();
                            }
                            /* [Docs] */
                            else if (mx >= w->x + 166 && mx <= w->x + 192 && my >= tb_y && my <= tb_y + 11) {
                                code_show_docs = !code_show_docs;
                                sound_click();
                            }
                        }
                        else if (w->app == APP_BROWSER) {
                            int tab_bar_y = w->y + 13;
                            int tab_area_w = w->w - 24;
                            int tab_w = tab_area_w / browser_tab_count;
                            if (tab_w > 90) tab_w = 90;
                            if (tab_w < 45) tab_w = 45;
                            int plus_x = w->x + 4 + browser_tab_count * (tab_w + 2);

                            /* Row 1: Firefox Tab Bar Clicks */
                            if (my >= tab_bar_y && my <= tab_bar_y + 12) {
                                if (mx >= plus_x && mx <= plus_x + 12) {
                                    browser_new_tab("about:home");
                                } else {
                                    for (int i = 0; i < browser_tab_count; i++) {
                                        int tx = w->x + 4 + i * (tab_w + 2);
                                        if (mx >= tx && mx <= tx + tab_w) {
                                            if (mx >= tx + tab_w - 10) {
                                                browser_close_tab(i);
                                            } else {
                                                browser_switch_tab(i);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                            /* Row 2: Navigation & Pill Address Bar Clicks */
                            else if (my >= w->y + 25 && my <= w->y + 38) {
                                int url_w = w->w - 84;
                                int go_x = w->x + 46 + url_w + 3;

                                /* [ < ] Back */
                                if (mx >= w->x + 4 && mx <= w->x + 16) {
                                    if (browser_hist_pos > 0) {
                                        browser_pop_history_back();
                                        sound_click();
                                    }
                                }
                                /* [ > ] Forward */
                                else if (mx >= w->x + 18 && mx <= w->x + 30) {
                                    if (browser_hist_pos < browser_hist_count - 1 && browser_hist_count > 0) {
                                        browser_pop_history_fwd();
                                        sound_click();
                                    }
                                }
                                /* [ ⟳ ] Reload */
                                else if (mx >= w->x + 32 && mx <= w->x + 44) {
                                    browser_fetch_internal(browser_url, 0);
                                    sound_click();
                                }
                                /* Address Bar */
                                else if (mx >= w->x + 46 && mx <= w->x + 46 + url_w) {
                                    browser_url_focus = 1;
                                    browser_nav_focus = BROWSER_FOCUS_URL;
                                    browser_input_focus = -1;
                                    browser_url[0] = '\0';
                                    browser_url_cursor = 0;
                                    browser_url_scroll = 0;
                                    sound_click();
                                    redraw_all_frame(mx, my);
                                }
                                /* [ Go ] Button */
                                else if (mx >= go_x && mx <= go_x + 26) {
                                    browser_url_focus = 0;
                                    browser_input_focus = -1;
                                    browser_fetch(browser_url);
                                    sound_open();
                                }
                            }
                            /* Row 3: Bookmarks Bar */
                            else if (my >= w->y + 39 && my <= w->y + 49) {
                                if (mx >= w->x + 4 && mx <= w->x + 38) {
                                    browser_fetch("about:home");
                                    sound_click();
                                } else if (mx >= w->x + 44 && mx <= w->x + 114) {
                                    browser_fetch("https://lite.duckduckgo.com/lite/");
                                    sound_click();
                                } else if (mx >= w->x + 118 && mx <= w->x + 182) {
                                    browser_fetch("https://en.wikipedia.org/wiki/Main_Page");
                                    sound_click();
                                } else if (mx >= w->x + 186 && mx <= w->x + 232) {
                                    browser_fetch("about:media");
                                    sound_click();
                                }
                            }
                            /* Row 4.5: Docked In-Browser Media Bar Clicks */
                            else if (browser_media_active && my >= w->y + w->h - 26 && my < w->y + w->h - 12) {
                                int mb_x = w->x + 2;
                                int mb_w = w->w - 4;
                                if (mx >= mb_x + 36 && mx <= mb_x + 48) {
                                    /* Play / Pause */
                                    if (browser_media_is_video) video_toggle_play();
                                    else audio_toggle_play();
                                    sound_click();
                                    redraw_all_frame(mx, my);
                                } else if (mx >= mb_x + 50 && mx <= mb_x + 62) {
                                    /* Stop */
                                    if (browser_media_is_video) video_stop();
                                    else audio_stop();
                                    sound_click();
                                    redraw_all_frame(mx, my);
                                } else if (mx >= mb_x + mb_w - 28 && mx <= mb_x + mb_w - 16) {
                                    /* Fullscreen Toggle [FS] */
                                    sound_click();
                                    if (!w->maximized) {
                                        w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                                        w->x = 0; w->y = 0; w->w = SCREEN_W; w->h = TASKBAR_Y;
                                        w->maximized = 1;
                                        show_toast("Fullscreen Mode (F11)", TOAST_INFO);
                                    } else {
                                        w->x = w->saved_x; w->y = w->saved_y; w->w = w->saved_w; w->h = w->saved_h;
                                        w->maximized = 0;
                                        show_toast("Window Restored", TOAST_INFO);
                                    }
                                    redraw_all_frame(mx, my);
                                } else if (mx >= mb_x + mb_w - 14 && mx <= mb_x + mb_w - 3) {
                                    /* Close Media Bar */
                                    if (browser_media_is_video) video_stop();
                                    else audio_stop();
                                    browser_media_active = false;
                                    sound_click();
                                    redraw_all_frame(mx, my);
                                } else if (mx >= mb_x + 10 && mx <= mb_x + mb_w - 10) {
                                    /* Seek */
                                    int sw = mb_w - 20;
                                    int pct = (mx - (mb_x + 10)) * 100 / sw;
                                    if (pct < 0) pct = 0;
                                    if (pct > 100) pct = 100;
                                    if (browser_media_is_video) video_seek_percent(pct);
                                    else audio_seek_percent(pct);
                                    sound_click();
                                    redraw_all_frame(mx, my);
                                }
                            }
                            /* Row 4: Web Viewport Clicks: NetSurf Interactive Elements */
                            else if (my >= w->y + 50 && my <= w->y + w->h - (browser_media_active ? 28 : 14)) {
                                int clicked_idx = -1;
                                /* Pass 1: Prioritize inputs and buttons */
                                for (int k = 0; k < ns_element_count; k++) {
                                    if (ns_elements[k].is_input || ns_elements[k].is_button) {
                                        int elem_x = w->x + 2 + ns_elements[k].x;
                                        int elem_y = w->y + 50 + ns_elements[k].y - browser_scroll_y;
                                        if (mx >= elem_x && mx <= elem_x + ns_elements[k].w &&
                                            my >= elem_y && my <= elem_y + ns_elements[k].h) {
                                            clicked_idx = k;
                                            break;
                                        }
                                    }
                                }
                                /* Pass 2: Fall back to links, video, images */
                                if (clicked_idx < 0) {
                                    for (int k = 0; k < ns_element_count; k++) {
                                        int elem_x = w->x + 2 + ns_elements[k].x;
                                        int elem_y = w->y + 50 + ns_elements[k].y - browser_scroll_y;
                                        if (mx >= elem_x && mx <= elem_x + ns_elements[k].w &&
                                            my >= elem_y && my <= elem_y + ns_elements[k].h) {
                                            clicked_idx = k;
                                            break;
                                        }
                                    }
                                }

                                if (clicked_idx >= 0) {
                                    int k = clicked_idx;
                                    browser_nav_focus = k;
                                    serial_printf(COM1_BASE, "[BROWSER_CLICK] hit elem %d at mx=%d my=%d (is_vid=%d is_aud=%d text='%s' src='%s')\n", k, mx, my, ns_elements[k].is_video, ns_elements[k].is_audio, ns_elements[k].text, ns_elements[k].src);

                                    if (ns_elements[k].is_input) {
                                        browser_input_focus = k;
                                        browser_url_focus = 0;
                                        sound_click();
                                        redraw_all_frame(mx, my);
                                    } else if (ns_elements[k].onclick[0]) {
                                        js_engine_exec_event(ns_elements[k].onclick);
                                        sound_click();
                                        redraw_all_frame(mx, my);
                                    } else if (ns_elements[k].is_button) {
                                        int inp_idx = -1;
                                        for (int j = 0; j < ns_element_count; j++) {
                                            if (ns_elements[j].is_input) {
                                                inp_idx = j;
                                                break;
                                            }
                                        }
                                        if (ns_elements[k].form_action[0]) {
                                            char query_val[128] = "";
                                            const char *param_name = "q";
                                            if (inp_idx >= 0) {
                                                strncpy(query_val, ns_elements[inp_idx].text, sizeof(query_val) - 1);
                                                if (ns_elements[inp_idx].name[0]) param_name = ns_elements[inp_idx].name;
                                            }
                                            char act_url[256];
                                            html_resolve_url(browser_url, ns_elements[k].form_action, act_url, sizeof(act_url));
                                            char enc_q[192];
                                            url_encode(query_val, enc_q, sizeof(enc_q));
                                            char form_url[256];
                                            snprintf(form_url, sizeof(form_url), "%s%s%s=%s", act_url, strchr(act_url, '?') ? "&" : "?", param_name, enc_q);
                                            browser_fetch(form_url);
                                            sound_open();
                                        } else {
                                            int is_search_btn = (strstr(ns_elements[k].text, "Search") != NULL ||
                                                                 strstr(ns_elements[k].text, "search") != NULL ||
                                                                 strstr(ns_elements[k].text, "Go") != NULL ||
                                                                 strstr(ns_elements[k].text, "Submit") != NULL ||
                                                                 strstr(ns_elements[k].text, "Find") != NULL);
                                            if (is_search_btn && inp_idx >= 0 && ns_elements[inp_idx].text[0]) {
                                                browser_submit_search(ns_elements[inp_idx].text);
                                                sound_open();
                                            } else if (is_search_btn) {
                                                browser_nav_focus = (inp_idx >= 0) ? inp_idx : BROWSER_FOCUS_URL;
                                                str_cpy(browser_status, "Please enter a search query", sizeof(browser_status));
                                                sound_click();
                                                redraw_all_frame(mx, my);
                                            } else {
                                                /* Generic non-search button: do not trigger ghost web search */
                                                sound_click();
                                                redraw_all_frame(mx, my);
                                            }
                                        }
                                    } else if (ns_elements[k].is_image) {
                                        decoded_image_t *existing = image_cache_get_or_load(ns_elements[k].src, NULL, 0);
                                        if (!existing) {
                                            snprintf(browser_status, sizeof(browser_status), "Loading image: %s...", ns_elements[k].text);
                                            sound_click();
                                            redraw_all_frame(mx, my);

                                            decoded_image_t *loaded = image_fetch_and_cache(ns_elements[k].src, browser_url);
                                            if (loaded && loaded->pixels) {
                                                snprintf(browser_status, sizeof(browser_status), "Image loaded (%dx%d)", loaded->width, loaded->height);
                                                sound_tone(523, 20);
                                            } else {
                                                snprintf(browser_status, sizeof(browser_status), "Failed to load image");
                                            }
                                            redraw_all_frame(mx, my);
                                        } else if (ns_elements[k].is_link && ns_elements[k].href[0]) {
                                            char target_url[256];
                                            html_resolve_url(browser_url, ns_elements[k].href, target_url, sizeof(target_url));
                                            browser_fetch(target_url);
                                            sound_open();
                                        } else {
                                            snprintf(browser_status, sizeof(browser_status), "Image: %dx%d px", existing->width, existing->height);
                                            sound_click();
                                            redraw_all_frame(mx, my);
                                        }
                                    } else if (ns_elements[k].is_video) {
                                        char v_src[256];
                                        html_resolve_url(browser_url, ns_elements[k].src, v_src, sizeof(v_src));
                                        char resolved[256];
                                        media_resolve_url(v_src, resolved, sizeof(resolved));
                                        browser_play_in_browser_media(resolved, 1);
                                        ns_elements[k].is_playing = (video_get_state() == VIDEO_STATE_PLAYING);
                                        sound_click();
                                        redraw_all_frame(mx, my);
                                    } else if (ns_elements[k].is_audio) {
                                        char a_src[256];
                                        html_resolve_url(browser_url, ns_elements[k].src, a_src, sizeof(a_src));
                                        char resolved[256];
                                        media_resolve_url(a_src, resolved, sizeof(resolved));
                                        browser_play_in_browser_media(resolved, 0);
                                        ns_elements[k].is_playing = (audio_get_state() == AUDIO_STATE_PLAYING);
                                        sound_click();
                                        redraw_all_frame(mx, my);
                                    } else if (ns_elements[k].is_link && ns_elements[k].href[0]) {
                                        if (str_ncmp(ns_elements[k].href, "javascript:", 11) == 0) {
                                            js_engine_exec_event(ns_elements[k].href + 11);
                                            sound_click();
                                            redraw_all_frame(mx, my);
                                        } else {
                                            char target_url[256];
                                            html_resolve_url(browser_url, ns_elements[k].href, target_url, sizeof(target_url));
                                            if (browser_is_audio_media(target_url, ns_elements[k].text)) {
                                                char resolved[256];
                                                media_resolve_url(target_url, resolved, sizeof(resolved));
                                                browser_play_in_browser_media(resolved, 0);
                                                sound_win();
                                                redraw_all_frame(mx, my);
                                            } else if (browser_is_video_media(target_url, ns_elements[k].text)) {
                                                char resolved[256];
                                                media_resolve_url(target_url, resolved, sizeof(resolved));
                                                browser_play_in_browser_media(resolved, 1);
                                                sound_win();
                                                redraw_all_frame(mx, my);
                                            } else {
                                                browser_fetch(target_url);
                                                sound_open();
                                            }
                                        }
                                    }
                                } else {
                                    int page_x = mx - (w->x + 2);
                                    int page_y = my - (w->y + 50) + browser_scroll_y;
                                    char nav_url[256] = "";
                                    int fb = ns_engine_click_fallback(page_x, page_y, browser_url, nav_url, sizeof(nav_url));
                                    if (fb == 1) {
                                        sound_click();
                                        redraw_all_frame(mx, my);
                                    } else if (fb == 2 && nav_url[0]) {
                                        if (browser_is_audio_media(nav_url, "")) {
                                            char resolved[256];
                                            media_resolve_url(nav_url, resolved, sizeof(resolved));
                                            browser_play_in_browser_media(resolved, 0);
                                            sound_win();
                                            redraw_all_frame(mx, my);
                                        } else if (browser_is_video_media(nav_url, "")) {
                                            char resolved[256];
                                            media_resolve_url(nav_url, resolved, sizeof(resolved));
                                            browser_play_in_browser_media(resolved, 1);
                                            sound_win();
                                            redraw_all_frame(mx, my);
                                        } else {
                                            browser_fetch(nav_url);
                                            sound_open();
                                        }
                                    }
                                }
                            }
                        }

                        else if (w->app == APP_AI_CHAT) {
                            handle_ai_chat_click(w, mx, my);
                            redraw_all_frame(mx, my);
                        }
                    }
                }
            }
            else if (lclick && dragging_win >= 0)
            {
                gui_window_t *w = &windows[dragging_win];
                w->x = mx - drag_off_x;
                w->y = my - drag_off_y;
                if (w->x < 0) w->x = 0;
                if (w->x + w->w > SCREEN_W) w->x = SCREEN_W - w->w;
                if (w->y < 0) w->y = 0;
                if (w->y + w->h > TASKBAR_Y) w->y = TASKBAR_Y - w->h;
            }
            else if (lclick && focused_win >= 0 && windows[focused_win].app == APP_PAINTER && dragging_win < 0 && !windows[focused_win].minimized)
            {
                gui_window_t *w = &windows[focused_win];
                int cx, cy, cw, ch;
                paint_canvas_rect(w, &cx, &cy, &cw, &ch);
                /* Only draw if inside the canvas inner area */
                if (mx >= cx + 1 && mx < cx + cw - 1 && my >= cy + 1 && my < cy + ch - 1) {
                    /* Map screen coords → logical canvas coords */
                    int draw_w = cw - 2, draw_h = ch - 2;
                    if (draw_w < 1) draw_w = 1;
                    if (draw_h < 1) draw_h = 1;
                    int cur_px = ((mx - (cx + 1)) * PAINT_CW) / draw_w;
                    int cur_py = ((my - (cy + 1)) * PAINT_CH) / draw_h;
                    if (cur_px >= PAINT_CW) cur_px = PAINT_CW - 1;
                    if (cur_py >= PAINT_CH) cur_py = PAINT_CH - 1;
                    if (paint_fill) {
                        if (!prev_lclick) {
                            paint_flood_fill(cur_px, cur_py, paint_color);
                            sound_click();
                        }
                    } else if (prev_paint_x >= 0 && prev_paint_y >= 0) {
                        draw_paint_line(prev_paint_x, prev_paint_y, cur_px, cur_py, paint_color);
                    } else {
                        draw_paint_spot(cur_px, cur_py, paint_color);
                    }
                    prev_paint_x = cur_px;
                    prev_paint_y = cur_py;
                } else {
                    prev_paint_x = -1; prev_paint_y = -1;
                }
            }
            else if (!lclick)
            {
                if (dragging_win >= 0) {
                    gui_window_t *w = &windows[dragging_win];
                    /* Window Snapping Edge Detection */
                    if (mx <= 3) {
                        /* Snap Left (50%) */
                        w->saved_x = 20; w->saved_y = 15; w->saved_w = 200; w->saved_h = 130;
                        w->x = 0; w->y = 0; w->w = SCREEN_W / 2; w->h = TASKBAR_Y;
                        w->split_state = 1; w->maximized = 0;
                        show_toast("Snapped Left (50%)", TOAST_INFO);
                        sound_click();
                    } else if (mx >= SCREEN_W - 4) {
                        /* Snap Right (50%) */
                        w->saved_x = 20; w->saved_y = 15; w->saved_w = 200; w->saved_h = 130;
                        w->x = SCREEN_W / 2; w->y = 0; w->w = SCREEN_W / 2; w->h = TASKBAR_Y;
                        w->split_state = 2; w->maximized = 0;
                        show_toast("Snapped Right (50%)", TOAST_INFO);
                        sound_click();
                    } else if (my <= 2) {
                        /* Snap Top / Maximize */
                        w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                        w->x = 0; w->y = 0; w->w = SCREEN_W; w->h = TASKBAR_Y;
                        w->maximized = 1; w->split_state = 0;
                        show_toast("Maximized", TOAST_INFO);
                        sound_click();
                    }
                    dragging_win = -1;
                }
                prev_paint_x = -1;
                prev_paint_y = -1;
            }

            /* Update dynamic mouse cursor based on hover */
            if (focused_win >= 0 && windows[focused_win].app == APP_BROWSER && windows[focused_win].visible && !windows[focused_win].minimized) {
                gui_window_t *bw = &windows[focused_win];
                int tab_bar_y = bw->y + 13;
                int tab_area_w = bw->w - 24;
                int tab_w = tab_area_w / browser_tab_count;
                if (tab_w > 90) tab_w = 90;
                if (tab_w < 45) tab_w = 45;
                int plus_x = bw->x + 4 + browser_tab_count * (tab_w + 2);
                int url_w = bw->w - 84;
                int go_x = bw->x + 46 + url_w + 3;

                if (my >= tab_bar_y && my <= tab_bar_y + 12) {
                    if (mx >= bw->x + 4 && mx <= plus_x + 12) gui_cursor_type = 1; /* Tab / Plus -> Hand */
                    else gui_cursor_type = 0;
                } else if (my >= bw->y + 25 && my <= bw->y + 38) {
                    if (mx >= bw->x + 4 && mx <= bw->x + 44) gui_cursor_type = 1; /* Back/Fwd/Reload -> Hand */
                    else if (mx >= bw->x + 46 && mx <= bw->x + 46 + url_w) gui_cursor_type = 2; /* URL Bar -> I-Beam */
                    else if (mx >= go_x && mx <= go_x + 26) gui_cursor_type = 1; /* Go -> Hand */
                    else gui_cursor_type = 0;
                } else if (my >= bw->y + 39 && my <= bw->y + 49) {
                    gui_cursor_type = 1; /* Bookmarks Bar -> Hand */
                } else if (my >= bw->y + 50 && my <= bw->y + bw->h - 14) {
                    gui_cursor_type = 0;
                    for (int k = 0; k < ns_element_count; k++) {
                        if (ns_elements[k].is_input) {
                            int ex = bw->x + 2 + ns_elements[k].x;
                            int ey = bw->y + 50 + ns_elements[k].y - browser_scroll_y;
                            if (mx >= ex && mx <= ex + ns_elements[k].w && my >= ey && my <= ey + ns_elements[k].h) {
                                gui_cursor_type = 2; /* I-Beam */
                                break;
                            }
                        }
                    }
                    if (gui_cursor_type == 0) {
                        for (int k = 0; k < ns_element_count; k++) {
                            int ex = bw->x + 2 + ns_elements[k].x;
                            int ey = bw->y + 50 + ns_elements[k].y - browser_scroll_y;
                            if (mx >= ex && mx <= ex + ns_elements[k].w && my >= ey && my <= ey + ns_elements[k].h) {
                                if (ns_elements[k].is_link || ns_elements[k].is_button || ns_elements[k].is_video || ns_elements[k].is_audio) gui_cursor_type = 1; /* Hand */
                                break;
                            }
                        }
                    }
                    if (browser_media_active && my >= bw->y + bw->h - 26 && my < bw->y + bw->h - 12 && mx >= bw->x + 36 && mx <= bw->x + bw->w - 3) {
                        gui_cursor_type = 1; /* Hand over media controls */
                    }
                } else {
                    gui_cursor_type = 0;
                }
            } else {
                gui_cursor_type = 0;
            }

            prev_mx = mx;
            prev_my = my;
            prev_lclick = lclick;
            prev_rclick = rclick;
            redraw_all_frame(mx, my);
        }

        /* Periodic Video & Audio Playback Animator */
        static uint32_t last_video_anim_tick = 0;
        if (now_ticks - last_video_anim_tick >= 9) {
            last_video_anim_tick = now_ticks;
            bool any_media_playing = false;
            for (int k = 0; k < ns_element_count; k++) {
                if ((ns_elements[k].is_video || ns_elements[k].is_audio) && ns_elements[k].is_playing) {
                    any_media_playing = true;
                    break;
                }
            }
            if (browser_media_active && (audio_get_state() == AUDIO_STATE_PLAYING || video_get_state() == VIDEO_STATE_PLAYING)) {
                any_media_playing = true;
            }
            if (any_media_playing && focused_win >= 0 && windows[focused_win].app == APP_BROWSER && windows[focused_win].visible) {
                redraw_all_frame(mx, my);
            }
        }

        uint8_t kbd_status = inb(0x64);
        if (!irq_kbd_fired && (kbd_status & 0x01) && !(kbd_status & 0x20)) {
            last_scancode = inb(0x60);
            irq_kbd_fired = 1;
        }

        if (irq_kbd_fired) {
            uint8_t sc = last_scancode;
            irq_kbd_fired = 0;

            if (screensaver_active) {
                screensaver_active = 0;
                redraw_all_frame(mx, my);
            }
            last_activity_tick = now_ticks;

            static int ctrl_down = 0;
            static int alt_down = 0;
            if (sc == 0x2A || sc == 0x36) shift_down = 1;
            else if (sc == 0xAA || sc == 0xB6) shift_down = 0;
            else if (sc == 0x3A) { caps_lock_state = !caps_lock_state; sound_click(); }
            else if (sc == 0x1D) ctrl_down = 1;
            else if (sc == 0x9D) ctrl_down = 0;
            else if (sc == 0x38) alt_down = 1;
            else if (sc == 0xB8) alt_down = 0;

            clipboard_check_serial_input();

            /* Alt+Tab: Cycle Window Focus */
            if (alt_down && sc == 0x0F) {
                int next_win = (focused_win + 1) % win_count;
                int checked = 0;
                while (checked < win_count) {
                    if (windows[next_win].visible) {
                        if (focused_win >= 0 && focused_win < win_count) windows[focused_win].focused = 0;
                        focused_win = next_win;
                        windows[focused_win].focused = 1;
                        windows[focused_win].minimized = 0;
                        sound_win_open();
                        redraw_all_frame(mx, my);
                        break;
                    }
                    next_win = (next_win + 1) % win_count;
                    checked++;
                }
                continue;
            }

            /* Browser Tab Shortcuts (Ctrl+T: New Tab, Ctrl+W: Close Tab, Ctrl+Tab: Switch Tab) */
            if (focused_win >= 0 && windows[focused_win].app == APP_BROWSER && windows[focused_win].visible) {
                if (ctrl_down && sc == 0x14) { /* Ctrl+T */
                    browser_new_tab("about:home");
                    redraw_all_frame(mx, my);
                    continue;
                }
                if (ctrl_down && sc == 0x11) { /* Ctrl+W */
                    if (browser_tab_count > 1) {
                        browser_close_tab(browser_active_tab);
                        redraw_all_frame(mx, my);
                        continue;
                    }
                }
                if (ctrl_down && sc == 0x0F) { /* Ctrl+Tab */
                    if (browser_tab_count > 1) {
                        browser_switch_tab((browser_active_tab + 1) % browser_tab_count);
                        redraw_all_frame(mx, my);
                        continue;
                    }
                }
            }

            /* Global Volume HUD Shortcuts (Ctrl++ and Ctrl+-) */
            if (ctrl_down && (sc == 0x0D || sc == 0x4E)) { /* Ctrl + '+' */
                int nv = audio_get_volume() + 10;
                if (nv > 100) nv = 100;
                audio_set_volume(nv);
                char vbuf[32];
                snprintf(vbuf, sizeof(vbuf), "Volume: %d%%", nv);
                show_toast(vbuf, TOAST_INFO);
                sound_click();
                redraw_all_frame(mx, my);
                continue;
            }
            if (ctrl_down && (sc == 0x0C || sc == 0x4A)) { /* Ctrl + '-' */
                int nv = audio_get_volume() - 10;
                if (nv < 0) nv = 0;
                audio_set_volume(nv);
                char vbuf[32];
                snprintf(vbuf, sizeof(vbuf), "Volume: %d%%", nv);
                show_toast(vbuf, TOAST_INFO);
                sound_click();
                redraw_all_frame(mx, my);
                continue;
            }

            /* Window Close (Ctrl+W / Alt+F4) */
            if ((ctrl_down && sc == 0x11) || (alt_down && sc == 0x3E)) {
                if (focused_win >= 0 && focused_win < win_count && windows[focused_win].visible) {
                    windows[focused_win].visible = 0;
                    windows[focused_win].focused = 0;
                    focused_win = -1;
                    redraw_all_frame(mx, my);
                }
                continue;
            }

            /* Window Maximize / Restore / Fullscreen (Alt+Enter, F11, or Ctrl+M) */
            if ((alt_down && sc == 0x1C) || sc == 0x57 || (ctrl_down && sc == 0x32)) {
                if (focused_win >= 0 && focused_win < win_count && windows[focused_win].visible) {
                    gui_window_t *w = &windows[focused_win];
                    if (!w->maximized) {
                        w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                        w->x = 0; w->y = 0; w->w = SCREEN_W; w->h = TASKBAR_Y;
                        w->maximized = 1; w->split_state = 0;
                        show_toast("Fullscreen / Maximized", TOAST_INFO);
                    } else {
                        w->x = w->saved_x; w->y = w->saved_y; w->w = w->saved_w; w->h = w->saved_h;
                        w->maximized = 0;
                        show_toast("Restored", TOAST_INFO);
                    }
                    sound_click();
                    redraw_all_frame(mx, my);
                }
                continue;
            }

            /* Toggle Start Menu (F4 / Ctrl+Esc) */
            if (sc == 0x3E || (ctrl_down && sc == 0x01)) {
                start_menu_open = !start_menu_open;
                start_menu_selected_idx = 0;
                redraw_all_frame(mx, my);
                continue;
            }

            /* Start Menu Keyboard Navigation */
            if (start_menu_open) {
                if (sc == 0x48) { /* Up Arrow */
                    if (start_menu_selected_idx > 0) start_menu_selected_idx--;
                    else start_menu_selected_idx = 12;
                    redraw_all_frame(mx, my);
                    continue;
                } else if (sc == 0x50) { /* Down Arrow */
                    if (start_menu_selected_idx < 12) start_menu_selected_idx++;
                    else start_menu_selected_idx = 0;
                    redraw_all_frame(mx, my);
                    continue;
                } else if (sc == 0x1C) { /* Enter */
                    static const int menu_to_win[13] = {
                        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13
                    };
                    if (start_menu_selected_idx >= 0 && start_menu_selected_idx < 13) {
                        open_window(menu_to_win[start_menu_selected_idx]);
                    }
                    start_menu_open = 0;
                    redraw_all_frame(mx, my);
                    continue;
                } else if (sc == 0x01) { /* Escape */
                    start_menu_open = 0;
                    redraw_all_frame(mx, my);
                    continue;
                }
            }

            if (sc == 0x3B) { open_window(10); redraw_all_frame(mx, my); continue; } /* F1: App Studio */
            if (sc == 0x3C || (ctrl_down && sc == 0x14)) { open_window(9); redraw_all_frame(mx, my); continue; } /* F2 / Ctrl+T: Terminal */
            if (sc == 0x3D || (ctrl_down && sc == 0x30)) { open_window(12); redraw_all_frame(mx, my); continue; } /* F3 / Ctrl+B: Web Browser */
            if (sc == 0x3E) { open_window(13); redraw_all_frame(mx, my); continue; } /* F4: AI Assistant */

            /* Snake Keyboard Input */
            if (focused_win >= 0 && windows[focused_win].app == APP_SNAKE && windows[focused_win].visible && !windows[focused_win].minimized) {
                if (sc == 0x48 || sc == 0x11) { if (snake_dir != 2) snake_next_dir = 0; }      /* Up / W */
                else if (sc == 0x4D || sc == 0x20) { if (snake_dir != 3) snake_next_dir = 1; } /* Right / D */
                else if (sc == 0x50 || sc == 0x1F) { if (snake_dir != 0) snake_next_dir = 2; } /* Down / S */
                else if (sc == 0x4B || sc == 0x1E) { if (snake_dir != 1) snake_next_dir = 3; } /* Left / A */
                else if ((sc == 0x1C || sc == 0x39) && snake_dead) { snake_reset(); }           /* Enter / Space */
                redraw_all_frame(mx, my);
                continue;
            }

            /* Notepad Full ASCII Input */
            if (focused_win >= 0 && windows[focused_win].app == APP_NOTEPAD && windows[focused_win].visible && !windows[focused_win].minimized) {
                if (sc == 0x0E && notepad_len > 0) { /* Backspace */
                    notepad_buf[--notepad_len] = '\0';
                    redraw_all_frame(mx, my);
                } else if (sc == 0x1C && notepad_len < 500) { /* Enter */
                    notepad_buf[notepad_len++] = '\n';
                    notepad_buf[notepad_len] = '\0';
                    redraw_all_frame(mx, my);
                } else {
                    char ch = scancode_to_ascii(sc, shift_down);
                    if (ch && notepad_len < 500) {
                        notepad_buf[notepad_len++] = ch;
                        notepad_buf[notepad_len] = '\0';
                        redraw_all_frame(mx, my);
                    }
                }
                continue;
            }

            /* Terminal CLI Input */
            if (focused_win >= 0 && windows[focused_win].app == APP_CLI && windows[focused_win].visible && !windows[focused_win].minimized) {
                if (sc == 0x0E) { /* Backspace */
                    if (cli_cursor > 0) {
                        for (int k = cli_cursor - 1; k < cli_input_len - 1; k++) {
                            cli_input[k] = cli_input[k + 1];
                        }
                        cli_input_len--;
                        cli_cursor--;
                        cli_input[cli_input_len] = '\0';
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x53) { /* Delete */
                    if (cli_cursor < cli_input_len) {
                        for (int k = cli_cursor; k < cli_input_len - 1; k++) {
                            cli_input[k] = cli_input[k + 1];
                        }
                        cli_input_len--;
                        cli_input[cli_input_len] = '\0';
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x4B) { /* Left Arrow */
                    if (cli_cursor > 0) {
                        cli_cursor--;
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x4D) { /* Right Arrow */
                    if (cli_cursor < cli_input_len) {
                        cli_cursor++;
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x47) { /* Home */
                    cli_cursor = 0;
                    redraw_all_frame(mx, my);
                } else if (sc == 0x4F) { /* End */
                    cli_cursor = cli_input_len;
                    redraw_all_frame(mx, my);
                } else if (sc == 0x48) { /* Up Arrow (History Back) */
                    if (cli_hist_cnt > 0 && cli_hist_idx > 0) {
                        cli_hist_idx--;
                        str_cpy(cli_input, cli_history[cli_hist_idx], CLI_LINE_LEN);
                        cli_input_len = str_len(cli_input);
                        cli_cursor = cli_input_len;
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x50) { /* Down Arrow (History Forward) */
                    if (cli_hist_idx < cli_hist_cnt - 1) {
                        cli_hist_idx++;
                        str_cpy(cli_input, cli_history[cli_hist_idx], CLI_LINE_LEN);
                        cli_input_len = str_len(cli_input);
                        cli_cursor = cli_input_len;
                        redraw_all_frame(mx, my);
                    } else if (cli_hist_idx == cli_hist_cnt - 1) {
                        cli_hist_idx = cli_hist_cnt;
                        cli_input[0] = '\0';
                        cli_input_len = 0;
                        cli_cursor = 0;
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x49) { /* Page Up (Scroll up) */
                    if (cli_scroll < cli_line_count - 2) {
                        cli_scroll += 4;
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x51) { /* Page Down (Scroll down) */
                    if (cli_scroll > 0) {
                        cli_scroll -= 4;
                        if (cli_scroll < 0) cli_scroll = 0;
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x1C) { /* Enter */
                    cli_execute(&windows[focused_win]);
                    redraw_all_frame(mx, my);
                } else {
                    char ch = scancode_to_ascii(sc, shift_down);
                    if (ch && ch >= 32 && ch <= 126 && cli_input_len < CLI_LINE_LEN - 2) {
                        for (int k = cli_input_len; k >= cli_cursor; k--) {
                            cli_input[k + 1] = cli_input[k];
                        }
                        cli_input[cli_cursor++] = ch;
                        cli_input_len++;
                        cli_input[cli_input_len] = '\0';
                        redraw_all_frame(mx, my);
                    }
                }
                continue;
            }

            /* App Studio Code Input */
            if (focused_win >= 0 && windows[focused_win].app == APP_CODESTUDIO && windows[focused_win].visible && !windows[focused_win].minimized) {
                if (ctrl_down && sc == 0x2E) { /* Ctrl+C: Copy */
                    codestudio_copy();
                    redraw_all_frame(mx, my);
                } else if (ctrl_down && sc == 0x2F) { /* Ctrl+V: Paste */
                    codestudio_paste();
                    redraw_all_frame(mx, my);
                } else if (sc == 0x4B) { /* Left Arrow */
                    if (code_cursor_col > 0) {
                        code_cursor_col--;
                    } else if (code_cursor_line > 0) {
                        code_cursor_line--;
                        code_cursor_col = str_len(code_lines[code_cursor_line]);
                    }
                    redraw_all_frame(mx, my);
                } else if (sc == 0x4D) { /* Right Arrow */
                    int cur_len = str_len(code_lines[code_cursor_line]);
                    if (code_cursor_col < cur_len) {
                        code_cursor_col++;
                    } else if (code_cursor_line < code_line_cnt - 1) {
                        code_cursor_line++;
                        code_cursor_col = 0;
                    }
                    redraw_all_frame(mx, my);
                } else if (sc == 0x48) { /* Up Arrow */
                    if (code_cursor_line > 0) {
                        code_cursor_line--;
                        int next_len = str_len(code_lines[code_cursor_line]);
                        if (code_cursor_col > next_len) code_cursor_col = next_len;
                    }
                    redraw_all_frame(mx, my);
                } else if (sc == 0x50) { /* Down Arrow */
                    if (code_line_cnt > 0 && code_cursor_line < code_line_cnt - 1) {
                        code_cursor_line++;
                        int next_len = str_len(code_lines[code_cursor_line]);
                        if (code_cursor_col > next_len) code_cursor_col = next_len;
                    }
                    redraw_all_frame(mx, my);
                } else if (sc == 0x0E) { /* Backspace */
                    char *lptr = code_lines[code_cursor_line];
                    if (code_cursor_col > 0) {
                        int llen = str_len(lptr);
                        int del_count = 1;
                        int is_all_spaces = 1;
                        for (int k = 0; k < code_cursor_col; k++) {
                            if (lptr[k] != ' ') { is_all_spaces = 0; break; }
                        }
                        if (is_all_spaces && code_cursor_col >= 4) del_count = 4;
                        for (int k = code_cursor_col - del_count; k <= llen - del_count; k++) {
                            lptr[k] = lptr[k + del_count];
                        }
                        code_cursor_col -= del_count;
                        redraw_all_frame(mx, my);
                    } else if (code_cursor_line > 0) {
                        int prev_idx = code_cursor_line - 1;
                        char *prev_ptr = code_lines[prev_idx];
                        int prev_len = str_len(prev_ptr);
                        int cur_len  = str_len(lptr);
                        if (prev_len + cur_len < CODE_LINE_MAX_LEN - 1) {
                            for (int k = 0; k <= cur_len; k++) prev_ptr[prev_len + k] = lptr[k];
                            for (int k = code_cursor_line; k < code_line_cnt - 1; k++) {
                                str_cpy(code_lines[k], code_lines[k + 1], CODE_LINE_MAX_LEN);
                            }
                            code_lines[code_line_cnt - 1][0] = '\0';
                            code_line_cnt--;
                            code_cursor_line = prev_idx;
                            code_cursor_col = prev_len;
                        }
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x0F) { /* Tab Key */
                    char *lptr = code_lines[code_cursor_line];
                    int llen = str_len(lptr);
                    if (llen + 4 < CODE_LINE_MAX_LEN - 1) {
                        for (int k = llen; k >= code_cursor_col; k--) lptr[k + 4] = lptr[k];
                        for (int k = 0; k < 4; k++) lptr[code_cursor_col++] = ' ';
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x1C) { /* Enter Key */
                    if (code_line_cnt < CODE_MAX_LINES) {
                        char *lptr = code_lines[code_cursor_line];
                        int llen = str_len(lptr);
                        char right_part[CODE_LINE_MAX_LEN];
                        int rlen = 0;
                        for (int k = code_cursor_col; k < llen; k++) right_part[rlen++] = lptr[k];
                        right_part[rlen] = '\0';
                        lptr[code_cursor_col] = '\0';

                        int indent = 0;
                        while (lptr[indent] == ' ') indent++;

                        int end_idx = code_cursor_col;
                        while (end_idx > 0 && (lptr[end_idx - 1] == ' ' || lptr[end_idx - 1] == '\t' || lptr[end_idx - 1] == '\r')) end_idx--;
                        if (end_idx > 0) {
                            char last_c = lptr[end_idx - 1];
                            if ((code_lang == LANG_PYTHON && last_c == ':') ||
                                (code_lang == LANG_C && last_c == '{')) {
                                indent += 4;
                            }
                        }
                        if (indent > CODE_LINE_MAX_LEN - 10) indent = CODE_LINE_MAX_LEN - 10;

                        for (int k = code_line_cnt; k > code_cursor_line + 1; k--) {
                            str_cpy(code_lines[k], code_lines[k - 1], CODE_LINE_MAX_LEN);
                        }
                        code_line_cnt++;
                        code_cursor_line++;
                        char *next_ptr = code_lines[code_cursor_line];
                        int ni = 0;
                        for (int k = 0; k < indent; k++) next_ptr[ni++] = ' ';
                        for (int k = 0; k < rlen; k++) next_ptr[ni++] = right_part[k];
                        next_ptr[ni] = '\0';
                        code_cursor_col = indent;
                        redraw_all_frame(mx, my);
                    }
                } else {
                    char ch = scancode_to_ascii(sc, shift_down);
                    if (ch && ch >= 32 && ch <= 126) {
                        char *lptr = code_lines[code_cursor_line];
                        int llen = str_len(lptr);
                        if (llen < CODE_LINE_MAX_LEN - 1) {
                            for (int k = llen; k >= code_cursor_col; k--) lptr[k + 1] = lptr[k];
                            lptr[code_cursor_col++] = ch;
                            redraw_all_frame(mx, my);
                        }
                    }
                }
                continue;
            }

            /* Web Browser In-Page & Global Keyboard Navigation */
            if (focused_win >= 0 && windows[focused_win].app == APP_BROWSER && windows[focused_win].visible && !windows[focused_win].minimized) {
                /* Tab / Shift+Tab Navigation */
                if (sc == 0x0F) {
                    if (shift_down) {
                        /* Reverse Tab */
                        if (browser_nav_focus == BROWSER_FOCUS_URL) {
                            if (ns_element_count > 0) browser_nav_focus = ns_element_count - 1;
                        } else if (browser_nav_focus > 0) {
                            browser_nav_focus--;
                        } else {
                            browser_nav_focus = BROWSER_FOCUS_URL;
                        }
                    } else {
                        /* Forward Tab */
                        if (browser_nav_focus == BROWSER_FOCUS_NONE) {
                            browser_nav_focus = BROWSER_FOCUS_URL;
                        } else if (browser_nav_focus == BROWSER_FOCUS_URL) {
                            if (ns_element_count > 0) browser_nav_focus = 0;
                        } else if (browser_nav_focus < ns_element_count - 1) {
                            browser_nav_focus++;
                        } else {
                            browser_nav_focus = BROWSER_FOCUS_URL;
                        }
                    }

                    if (browser_nav_focus == BROWSER_FOCUS_URL) {
                        browser_url_focus = 1;
                    } else {
                        browser_url_focus = 0;
                    }

                    /* Auto-scroll viewport if target is offscreen */
                    gui_window_t *bw = &windows[focused_win];
                    int bview_y = bw->y + 29, bview_h = bw->h - 42;
                    if (browser_nav_focus >= 0 && browser_nav_focus < ns_element_count) {
                        int focus_y = bw->y + 29 + ns_elements[browser_nav_focus].y - browser_scroll_y;
                        if (focus_y < bview_y + 4) browser_scroll_y -= (bview_y + 10 - focus_y);
                        else if (focus_y > bview_y + bview_h - 16) browser_scroll_y += (focus_y - (bview_y + bview_h - 20));
                        if (browser_scroll_y < 0) browser_scroll_y = 0;
                    }

                    redraw_all_frame(mx, my);
                    continue;
                }

                /* Escape: Clear URL bar if focused, otherwise deselect */
                if (sc == 0x01) {
                    if (browser_url_focus || browser_nav_focus == BROWSER_FOCUS_URL) {
                        if (browser_url[0] != '\0') {
                            browser_url[0] = '\0';
                            browser_url_cursor = 0;
                            browser_url_scroll = 0;
                            redraw_all_frame(mx, my);
                            continue;
                        }
                    }
                    browser_nav_focus = BROWSER_FOCUS_NONE;
                    browser_input_focus = -1;
                    browser_url_focus = 0;
                    redraw_all_frame(mx, my);
                    continue;
                }

                /* F6 / Ctrl+L: Focus & Clear URL Bar for direct typing */
                if (sc == 0x40 || (ctrl_down && sc == 0x26)) {
                    browser_nav_focus = BROWSER_FOCUS_URL;
                    browser_url_focus = 1;
                    browser_url[0] = '\0';
                    browser_url_cursor = 0;
                    browser_url_scroll = 0;
                    redraw_all_frame(mx, my);
                    continue;
                }

                /* Enter: Activate Focused Control */
                if (sc == 0x1C) {
                    if (browser_nav_focus == BROWSER_FOCUS_URL || browser_url_focus) {
                        browser_url_focus = 0;
                        browser_fetch(browser_url);
                        sound_open();
                        redraw_all_frame(mx, my);
                    } else if (browser_nav_focus >= 0 && browser_nav_focus < ns_element_count) {
                        ns_interactive_elem_t *el = &ns_elements[browser_nav_focus];
                        if (el->onclick[0]) {
                            js_engine_exec_event(el->onclick);
                            sound_click();
                        } else if (el->is_input) {
                            if (el->form_action[0]) {
                                const char *pname = el->name[0] ? el->name : "q";
                                char act_url[256];
                                html_resolve_url(browser_url, el->form_action, act_url, sizeof(act_url));
                                char enc_q[192];
                                url_encode(el->text, enc_q, sizeof(enc_q));
                                char form_url[256];
                                snprintf(form_url, sizeof(form_url), "%s%s%s=%s", act_url, strchr(act_url, '?') ? "&" : "?", pname, enc_q);
                                browser_fetch(form_url);
                                sound_open();
                            } else if (el->text[0]) {
                                browser_submit_search(el->text);
                                sound_open();
                            } else {
                                str_cpy(browser_status, "Please enter a search query", sizeof(browser_status));
                                sound_click();
                            }
                        } else if (el->is_button) {
                            int inp_idx = -1;
                            for (int j = 0; j < ns_element_count; j++) {
                                if (ns_elements[j].is_input) {
                                    inp_idx = j;
                                    break;
                                }
                            }
                            if (el->form_action[0] && inp_idx >= 0) {
                                const char *pname = ns_elements[inp_idx].name[0] ? ns_elements[inp_idx].name : "q";
                                char act_url[256];
                                html_resolve_url(browser_url, el->form_action, act_url, sizeof(act_url));
                                char enc_q[192];
                                url_encode(ns_elements[inp_idx].text, enc_q, sizeof(enc_q));
                                char form_url[256];
                                snprintf(form_url, sizeof(form_url), "%s%s%s=%s", act_url, strchr(act_url, '?') ? "&" : "?", pname, enc_q);
                                browser_fetch(form_url);
                                sound_open();
                            } else {
                                int is_search_btn = (strstr(el->text, "Search") != NULL ||
                                                     strstr(el->text, "search") != NULL ||
                                                     strstr(el->text, "Go") != NULL ||
                                                     strstr(el->text, "Submit") != NULL ||
                                                     strstr(el->text, "Find") != NULL);
                                if (is_search_btn && inp_idx >= 0 && ns_elements[inp_idx].text[0]) {
                                    browser_submit_search(ns_elements[inp_idx].text);
                                    sound_open();
                                } else if (is_search_btn) {
                                    browser_nav_focus = (inp_idx >= 0) ? inp_idx : BROWSER_FOCUS_URL;
                                    str_cpy(browser_status, "Please enter a search query", sizeof(browser_status));
                                    sound_click();
                                } else {
                                    sound_click();
                                }
                            }
                        } else if (el->is_image) {
                            decoded_image_t *existing = image_cache_get_or_load(el->src, NULL, 0);
                            if (!existing) {
                                snprintf(browser_status, sizeof(browser_status), "Loading image: %s...", el->text);
                                sound_click();
                                redraw_all_frame(mx, my);

                                decoded_image_t *loaded = image_fetch_and_cache(el->src, browser_url);
                                if (loaded && loaded->pixels) {
                                    snprintf(browser_status, sizeof(browser_status), "Image loaded (%dx%d)", loaded->width, loaded->height);
                                    sound_tone(523, 20);
                                } else {
                                    snprintf(browser_status, sizeof(browser_status), "Failed to load image");
                                }
                            } else if (el->is_link && el->href[0]) {
                                char target_url[256];
                                html_resolve_url(browser_url, el->href, target_url, sizeof(target_url));
                                browser_fetch(target_url);
                                sound_open();
                            } else {
                                snprintf(browser_status, sizeof(browser_status), "Image: %dx%d px", existing->width, existing->height);
                                sound_click();
                            }
                        } else if (el->is_link && el->href[0]) {
                            if (str_ncmp(el->href, "javascript:", 11) == 0) {
                                js_engine_exec_event(el->href + 11);
                                sound_click();
                            } else {
                                char target_url[256];
                                html_resolve_url(browser_url, el->href, target_url, sizeof(target_url));
                                browser_fetch(target_url);
                                sound_open();
                            }
                        } else if (el->is_video) {
                            el->is_playing = !el->is_playing;
                            snprintf(browser_status, sizeof(browser_status), "%s Video Stream", el->is_playing ? "Playing" : "Paused");
                            if (el->is_playing) {
                                sound_tone(523, 15);
                                sound_tone(659, 15);
                                sound_tone(784, 25);
                            } else {
                                sound_click();
                            }
                        }
                        redraw_all_frame(mx, my);
                    }
                    continue;
                }

                /* Typing & Navigation in URL bar */
                if (browser_nav_focus == BROWSER_FOCUS_URL || browser_url_focus) {
                    gui_window_t *bw = &windows[focused_win];
                    int url_w = bw->w - 84;
                    int max_chars = (url_w - 22) / 6;
                    if (max_chars < 5) max_chars = 5;

                    if (sc == 0x01) { /* Escape: Clear URL */
                        browser_url[0] = '\0';
                        browser_url_cursor = 0;
                        browser_url_scroll = 0;
                        redraw_all_frame(mx, my);
                        continue;
                    } else if (sc == 0x4B) { /* Left Arrow */
                        if (browser_url_cursor > 0) {
                            browser_url_cursor--;
                            if (browser_url_cursor < browser_url_scroll) browser_url_scroll = browser_url_cursor;
                            redraw_all_frame(mx, my);
                        }
                        continue;
                    } else if (sc == 0x4D) { /* Right Arrow */
                        int ulen = str_len(browser_url);
                        if (browser_url_cursor < ulen) {
                            browser_url_cursor++;
                            if (browser_url_cursor > browser_url_scroll + max_chars) browser_url_scroll = browser_url_cursor - max_chars;
                            redraw_all_frame(mx, my);
                        }
                        continue;
                    } else if (sc == 0x47) { /* Home */
                        browser_url_cursor = 0;
                        browser_url_scroll = 0;
                        redraw_all_frame(mx, my);
                        continue;
                    } else if (sc == 0x4F) { /* End */
                        int ulen = str_len(browser_url);
                        browser_url_cursor = ulen;
                        if (browser_url_cursor > max_chars) browser_url_scroll = browser_url_cursor - max_chars;
                        else browser_url_scroll = 0;
                        redraw_all_frame(mx, my);
                        continue;
                    } else if (sc == 0x0E) { /* Backspace */
                        int ulen = str_len(browser_url);
                        if (browser_url_cursor > 0 && ulen > 0) {
                            for (int i = browser_url_cursor - 1; i < ulen; i++) {
                                browser_url[i] = browser_url[i + 1];
                            }
                            browser_url_cursor--;
                            if (browser_url_cursor < browser_url_scroll) browser_url_scroll = browser_url_cursor;
                            redraw_all_frame(mx, my);
                        }
                        continue;
                    } else if (sc == 0x53) { /* Delete */
                        int ulen = str_len(browser_url);
                        if (browser_url_cursor < ulen) {
                            for (int i = browser_url_cursor; i < ulen; i++) {
                                browser_url[i] = browser_url[i + 1];
                            }
                            redraw_all_frame(mx, my);
                        }
                        continue;
                    } else if (sc == 0x48 || sc == 0x50 || sc == 0x49 || sc == 0x51) {
                        /* Up/Down Arrow / Page Up / Page Down: scroll webpage */
                        browser_url_focus = 0;
                        browser_nav_focus = BROWSER_FOCUS_NONE;
                        if (sc == 0x48 && browser_scroll_y > 0) browser_scroll_y -= 30;
                        else if (sc == 0x50) browser_scroll_y += 30;
                        else if (sc == 0x49 && browser_scroll_y > 0) browser_scroll_y -= 60;
                        else if (sc == 0x51) browser_scroll_y += 60;
                        if (browser_scroll_y < 0) browser_scroll_y = 0;
                        redraw_all_frame(mx, my);
                        continue;
                    } else {
                        char ch = scancode_to_ascii(sc, shift_down);
                        if (ch && ch >= 32 && ch <= 126) {
                            int ulen = str_len(browser_url);
                            if (ulen < (int)sizeof(browser_url) - 2) {
                                for (int i = ulen; i >= browser_url_cursor; i--) {
                                    browser_url[i + 1] = browser_url[i];
                                }
                                browser_url[browser_url_cursor] = ch;
                                browser_url_cursor++;
                                if (browser_url_cursor > browser_url_scroll + max_chars) browser_url_scroll = browser_url_cursor - max_chars;
                                redraw_all_frame(mx, my);
                            }
                        }
                        continue;
                    }
                }

                /* Arrow Keys: Scrolling in Web Viewport */
                if (sc == 0x48) { /* Up Arrow */
                    if (browser_scroll_y > 0) {
                        browser_scroll_y -= 30;
                        if (browser_scroll_y < 0) browser_scroll_y = 0;
                        redraw_all_frame(mx, my);
                    }
                    continue;
                } else if (sc == 0x50) { /* Down Arrow */
                    browser_scroll_y += 30;
                    redraw_all_frame(mx, my);
                    continue;
                } else if (sc == 0x49) { /* Page Up */
                    if (browser_scroll_y > 0) {
                        browser_scroll_y -= 60;
                        if (browser_scroll_y < 0) browser_scroll_y = 0;
                        redraw_all_frame(mx, my);
                    }
                    continue;
                } else if (sc == 0x51) { /* Page Down */
                    browser_scroll_y += 60;
                    redraw_all_frame(mx, my);
                    continue;
                }

                /* Typing in in-page text input */
                if (browser_nav_focus >= 0 && browser_nav_focus < ns_element_count && ns_elements[browser_nav_focus].is_input) {
                    ns_interactive_elem_t *inp = &ns_elements[browser_nav_focus];
                    if (sc == 0x0E) { /* Backspace */
                        int ilen = str_len(inp->text);
                        if (ilen > 0) {
                            inp->text[ilen - 1] = '\0';
                            redraw_all_frame(mx, my);
                        }
                    } else {
                        char ch = scancode_to_ascii(sc, shift_down);
                        if (ch && ch >= 32 && ch <= 126) {
                            int ilen = str_len(inp->text);
                            if (ilen < (int)sizeof(inp->text) - 1) {
                                inp->text[ilen] = ch;
                                inp->text[ilen + 1] = '\0';
                                redraw_all_frame(mx, my);
                            }
                        }
                    }
                    continue;
                }
            }

            /* AI Assistant Chat Keyboard Input */
            if (focused_win >= 0 && windows[focused_win].app == APP_AI_CHAT && windows[focused_win].visible && !windows[focused_win].minimized) {
                if (sc == 0x0E) { /* Backspace */
                    if (ai_chat_input_len > 0) {
                        ai_chat_input[--ai_chat_input_len] = '\0';
                        redraw_all_frame(mx, my);
                    }
                } else if (sc == 0x1C) { /* Enter */
                    ai_chat_send_message();
                    redraw_all_frame(mx, my);
                } else {
                    char ch = scancode_to_ascii(sc, shift_down);
                    if (ch && ch >= 32 && ch <= 126 && ai_chat_input_len < 120) {
                        ai_chat_input[ai_chat_input_len++] = ch;
                        ai_chat_input[ai_chat_input_len] = '\0';
                        redraw_all_frame(mx, my);
                    }
                }
                continue;
            }



            /* Global Escape */
            if (sc == 0x01) {
                if (start_menu_open) {
                    start_menu_open = 0;
                    redraw_all_frame(mx, my);
                    continue;
                }
                if (ctx_menu_open) {
                    ctx_menu_open = 0;
                    redraw_all_frame(mx, my);
                    continue;
                }
                /* Return from GUI to CLI shell */
                break;
            }
        }
        asm volatile("sti; hlt");
    }

    vga_set_text_mode();
    vga_restore_font_and_text(font_backup);
    vga_restore_text_palette();

    vga_kbd_flush();
    vga_clear();
    gui_active = 0;
    /* Return to vga_prompt() which is already on the call stack —
     * do NOT call vga_prompt() here or it creates an infinite recursive loop. */
}
