// src/gui.h
#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stddef.h>

#define MAX_WINDOWS 24

/* Application Types */
typedef enum {
    APP_CALC = 1,
    APP_PAINTER,
    APP_MINESWEEPER,
    APP_FILEMAN,
    APP_NOTEPAD,
    APP_CPANEL,
    APP_TASKMAN,
    APP_IMGVIEW,
    APP_SNAKE,
    APP_CLI,
    APP_CODESTUDIO,
    APP_CONSOLE,
    APP_BROWSER,
    APP_COREVIEW,
    APP_USER_WIN
} app_type_t;

/* GUI Raw and Widget Event Types */
#define GUI_EVENT_NONE             0
#define GUI_EVENT_MOUSE_DOWN       1
#define GUI_EVENT_MOUSE_UP         2
#define GUI_EVENT_KEY_DOWN         3
#define GUI_EVENT_KEY_UP           4
#define GUI_EVENT_MOUSE_MOVE       5
#define GUI_EVENT_CLOSE            6
#define GUI_EVENT_BUTTON_CLICK     7
#define GUI_EVENT_TEXT_CHANGE      8
#define GUI_EVENT_CHECKBOX_TOGGLE  9

/* GUI Raw Event Structure */
typedef struct {
    int     type;
    char    c;
    uint8_t scancode;
    uint8_t _pad[2];
    int     rx;
    int     ry;
    int     x;
    int     y;
    int     mx;
    int     my;
    int     buttons;
} gui_raw_event_t;

/* Syscall GUI Draw Rect Argument Structure */
typedef struct {
    int     x;
    int     y;
    int     w;
    int     h;
    uint8_t color;
    uint8_t _pad[3];
} gui_draw_rect_args_t;

/* Window Structure */
typedef struct {
    int x, y, w, h;
    int client_w, client_h;
    int saved_x, saved_y, saved_w, saved_h;
    const char *title;
    const char *short_title;
    app_type_t app;
    int visible;    /* 1: open, 0: closed */
    int minimized;  /* 1: minimized to ProcessBar, 0: on desktop */
    int focused;
    int maximized;
    int split_state;
    int workspace;       /* 0: Workspace 1, 1: Workspace 2 */
    uint32_t open_tick;  /* tick when window was opened (for open animation) */

    /* User Window Canvas and Event Queue */
    uint8_t *user_canvas;
    uint32_t *user_canvas32;
    int user_canvas32_w;
    int user_canvas32_h;
    int is_user_win;
    gui_raw_event_t event_queue[128];
    int event_head, event_tail;
    char user_title[32];
    uint8_t has_custom_icon;
    uint8_t custom_icon[7][7];
    uint8_t custom_icon_color;
    char author[32];
    int cursor_mode;    /* 0: normal, 1: hidden, 2: relative */
} gui_window_t;

extern int gui_active;
extern int gui_vesa_active;
void gui_enter(void);   /* run desktop (VESA 800x600 true-color if available, or Mode 13h) */
void gui_set_force_vga(int force);
void gui_set_pending_file(const char *path);
void *gui_get_windows_addr(void);

/* Kernel-Side GUI Compositor API for Syscalls */
int  gui_kernel_create_window(const char *title, int w, int h);
void gui_kernel_draw_rect(int win, int x, int y, int w, int h, uint8_t color);
void gui_kernel_draw_text(int win, int x, int y, const char *text, uint8_t color);
void gui_kernel_draw_pixel(int win, int x, int y, uint8_t color);
void gui_kernel_draw_buffer(int win, const void *buf, int w, int h, int format);
void gui_kernel_clear(int win, uint8_t color);
void gui_kernel_update(int win);
int  gui_kernel_get_event(int win, gui_raw_event_t *ev);
void gui_kernel_close_window(int win);
void gui_kernel_push_event(gui_window_t *w, const gui_raw_event_t *ev);
void gui_kernel_set_icon(int win, const uint8_t *icon, uint8_t color);
void gui_kernel_set_author(int win, const char *author);
void gui_kernel_set_title(int win, const char *title);
void gui_kernel_set_cursor_mode(int win, int mode);
void sound_tone(uint32_t freq_hz, uint32_t ms);

#endif

