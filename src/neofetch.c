// src/neofetch.c
#include "neofetch.h"
#include "vga.h"
#include "mm.h"
#include "kernel.h"
#include <stdint.h>

/* ASCII art logo — 8 rows */
static const char *LOGO[] = {
    "   ___           _           ___  ____  ",
    "  / _ |  _______/ /  ___ _  / _ \\/ __/  ",
    " / __ | / __/ __/ _ \\/ _` / / // /\\ \\    ",
    "/_/ |_|/_/  \\__/_//_/\\__,_//____/___/    ",
    "                                          ",
};
#define LOGO_LINES 5

void cmd_neofetch(void)
{
    mm_stats_t s = mm_stats();
    char buf[16];

    /* Logo + info side by side */
    vga_print("\n");

    /* Line 0: logo + OS */
    vga_print_color(LOGO[0], 0x0F);
    vga_print_color("  OS      ", 0x0A);
    vga_print("ArchaOS v0.4 \"IRQ\"\n");

    /* Line 1: logo + Kernel */
    vga_print_color(LOGO[1], 0x0F);
    vga_print_color("  Kernel  ", 0x0A);
    vga_print("x86 32-bit protected mode\n");

    /* Line 2: logo + Shell */
    vga_print_color(LOGO[2], 0x0F);
    vga_print_color("  Shell   ", 0x0A);
    vga_print("ArchaOS Shell\n");

    /* Line 3: logo + Memory */
    vga_print_color(LOGO[3], 0x0F);
    vga_print_color("  Memory  ", 0x0A);
    itoa((int)(s.used / 1024), buf, 10); vga_print(buf);
    vga_print("K used / ");
    itoa((int)(s.total / 1024), buf, 10); vga_print(buf);
    vga_print("K total\n");

    /* Line 4: logo + Author */
    vga_print_color(LOGO[4], 0x0F);
    vga_print_color("  Author  ", 0x0A);
    vga_print("Akshaj\n");

    /* Color palette */
    vga_print("\n  ");
    for (int i = 0; i < 8; i++) {
        uint8_t attr = (uint8_t)((i << 4) | i);
        vga_print_color("  ", attr);
    }
    vga_print("\n\n");
}
