// src/theme.c
#include "theme.h"
#include "vga.h"

/* Default: green */
theme_t current_theme = { 0x0A, 0x07, 0x0F };

static int streq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }

void theme_set(const char *name)
{
    if (streq(name, "green")) {
        current_theme = (theme_t){ 0x0A, 0x07, 0x0F };
        vga_print("Theme: green\n");
    } else if (streq(name, "amber")) {
        current_theme = (theme_t){ 0x0E, 0x06, 0x0E };
        vga_print("Theme: amber\n");
    } else if (streq(name, "blue")) {
        current_theme = (theme_t){ 0x0B, 0x07, 0x0F };
        vga_print("Theme: blue\n");
    } else if (streq(name, "red")) {
        current_theme = (theme_t){ 0x0C, 0x07, 0x0F };
        vga_print("Theme: red\n");
    } else if (streq(name, "white")) {
        current_theme = (theme_t){ 0x0F, 0x07, 0x0F };
        vga_print("Theme: white\n");
    } else {
        vga_print("Unknown theme. Try: green amber blue red white\n");
    }
}

void theme_list(void)
{
    vga_print("Available themes: green  amber  blue  red  white\n");
}
