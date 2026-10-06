#include "gui.h"
#include "libc.h"
#include "syscall.h"

/* ========================================================================= */
/* Data Structures & Internal State                                          */
/* ========================================================================= */

typedef struct {
    int     x;
    int     y;
    int     w;
    int     h;
    uint8_t color;
    uint8_t _pad[3];
} gui_draw_rect_args_t;

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

typedef struct window_node {
    int win;
    int w, h;
    uint32_t *fb32;
    gui_widget_t *widgets;
    int next_widget_id;
    struct window_node *next;
} window_node_t;

static window_node_t *s_windows_head = NULL;
static uint8_t s_key_states[32][128];

static window_node_t *find_window_node(int win)
{
    window_node_t *cur = s_windows_head;
    while (cur) {
        if (cur->win == win) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static window_node_t *get_or_create_window_node(int win)
{
    window_node_t *cur = find_window_node(win);
    if (cur) {
        return cur;
    }

    cur = (window_node_t *)malloc(sizeof(window_node_t));
    if (!cur) {
        return NULL;
    }

    cur->win = win;
    cur->w = 0;
    cur->h = 0;
    cur->fb32 = NULL;
    cur->widgets = NULL;
    cur->next_widget_id = 1;
    cur->next = s_windows_head;
    s_windows_head = cur;
    return cur;
}

static int find_window_for_widget(gui_widget_t *target)
{
    if (!target) return -1;
    window_node_t *node = s_windows_head;
    while (node) {
        gui_widget_t *w = node->widgets;
        while (w) {
            if (w == target) {
                return node->win;
            }
            w = w->next;
        }
        node = node->next;
    }
    return -1;
}

static void append_widget(window_node_t *node, gui_widget_t *widget)
{
    widget->next = NULL;
    if (!node->widgets) {
        node->widgets = widget;
    } else {
        gui_widget_t *cur = node->widgets;
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = widget;
    }
}

/* ========================================================================= */
/* Low-Level GUI System Calls (Numbers 20 - 27)                             */
/* ========================================================================= */

int gui_create_window(const char *title, int width, int height)
{
    int win = sys_call3(SYS_GUI_CREATE_WINDOW, (uint32_t)title, (uint32_t)width, (uint32_t)height);
    if (win >= 0) {
        window_node_t *node = get_or_create_window_node(win);
        if (node) {
            node->w = (width > 0) ? width : 320;
            node->h = (height > 0) ? height : 200;
        }
        gui_clear(win, GUI_COLOR_LIGHT_GRAY);
    } else {
        printf("[GUI] Error: GUI desktop is not active. Run 'gui <app>' or launch from desktop.\n");
    }
    return win;
}

int gui_create_window_fullscreen(const char *title)
{
    return gui_create_window(title, GUI_WIN_FULLSCREEN, GUI_WIN_FULLSCREEN);
}

int gui_create_window_halfscreen(const char *title)
{
    return gui_create_window(title, GUI_WIN_HALFSCREEN, GUI_WIN_HALFSCREEN);
}

void gui_close_window(int win)
{
    sys_call1(SYS_GUI_CLOSE_WINDOW, (uint32_t)win);

    if (win >= 0 && win < 32) {
        memset(s_key_states[win], 0, sizeof(s_key_states[win]));
    }

    /* Free all registered widgets and buffers associated with this window */
    window_node_t **curr = &s_windows_head;
    while (*curr) {
        if ((*curr)->win == win) {
            window_node_t *node = *curr;
            *curr = node->next;

            if (node->fb32) {
                free(node->fb32);
                node->fb32 = NULL;
            }

            gui_widget_t *w = node->widgets;
            while (w) {
                gui_widget_t *next_w = w->next;
                free(w);
                w = next_w;
            }
            free(node);
            return;
        }
        curr = &(*curr)->next;
    }
}

void gui_clear(int win, uint8_t color)
{
    sys_call2(SYS_GUI_CLEAR, (uint32_t)win, (uint32_t)color);
}

void gui_draw_rect(int win, int x, int y, int width, int height, uint8_t color)
{
    gui_draw_rect_args_t param;
    param.x = x;
    param.y = y;
    param.w = width;
    param.h = height;
    param.color = color;
    param._pad[0] = param._pad[1] = param._pad[2] = 0;
    sys_call2(SYS_GUI_DRAW_RECT, (uint32_t)win, (uint32_t)&param);
}

void gui_draw_line(int win, int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = x1 - x0;
    if (dx < 0) dx = -dx;
    int dy = y1 - y0;
    if (dy < 0) dy = -dy;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        gui_draw_pixel(win, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gui_draw_circle(int win, int cx, int cy, int radius, uint8_t color)
{
    if (radius < 0) return;
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    while (x <= y) {
        gui_draw_pixel(win, cx + x, cy + y, color);
        gui_draw_pixel(win, cx - x, cy + y, color);
        gui_draw_pixel(win, cx + x, cy - y, color);
        gui_draw_pixel(win, cx - x, cy - y, color);
        gui_draw_pixel(win, cx + y, cy + x, color);
        gui_draw_pixel(win, cx - y, cy + x, color);
        gui_draw_pixel(win, cx + y, cy - x, color);
        gui_draw_pixel(win, cx - y, cy - x, color);

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void gui_fill_circle(int win, int cx, int cy, int radius, uint8_t color)
{
    if (radius < 0) return;
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    while (x <= y) {
        gui_draw_rect(win, cx - x, cy + y, 2 * x + 1, 1, color);
        gui_draw_rect(win, cx - x, cy - y, 2 * x + 1, 1, color);
        gui_draw_rect(win, cx - y, cy + x, 2 * y + 1, 1, color);
        gui_draw_rect(win, cx - y, cy - x, 2 * y + 1, 1, color);

        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void gui_draw_text(int win, int x, int y, const char *str, uint8_t color)
{
    sys_call5(SYS_GUI_DRAW_TEXT, (uint32_t)win, (uint32_t)x, (uint32_t)y, (uint32_t)str, (uint32_t)color);
}

void gui_draw_pixel(int win, int x, int y, uint8_t color)
{
    sys_call4(SYS_GUI_DRAW_PIXEL, (uint32_t)win, (uint32_t)x, (uint32_t)y, (uint32_t)color);
}

void gui_draw_buffer(int win, const void *buf, int w, int h, int format)
{
    sys_call5(SYS_GUI_DRAW_BUFFER, (uint32_t)win, (uint32_t)buf, (uint32_t)w, (uint32_t)h, (uint32_t)format);
}

void gui_draw_buffer32(int win, const uint32_t *buf, int w, int h)
{
    gui_draw_buffer(win, buf, w, h, 1);
}

void gui_draw_buffer8(int win, const uint8_t *buf, int w, int h)
{
    gui_draw_buffer(win, buf, w, h, 0);
}

void gui_update(int win)
{
    sys_call1(SYS_GUI_UPDATE, (uint32_t)win);
}

int gui_get_raw_event(int win, gui_event_t *ev)
{
    if (!ev) return 0;
    gui_raw_event_t raw;
    int res = sys_call2(SYS_GUI_GET_EVENT, (uint32_t)win, (uint32_t)&raw);
    if (res <= 0 || raw.type == GUI_EVENT_NONE) {
        ev->type = GUI_EVENT_NONE;
        return 0;
    }

    ev->type = raw.type;
    ev->x = raw.x;
    ev->y = raw.y;
    ev->rx = raw.rx;
    ev->ry = raw.ry;
    ev->button = raw.buttons;
    ev->key = raw.c;
    ev->scancode = raw.scancode;
    ev->widget_id = 0;

    /* Update key tracking state */
    if (win >= 0 && win < 32 && (raw.scancode & 0x7F) < 128) {
        if (raw.type == GUI_EVENT_KEY_DOWN) {
            s_key_states[win][raw.scancode & 0x7F] = 1;
        } else if (raw.type == GUI_EVENT_KEY_UP) {
            s_key_states[win][raw.scancode & 0x7F] = 0;
        }
    }

    return 1;
}

int gui_poll_key_event(int win, int *pressed, uint8_t *scancode, char *ascii)
{
    gui_event_t ev;
    while (gui_get_raw_event(win, &ev)) {
        if (ev.type == GUI_EVENT_KEY_DOWN || ev.type == GUI_EVENT_KEY_UP) {
            if (pressed) *pressed = (ev.type == GUI_EVENT_KEY_DOWN);
            if (scancode) *scancode = ev.scancode;
            if (ascii) *ascii = ev.key;
            return 1;
        }
    }
    return 0;
}

int gui_is_key_down(int win, uint8_t scancode)
{
    if (win < 0 || win >= 32) return 0;
    return s_key_states[win][scancode & 0x7F] ? 1 : 0;
}

void gui_set_title(int win, const char *title)
{
    sys_call2(SYS_GUI_SET_TITLE, (uint32_t)win, (uint32_t)title);
}

void gui_set_icon(int win, const uint8_t icon[7][7], uint8_t color)
{
    sys_call3(SYS_GUI_SET_ICON, (uint32_t)win, (uint32_t)icon, (uint32_t)color);
}

void gui_set_author(int win, const char *author)
{
    sys_call2(SYS_GUI_SET_AUTHOR, (uint32_t)win, (uint32_t)author);
}

void gui_beep(uint32_t freq_hz, uint32_t ms)
{
    sys_call2(SYS_BEEP, freq_hz, ms);
}

/* ========================================================================= */
/* Widget Rendering                                                          */
/* ========================================================================= */

void gui_draw_widget(int win, gui_widget_t *w)
{
    if (!w) return;

    switch (w->type) {
        case WIDGET_BUTTON: {
            int bx = w->x, by = w->y, bw = w->w, bh = w->h;
            uint8_t top_left = w->pressed ? GUI_COLOR_DARK_GRAY : GUI_COLOR_WHITE;
            uint8_t bot_right = w->pressed ? GUI_COLOR_WHITE : GUI_COLOR_DARK_GRAY;
            uint8_t bg = w->bg_color ? w->bg_color : GUI_COLOR_LIGHT_GRAY;

            /* Interior fill */
            if (bw > 2 && bh > 2) {
                gui_draw_rect(win, bx + 1, by + 1, bw - 2, bh - 2, bg);
            }

            /* 3D bevel (highlight white top/left, shadow dark bottom/right; invert if pressed) */
            gui_draw_rect(win, bx, by, bw, 1, top_left);             /* Top */
            gui_draw_rect(win, bx, by, 1, bh, top_left);             /* Left */
            gui_draw_rect(win, bx + bw - 1, by, 1, bh, bot_right);   /* Right */
            gui_draw_rect(win, bx, by + bh - 1, bw, 1, bot_right);   /* Bottom */

            /* Centered text */
            int len = (int)strlen(w->text);
            if (len > 0) {
                int text_w = len * 8;
                int tx = bx + (bw - text_w) / 2;
                int ty = by + (bh - 8) / 2;
                if (w->pressed) {
                    tx += 1;
                    ty += 1;
                }
                if (tx < bx + 2) tx = bx + 2;
                if (ty < by + 1) ty = by + 1;
                uint8_t fg = w->fg_color ? w->fg_color : GUI_COLOR_BLACK;
                gui_draw_text(win, tx, ty, w->text, fg);
            }
            break;
        }

        case WIDGET_TEXTBOX: {
            int tx = w->x, ty = w->y, tw = w->w, th = w->h;
            uint8_t border_col = w->focused ? GUI_COLOR_BLUE : GUI_COLOR_DARK_GRAY;
            uint8_t bg = w->bg_color ? w->bg_color : GUI_COLOR_WHITE;

            /* 1px border (blue if focused, gray if not) */
            gui_draw_rect(win, tx, ty, tw, 1, border_col);
            gui_draw_rect(win, tx, ty, 1, th, border_col);
            gui_draw_rect(win, tx + tw - 1, ty, 1, th, border_col);
            gui_draw_rect(win, tx, ty + th - 1, tw, 1, border_col);

            /* White interior */
            if (tw > 2 && th > 2) {
                gui_draw_rect(win, tx + 1, ty + 1, tw - 2, th - 2, bg);
            }

            /* Text string */
            int text_y = ty + (th - 8) / 2;
            if (text_y < ty + 1) text_y = ty + 1;
            uint8_t fg = w->fg_color ? w->fg_color : GUI_COLOR_BLACK;
            int cur_x = tx + 3;
            gui_draw_text(win, cur_x, text_y, w->text, fg);

            /* Cursor '|' if focused */
            if (w->focused) {
                int cx = cur_x + (int)strlen(w->text) * 8;
                if (cx + 8 <= tx + tw - 1) {
                    gui_draw_text(win, cx, text_y, "|", GUI_COLOR_BLACK);
                }
            }
            break;
        }

        case WIDGET_CHECKBOX: {
            int cx = w->x, cy = w->y;

            /* Draw small 10x10 box */
            gui_draw_rect(win, cx, cy, 10, 1, GUI_COLOR_DARK_GRAY);
            gui_draw_rect(win, cx, cy, 1, 10, GUI_COLOR_DARK_GRAY);
            gui_draw_rect(win, cx + 9, cy, 1, 10, GUI_COLOR_WHITE);
            gui_draw_rect(win, cx, cy + 9, 10, 1, GUI_COLOR_WHITE);

            /* Interior box */
            gui_draw_rect(win, cx + 1, cy + 1, 8, 8, GUI_COLOR_WHITE);

            /* Draw 'X' if checked */
            if (w->checked) {
                gui_draw_text(win, cx + 1, cy + 1, "X", GUI_COLOR_BLACK);
            }

            /* Draw label text next to it */
            if (w->text[0] != '\0') {
                uint8_t fg = w->fg_color ? w->fg_color : GUI_COLOR_BLACK;
                gui_draw_text(win, cx + 14, cy + 1, w->text, fg);
            }
            break;
        }

        case WIDGET_PROGRESSBAR: {
            int px = w->x, py = w->y, pw = w->w, ph = w->h;

            /* Sunken gray container border */
            gui_draw_rect(win, px, py, pw, 1, GUI_COLOR_DARK_GRAY);
            gui_draw_rect(win, px, py, 1, ph, GUI_COLOR_DARK_GRAY);
            gui_draw_rect(win, px + pw - 1, py, 1, ph, GUI_COLOR_WHITE);
            gui_draw_rect(win, px, py + ph - 1, pw, 1, GUI_COLOR_WHITE);

            /* Sunken interior background */
            if (pw > 2 && ph > 2) {
                uint8_t bg = w->bg_color ? w->bg_color : GUI_COLOR_LIGHT_GRAY;
                gui_draw_rect(win, px + 1, py + 1, pw - 2, ph - 2, bg);
            }

            /* Green/blue fill bar proportional to progress% */
            int p = w->progress;
            if (p < 0) p = 0;
            if (p > 100) p = 100;
            int inner_w = pw - 2;
            int fill_w = (inner_w * p) / 100;
            if (fill_w > 0 && ph > 2) {
                uint8_t fill_color = w->fg_color ? w->fg_color : GUI_COLOR_LIGHT_BLUE;
                gui_draw_rect(win, px + 1, py + 1, fill_w, ph - 2, fill_color);
            }
            break;
        }

        case WIDGET_LABEL: {
            uint8_t fg = w->fg_color ? w->fg_color : GUI_COLOR_BLACK;
            gui_draw_text(win, w->x, w->y, w->text, fg);
            break;
        }

        default:
            break;
    }
}

void gui_draw_widgets(int win)
{
    window_node_t *node = find_window_node(win);
    if (!node) return;

    for (gui_widget_t *w = node->widgets; w; w = w->next) {
        gui_draw_widget(win, w);
    }
}

/* ========================================================================= */
/* Widget Toolkit API                                                        */
/* ========================================================================= */

gui_widget_t *gui_add_button(int win, int x, int y, int w, int h, const char *text)
{
    window_node_t *node = get_or_create_window_node(win);
    if (!node) return NULL;

    gui_widget_t *btn = (gui_widget_t *)malloc(sizeof(gui_widget_t));
    if (!btn) return NULL;

    memset(btn, 0, sizeof(gui_widget_t));
    btn->id = node->next_widget_id++;
    btn->type = WIDGET_BUTTON;
    btn->x = x;
    btn->y = y;
    btn->w = w;
    btn->h = h;
    strncpy(btn->text, text ? text : "", sizeof(btn->text) - 1);
    btn->text[sizeof(btn->text) - 1] = '\0';
    btn->fg_color = GUI_COLOR_BLACK;
    btn->bg_color = GUI_COLOR_LIGHT_GRAY;

    append_widget(node, btn);
    gui_draw_widget(win, btn);
    gui_update(win);

    return btn;
}

gui_widget_t *gui_add_textbox(int win, int x, int y, int w, int h, const char *initial_text)
{
    window_node_t *node = get_or_create_window_node(win);
    if (!node) return NULL;

    gui_widget_t *tb = (gui_widget_t *)malloc(sizeof(gui_widget_t));
    if (!tb) return NULL;

    memset(tb, 0, sizeof(gui_widget_t));
    tb->id = node->next_widget_id++;
    tb->type = WIDGET_TEXTBOX;
    tb->x = x;
    tb->y = y;
    tb->w = w;
    tb->h = h;
    strncpy(tb->text, initial_text ? initial_text : "", sizeof(tb->text) - 1);
    tb->text[sizeof(tb->text) - 1] = '\0';
    tb->fg_color = GUI_COLOR_BLACK;
    tb->bg_color = GUI_COLOR_WHITE;
    tb->focused = 0;

    append_widget(node, tb);
    gui_draw_widget(win, tb);
    gui_update(win);

    return tb;
}

gui_widget_t *gui_add_checkbox(int win, int x, int y, const char *label, int checked)
{
    window_node_t *node = get_or_create_window_node(win);
    if (!node) return NULL;

    gui_widget_t *cb = (gui_widget_t *)malloc(sizeof(gui_widget_t));
    if (!cb) return NULL;

    memset(cb, 0, sizeof(gui_widget_t));
    cb->id = node->next_widget_id++;
    cb->type = WIDGET_CHECKBOX;
    cb->x = x;
    cb->y = y;
    strncpy(cb->text, label ? label : "", sizeof(cb->text) - 1);
    cb->text[sizeof(cb->text) - 1] = '\0';
    cb->w = 14 + (int)strlen(cb->text) * 8;
    cb->h = 10;
    cb->checked = checked ? 1 : 0;
    cb->fg_color = GUI_COLOR_BLACK;
    cb->bg_color = GUI_COLOR_LIGHT_GRAY;

    append_widget(node, cb);
    gui_draw_widget(win, cb);
    gui_update(win);

    return cb;
}

gui_widget_t *gui_add_progressbar(int win, int x, int y, int w, int h, int progress)
{
    window_node_t *node = get_or_create_window_node(win);
    if (!node) return NULL;

    gui_widget_t *pb = (gui_widget_t *)malloc(sizeof(gui_widget_t));
    if (!pb) return NULL;

    memset(pb, 0, sizeof(gui_widget_t));
    pb->id = node->next_widget_id++;
    pb->type = WIDGET_PROGRESSBAR;
    pb->x = x;
    pb->y = y;
    pb->w = w;
    pb->h = h;
    pb->progress = progress < 0 ? 0 : (progress > 100 ? 100 : progress);
    pb->fg_color = GUI_COLOR_LIGHT_BLUE;
    pb->bg_color = GUI_COLOR_LIGHT_GRAY;

    append_widget(node, pb);
    gui_draw_widget(win, pb);
    gui_update(win);

    return pb;
}

gui_widget_t *gui_add_label(int win, int x, int y, const char *text)
{
    window_node_t *node = get_or_create_window_node(win);
    if (!node) return NULL;

    gui_widget_t *lbl = (gui_widget_t *)malloc(sizeof(gui_widget_t));
    if (!lbl) return NULL;

    memset(lbl, 0, sizeof(gui_widget_t));
    lbl->id = node->next_widget_id++;
    lbl->type = WIDGET_LABEL;
    lbl->x = x;
    lbl->y = y;
    strncpy(lbl->text, text ? text : "", sizeof(lbl->text) - 1);
    lbl->text[sizeof(lbl->text) - 1] = '\0';
    lbl->w = (int)strlen(lbl->text) * 8;
    lbl->h = 8;
    lbl->fg_color = GUI_COLOR_BLACK;
    lbl->bg_color = GUI_COLOR_LIGHT_GRAY;

    append_widget(node, lbl);
    gui_draw_widget(win, lbl);
    gui_update(win);

    return lbl;
}

void gui_set_progress(gui_widget_t *bar, int progress)
{
    if (!bar) return;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    bar->progress = progress;

    int win = find_window_for_widget(bar);
    if (win >= 0) {
        gui_draw_widget(win, bar);
        gui_update(win);
    }
}

void gui_set_text(gui_widget_t *widget, const char *text)
{
    if (!widget) return;
    int win = find_window_for_widget(widget);

    if (widget->type == WIDGET_LABEL && win >= 0) {
        /* Clear previous label footprint before changing bounds */
        uint8_t bg = widget->bg_color ? widget->bg_color : GUI_COLOR_LIGHT_GRAY;
        int clear_w = widget->w > 210 ? widget->w : 210;
        gui_draw_rect(win, widget->x, widget->y, clear_w, 10, bg);
    }

    strncpy(widget->text, text ? text : "", sizeof(widget->text) - 1);
    widget->text[sizeof(widget->text) - 1] = '\0';

    if (widget->type == WIDGET_LABEL) {
        widget->w = (int)strlen(widget->text) * 8;
    } else if (widget->type == WIDGET_CHECKBOX) {
        widget->w = 14 + (int)strlen(widget->text) * 8;
    }

    if (win >= 0) {
        gui_draw_widget(win, widget);
        gui_update(win);
    }
}

/* ========================================================================= */
/* Event Polling & Automatic Widget Event Processing                        */
/* ========================================================================= */

int gui_poll_event(int win, gui_event_t *ev)
{
    if (!ev) return 0;

    int res = gui_get_raw_event(win, ev);
    if (res <= 0 || ev->type == GUI_EVENT_NONE) {
        return 0;
    }

    window_node_t *node = find_window_node(win);
    if (!node) {
        return 1;
    }

    if (ev->type == GUI_EVENT_MOUSE_DOWN) {
        gui_widget_t *hit = NULL;
        for (gui_widget_t *w = node->widgets; w; w = w->next) {
            if (ev->x >= w->x && ev->x < w->x + w->w &&
                ev->y >= w->y && ev->y < w->y + w->h) {
                hit = w;
                break;
            }
        }

        if (hit) {
            ev->widget_id = hit->id;

            if (hit->type == WIDGET_BUTTON) {
                hit->pressed = 1;
                gui_draw_widget(win, hit);
                gui_update(win);
                ev->type = GUI_EVENT_BUTTON_CLICK;
                return 1;
            } else if (hit->type == WIDGET_TEXTBOX) {
                /* Set focused=1, unfocus all other textboxes */
                for (gui_widget_t *w = node->widgets; w; w = w->next) {
                    if (w->type == WIDGET_TEXTBOX) {
                        int want_focus = (w == hit);
                        if (w->focused != want_focus) {
                            w->focused = want_focus;
                            gui_draw_widget(win, w);
                        }
                    }
                }
                gui_update(win);
                return 1;
            } else if (hit->type == WIDGET_CHECKBOX) {
                hit->checked ^= 1;
                gui_draw_widget(win, hit);
                gui_update(win);
                ev->type = GUI_EVENT_CHECKBOX_TOGGLE;
                return 1;
            }
        } else {
            /* Clicked outside any widget: unfocus all textboxes */
            int unfocused = 0;
            for (gui_widget_t *w = node->widgets; w; w = w->next) {
                if (w->type == WIDGET_TEXTBOX && w->focused) {
                    w->focused = 0;
                    gui_draw_widget(win, w);
                    unfocused = 1;
                }
            }
            if (unfocused) {
                gui_update(win);
            }
        }
        return 1;
    }

    if (ev->type == GUI_EVENT_MOUSE_UP) {
        int changed = 0;
        for (gui_widget_t *w = node->widgets; w; w = w->next) {
            if (w->type == WIDGET_BUTTON && w->pressed) {
                w->pressed = 0;
                gui_draw_widget(win, w);
                changed = 1;
            }
        }
        if (changed) {
            gui_update(win);
        }
        return 1;
    }

    if (ev->type == GUI_EVENT_KEY_DOWN) {
        /* Check if any textbox has focus */
        gui_widget_t *tb = NULL;
        for (gui_widget_t *w = node->widgets; w; w = w->next) {
            if (w->type == WIDGET_TEXTBOX && w->focused) {
                tb = w;
                break;
            }
        }

        if (tb) {
            char k = ev->key;
            if (k == '\b' || k == 0x08 || (unsigned char)k == 127) {
                size_t len = strlen(tb->text);
                if (len > 0) {
                    tb->text[len - 1] = '\0';
                    gui_draw_widget(win, tb);
                    gui_update(win);
                    ev->type = GUI_EVENT_TEXT_CHANGE;
                    ev->widget_id = tb->id;
                    return 1;
                }
            } else if (k >= 32 && k <= 126) {
                size_t len = strlen(tb->text);
                if (len + 1 < sizeof(tb->text)) {
                    tb->text[len] = k;
                    tb->text[len + 1] = '\0';
                    gui_draw_widget(win, tb);
                    gui_update(win);
                    ev->type = GUI_EVENT_TEXT_CHANGE;
                    ev->widget_id = tb->id;
                    return 1;
                }
            }
        }
        return 1;
    }

    return 1;
}

/* ========================================================================= */
/* Developer Productivity Suite Extensions                                   */
/* ========================================================================= */

int gui_set_cursor_mode(int win, int mode)
{
    return sys_call2(SYS_GUI_SET_CURSOR_MODE, (uint32_t)win, (uint32_t)mode);
}

void gui_blend_rect(uint32_t *buf, int buf_w, int buf_h, int x, int y, int w, int h, uint32_t argb)
{
    if (!buf || buf_w <= 0 || buf_h <= 0 || w <= 0 || h <= 0) return;

    uint32_t sa = (argb >> 24) & 0xFF;
    if (sa == 0) return;
    uint32_t sr = (argb >> 16) & 0xFF;
    uint32_t sg = (argb >> 8) & 0xFF;
    uint32_t sb = argb & 0xFF;
    uint32_t inv_a = 255 - sa;

    int x0 = (x < 0) ? 0 : x;
    int y0 = (y < 0) ? 0 : y;
    int x1 = (x + w > buf_w) ? buf_w : (x + w);
    int y1 = (y + h > buf_h) ? buf_h : (y + h);

    for (int cy = y0; cy < y1; cy++) {
        uint32_t *row = &buf[cy * buf_w];
        for (int cx = x0; cx < x1; cx++) {
            if (sa == 255) {
                row[cx] = argb;
            } else {
                uint32_t dst = row[cx];
                uint32_t dr = (dst >> 16) & 0xFF;
                uint32_t dg = (dst >> 8) & 0xFF;
                uint32_t db = dst & 0xFF;
                uint32_t r = (sr * sa + dr * inv_a) / 255;
                uint32_t g = (sg * sa + dg * inv_a) / 255;
                uint32_t b = (sb * sa + db * inv_a) / 255;
                row[cx] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }
}

void gui_draw_rect_alpha(int win, int x, int y, int w, int h, uint32_t argb)
{
    window_node_t *node = find_window_node(win);
    if (!node) return;

    if (!node->fb32) {
        int fw = (node->w > 0) ? node->w : 320;
        int fh = (node->h > 0) ? node->h : 200;
        node->fb32 = (uint32_t *)malloc(fw * fh * sizeof(uint32_t));
        if (!node->fb32) return;
        for (int i = 0; i < fw * fh; i++) {
            node->fb32[i] = 0xFFC0C0C0;
        }
    }

    gui_blend_rect(node->fb32, node->w, node->h, x, y, w, h, argb);
    gui_draw_buffer32(win, node->fb32, node->w, node->h);
}

uint32_t *gui_load_bmp(const char *path, int *out_w, int *out_h)
{
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t header[54];
    if (fread(header, 1, 54, f) != 54) {
        fclose(f);
        return NULL;
    }

    if (header[0] != 'B' || header[1] != 'M') {
        fclose(f);
        return NULL;
    }

    uint32_t data_offset = *(uint32_t *)&header[10];
    int32_t width = *(int32_t *)&header[18];
    int32_t height = *(int32_t *)&header[22];
    uint16_t bpp = *(uint16_t *)&header[28];
    uint32_t compression = *(uint32_t *)&header[30];

    if (width <= 0 || height == 0 || (bpp != 24 && bpp != 32) || compression != 0) {
        fclose(f);
        return NULL;
    }

    int top_down = 0;
    if (height < 0) {
        top_down = 1;
        height = -height;
    }

    fseek(f, (long)data_offset, SEEK_SET);

    uint32_t *pixels = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (!pixels) {
        fclose(f);
        return NULL;
    }

    int row_stride = (bpp == 24) ? ((width * 3 + 3) & ~3) : (width * 4);
    uint8_t *row_buf = (uint8_t *)malloc(row_stride);
    if (!row_buf) {
        free(pixels);
        fclose(f);
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        if (fread(row_buf, 1, row_stride, f) != (size_t)row_stride) {
            break;
        }

        int target_y = top_down ? y : (height - 1 - y);
        uint32_t *dst_row = &pixels[target_y * width];

        if (bpp == 24) {
            for (int x = 0; x < width; x++) {
                uint8_t b = row_buf[x * 3 + 0];
                uint8_t g = row_buf[x * 3 + 1];
                uint8_t r = row_buf[x * 3 + 2];
                dst_row[x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        } else if (bpp == 32) {
            for (int x = 0; x < width; x++) {
                uint8_t b = row_buf[x * 4 + 0];
                uint8_t g = row_buf[x * 4 + 1];
                uint8_t r = row_buf[x * 4 + 2];
                uint8_t a = row_buf[x * 4 + 3];
                if (a == 0) a = 0xFF;
                dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }

    free(row_buf);
    fclose(f);

    if (out_w) *out_w = width;
    if (out_h) *out_h = height;
    return pixels;
}

void gui_sync_frame(int target_fps)
{
    if (target_fps <= 0) return;
    static uint32_t last_frame_tick = 0;

    uint32_t target_ms = 1000 / target_fps;
    uint32_t now = time_ticks();

    if (last_frame_tick != 0) {
        uint32_t elapsed = now - last_frame_tick;
        if (elapsed < target_ms) {
            sleep(target_ms - elapsed);
        }
    }
    last_frame_tick = time_ticks();
}

int gui_message_box(const char *title, const char *message)
{
    int win_w = 260;
    int win_h = 100;
    int win = gui_create_window(title ? title : "Message", win_w, win_h);
    if (win < 0) return -1;

    gui_clear(win, GUI_COLOR_LIGHT_GRAY);

    if (message) {
        gui_draw_text(win, 16, 20, message, GUI_COLOR_BLACK);
    }

    gui_widget_t *btn_ok = gui_add_button(win, 100, 50, 60, 20, "OK");
    gui_update(win);

    /* Flush any stale events prior to modal dialog loop */
    gui_event_t flush_ev;
    while (gui_poll_event(win, &flush_ev));

    int result = 0;
    while (1) {
        gui_event_t ev;
        if (gui_poll_event(win, &ev)) {
            if (ev.type == GUI_EVENT_BUTTON_CLICK && ev.widget_id == btn_ok->id) {
                result = 1;
                break;
            }
            if (ev.type == GUI_EVENT_CLOSE) {
                result = 0;
                break;
            }
            if (ev.type == GUI_EVENT_KEY_DOWN) {
                if (ev.scancode == GUI_KEY_ENTER || ev.scancode == GUI_KEY_ESCAPE || ev.key == '\n') {
                    result = (ev.scancode == GUI_KEY_ESCAPE) ? 0 : 1;
                    break;
                }
            }
        }
        gui_sync_frame(60);
    }

    gui_close_window(win);
    return result;
}

