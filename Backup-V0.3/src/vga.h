#ifndef VGA_H
#define VGA_H

void vga_clear(void);

void vga_print(const char *str);

void vga_print_char(char c);

void vga_print_center(const char *str);

void vga_set_cursor(int x, int y);

void vga_show_welcome(void);

void vga_prompt(void);

void vga_scroll(void);

void vga_ensure_visible(void);

void vga_execute_command(const char *cmd);

#endif
