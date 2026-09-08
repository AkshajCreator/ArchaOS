// src/splash.c — ArchaOS Mode 13h (320x200) Pixel-Art Graphical Boot Splash
#include "splash.h"
#include "pit.h"
#include "vga.h"
#include "font.h"
#include "audio.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#define SCREEN_W   320
#define SCREEN_H   200
#define FB_ADDR    0xA0000

/* 64 KB Off-screen backbuffer for smooth flicker-free rendering */
static uint8_t splash_buf[SCREEN_W * SCREEN_H];
static uint8_t splash_font_backup[256 * 32];

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline int splash_check_key(void)
{
    uint8_t st = inb(0x64);
    if ((st & 0x01) && !(st & 0x20)) {
        inb(0x60); /* discard scancode */
        return 1;
    }
    return 0;
}

/* Fast 32-bit memcpy to 0xA0000 linear framebuffer */
static inline void splash_flip(void)
{
    uint32_t *dst = (uint32_t *)FB_ADDR;
    uint32_t *src = (uint32_t *)splash_buf;
    asm volatile(
        "cld; rep movsl"
        : "+D"(dst), "+S"(src)
        : "c"((SCREEN_W * SCREEN_H) / 4)
        : "memory"
    );
}

/* DAC Palette Programmer (6-bit RGB components: 0 to 63) */
static void set_dac(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    outb(0x3C8, idx);
    outb(0x3C9, r > 63 ? 63 : r);
    outb(0x3C9, g > 63 ? 63 : g);
    outb(0x3C9, b > 63 ? 63 : b);
}

/* Setup custom cyberpunk palette */
static void setup_splash_palette(void)
{
    /* 0: Background: Pitch Obsidian */
    set_dac(0, 1, 2, 4);

    /* 1-8: Deep Navy / Midnight Blue Vignette Ramp */
    set_dac(1, 2, 5, 11);
    set_dac(2, 3, 7, 16);
    set_dac(3, 5, 11, 24);
    set_dac(4, 7, 15, 32);
    set_dac(5, 10, 20, 42);
    set_dac(6, 14, 27, 50);
    set_dac(7, 18, 35, 58);
    set_dac(8, 22, 42, 63);

    /* 9-15: Electric Neon Cyan & Aqua (Emblem & Glow) */
    set_dac(9,  0, 25, 38);
    set_dac(10, 0, 36, 48);
    set_dac(11, 0, 48, 58);
    set_dac(12, 10, 56, 63);
    set_dac(13, 24, 60, 63);
    set_dac(14, 42, 63, 63);
    set_dac(15, 56, 63, 63);

    /* 16-23: Emerald Matrix Green ([ OK ] Indicators) */
    set_dac(16, 0, 20, 5);
    set_dac(17, 0, 32, 8);
    set_dac(18, 2, 44, 12);
    set_dac(19, 8, 54, 18);
    set_dac(20, 20, 63, 28);
    set_dac(21, 36, 63, 42);
    set_dac(22, 50, 63, 52);
    set_dac(23, 60, 63, 60);

    /* 24-31: Monochromatic Text & Highlight Ramp */
    set_dac(24, 10, 10, 12);
    set_dac(25, 18, 18, 22);
    set_dac(26, 26, 26, 30);
    set_dac(27, 36, 36, 40);
    set_dac(28, 46, 46, 50);
    set_dac(29, 54, 54, 58);
    set_dac(30, 59, 59, 61);
    set_dac(31, 63, 63, 63); /* Radiant Pure White */
}

/* ============================================================
 * PIXEL RENDERING PRIMITIVES
 * ============================================================ */

static inline void splash_put_pixel(int x, int y, uint8_t color)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        splash_buf[y * SCREEN_W + x] = color;
    }
}

static void splash_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    for (int r = 0; r < h; r++) {
        uint8_t *row = &splash_buf[(y + r) * SCREEN_W + x];
        for (int c = 0; c < w; c++) {
            row[c] = color;
        }
    }
}

static void splash_draw_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    for (int c = 0; c < w; c++) {
        splash_put_pixel(x + c, y, color);
        splash_put_pixel(x + c, y + h - 1, color);
    }
    for (int r = 0; r < h; r++) {
        splash_put_pixel(x, y + r, color);
        splash_put_pixel(x + w - 1, y + r, color);
    }
}

