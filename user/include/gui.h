#ifndef _GUI_H
#define _GUI_H

#include <stdint.h>
#include <stddef.h>

/* Window Sizing Presets */
#define GUI_WIN_DEFAULT            0     /* Unspecified / default -> half-screen centered */
#define GUI_WIN_FULLSCREEN        -1     /* Fullscreen desktop above taskbar */
#define GUI_WIN_HALFSCREEN        -2     /* Half-screen desktop window */

/* Cursor Modes */
#define GUI_CURSOR_NORMAL          0     /* Visible standard system cursor */
#define GUI_CURSOR_HIDDEN          1     /* Invisible cursor */
#define GUI_CURSOR_RELATIVE        2     /* Invisible & locked cursor with relative (rx, ry) deltas */

/* Raw and high-level event types */
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

/* Standard Scancode Definitions for Games and Key Capture */
#define GUI_KEY_ESCAPE      0x01
#define GUI_KEY_1           0x02
#define GUI_KEY_2           0x03
#define GUI_KEY_3           0x04
#define GUI_KEY_4           0x05
#define GUI_KEY_5           0x06
#define GUI_KEY_6           0x07
#define GUI_KEY_7           0x08
#define GUI_KEY_8           0x09
#define GUI_KEY_9           0x0A
#define GUI_KEY_0           0x0B
#define GUI_KEY_MINUS       0x0C
#define GUI_KEY_EQUALS      0x0D
#define GUI_KEY_BACKSPACE   0x0E
#define GUI_KEY_TAB         0x0F
#define GUI_KEY_Q           0x10
#define GUI_KEY_W           0x11
#define GUI_KEY_E           0x12
#define GUI_KEY_R           0x13
#define GUI_KEY_T           0x14
#define GUI_KEY_Y           0x15
#define GUI_KEY_U           0x16
#define GUI_KEY_I           0x17
#define GUI_KEY_O           0x18
#define GUI_KEY_P           0x19
#define GUI_KEY_ENTER       0x1C
#define GUI_KEY_LCTRL       0x1D
#define GUI_KEY_A           0x1E
#define GUI_KEY_S           0x1F
#define GUI_KEY_D           0x20
#define GUI_KEY_F           0x21
#define GUI_KEY_G           0x22
#define GUI_KEY_H           0x23
#define GUI_KEY_J           0x24
#define GUI_KEY_K           0x25
#define GUI_KEY_L           0x26
#define GUI_KEY_LSHIFT      0x2A
#define GUI_KEY_Z           0x2C
#define GUI_KEY_X           0x2D
#define GUI_KEY_C           0x2E
#define GUI_KEY_V           0x2F
#define GUI_KEY_B           0x30
#define GUI_KEY_N           0x31
#define GUI_KEY_M           0x32
#define GUI_KEY_RSHIFT      0x36
#define GUI_KEY_LALT        0x38
#define GUI_KEY_SPACE       0x39
#define GUI_KEY_F1          0x3B
#define GUI_KEY_F2          0x3C
#define GUI_KEY_F3          0x3D
#define GUI_KEY_F4          0x3E
#define GUI_KEY_F5          0x3F
#define GUI_KEY_F6          0x40
#define GUI_KEY_F7          0x41
#define GUI_KEY_F8          0x42
#define GUI_KEY_F9          0x43
#define GUI_KEY_F10         0x44
#define GUI_KEY_UP          0x48
#define GUI_KEY_LEFT        0x4B
#define GUI_KEY_RIGHT       0x4D
#define GUI_KEY_DOWN        0x50
#define GUI_KEY_F11         0x57
#define GUI_KEY_F12         0x58

/* Standard 256-color palette shortcuts */
#define GUI_COLOR_BLACK       0x00
#define GUI_COLOR_BLUE        0x01
#define GUI_COLOR_GREEN       0x02
#define GUI_COLOR_CYAN        0x03
#define GUI_COLOR_RED         0x04
#define GUI_COLOR_MAGENTA     0x05
#define GUI_COLOR_BROWN       0x06
#define GUI_COLOR_LIGHT_GRAY  0x07
#define GUI_COLOR_DARK_GRAY   0x08
#define GUI_COLOR_LIGHT_BLUE  0x09
#define GUI_COLOR_LIGHT_GREEN 0x0A
#define GUI_COLOR_LIGHT_CYAN  0x0B
#define GUI_COLOR_LIGHT_RED   0x0C
#define GUI_COLOR_YELLOW      0x0E
#define GUI_COLOR_WHITE       0x0F

