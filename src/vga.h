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

#include "multiboot.h"

void vga_write_cell(int x, int y, uint16_t cell);
uint16_t vga_read_cell(int x, int y);
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
void vga_execute_command(const char *cmd);
void vga_kbd_flush(void);
void vga_get_input(const char *prompt_str, char *out_buf, int max_len);

/* Hardware VGA Mode Switching (Mode 13h 320x200 256-color & Text Mode) */
void vga_set_mode13h(void);
void vga_set_text_mode(void);
void vga_font_backup_save(uint8_t *font_buf);
void vga_restore_text_palette(void);
void vga_restore_font_and_text(uint8_t *font_buf);

#endif
