// src/gui.c — ArchaOS v4.0 Desktop & Program Suite
// VGA Mode 13h: 320x200, 256 colors, linear framebuffer at 0xA0000
// 100% Double-Buffered: 0% Tearing, 0% Flickering, 0% Mouse Trails!

#include "gui.h"
#include "idt.h"
#include "pit.h"
#include "kernel.h"
#include "fs.h"
#include "vga.h"
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

static void set_mode13h(void)
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

static void font_backup_save(uint8_t *font_buf)
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

static void set_text_mode(void)
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
        0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,
        0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
        0x0C,0x00,0x0F,0x08,0x00
    };
    for (int i = 0; i < 5; i++)  { outb(0x3C4, i); outb(0x3C5, SEQ[i]); }
    outb(0x3D4, 0x03); outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & ~0x80);
    for (int i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, CRTC[i]); }
    for (int i = 0; i < 9; i++)  { outb(0x3CE, i); outb(0x3CF, GC[i]); }
    inb(0x3DA);
    for (int i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, AC[i]); }
    outb(0x3C0, 0x20);

    /* Restore plane 2 font */
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);
    outb(0x3C4, 0x04); outb(0x3C5, 0x06);
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);
    uint8_t *p2 = (uint8_t *)0xA0000;
    for (int i = 0; i < 256 * 32; i++) p2[i] = font_backup[i];
    outb(0x3C4, 0x02); outb(0x3C5, 0x03);
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
}

static void set_dac_color(uint8_t dac_idx, uint8_t r, uint8_t g, uint8_t b)
{
    outb(0x3C8, dac_idx);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
}

static void restore_text_palette(void)
{
    static const uint8_t cga[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };
    for (int i = 0; i < 16; i++) set_dac_color((uint8_t)i, cga[i][0], cga[i][1], cga[i][2]);
    set_dac_color(0x14, cga[6][0], cga[6][1], cga[6][2]);
    for (int i = 8; i < 16; i++) set_dac_color((uint8_t)(0x30 + i), cga[i][0], cga[i][1], cga[i][2]);
}

/* Fast 32-bit Frame Flip to VGA Memory 0xA0000 */
static void gui_flip(void)
{
    uint32_t *dst = (uint32_t *)FB_ADDR;
    uint32_t *src = (uint32_t *)backbuffer;
    for (int i = 0; i < (SCREEN_W * SCREEN_H) / 4; i++)
        dst[i] = src[i];
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
                outb(0x3C9, (uint8_t)(r * 63 / 5));
                outb(0x3C9, (uint8_t)(g * 63 / 5));
                outb(0x3C9, (uint8_t)(b * 63 / 5));
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

static void fill_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int r = y; r < y + h; r++)
        for (int c = x; c < x + w; c++)
            put_pixel(c, r, color);
}

static void draw_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int c = x; c < x + w; c++) { put_pixel(c, y, color); put_pixel(c, y+h-1, color); }
    for (int r = y; r < y + h; r++) { put_pixel(x, r, color); put_pixel(x+w-1, r, color); }
}

static void draw_hline(int x, int y, int w, uint8_t color)
{ for (int c = x; c < x+w; c++) put_pixel(c, y, color); }

/* ============================================================
 * FULL ASCII 5x7 FONT
 * ============================================================ */

static const uint8_t FONT[][5] = {
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
    {0x00,0x30,0x30,0x00,0x00}, /* '.' */
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
    {0x7F,0x02,0x04,0x02,0x7F}, /* 'M' */
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
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' (64) */
};

static void draw_char(int x, int y, char c, uint8_t fg, uint8_t bg)
{
    uint8_t lower = 0;
    if (c >= 'a' && c <= 'z') { lower = 1; c -= 32; }

    int idx = (int)(c - 32);
    if (c == '_') idx = 63;
    else if (c == '|') idx = 64;
    else if (idx < 0 || idx > 64) idx = 31; /* '?' */

    const uint8_t *glyph = FONT[idx];
    int y_off = lower ? 1 : 0; /* subtle baseline offset for lowercase */

    for (int r = 0; r < 7; r++) {
        for (int col = 0; col < 5; col++) {
            uint8_t bit = (glyph[col] >> r) & 1;
            put_pixel(x + col, y + r + y_off, bit ? fg : bg);
        }
    }
}

