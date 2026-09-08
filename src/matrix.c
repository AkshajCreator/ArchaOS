// src/matrix.c — ArchaOS Animated Matrix Digital Rain Screensaver
#include "matrix.h"
#include "vga.h"
#include "pit.h"
#include "idt.h"
#include "kernel.h"
#include <stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEM     ((volatile uint16_t *)0xB8000)

static uint32_t lcg_seed = 987654321;

static uint32_t lcg_rand(void)
{
    lcg_seed = lcg_seed * 1103515245 + 12345;
    return (lcg_seed >> 16) & 0x7FFF;
}

static char get_matrix_glyph(void)
{
    static const char GLYPHS[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "!#$%&*+-/<=>?@[]^{}~";
    return GLYPHS[lcg_rand() % (sizeof(GLYPHS) - 1)];
}

typedef struct {
    int y;          /* Current head row position */
    int length;     /* Length of the fading trail */
    int speed;      /* Speed divisor (1 = fastest, 3 = slower) */
    int delay;      /* Remaining ticks before step */
} matrix_col_t;

void cmd_matrix(void)
{
    matrix_col_t cols[VGA_WIDTH];

    /* Initialize seed with timer ticks */
    lcg_seed ^= pit_ticks();

    /* Clear screen to solid black */
    vga_clear();

    /* Hide cursor */
    vga_set_cursor(VGA_WIDTH, VGA_HEIGHT);

    /* Initialize columns */
    for (int x = 0; x < VGA_WIDTH; x++) {
        cols[x].length = 6 + (lcg_rand() % 12);
        cols[x].speed  = 1 + (lcg_rand() % 3);
        cols[x].delay  = lcg_rand() % 8;
        cols[x].y      = -(lcg_rand() % 20);
    }

    /* Flush any pending keystrokes (e.g. Enter keypress) */
    vga_kbd_flush();
    irq_kbd_fired = 0;
    last_scancode = 0;

    uint32_t start_tick = pit_ticks();

    /* Animation loop */
    while (1) {
        /* Check if a key was pressed */
        if (irq_kbd_fired) {
            irq_kbd_fired = 0;
            uint8_t sc = last_scancode;
            /* Ignore the Enter key (press or release) that launched the command */
            if (sc == 0x9C || sc == 0x1C) {
                continue;
            }
            /* Grace period of 250ms: ignore any key bounce while finger lifts off keyboard */
            if (pit_ticks() - start_tick < 250) {
                continue;
            }
            if (sc != 0 && sc != 0xE0) {
                vga_kbd_flush();
                break;
            }
        }

        for (int x = 0; x < VGA_WIDTH; x++) {
            if (cols[x].delay > 0) {
                cols[x].delay--;
                continue;
            }
            cols[x].delay = cols[x].speed;

            int head_y = cols[x].y;
            int tail_y = head_y - cols[x].length;

            /* Erase tail character */
            if (tail_y >= 0 && tail_y < VGA_HEIGHT) {
                VGA_MEM[tail_y * VGA_WIDTH + x] = (0x00 << 8) | ' ';
            }

            /* Dark green tail fade */
            int mid_start = head_y - 3;
            if (mid_start >= 0 && mid_start < VGA_HEIGHT) {
                uint16_t cur = VGA_MEM[mid_start * VGA_WIDTH + x];
                VGA_MEM[mid_start * VGA_WIDTH + x] = (0x02 << 8) | (cur & 0xFF);
            }

            /* Bright green intermediate trail */
            int trail_y = head_y - 1;
            if (trail_y >= 0 && trail_y < VGA_HEIGHT) {
                uint16_t cur = VGA_MEM[trail_y * VGA_WIDTH + x];
                VGA_MEM[trail_y * VGA_WIDTH + x] = (0x0A << 8) | (cur & 0xFF);
            }

            /* Draw bright white leading head */
            if (head_y >= 0 && head_y < VGA_HEIGHT) {
                char ch = get_matrix_glyph();
                VGA_MEM[head_y * VGA_WIDTH + x] = (0x0F << 8) | (uint8_t)ch;
            }

            /* Advance column */
            cols[x].y++;

            /* Reset column once entire tail falls off the screen */
            if (tail_y >= VGA_HEIGHT) {
                cols[x].length = 6 + (lcg_rand() % 12);
                cols[x].speed  = 1 + (lcg_rand() % 3);
                cols[x].delay  = lcg_rand() % 6;
                cols[x].y      = -(lcg_rand() % 10);
            }
        }

        /* Random glyph flickering on existing active characters */
        for (int i = 0; i < 5; i++) {
            int rx = lcg_rand() % VGA_WIDTH;
            int ry = lcg_rand() % VGA_HEIGHT;
            uint16_t cell = VGA_MEM[ry * VGA_WIDTH + rx];
            uint8_t attr = (cell >> 8) & 0xFF;
            if (attr == 0x0A || attr == 0x02) {
                VGA_MEM[ry * VGA_WIDTH + rx] = (attr << 8) | (uint8_t)get_matrix_glyph();
            }
        }

        pit_sleep(25);
    }

    /* Clean exit: clear screen and restore cursor */
    vga_clear();
    vga_set_cursor(0, 0);
}
