#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void    keyboard_init(void);
void    keyboard_handle_scancode(uint8_t sc);
uint8_t keyboard_get_scancode(void);
char    keyboard_scancode_to_ascii(uint8_t sc, int shift, int caps);
char    keyboard_getchar(void);
int     keyboard_has_char(void);

int     keyboard_is_shift(void);
int     keyboard_is_ctrl(void);
int     keyboard_is_caps(void);
int     keyboard_is_alt(void);

#endif
