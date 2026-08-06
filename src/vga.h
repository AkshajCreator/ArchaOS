#ifndef VGA_H
#define VGA_H

#include <stdint.h>

/* Screen dimensions */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* Color attributes */
#define ATTR(fg, bg)        (((bg) << 4) | (fg))
#define ATTR_NORMAL         ATTR(0xF, 0x0)   /* white on black */
#define ATTR_PROMPT         ATTR(0xA, 0x0)   /* bright green on black */
#define ATTR_BRIGHT_GREEN   ATTR(0xA, 0x0)
#define ATTR_GREEN          ATTR(0x2, 0x0)

void vga_clear(void);
void vga_print(const char *str);
void vga_print_char(char c);
void vga_print_color(const char *str, uint8_t attr);
void vga_print_center(const char *str);
void vga_set_cursor(int x, int y);
void vga_show_welcome(void);
void vga_prompt(void);
void vga_scroll(void);
void vga_scroll_up(int lines);
void vga_scroll_down(int lines);
int  vga_in_scrollback(void);
void vga_ensure_visible(void);
void vga_execute_command(const char *cmd);
void vga_kbd_flush(void);

#endif
