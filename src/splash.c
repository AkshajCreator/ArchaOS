// src/splash.c — ArchaOS boot splash
// Windows 1.x style: 4-corner convergence animation, green on black,
// CRT feel via phosphor-style two-pass reveal + scanline border.

#include "splash.h"
#include "pit.h"
#include <stdint.h>

/* ── VGA ── */
static uint16_t *vga = (uint16_t *)0xB8000;
#define VGA_W       80
#define VGA_H       25
#define ATTR_BRIGHT 0x0A   /* bright green — fully converged pixel */
#define ATTR_DIM    0x02   /* dark green   — phosphor trail        */
#define ATTR_BORDER 0x02
#define ATTR_VER    0x02

static void splash_putc(int x, int y, char c, uint8_t attr)
{
    if (x<0||x>=VGA_W||y<0||y>=VGA_H) return;
    vga[y*VGA_W+x] = ((uint16_t)(unsigned char)c)|((uint16_t)attr<<8);
}

static void splash_clear(void)
{
    for (int i=0;i<VGA_W*VGA_H;i++)
        vga[i] = ((uint16_t)' ')|((uint16_t)0x00<<8);
}

static void splash_str(int x, int y, const char *s, uint8_t attr)
{ while (*s) splash_putc(x++,y,*s++,attr); }

/* ── Letter pixel maps 5x7 ── */
static const uint8_t L_A[7]={0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001};
static const uint8_t L_r[7]={0b00000,0b00000,0b01110,0b10001,0b10000,0b10000,0b10000};
static const uint8_t L_c[7]={0b00000,0b00000,0b01110,0b10001,0b10000,0b10001,0b01110};
static const uint8_t L_h[7]={0b10000,0b10000,0b11110,0b10001,0b10001,0b10001,0b10001};
static const uint8_t L_a[7]={0b00000,0b00000,0b01110,0b00001,0b01111,0b10001,0b01111};
static const uint8_t L_O[7]={0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110};
static const uint8_t L_S[7]={0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110};

static const uint8_t *LETTERS[7]={L_A,L_r,L_c,L_h,L_a,L_O,L_S};
#define LETTER_W    5
#define LETTER_GAP  1
#define LETTER_H    7
#define NUM_LETTERS 7
#define LOGO_W  (NUM_LETTERS*(LETTER_W+LETTER_GAP)-LETTER_GAP)
#define LOGO_H  LETTER_H
#define LOGO_X  ((VGA_W-LOGO_W)/2)
#define LOGO_Y  ((VGA_H-LOGO_H)/2 - 1)

/* ── Pixel list ── */
#define MAX_PIXELS 256
typedef struct { int fx, fy; } pixel_t;
static pixel_t pixels[MAX_PIXELS];
static int     pixel_count = 0;

static void build_pixels(void)
{
    pixel_count = 0;
    for (int l=0;l<NUM_LETTERS;l++) {
        int lx = LOGO_X + l*(LETTER_W+LETTER_GAP);
        for (int row=0;row<LETTER_H;row++) {
            uint8_t mask = LETTERS[l][row];
            for (int col=0;col<LETTER_W;col++) {
                if (mask&(1<<(LETTER_W-1-col))) {
                    if (pixel_count<MAX_PIXELS) {
                        pixels[pixel_count].fx = lx+col;
                        pixels[pixel_count].fy = LOGO_Y+row;
                        pixel_count++;
                    }
                }
            }
        }
    }
}

/* ── Linear interpolation ── */
#define CX (VGA_W/2)
#define CY (VGA_H/2)

static int lerp(int a, int b, int t, int total)
{ return a + (b-a)*t/total; }

/* ── Border ── */
static void draw_border(void)
{
    for (int x=0;x<VGA_W;x++) {
        splash_putc(x,0,      '\xC4',ATTR_BORDER);
        splash_putc(x,VGA_H-1,'\xC4',ATTR_BORDER);
    }
    for (int y=0;y<VGA_H;y++) {
        splash_putc(0,      y,'\xB3',ATTR_BORDER);
        splash_putc(VGA_W-1,y,'\xB3',ATTR_BORDER);
    }
    splash_putc(0,      0,      '\xDA',ATTR_BORDER);
    splash_putc(VGA_W-1,0,      '\xBF',ATTR_BORDER);
    splash_putc(0,      VGA_H-1,'\xC0',ATTR_BORDER);
    splash_putc(VGA_W-1,VGA_H-1,'\xD9',ATTR_BORDER);
}

/* ── Version right-aligned ── */
static const char *VERSION = "v4.0 \"Phosphor\"";
static void draw_version(void)
{
    int vlen=0; while (VERSION[vlen]) vlen++;
    splash_str(VGA_W-vlen-2, VGA_H-2, VERSION, ATTR_VER);
}

/* ── CRT phosphor draw:
 *  Pass 1 — dim trail (pixels at ~75% of their journey)
 *  Pass 2 — bright final position
 *  This gives the "phosphor smear" look of a CRT electron gun
 * ── */
#define FRAMES     20
#define FRAME_MS   50
#define TRAIL_LEAD  3   /* how many frames ahead the trail is */

static void draw_crt_frame(int frame)
{
    splash_clear();

    for (int i=0;i<pixel_count;i++) {
        int fx = pixels[i].fx;
        int fy = pixels[i].fy;

        /* Corner source for this pixel */
        int sx = (fx < CX) ? 0 : VGA_W-1;
        int sy = (fy < CY) ? 0 : VGA_H-1;

        /* Bright dot — current frame */
        int bx = lerp(sx, fx, frame, FRAMES);
        int by = lerp(sy, fy, frame, FRAMES);
        splash_putc(bx, by, '\xDB', ATTR_BRIGHT);

        /* Dim trail — a few frames behind */
        if (frame >= TRAIL_LEAD) {
            int tx = lerp(sx, fx, frame - TRAIL_LEAD, FRAMES);
            int ty = lerp(sy, fy, frame - TRAIL_LEAD, FRAMES);
            /* Only draw trail if it's not on top of bright dot */
            if (tx != bx || ty != by)
                splash_putc(tx, ty, '\xB1', ATTR_DIM);
        }
    }

    draw_border();
}

/* ── Public ── */
void splash_show(void)
{
    build_pixels();
    splash_clear();

    /* Animate with CRT phosphor effect */
    for (int f=0; f<=FRAMES; f++) {
        draw_crt_frame(f);
        pit_sleep(FRAME_MS);
    }

    /* Final frame — all bright, no trail */
    splash_clear();
    for (int i=0;i<pixel_count;i++)
        splash_putc(pixels[i].fx, pixels[i].fy, '\xDB', ATTR_BRIGHT);

    draw_version();
    draw_border();

    /* Hold 2 seconds */
    pit_sleep(2000);
    splash_clear();
}