static void draw_str(int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    while (*s) { draw_char(x, y, *s++, fg, bg); x += 6; }
}

static void draw_str_clip(int x, int y, const char *s, uint8_t fg, uint8_t bg, int clip_x)
{
    while (*s && x + 5 < clip_x) { draw_char(x, y, *s++, fg, bg); x += 6; }
}

static int str_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) {
    while(*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
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
    APP_CPANEL
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
} gui_window_t;

#define MAX_WINDOWS 6
static gui_window_t windows[MAX_WINDOWS];
static int win_count = 0;
static int focused_win = -1;

/* Alert Dialog Box */
static int alert_visible = 0;
static const char *alert_msg = "";

static void show_alert(const char *msg) {
    alert_msg = msg;
    alert_visible = 1;
}

/* Calculator State */
static int calc_val = 0;
static int calc_acc = 0;
static char calc_op = 0;
static char calc_display[16] = "0";

/* Painter State */
static uint8_t paint_canvas[140 * 65];
static uint8_t paint_color = COL_BLACK;
static int paint_brush_size = 1; /* 1: Pencil (1px), 3: Brush (3px) */
static int paint_eraser = 0;     /* 1: Eraser mode */
static int prev_paint_x = -1, prev_paint_y = -1;

static void draw_paint_spot(int cx, int cy, uint8_t color) {
    int radius = paint_eraser ? 3 : (paint_brush_size == 3 ? 1 : 0);
    uint8_t draw_col = paint_eraser ? COL_WHITE : color;

    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < 138 && py >= 0 && py < 63)
                paint_canvas[py * 140 + px] = draw_col;
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

    if (unrevealed_safe == 0) mine_win = 1;
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

/* File Manager State */
static fs_node_t *fileman_cur_dir = NULL;

/* 7x7 Pixel Program Icons */
static void draw_app_icon(int x, int y, app_type_t app)
{
    static const uint8_t ICONS[6][7][7] = {
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
        { {0,0,1,1,1,0,0},{0,1,0,0,0,1,0},{1,0,1,1,1,0,1},{1,0,1,0,1,0,1},{1,0,1,1,1,0,1},{0,1,0,0,0,1,0},{0,0,1,1,1,0,0} }
    };

    uint8_t fg = COL_BLACK;
    if (app == APP_CALC) fg = COL_BLUE;
    else if (app == APP_PAINTER) fg = COL_RED;
    else if (app == APP_MINESWEEPER) fg = COL_GREEN;
    else if (app == APP_FILEMAN) fg = COL_YELLOW;
    else if (app == APP_CPANEL) fg = COL_TEAL;

    int idx = (int)app - 1;
    if (idx < 0 || idx >= 6) return;

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

    windows[0] = (gui_window_t){ 15, 15, 110, 115, 15, 15, 110, 115, "Calculator", "Calc", APP_CALC, 0, 0, 0, 0, 0 };
    windows[1] = (gui_window_t){ 135, 15, 160, 100, 135, 15, 160, 100, "Painter", "Painter", APP_PAINTER, 0, 0, 0, 0, 0 };
    windows[2] = (gui_window_t){ 15, 65, 110, 105, 15, 65, 110, 105, "Minesweeper", "Mine", APP_MINESWEEPER, 0, 0, 0, 0, 0 };
    windows[3] = (gui_window_t){ 130, 50, 150, 110, 130, 50, 150, 110, "File Manager", "Files", APP_FILEMAN, 0, 0, 0, 0, 0 };
    windows[4] = (gui_window_t){ 35, 30, 150, 110, 35, 30, 150, 110, "Notepad", "Notepad", APP_NOTEPAD, 0, 0, 0, 0, 0 };
    windows[5] = (gui_window_t){ 150, 25, 145, 110, 150, 25, 145, 110, "Control Panel", "CPanel", APP_CPANEL, 0, 0, 0, 0, 0 };

    win_count = 6;
    focused_win = -1;
    fileman_cur_dir = fs_root();
    load_archaos_conf();
    minesweeper_reset();
    for (int i=0; i<140*65; i++) paint_canvas[i] = COL_WHITE;
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

static void draw_painter(gui_window_t *w)
{
    /* Action Bar: [Clear] [Pencil] [Brush] [Eraser] */
    fill_rect(w->x + 8, w->y + 16, 30, 10, GRAY(18));
    draw_rect(w->x + 8, w->y + 16, 30, 10, GRAY(8));
    draw_str(w->x + 10, w->y + 17, "Clear", COL_BLACK, GRAY(18));

    fill_rect(w->x + 42, w->y + 16, 32, 10, (paint_brush_size == 1 && !paint_eraser) ? GRAY(10) : GRAY(18));
    draw_rect(w->x + 42, w->y + 16, 32, 10, GRAY(8));
    draw_str(w->x + 44, w->y + 17, "Pencil", COL_BLACK, (paint_brush_size == 1 && !paint_eraser) ? GRAY(10) : GRAY(18));

    fill_rect(w->x + 78, w->y + 16, 28, 10, (paint_brush_size == 3 && !paint_eraser) ? GRAY(10) : GRAY(18));
    draw_rect(w->x + 78, w->y + 16, 28, 10, GRAY(8));
    draw_str(w->x + 80, w->y + 17, "Brush", COL_BLACK, (paint_brush_size == 3 && !paint_eraser) ? GRAY(10) : GRAY(18));

    fill_rect(w->x + 110, w->y + 16, 32, 10, paint_eraser ? GRAY(10) : GRAY(18));
    draw_rect(w->x + 110, w->y + 16, 32, 10, GRAY(8));
    draw_str(w->x + 112, w->y + 17, "Erase", COL_BLACK, paint_eraser ? GRAY(10) : GRAY(18));

    int cx = w->x + 8, cy = w->y + 28, cw = 140, ch = 54;
    fill_rect(cx, cy, cw, ch, COL_WHITE);
    draw_rect(cx, cy, cw, ch, COL_BLACK);

    for (int r = 0; r < ch - 2; r++)
        for (int c = 0; c < cw - 2; c++)
            put_pixel(cx + 1 + c, cy + 1 + r, paint_canvas[r * 140 + c]);

    static const uint8_t palette_cols[6] = { COL_BLACK, COL_RED, COL_GREEN, COL_BLUE, COL_YELLOW, COL_TEAL };
    for (int i = 0; i < 6; i++) {
        int px = w->x + 10 + i * 18;
        int py = w->y + 85;
        fill_rect(px, py, 14, 10, palette_cols[i]);
        draw_rect(px, py, 14, 10, (paint_color == palette_cols[i] && !paint_eraser) ? COL_WHITE : COL_BLACK);
    }
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
    fill_rect(w->x + 8, w->y + 16, w->w - 16, 85, COL_WHITE);
    draw_rect(w->x + 8, w->y + 16, w->w - 16, 85, COL_BLACK);

    /* Directory Path Header & Up [..] button */
    fill_rect(w->x + 10, w->y + 18, 18, 10, GRAY(18));
    draw_rect(w->x + 10, w->y + 18, 18, 10, GRAY(8));
    draw_str(w->x + 12, w->y + 19, "..", COL_BLACK, GRAY(18));

    const char *dname = (fileman_cur_dir && fileman_cur_dir->name[0]) ? fileman_cur_dir->name : "/";
    draw_str(w->x + 32, w->y + 19, dname, COL_BLUE, COL_WHITE);
    draw_hline(w->x + 8, w->y + 30, w->w - 16, GRAY(10));

    fs_node_t *dir = fileman_cur_dir ? fileman_cur_dir : fs_root();
    if (dir) {
        for (int i = 0; i < dir->child_count && i < 6; i++) {
            fs_node_t *c = dir->children[i];
            draw_str(w->x + 12, w->y + 34 + i * 9, c->type == FS_DIR ? "[DIR]" : "[FILE]", COL_DARK_GRAY, COL_WHITE);
            draw_str_clip(w->x + 52, w->y + 34 + i * 9, c->name, COL_BLACK, COL_WHITE, w->x + w->w - 10);
        }
    }
}

static void draw_notepad(gui_window_t *w)
{
    /* Action Bar: [Clear] [Save] */
    fill_rect(w->x + 8, w->y + 16, 36, 12, GRAY(18));
    draw_rect(w->x + 8, w->y + 16, 36, 12, GRAY(8));
    draw_str(w->x + 12, w->y + 18, "Clear", COL_BLACK, GRAY(18));

    fill_rect(w->x + 48, w->y + 16, 32, 12, GRAY(18));
    draw_rect(w->x + 48, w->y + 16, 32, 12, GRAY(8));
    draw_str(w->x + 52, w->y + 18, "Save", COL_BLACK, GRAY(18));

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

static void draw_cpanel(gui_window_t *w)
{
    draw_str(w->x + 10, w->y + 20, "ArchaOS v4.0 Settings", COL_BLACK, COL_WINBG);
    draw_str(w->x + 10, w->y + 32, "Themes:", COL_BLACK, COL_WINBG);
    static const char *tbtn[4] = {"Teal","Win31","Matrix","Amber"};
    for (int i=0; i<4; i++) {
        int tx = w->x + 10 + (i%2)*62;
        int ty = w->y + 42 + (i/2)*14;
        fill_rect(tx, ty, 58, 12, GRAY(18));
        draw_rect(tx, ty, 58, 12, GRAY(8));
        draw_str(tx + 4, ty + 2, tbtn[i], COL_BLACK, GRAY(18));
    }

    draw_str(w->x + 10, w->y + 74, "Wallpapers:", COL_BLACK, COL_WINBG);
    static const char *wpbtn[4] = {"Solid","Stars","Grid","Sun"};
    for (int i=0; i<4; i++) {
        int wx = w->x + 10 + (i%2)*62;
        int wy = w->y + 86 + (i/2)*14;
        fill_rect(wx, wy, 58, 12, GRAY(18));
        draw_rect(wx, wy, 58, 12, GRAY(8));
        draw_str(wx + 4, wy + 2, wpbtn[i], COL_BLACK, GRAY(18));
    }
}

/* ============================================================
 * DRAW WINDOW & DESKTOP
 * ============================================================ */

#define TASKBAR_Y  188
#define TASKBAR_H  12
#define COL_TASKBAR GRAY(3)

static int start_menu_open = 0;

static void draw_window(int idx)
{
    gui_window_t *w = &windows[idx];
    if (!w->visible || w->minimized) return;

    uint8_t title_bg = w->focused ? COL_BLUE : COL_DARK_GRAY;
    uint8_t border   = w->focused ? COL_TEAL : GRAY(8);

    /* Shadow */
    fill_rect(w->x+3, w->y+3, w->w, w->h, GRAY(2));
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

    /* Border */
    draw_rect(w->x, w->y, w->w, w->h, border);

    /* Render App Contents */
    switch (w->app) {
        case APP_CALC:       draw_calculator(w); break;
        case APP_PAINTER:    draw_painter(w); break;
        case APP_MINESWEEPER:draw_minesweeper(w); break;
        case APP_FILEMAN:    draw_fileman(w); break;
        case APP_NOTEPAD:    draw_notepad(w); break;
        case APP_CPANEL:     draw_cpanel(w); break;
        default: break;
    }
}

static void draw_start_menu(void)
{
    if (!start_menu_open) return;
    int mx = 2, my = TASKBAR_Y - 95, mw = 115, mh = 93;
    fill_rect(mx + 2, my + 2, mw, mh, GRAY(2));
    fill_rect(mx, my, mw, mh, COL_WINBG);
    draw_rect(mx, my, mw, mh, COL_BLACK);

    /* Intricate Vertical ArchaOS Banner */
    fill_rect(mx, my, 18, mh, COL_BLUE);
    draw_rect(mx, my, 18, mh, COL_TEAL);
    draw_char(mx + 6, my + 76, 'A', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 64, 'R', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 52, 'C', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 40, 'H', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 28, 'A', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 16, 'O', COL_WHITE, COL_BLUE);
    draw_char(mx + 6, my + 4,  'S', COL_WHITE, COL_BLUE);

    static const char *menu_items[6] = {
        "Calculator", "Painter", "Minesweeper", "File Manager", "Notepad", "Control Panel"
    };

    for (int i = 0; i < 6; i++) {
        draw_app_icon(mx + 22, my + 5 + i * 14, (app_type_t)(i + 1));
        draw_str(mx + 32, my + 5 + i * 14, menu_items[i], COL_BLACK, COL_WINBG);
        draw_hline(mx + 20, my + 17 + i * 14, mw - 22, GRAY(18));
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

    /* Clean Solid Black "ArchaOS v4.0" Watermark */
    draw_str(SCREEN_W - 74, TASKBAR_Y - 14, "ArchaOS v4.0", COL_BLACK, gui_desktop_color);
}

static void draw_alert_dialog(void)
{
    if (!alert_visible) return;
    int ax = 60, ay = 70, aw = 200, ah = 60;
    fill_rect(ax + 3, ay + 3, aw, ah, GRAY(2));
    fill_rect(ax, ay, aw, ah, COL_WINBG);
    draw_rect(ax, ay, aw, ah, COL_BLACK);

    fill_rect(ax, ay, aw, 12, COL_RED);
    draw_str(ax + 5, ay + 2, "Error", COL_WHITE, COL_RED);

    draw_str(ax + 10, ay + 22, alert_msg, COL_BLACK, COL_WINBG);

    fill_rect(ax + 80, ay + 42, 40, 14, GRAY(18));
    draw_rect(ax + 80, ay + 42, 40, 14, GRAY(8));
    draw_str(ax + 93, ay + 45, "OK", COL_BLACK, GRAY(18));
}

/* Mouse Pointer Graphic (Arrow) */
static const uint8_t MOUSE_SHAPE[11][11] = {
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

static void draw_mouse_cursor(int mx, int my)
{
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 11; c++) {
            uint8_t v = MOUSE_SHAPE[r][c];
            if (v == 1) put_pixel(mx + c, my + r, COL_BLACK);
            else if (v == 2) put_pixel(mx + c, my + r, COL_WHITE);
        }
}

static void redraw_all_frame(int mx, int my)
{
    draw_desktop();
    for (int i = win_count - 1; i >= 0; i--)
        if (i != focused_win) draw_window(i);

    if (focused_win >= 0 && focused_win < win_count)
        draw_window(focused_win);

    draw_start_menu();
    draw_processbar();
    draw_alert_dialog();
    draw_mouse_cursor(mx, my);

    gui_flip();
}

/* Open file with associated app */
static void open_associated_file(fs_node_t *f) {
    if (!f || f->type != FS_FILE) return;

    /* Check File Extension */
    int len = str_len(f->name);
    if (len >= 4 && (str_cmp(f->name + len - 4, ".txt") == 0 ||
                     str_cmp(f->name + len - 5, ".conf") == 0 ||
                     str_cmp(f->name + len - 2, ".c") == 0 ||
                     str_cmp(f->name + len - 2, ".h") == 0 ||
                     str_cmp(f->name + len - 4, ".asm") == 0 ||
                     str_cmp(f->name + len - 3, ".md") == 0)) {
        /* Open in Notepad */
        notepad_len = 0;
        if (f->data) {
            for (size_t i = 0; i < f->size && i < 500; i++) notepad_buf[i] = f->data[i];
            notepad_len = f->size < 500 ? f->size : 500;
        }
        notepad_buf[notepad_len] = '\0';
        windows[4].visible = 1;
        windows[4].minimized = 0;
        focused_win = 4;
    }
    else if (len >= 4 && (str_cmp(f->name + len - 4, ".bmp") == 0 ||
                          str_cmp(f->name + len - 4, ".png") == 0 ||
                          str_cmp(f->name + len - 4, ".jpg") == 0)) {
        /* Open in Painter */
        windows[1].visible = 1;
        windows[1].minimized = 0;
        focused_win = 1;
    }
    else {
        show_alert("Unknown File Extension!");
    }
}

/* Shift Key Scancode Conversion Table */
static char scancode_to_ascii(uint8_t sc, int shift) {
    if (sc & 0x80) return 0;
    switch (sc) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x39: return ' ';
        default: break;
    }
    static const char unshifted_alpha[128] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        'q','w','e','r','t','y','u','i','o','p',0,0,0,0,
        'a','s','d','f','g','h','j','k','l',0,0,0,0,0,
        'z','x','c','v','b','n','m',0,0,0,0,0
    };
    char ch = unshifted_alpha[sc];
    if (ch >= 'a' && ch <= 'z') {
        return shift ? (ch - 32) : ch;
    }
    return 0;
}

/* ============================================================
 * MAIN GUI LOOP
 * ============================================================ */

void gui_enter(void)
{
    font_backup_save(font_backup);
    set_mode13h();
    set_palette();
    mouse_init();

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
        uint32_t now_ticks = pit_ticks();
        if (now_ticks - last_clock >= 1000) {
            last_clock = now_ticks;
            redraw_all_frame(mouse_x, mouse_y);
        }

        /* 3-Second Auto Reset for Minesweeper on Loss */
        if (mine_game_over && mine_lost_ticks > 0 && now_ticks - mine_lost_ticks >= 3000) {
            minesweeper_reset();
            redraw_all_frame(mouse_x, mouse_y);
        }

        mx = mouse_x;
        my = mouse_y;
        uint8_t lclick = mouse_left_click;
        uint8_t rclick = mouse_right_click;

        /* Right-Click Flagging in Minesweeper */
        if (rclick && !prev_rclick && focused_win >= 0 && windows[focused_win].app == APP_MINESWEEPER && !windows[focused_win].minimized) {
            gui_window_t *w = &windows[focused_win];
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++) {
                    int gx = w->x + 12 + c * 11;
                    int gy = w->y + 34 + r * 8;
                    if (mx >= gx && mx < gx + 10 && my >= gy && my < gy + 7) {
                        if (mine_revealed[r][c] == 0) mine_revealed[r][c] = 2; /* Flag */
                        else if (mine_revealed[r][c] == 2) mine_revealed[r][c] = 0; /* Unflag */
                    }
                }
            redraw_all_frame(mx, my);
        }

        /* Left-Click Mouse Event Processing */
        if (mx != prev_mx || my != prev_my || lclick != prev_lclick)
        {
            if (lclick && !prev_lclick)
            {
                /* Close Alert Dialog */
                if (alert_visible) {
                    if (mx >= 140 && mx <= 180 && my >= 112 && my <= 126) alert_visible = 0;
                }
                /* Start Button Click */
                else if (my >= TASKBAR_Y && mx >= 2 && mx <= 56) {
                    start_menu_open = !start_menu_open;
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
                                    } else {
                                        if (focused_win >= 0 && focused_win < win_count)
                                            windows[focused_win].focused = 0;
                                        focused_win = i;
                                        windows[i].focused = 1;
                                        windows[i].minimized = 0;
                                    }
                                    break;
                                }
                                cur++;
                            }
                        }
                    }
                }
                /* Start Menu Item Click */
                else if (start_menu_open && mx >= 2 && mx <= 117 && my >= TASKBAR_Y - 95 && my < TASKBAR_Y) {
                    int item = (my - (TASKBAR_Y - 95)) / 14;
                    start_menu_open = 0;
                    if (item >= 0 && item <= 5) {
                        windows[item].visible = 1;
                        windows[item].minimized = 0;
                        if (focused_win >= 0 && focused_win < win_count)
                            windows[focused_win].focused = 0;
                        focused_win = item;
                        windows[focused_win].focused = 1;

                        if (item == 2) minesweeper_reset();
                    }
                }
                else
                {
                    start_menu_open = 0;

                    /* Check Window Clicks */
                    int clicked = -1;
                    for (int i = 0; i < win_count; i++) {
                        gui_window_t *w = &windows[i];
                        if (w->visible && !w->minimized && mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h) {
                            clicked = i; break;
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
                        }
                        /* Maximize button = */
                        else if (mx >= w->x + w->w - 16 && mx <= w->x + w->w - 9 && my >= w->y + 2 && my <= w->y + 10) {
                            if (!w->maximized) {
                                w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                                w->x = 0; w->y = 0; w->w = SCREEN_W; w->h = TASKBAR_Y;
                                w->maximized = 1;
                            } else {
                                w->x = w->saved_x; w->y = w->saved_y; w->w = w->saved_w; w->h = w->saved_h;
                                w->maximized = 0;
                            }
                        }
                        /* Split Screen button | */
                        else if (mx >= w->x + w->w - 24 && mx <= w->x + w->w - 17 && my >= w->y + 2 && my <= w->y + 10) {
                            if (w->split_state == 0) {
                                w->saved_x = w->x; w->saved_y = w->y; w->saved_w = w->w; w->saved_h = w->h;
                                w->x = 0; w->y = 0; w->w = 160; w->h = TASKBAR_Y;
                                w->split_state = 1;
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
                        }
                        /* Title bar drag */
                        else if (my >= w->y && my < w->y + 12 && !w->maximized) {
                            dragging_win = clicked;
                            drag_off_x = mx - w->x;
                            drag_off_y = my - w->y;
                        }
                        /* App Interactions */
                        else if (w->app == APP_CALC) {
                            for (int r = 0; r < 4; r++)
                                for (int c = 0; c < 4; c++) {
                                    int bx = w->x + 10 + c * 23;
                                    int by = w->y + 34 + r * 18;
                                    if (mx >= bx && mx < bx + 20 && my >= by && my < by + 15) {
                                        static const char bch[4][4] = {{'7','8','9','/'},{'4','5','6','*'},{'1','2','3','-'},{'0','C','=','+'}};
                                        char ch = bch[r][c];
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
                                            calc_val = calc_acc;
                                            itoa(calc_acc, calc_display, 10);
                                        }
                                    }
                                }
                        }
                        else if (w->app == APP_PAINTER) {
                            /* Clear */
                            if (mx >= w->x + 8 && mx <= w->x + 38 && my >= w->y + 16 && my <= w->y + 26) {
                                for (int i=0; i<140*65; i++) paint_canvas[i] = COL_WHITE;
                            }
                            /* Pencil */
                            else if (mx >= w->x + 42 && mx <= w->x + 74 && my >= w->y + 16 && my <= w->y + 26) {
                                paint_brush_size = 1; paint_eraser = 0;
                            }
                            /* Brush */
                            else if (mx >= w->x + 78 && mx <= w->x + 106 && my >= w->y + 16 && my <= w->y + 26) {
                                paint_brush_size = 3; paint_eraser = 0;
                            }
                            /* Erase */
                            else if (mx >= w->x + 110 && mx <= w->x + 142 && my >= w->y + 16 && my <= w->y + 26) {
                                paint_eraser = 1;
                            }
                            /* Color selection */
                            else if (my >= w->y + 85 && my <= w->y + 95) {
                                int pidx = (mx - (w->x + 10)) / 18;
                                static const uint8_t pcols[6] = { COL_BLACK, COL_RED, COL_GREEN, COL_BLUE, COL_YELLOW, COL_TEAL };
                                if (pidx >= 0 && pidx < 6) {
                                    paint_color = pcols[pidx];
                                    paint_eraser = 0;
                                }
                                prev_paint_x = -1; prev_paint_y = -1;
                            }
                        }
                        else if (w->app == APP_MINESWEEPER) {
                            for (int r = 0; r < 8; r++)
                                for (int c = 0; c < 8; c++) {
                                    int gx = w->x + 12 + c * 11;
                                    int gy = w->y + 34 + r * 8;
                                    if (mx >= gx && mx < gx + 10 && my >= gy && my < gy + 7) {
                                        mine_revealed[r][c] = 1;
                                        if (mine_grid[r][c] == 9) {
                                            mine_game_over = 1;
                                            mine_lost_ticks = pit_ticks();
                                        } else {
                                            minesweeper_check_win();
                                        }
                                    }
                                }
                        }
                        else if (w->app == APP_FILEMAN) {
                            /* Up [..] button */
                            if (mx >= w->x + 10 && mx <= w->x + 28 && my >= w->y + 18 && my <= w->y + 28) {
                                if (fileman_cur_dir && fileman_cur_dir->parent)
                                    fileman_cur_dir = fileman_cur_dir->parent;
                            }
                            /* Directory Listing Item Clicks */
                            else if (my >= w->y + 34 && my <= w->y + 88) {
                                int item_idx = (my - (w->y + 34)) / 9;
                                fs_node_t *dir = fileman_cur_dir ? fileman_cur_dir : fs_root();
                                if (dir && item_idx >= 0 && item_idx < dir->child_count) {
                                    fs_node_t *c = dir->children[item_idx];
                                    if (c->type == FS_DIR) fileman_cur_dir = c;
                                    else open_associated_file(c);
                                }
                            }
                        }
                        else if (w->app == APP_NOTEPAD) {
                            /* Clear button */
                            if (mx >= w->x + 8 && mx <= w->x + 44 && my >= w->y + 16 && my <= w->y + 28) {
                                notepad_len = 0;
                                notepad_buf[0] = '\0';
                            }
                            /* Save button */
                            else if (mx >= w->x + 48 && mx <= w->x + 80 && my >= w->y + 16 && my <= w->y + 28) {
                                fs_write("note.txt", notepad_buf, notepad_len);
                            }
                        }
                        else if (w->app == APP_CPANEL) {
                            if (my >= w->y + 42 && my <= w->y + 68) {
                                int tidx = (mx - (w->x + 10)) / 62 + ((my - (w->y + 42)) / 14) * 2;
                                if (tidx == 0) gui_desktop_color = RGB(0,3,3); /* Teal */
                                else if (tidx == 1) gui_desktop_color = RGB(0,1,4); /* Win31 Navy */
                                else if (tidx == 2) gui_desktop_color = RGB(0,3,0); /* Matrix Green */
                                else if (tidx == 3) gui_desktop_color = RGB(5,3,0); /* Amber Gold */
                                save_archaos_conf();
                            }
                            else if (my >= w->y + 86 && my <= w->y + 112) {
                                int widx = (mx - (w->x + 10)) / 62 + ((my - (w->y + 86)) / 14) * 2;
                                if (widx >= 0 && widx < 4) {
                                    gui_wallpaper_type = widx;
                                    save_archaos_conf();
                                }
                            }
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
                int cx = w->x + 8, cy = w->y + 28;
                if (mx >= cx + 1 && mx < cx + 139 && my >= cy + 1 && my < cy + 53) {
                    int cur_px = mx - (cx + 1);
                    int cur_py = my - (cy + 1);
                    if (prev_paint_x >= 0 && prev_paint_y >= 0) {
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
                dragging_win = -1;
                prev_paint_x = -1;
                prev_paint_y = -1;
            }

            prev_mx = mx;
            prev_my = my;
            prev_lclick = lclick;
            prev_rclick = rclick;
            redraw_all_frame(mx, my);
        }

        if (irq_kbd_fired) {
            uint8_t sc = last_scancode;
            irq_kbd_fired = 0;

            if (sc == 0x2A || sc == 0x36) shift_down = 1;
            else if (sc == 0xAA || sc == 0xB6) shift_down = 0;

            if (sc == 0x01) break; /* ESC exit */

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
            }
        }
        asm volatile("sti; hlt");
    }

    set_text_mode();
    restore_text_palette();

    vga_kbd_flush();
    vga_clear();
    /* Return to vga_prompt() which is already on the call stack —
     * do NOT call vga_prompt() here or it creates an infinite recursive loop. */
}