static void splash_draw_line(int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        splash_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void splash_draw_char(int x, int y, char c, uint8_t color)
{
    if (c < 32 || c > 126) return;
    int idx = c - 32;
    for (int col = 0; col < 5; col++) {
        uint8_t line = FONT_5X7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                splash_put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void splash_draw_char_2x(int x, int y, char c, uint8_t color)
{
    if (c < 32 || c > 126) return;
    int idx = c - 32;
    for (int col = 0; col < 5; col++) {
        uint8_t line = FONT_5X7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                splash_fill_rect(x + col * 2, y + row * 2, 2, 2, color);
            }
        }
    }
}

static void splash_draw_str(int x, int y, const char *s, uint8_t color)
{
    while (*s) {
        splash_draw_char(x, y, *s++, color);
        x += 6;
    }
}

static void splash_draw_str_2x(int x, int y, const char *s, uint8_t color)
{
    while (*s) {
        splash_draw_char_2x(x, y, *s++, color);
        x += 12;
    }
}

/* ============================================================
 * PIXEL-ART ARCHAOS SHIELD & EMBLEM
 * ============================================================ */

static void splash_draw_emblem(int cx, int cy)
{
    /* Background subtle halo behind shield */
    for (int dy = -16; dy <= 16; dy++) {
        int span = 24 - ((dy < 0 ? -dy : dy) / 2);
        for (int dx = -span; dx <= span; dx++) {
            splash_put_pixel(cx + dx, cy + dy, 2);
        }
    }

    /* Circuit Trace Lines radiating outwards */
    /* Left bus */
    splash_draw_line(cx - 24, cy, cx - 50, cy, 9);
    splash_draw_line(cx - 50, cy, cx - 62, cy - 10, 10);
    splash_fill_rect(cx - 64, cy - 12, 4, 4, 12);
    splash_put_pixel(cx - 63, cy - 11, 31); /* Glowing center node */

    splash_draw_line(cx - 40, cy, cx - 48, cy + 8, 9);
    splash_fill_rect(cx - 50, cy + 7, 3, 3, 11);

    /* Right bus */
    splash_draw_line(cx + 24, cy, cx + 50, cy, 9);
    splash_draw_line(cx + 50, cy, cx + 62, cy - 10, 10);
    splash_fill_rect(cx + 61, cy - 12, 4, 4, 12);
    splash_put_pixel(cx + 62, cy - 11, 31); /* Glowing center node */

    splash_draw_line(cx + 40, cy, cx + 48, cy + 8, 9);
    splash_fill_rect(cx + 48, cy + 7, 3, 3, 11);

    /* Outer Shield Diamond / Hexagon Border (Double Layer Neon) */
    /* Layer 1: Outer cyan frame */
    splash_draw_line(cx - 14, cy - 18, cx + 14, cy - 18, 11);
    splash_draw_line(cx - 24, cy - 8,  cx - 14, cy - 18, 11);
    splash_draw_line(cx + 24, cy - 8,  cx + 14, cy - 18, 11);
    splash_draw_line(cx - 24, cy - 8,  cx - 24, cy + 4,  11);
    splash_draw_line(cx + 24, cy - 8,  cx + 24, cy + 4,  11);
    splash_draw_line(cx - 24, cy + 4,  cx,      cy + 18, 11);
    splash_draw_line(cx + 24, cy + 4,  cx,      cy + 18, 11);

    /* Layer 2: Bright inner frame */
    splash_draw_line(cx - 13, cy - 17, cx + 13, cy - 17, 14);
    splash_draw_line(cx - 23, cy - 8,  cx - 13, cy - 17, 14);
    splash_draw_line(cx + 23, cy - 8,  cx + 13, cy - 17, 14);
    splash_draw_line(cx - 23, cy - 8,  cx - 23, cy + 3,  14);
    splash_draw_line(cx + 23, cy - 8,  cx + 23, cy + 3,  14);
    splash_draw_line(cx - 23, cy + 3,  cx,      cy + 17, 14);
    splash_draw_line(cx + 23, cy + 3,  cx,      cy + 17, 14);

    /* Center Emblem: Stylized Geometric "A" with White Core */
    /* Left leg */
    splash_draw_line(cx - 1, cy - 12, cx - 12, cy + 10, 13);
    splash_draw_line(cx,     cy - 12, cx - 11, cy + 10, 31);
    splash_draw_line(cx + 1, cy - 12, cx - 10, cy + 10, 14);

    /* Right leg */
    splash_draw_line(cx - 1, cy - 12, cx + 10, cy + 10, 14);
    splash_draw_line(cx,     cy - 12, cx + 11, cy + 10, 31);
    splash_draw_line(cx + 1, cy - 12, cx + 12, cy + 10, 13);

    /* Horizontal Crossbar */
    splash_draw_line(cx - 7, cy + 1, cx + 7, cy + 1, 31);
    splash_draw_line(cx - 6, cy + 2, cx + 6, cy + 2, 12);

    /* Glowing Core Diamond */
    splash_put_pixel(cx, cy - 1, 31);
    splash_put_pixel(cx - 1, cy, 31);
    splash_put_pixel(cx + 1, cy, 31);
    splash_put_pixel(cx, cy + 1, 31);
}

/* ============================================================
 * DIAGNOSTICS & SYSTEM STATUS DEFINITIONS
 * ============================================================ */

typedef struct {
    const char *subsystem;
    const char *desc;
} diag_step_t;

static const diag_step_t DIAG_STEPS[5] = {
    { "CPU ", "Intel x86 Ring 0 Protected Core" },
    { "MEM ", "Physical PMM & 8MB Dynamic Heap" },
    { "NET ", "Intel E1000 Gigabit NIC & TCP/IP" },
    { "SND ", "Sound Blaster 16 & Synth Engine" },
    { "VFS ", "Hierarchical RAM & IDE Storage" }
};

static const uint16_t CHIME_FREQS[5] = { 440, 554, 659, 880, 1108 };

/* ============================================================
 * MAIN GRAPHICAL BOOT SPLASH
 * ============================================================ */

void splash_show(void)
{
    serial_puts(COM1_BASE, "\n=== ArchaOS Graphical Boot Splash (Mode 13h) Started ===\n");

    /* 1. Backup Text Mode Font Plane */
    vga_font_backup_save(splash_font_backup);

    /* 2. Transition hardware to VGA Mode 13h (320x200 256 colors) */
    vga_set_mode13h();
    setup_splash_palette();

    /* 3. Base Frame Rendering into Backbuffer */
    splash_fill_rect(0, 0, SCREEN_W, SCREEN_H, 0);

    /* Top decorative glow lines */
    splash_fill_rect(0, 0, SCREEN_W, 1, 4);
    splash_fill_rect(0, 1, SCREEN_W, 1, 6);
    splash_fill_rect(0, 2, SCREEN_W, 1, 3);

    /* Retro starry dust in deep navy */
    splash_put_pixel(28, 18, 4);
    splash_put_pixel(72, 28, 3);
    splash_put_pixel(50, 48, 5);
    splash_put_pixel(248, 22, 4);
    splash_put_pixel(285, 34, 5);
    splash_put_pixel(270, 52, 3);
    splash_put_pixel(15, 75, 4);
    splash_put_pixel(302, 80, 4);

    /* Draw Central Pixel-Art Shield & Radiating Circuits */
    splash_draw_emblem(160, 36);

    /* Typography: Centered Title with Drop Shadow */
    splash_draw_str_2x(95, 61, "A R C H A O S", 2);
    splash_draw_str_2x(94, 60, "A R C H A O S", 31);

    /* Cyan Accent Divider */
    splash_fill_rect(76, 75, 168, 1, 12);
    splash_put_pixel(75, 75, 9);
    splash_put_pixel(244, 75, 9);

    /* Subtitles */
    splash_draw_str(88, 80, "QUANTUM MICROKERNEL v0.5", 13);
    splash_draw_str(46, 90, "32-Bit Protected Mode Operating System", 27);

    /* Diagnostics Box Outer Frame (w=288 centered with 16px margins) */
    int db_x = 16, db_y = 102, db_w = 288, db_h = 58;
    splash_fill_rect(db_x, db_y, db_w, db_h, 1);
    splash_draw_rect(db_x, db_y, db_w, db_h, 3);
    splash_draw_line(db_x, db_y, db_x + db_w - 1, db_y, 6); /* Bevel highlight */

    /* Progress Bar Cavity */
    int pb_x = 16, pb_y = 166, pb_w = 288, pb_h = 8;
    splash_draw_rect(pb_x, pb_y, pb_w, pb_h, 26);
    splash_fill_rect(pb_x + 1, pb_y + 1, pb_w - 2, pb_h - 2, 0);

    /* Bottom Skip Hint */
    splash_draw_str(44, 189, "Press ANY KEY to skip directly to shell", 25);

    splash_flip();

    /* 4. Sequential Animated Diagnostic Steps */
    int current_pct = 0;

    for (int step = 0; step < 5; step++) {
        if (splash_check_key()) goto splash_done;

        /* Render Diagnostic Entry */
        int entry_y = db_y + 4 + step * 10;
        splash_draw_str(db_x + 6, entry_y, "[", 26);
        splash_draw_str(db_x + 12, entry_y, "OK", 20); /* Bright Emerald Green */
        splash_draw_str(db_x + 24, entry_y, "]", 26);
        splash_draw_str(db_x + 34, entry_y, DIAG_STEPS[step].subsystem, 14);
        splash_draw_str(db_x + 64, entry_y, DIAG_STEPS[step].desc, 30);

        /* Target percentage for this step */
        int target_pct = (step + 1) * 20;

        /* Play synchronized audio chime tone */
        audio_play_freq(CHIME_FREQS[step]);

        /* Smoothly interpolate progress bar pixels to target percentage */
        while (current_pct < target_pct) {
            if (splash_check_key()) goto splash_done;
            current_pct++;

            int fill_max = pb_w - 4;
            int fill_w = (current_pct * fill_max) / 100;
            if (fill_w > fill_max) fill_w = fill_max;

            /* Render gradient progress bar */
            if (fill_w > 0) {
                /* Top row: cyan highlight */
                splash_fill_rect(pb_x + 2, pb_y + 2, fill_w, 1, 14);
                /* Middle body: electric cyan */
                splash_fill_rect(pb_x + 2, pb_y + 3, fill_w, 2, 11);
                /* Bottom shadow: cobalt blue */
                splash_fill_rect(pb_x + 2, pb_y + 5, fill_w, 1, 4);

                /* Glowing White Leading-Edge Pulse Glare (last 4 pixels) */
                int pulse_w = fill_w > 4 ? 4 : fill_w;
                splash_fill_rect(pb_x + 2 + fill_w - pulse_w, pb_y + 2, pulse_w, 4, 31);
            }

            /* Update Percentage and Status Label */
            splash_fill_rect(pb_x, pb_y + 11, pb_w, 8, 0);
            char pct_str[8];
            pct_str[0] = (current_pct / 100) ? ('0' + (current_pct / 100)) : ' ';
            pct_str[1] = (current_pct >= 10) ? ('0' + ((current_pct / 10) % 10)) : ' ';
            pct_str[2] = '0' + (current_pct % 10);
            pct_str[3] = '%';
            pct_str[4] = '\0';
            splash_draw_str(pb_x + pb_w - 28, pb_y + 11, pct_str, 14);

            const char *status_msg = (current_pct < 40) ? "Probing System Hardware..." :
                                     (current_pct < 80) ? "Loading Kernel Drivers..." :
                                     (current_pct < 100) ? "Initializing Network Stack..." :
                                     "System Ready. Launching Shell...";
            splash_draw_str(pb_x, pb_y + 11, status_msg, 28);

            splash_flip();
            pit_sleep(6);
        }

        /* Silence speaker after brief note */
        pit_sleep(25);
        audio_stop();
        pit_sleep(35);
    }

    /* Final brief pause at 100% */
    for (int wait = 0; wait < 6; wait++) {
        if (splash_check_key()) break;
        pit_sleep(50);
    }

splash_done:
    audio_stop();

    /* 5. Clean Restoration to VGA 80x25 Text Mode */
    vga_set_text_mode();
    vga_restore_font_and_text(splash_font_backup);
    vga_restore_text_palette();

    vga_kbd_flush();
    vga_clear();
    vga_set_cursor(0, 0);
    serial_puts(COM1_BASE, "=== ArchaOS Graphical Boot Splash Finished ===\n");
}
