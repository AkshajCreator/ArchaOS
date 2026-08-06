#pragma once
#include <stdint.h>

void keyboard_init(void);
int  keyboard_has_char(void);
char keyboard_get_char(void);
void keyboard_isr(void);
