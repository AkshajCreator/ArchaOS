// src/theme.h
#ifndef THEME_H
#define THEME_H
#include <stdint.h>

typedef struct {
    uint8_t prompt;
    uint8_t normal;
    uint8_t bright;
} theme_t;

extern theme_t current_theme;

void theme_set(const char *name);
void theme_list(void);

/* Default theme colors used by vga.c */
#define THEME_PROMPT  (current_theme.prompt)
#define THEME_NORMAL  (current_theme.normal)
#define THEME_BRIGHT  (current_theme.bright)

#endif