/* Event structure */
typedef struct {
    int type;
    int x, y;          /* Mouse coordinates relative to window interior */
    int rx, ry;        /* Relative motion deltas */
    int button;        /* 1=Left, 2=Right */
    char key;          /* Key character */
    uint8_t scancode;  /* Hardware scancode */
    int widget_id;     /* For high-level widget events */
} gui_event_t;

/* Widget types and structure */
#define WIDGET_BUTTON       1
#define WIDGET_TEXTBOX      2
#define WIDGET_CHECKBOX     3
#define WIDGET_PROGRESSBAR  4
#define WIDGET_LABEL        5

typedef struct gui_widget {
    int id;
    int type;
    int x, y, w, h;
    char text[64];
    int checked;       /* 0 or 1 for checkbox */
    int progress;      /* 0..100 for progressbar */
    int focused;       /* 1 if textbox has focus */
    int pressed;       /* 1 if button is pressed */
    uint8_t fg_color;
    uint8_t bg_color;
    struct gui_widget *next;
} gui_widget_t;

/* Window Creation & Management */
int  gui_create_window(const char *title, int width, int height);
int  gui_create_window_fullscreen(const char *title);
int  gui_create_window_halfscreen(const char *title);
void gui_close_window(int win);
void gui_set_title(int win, const char *title);
void gui_set_author(int win, const char *author);
void gui_set_icon(int win, const uint8_t icon[7][7], uint8_t color);
int  gui_set_cursor_mode(int win, int mode);
void gui_clear(int win, uint8_t color);
void gui_update(int win);

/* Drawing Primitives */
void gui_draw_pixel(int win, int x, int y, uint8_t color);
void gui_draw_rect(int win, int x, int y, int width, int height, uint8_t color);
void gui_draw_rect_alpha(int win, int x, int y, int w, int h, uint32_t argb);
void gui_blend_rect(uint32_t *buf, int buf_w, int buf_h, int x, int y, int w, int h, uint32_t argb);
void gui_draw_line(int win, int x0, int y0, int x1, int y1, uint8_t color);
void gui_draw_circle(int win, int cx, int cy, int radius, uint8_t color);
void gui_fill_circle(int win, int cx, int cy, int radius, uint8_t color);
void gui_draw_text(int win, int x, int y, const char *str, uint8_t color);

/* High-Performance Frame Buffering & Asset Loading */
void gui_draw_buffer(int win, const void *buf, int w, int h, int format);
void gui_draw_buffer32(int win, const uint32_t *buf, int w, int h);
void gui_draw_buffer8(int win, const uint8_t *buf, int w, int h);
uint32_t *gui_load_bmp(const char *path, int *out_w, int *out_h);
void gui_sync_frame(int target_fps);

/* Event & Key Capture */
int  gui_get_raw_event(int win, gui_event_t *ev);
int  gui_poll_key_event(int win, int *pressed, uint8_t *scancode, char *ascii);
int  gui_is_key_down(int win, uint8_t scancode);

/* Dialogs & Audio Feedback */
int  gui_message_box(const char *title, const char *message);
void gui_beep(uint32_t freq_hz, uint32_t ms);

/* Widget Toolkit */
gui_widget_t *gui_add_button(int win, int x, int y, int w, int h, const char *text);
gui_widget_t *gui_add_textbox(int win, int x, int y, int w, int h, const char *initial_text);
gui_widget_t *gui_add_checkbox(int win, int x, int y, const char *label, int checked);
gui_widget_t *gui_add_progressbar(int win, int x, int y, int w, int h, int progress);
gui_widget_t *gui_add_label(int win, int x, int y, const char *text);
void gui_set_progress(gui_widget_t *bar, int progress);
void gui_set_text(gui_widget_t *widget, const char *text);
int  gui_poll_event(int win, gui_event_t *ev);
void gui_draw_widget(int win, gui_widget_t *w);
void gui_draw_widgets(int win);

/* Desktop Application Manifest Declaration Markers */
#ifndef ARCHAOS_GUI_APP
#define ARCHAOS_GUI_APP(name) \
    const char __archaos_gui_app_meta[] __attribute__((used, section(".rodata"))) = "ARCHAOS_GUI:" name
#endif

#ifndef ARCHAOS_APP_TITLE
#define ARCHAOS_APP_TITLE(name) ARCHAOS_GUI_APP(name)
#endif

#ifndef ARCHAOS_APP_AUTHOR
#define ARCHAOS_APP_AUTHOR(author) \
    const char __archaos_app_author_meta[] __attribute__((used, section(".rodata"))) = "ARCHAOS_AUTHOR:" author
#endif

#endif /* _GUI_H */
