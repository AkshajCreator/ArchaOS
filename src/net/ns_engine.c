#include "ns_engine.h"
#include "netsurf_shim.h"
#include "image_decoder.h"
#include "html.h"
#include "js/js_engine.h"
#include "../gui.h"
#include "../serial.h"
#include "../fs.h"
#include "../audio.h"
#include "../video.h"

#include <libwapcaplet/libwapcaplet.h>
#include <dom/dom.h>
#include "netsurf/libdom/bindings/hubbub/parser.h"

#define RGB(r,g,b)  ((uint8_t)(16 + (r)*36 + (g)*6 + (b)))
#define GRAY(n)     ((uint8_t)(232 + (n)))
#define COL_BLACK      0
#define COL_BLUE       1
#define COL_GREEN      2
#define COL_CYAN       3
#define COL_RED        4
#define COL_MAGENTA    5
#define COL_BROWN      6
#define COL_LIGHT_GRAY 7
#define COL_DARK_GRAY  8
#define COL_LIGHT_BLUE 9
#define COL_LIGHT_GREEN 10
#define COL_LIGHT_CYAN 11
#define COL_LIGHT_RED  12
#define COL_LIGHT_MAGENTA 13
#define COL_YELLOW     14
#define COL_WHITE      15

/* ArchaOS GUI & Kernel Externs */
extern void draw_pixel_fb(int x, int y, uint8_t color);
extern void fill_rect(int x, int y, int w, int h, uint8_t color);
extern void draw_rect(int x, int y, int w, int h, uint8_t color);
extern void draw_hline(int x, int y, int w, uint8_t color);
extern void draw_str(int x, int y, const char *str, uint8_t fg, uint8_t bg);
extern void draw_str_clip(int x, int y, const char *str, uint8_t fg, uint8_t bg, int max_x);
extern void draw_char_scale(int x, int y, char c, uint8_t fg, uint8_t bg, int scale);
extern void draw_str_scale(int x, int y, const char *str, uint8_t fg, uint8_t bg, int scale);
extern uint32_t pit_ticks(void);
extern void sound_click(void);
extern void sound_tone(uint32_t freq, uint32_t ms);
extern int atoi(const char *s);


/* Interactive elements for Keyboard & Mouse */
ns_interactive_elem_t ns_elements[NS_MAX_INTERACTIVE];
int ns_element_count = 0;

static dom_document *current_doc = NULL;
static char doc_title[128] = "Home";
static int doc_content_height = 0;
static uint8_t doc_bg_color = COL_WHITE;
static uint8_t doc_fg_color = COL_BLACK;

/* CSS Alignment */
#define ALIGN_LEFT    0
#define ALIGN_CENTER  1
#define ALIGN_RIGHT   2

/* ============================================================
 * CSS COLOR & VALUE PARSER
 * ============================================================ */
static uint8_t parse_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int css_try_parse_color(const char *s, uint8_t *out_col) {
    if (!s || !*s) return 0;
    while (*s == ' ' || *s == '\t') s++;

    if (*s == '#') {
        s++;
        int len = 0;
        while (s[len] && s[len] != ' ' && s[len] != ';' && s[len] != ')' && s[len] != '\"' && s[len] != '\'') len++;
        if (len == 6) {
            uint8_t r = (parse_hex_digit(s[0]) << 4) | parse_hex_digit(s[1]);
            uint8_t g = (parse_hex_digit(s[2]) << 4) | parse_hex_digit(s[3]);
            uint8_t b = (parse_hex_digit(s[4]) << 4) | parse_hex_digit(s[5]);
            if (out_col) *out_col = RGB((r * 5) / 255, (g * 5) / 255, (b * 5) / 255);
            return 1;
        } else if (len == 3) {
            uint8_t r = parse_hex_digit(s[0]);
            uint8_t g = parse_hex_digit(s[1]);
            uint8_t b = parse_hex_digit(s[2]);
            if (out_col) *out_col = RGB((r * 5) / 15, (g * 5) / 15, (b * 5) / 15);
            return 1;
        }
        return 0;
    }

    if (strncasecmp(s, "rgb", 3) == 0) {
        const char *p = strchr(s, '(');
        if (p) {
            p++;
            int r = 0, g = 0, b = 0;
            while (*p == ' ') p++;
            while (*p >= '0' && *p <= '9') r = r * 10 + (*p++ - '0');
            while (*p == ' ' || *p == ',') p++;
            while (*p >= '0' && *p <= '9') g = g * 10 + (*p++ - '0');
            while (*p == ' ' || *p == ',') p++;
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            if (out_col) *out_col = RGB((r * 5) / 255, (g * 5) / 255, (b * 5) / 255);
            return 1;
        }
    }

    if (strncasecmp(s, "white", 5) == 0) { if (out_col) *out_col = COL_WHITE; return 1; }
    if (strncasecmp(s, "black", 5) == 0) { if (out_col) *out_col = COL_BLACK; return 1; }
    if (strncasecmp(s, "red", 3) == 0) { if (out_col) *out_col = RGB(5,0,0); return 1; }
    if (strncasecmp(s, "green", 5) == 0) { if (out_col) *out_col = RGB(0,4,0); return 1; }
    if (strncasecmp(s, "blue", 4) == 0) { if (out_col) *out_col = RGB(0,2,5); return 1; }
    if (strncasecmp(s, "yellow", 6) == 0) { if (out_col) *out_col = RGB(5,5,0); return 1; }
    if (strncasecmp(s, "cyan", 4) == 0) { if (out_col) *out_col = RGB(0,5,5); return 1; }
    if (strncasecmp(s, "magenta", 7) == 0 || strncasecmp(s, "purple", 6) == 0) { if (out_col) *out_col = RGB(4,0,4); return 1; }
    if (strncasecmp(s, "orange", 6) == 0) { if (out_col) *out_col = RGB(5,3,0); return 1; }
    if (strncasecmp(s, "gray", 4) == 0 || strncasecmp(s, "grey", 4) == 0) { if (out_col) *out_col = GRAY(14); return 1; }
    if (strncasecmp(s, "darkgray", 8) == 0 || strncasecmp(s, "darkgrey", 8) == 0) { if (out_col) *out_col = GRAY(6); return 1; }
    if (strncasecmp(s, "lightgray", 9) == 0 || strncasecmp(s, "lightgrey", 9) == 0) { if (out_col) *out_col = GRAY(22); return 1; }
    if (strncasecmp(s, "navy", 4) == 0) { if (out_col) *out_col = RGB(0,0,3); return 1; }
    if (strncasecmp(s, "teal", 4) == 0) { if (out_col) *out_col = RGB(0,3,3); return 1; }

    return 0;
}

uint8_t css_parse_color(const char *s, uint8_t default_col) {
    uint8_t col = default_col;
    if (css_try_parse_color(s, &col)) return col;
    return default_col;
}

static int parse_px_val(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/* ============================================================
 * STYLESHEET RULE DATABASE
 * ============================================================ */
#define MAX_CSS_RULES 64

typedef struct {
    char selector[32];
    uint8_t fg;
    uint8_t bg;
    uint8_t border_col;
    int has_fg;
    int has_bg;
    int has_border;
    int border_width;
    int border_radius;
    int padding;
    int margin;
    int text_align; /* -1 = inherit, 0 = left, 1 = center, 2 = right */
    int is_bold;
    int width;
    int is_none;
    int is_flex;
    int is_inline;
} css_rule_t;

static css_rule_t css_rules[MAX_CSS_RULES];
static int css_rule_count = 0;

static void css_parse_declarations(const char *style,
                                   uint8_t *fg, uint8_t *bg, uint8_t *border_col,
                                   int *has_fg, int *has_bg, int *has_border,
                                   int *border_w, int *border_r,
                                   int *padding, int *margin, int *align,
                                   int *is_bold, int *width,
                                   int *is_none, int *is_flex, int *is_inline) {
    if (!style || !*style) return;
    const char *p = style;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        const char *prop_start = p;
        while (*p && *p != ':' && *p != ';') p++;
        if (*p != ':') { if (*p) p++; continue; }

        int prop_len = (int)(p - prop_start);
        p++; /* skip ':' */
        while (*p == ' ' || *p == '\t') p++;

        const char *val_start = p;
        while (*p && *p != ';' && *p != '\"' && *p != '\'' && *p != '}') p++;
        int val_len = (int)(p - val_start);

        while (prop_len > 0 && (prop_start[prop_len - 1] == ' ' || prop_start[prop_len - 1] == '\t')) prop_len--;
        while (val_len > 0 && (val_start[val_len - 1] == ' ' || val_start[val_len - 1] == '\t' || val_start[val_len - 1] == '\r' || val_start[val_len - 1] == '\n')) val_len--;

        char prop[32];
        char val[64];
        if (prop_len >= 31) prop_len = 31;
        if (val_len >= 63) val_len = 63;
        for (int i = 0; i < prop_len; i++) prop[i] = prop_start[i];
        prop[prop_len] = '\0';
        for (int i = 0; i < val_len; i++) val[i] = val_start[i];
        val[val_len] = '\0';

        if (strcasecmp(prop, "color") == 0 && fg) {
            uint8_t c;
            if (css_try_parse_color(val, &c)) {
                *fg = c;
                if (has_fg) *has_fg = 1;
            }
        } else if ((strcasecmp(prop, "background") == 0 || strcasecmp(prop, "background-color") == 0) && bg) {
            uint8_t c;
            if (css_try_parse_color(val, &c)) {
                *bg = c;
                if (has_bg) *has_bg = 1;
            }
        } else if (strncasecmp(prop, "border", 6) == 0 && border_col) {
            if (strstr(val, "none") || strstr(val, "hidden") || strstr(val, "0px") || strstr(val, "0 ") || strcmp(val, "0") == 0 || strstr(val, "transparent")) {
                if (has_border) *has_border = 0;
            } else {
                const char *hash = strchr(val, '#');
                uint8_t bc;
                if (hash && css_try_parse_color(hash, &bc)) {
                    *border_col = bc;
                    if (has_border) *has_border = 1;
                } else if (strstr(val, "solid")) {
                    if (css_try_parse_color(val, &bc)) {
                        *border_col = bc;
                    } else {
                        *border_col = GRAY(12);
                    }
                    if (has_border) *has_border = 1;
                }
            }
            if (border_w) *border_w = parse_px_val(val);
        } else if (strcasecmp(prop, "border-radius") == 0 && border_r) {
            *border_r = parse_px_val(val);
        } else if (strcasecmp(prop, "padding") == 0 && padding) {
            *padding = parse_px_val(val);
        } else if (strcasecmp(prop, "margin") == 0 && margin) {
            if (strstr(val, "auto")) {
                if (align) *align = ALIGN_CENTER;
            } else {
                *margin = parse_px_val(val);
            }
        } else if (strcasecmp(prop, "text-align") == 0 && align) {
            if (strstr(val, "center")) *align = ALIGN_CENTER;
            else if (strstr(val, "right")) *align = ALIGN_RIGHT;
            else if (strstr(val, "left")) *align = ALIGN_LEFT;
        } else if (strcasecmp(prop, "font-weight") == 0 && is_bold) {
            if (strstr(val, "bold") || strstr(val, "700")) *is_bold = 1;
        } else if (strcasecmp(prop, "width") == 0 && width) {
            *width = parse_px_val(val);
        } else if (strcasecmp(prop, "display") == 0) {
            if (strstr(val, "none")) {
                if (is_none) *is_none = 1;
            } else if (strstr(val, "flex") || strstr(val, "row")) {
                if (is_flex) *is_flex = 1;
            } else if (strstr(val, "inline")) {
                if (is_inline) *is_inline = 1;
            }
        } else if (strcasecmp(prop, "flex-direction") == 0) {
            if (strstr(val, "row")) {
                if (is_flex) *is_flex = 1;
            } else if (strstr(val, "column")) {
                if (is_flex) *is_flex = 0;
            }
        }
    }
}

static void css_parse_stylesheet(const char *css_text) {
    if (!css_text) return;
    const char *p = css_text;
    while (*p && css_rule_count < MAX_CSS_RULES) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        /* Find selector */
        const char *sel_start = p;
        while (*p && *p != '{') p++;
        if (*p != '{') break;

        const char *sel_end = p - 1;
        while (sel_end > sel_start && (*sel_end == ' ' || *sel_end == '\t' || *sel_end == '\r' || *sel_end == '\n')) sel_end--;
        int sel_len = (int)(sel_end - sel_start + 1);
        if (sel_len <= 0) { p++; continue; }
        if (sel_len > 31) sel_len = 31;

        p++; /* skip '{' */
        const char *body_start = p;
        while (*p && *p != '}') p++;
        int body_len = (int)(p - body_start);
        if (*p == '}') p++;

        char sel[32];
        strncpy(sel, sel_start, sel_len);
        sel[sel_len] = '\0';

        char body[256];
        if (body_len > 255) body_len = 255;
        strncpy(body, body_start, body_len);
        body[body_len] = '\0';

        css_rule_t *rule = &css_rules[css_rule_count++];
        memset(rule, 0, sizeof(css_rule_t));
        strncpy(rule->selector, sel, sizeof(rule->selector) - 1);
        rule->text_align = -1;

        css_parse_declarations(body, &rule->fg, &rule->bg, &rule->border_col,
                               &rule->has_fg, &rule->has_bg, &rule->has_border,
                               &rule->border_width, &rule->border_radius,
                               &rule->padding, &rule->margin, &rule->text_align,
                               &rule->is_bold, &rule->width,
                               &rule->is_none, &rule->is_flex, &rule->is_inline);
    }
}

static void apply_css_rule(const css_rule_t *r,
                           uint8_t *fg, uint8_t *bg, uint8_t *border_col,
                           int *has_fg, int *has_bg, int *has_border,
                           int *border_w, int *border_r,
                           int *padding, int *margin, int *align,
                           int *is_bold, int *width,
                           int *is_none, int *is_flex, int *is_inline) {
    if (r->has_fg && fg) { *fg = r->fg; if (has_fg) *has_fg = 1; }
    if (r->has_bg && bg) { *bg = r->bg; if (has_bg) *has_bg = 1; }
    if (r->has_border && border_col) { *border_col = r->border_col; if (has_border) *has_border = 1; }
    if (r->border_width && border_w) *border_w = r->border_width;
    if (r->border_radius && border_r) *border_r = r->border_radius;
    if (r->padding && padding) *padding = r->padding;
    if (r->margin && margin) *margin = r->margin;
    if (r->text_align >= 0 && align) *align = r->text_align;
    if (r->is_bold && is_bold) *is_bold = r->is_bold;
    if (r->width && width) *width = r->width;
    if (r->is_none && is_none) *is_none = 1;
    if (r->is_flex && is_flex) *is_flex = 1;
    if (r->is_inline && is_inline) *is_inline = 1;
}

static void css_compute_element_style(const char *tag, const char *cls, const char *id, const char *style,
                                      uint8_t *fg, uint8_t *bg, uint8_t *border_col,
                                      int *has_fg, int *has_bg, int *has_border,
                                      int *border_w, int *border_r,
                                      int *padding, int *margin, int *align,
                                      int *is_bold, int *width,
                                      int *is_none, int *is_flex, int *is_inline) {
    /* 1. Tag selectors */
    for (int i = 0; i < css_rule_count; i++) {
        if (strcasecmp(css_rules[i].selector, tag) == 0) {
            apply_css_rule(&css_rules[i], fg, bg, border_col, has_fg, has_bg, has_border,
                           border_w, border_r, padding, margin, align, is_bold, width,
                           is_none, is_flex, is_inline);
        }
    }
    /* 2. Class selectors */
    if (cls && cls[0]) {
        for (int i = 0; i < css_rule_count; i++) {
            if (css_rules[i].selector[0] == '.') {
                if (strstr(cls, css_rules[i].selector + 1) != NULL) {
                    apply_css_rule(&css_rules[i], fg, bg, border_col, has_fg, has_bg, has_border,
                                   border_w, border_r, padding, margin, align, is_bold, width,
                                   is_none, is_flex, is_inline);
                }
            }
        }
    }
    /* 3. ID selectors */
    if (id && id[0]) {
        for (int i = 0; i < css_rule_count; i++) {
            if (css_rules[i].selector[0] == '#' && strcasecmp(id, css_rules[i].selector + 1) == 0) {
                apply_css_rule(&css_rules[i], fg, bg, border_col, has_fg, has_bg, has_border,
                               border_w, border_r, padding, margin, align, is_bold, width,
                               is_none, is_flex, is_inline);
            }
        }
    }
    /* 4. Inline style (highest priority) */
    if (style && style[0]) {
        css_parse_declarations(style, fg, bg, border_col, has_fg, has_bg, has_border,
                               border_w, border_r, padding, margin, align, is_bold, width,
                               is_none, is_flex, is_inline);
    }
}

/* ============================================================
 * RENDER BOX & LAYOUT CONTEXT
 * ============================================================ */
typedef struct ns_render_box {
    int x;
    int y;
    int w;
    int h;
    uint8_t fg_col;
    uint8_t bg_col;
    uint8_t border_col;
    char text[128];
    char src[256];
    char poster[128];
    char href[256];
    bool is_hr;
    bool is_container;
    bool is_card;
    bool is_table;
    bool is_table_cell;
    bool is_table_header;
    bool is_link;
    bool is_button;
    bool is_input;
    bool is_image;
    bool is_video;
    bool is_audio;
    bool is_playing;
    bool is_blockquote;
    bool has_custom_bg;
    bool has_custom_border;
    int border_radius;
    struct ns_render_box *next;
} ns_render_box_t;

static ns_render_box_t *box_head = NULL;
static ns_render_box_t *box_tail = NULL;

static void free_render_boxes(void) {
    ns_render_box_t *cur = box_head;
    while (cur) {
        ns_render_box_t *next = cur->next;
        free(cur);
        cur = next;
    }
    box_head = NULL;
    box_tail = NULL;
    ns_element_count = 0;
    memset(ns_elements, 0, sizeof(ns_elements));
    css_rule_count = 0;
}

static ns_render_box_t *add_render_box(void) {
    ns_render_box_t *box = (ns_render_box_t *)calloc(1, sizeof(ns_render_box_t));
    if (!box) return NULL;
    box->fg_col = doc_fg_color;
    box->bg_col = doc_bg_color;
    if (!box_head) {
        box_head = box;
        box_tail = box;
    } else {
        box_tail->next = box;
        box_tail = box;
    }
    return box;
}

void ns_engine_init(void) {
    serial_printf(COM1_BASE, "[NetSurf] Initializing NetSurf Engine with Real CSS Box Model & Duktape JS...\n");
    image_decoder_init();
    free_render_boxes();
}

/* Layout Context State */
typedef struct {
    int cur_x;
    int cur_y;
    int base_x;
    int view_w;
    int line_h;
    int list_counter;
    bool is_ordered_list;
    uint8_t cur_fg;
    uint8_t cur_bg;
    bool in_link;
    char link_href[128];
    int align; /* 0=left, 1=center, 2=right */
    char form_action[128];
    char form_method[16];

    /* Horizontal Row / Flex Layout State */
    bool in_row;
    int row_start_y;
    int row_max_h;
    int row_base_x;

    /* Two-Pass Line Buffering for Centering & Alignment */
    ns_render_box_t *line_boxes[64];
    int line_box_count;
    int line_elem_indices[64];
    int line_elem_count;
} layout_ctx_t;

static void layout_break_line(layout_ctx_t *ctx) {
    if (ctx->line_box_count > 0) {
        int first_x = ctx->line_boxes[0]->x;
        int last_x = ctx->line_boxes[ctx->line_box_count - 1]->x + ctx->line_boxes[ctx->line_box_count - 1]->w;
        int line_w = last_x - first_x;

        if (!ctx->in_row && ctx->align == ALIGN_CENTER) {
            int target_x = ctx->base_x + (ctx->view_w - line_w) / 2;
            if (target_x < ctx->base_x) target_x = ctx->base_x;
            int shift = target_x - first_x;
            if (shift != 0) {
                for (int i = 0; i < ctx->line_box_count; i++) {
                    ctx->line_boxes[i]->x += shift;
                }
                for (int i = 0; i < ctx->line_elem_count; i++) {
                    int ei = ctx->line_elem_indices[i];
                    if (ei >= 0 && ei < ns_element_count) {
                        ns_elements[ei].x += shift;
                    }
                }
            }
        } else if (!ctx->in_row && ctx->align == ALIGN_RIGHT) {
            int target_x = ctx->base_x + ctx->view_w - line_w;
            if (target_x < ctx->base_x) target_x = ctx->base_x;
            int shift = target_x - first_x;
            if (shift != 0) {
                for (int i = 0; i < ctx->line_box_count; i++) {
                    ctx->line_boxes[i]->x += shift;
                }
                for (int i = 0; i < ctx->line_elem_count; i++) {
                    int ei = ctx->line_elem_indices[i];
                    if (ei >= 0 && ei < ns_element_count) {
                        ns_elements[ei].x += shift;
                    }
                }
            }
        }
        ctx->line_box_count = 0;
        ctx->line_elem_count = 0;
    }
    if (ctx->cur_x > ctx->base_x) {
        ctx->cur_x = ctx->base_x;
        int step = (ctx->in_row && ctx->row_max_h > 0) ? ctx->row_max_h : ctx->line_h;
        ctx->cur_y += step;
        if (ctx->in_row) ctx->row_max_h = ctx->line_h;
    }
}

/* Add text fragment with word wrapping and line recording */
static void layout_add_text(layout_ctx_t *ctx, const char *text) {
    if (!text || !*text) return;
    int max_x = ctx->base_x + ctx->view_w - 6;

    const char *p = text;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;

        const char *w_start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        int w_len = (int)(p - w_start);
        if (w_len <= 0) continue;

        char word[128];
        int cp_len = w_len < 120 ? w_len : 120;
        for (int i = 0; i < cp_len; i++) word[i] = w_start[i];
        word[cp_len] = '\0';

        int word_px = cp_len * 6;
        if (ctx->cur_x + word_px > max_x && ctx->cur_x > ctx->base_x) {
            layout_break_line(ctx);
        }

        ns_render_box_t *b = add_render_box();
        if (b) {
            b->x = ctx->cur_x;
            b->y = ctx->cur_y;
            b->w = word_px;
            b->h = ctx->line_h;
            b->fg_col = ctx->cur_fg;
            b->bg_col = ctx->cur_bg;
            b->is_link = ctx->in_link;
            strncpy(b->text, word, sizeof(b->text) - 1);

            if (ctx->in_row && ctx->line_h > ctx->row_max_h) {
                ctx->row_max_h = ctx->line_h;
            }

            if (ctx->line_box_count < 64) {
                ctx->line_boxes[ctx->line_box_count++] = b;
            }

            if (ctx->in_link) {
                strncpy(b->href, ctx->link_href, sizeof(b->href) - 1);
                if (ns_element_count < NS_MAX_INTERACTIVE) {
                    ns_elements[ns_element_count].x = b->x;
                    ns_elements[ns_element_count].y = b->y;
                    ns_elements[ns_element_count].w = b->w;
                    ns_elements[ns_element_count].h = b->h;
                    ns_elements[ns_element_count].is_link = true;
                    ns_elements[ns_element_count].fg_col = b->fg_col;
                    ns_elements[ns_element_count].bg_col = b->bg_col;
                    ns_elements[ns_element_count].border_col = b->border_col;
                    strncpy(ns_elements[ns_element_count].text, b->text, 63);
                    strncpy(ns_elements[ns_element_count].href, b->href, 127);
                    if (ctx->line_elem_count < 64) {
                        ctx->line_elem_indices[ctx->line_elem_count++] = ns_element_count;
                    }
                    ns_element_count++;
                }
            }
        }
        ctx->cur_x += word_px + 6; /* space */
    }
}

/* ============================================================
 * RECURSIVE DOM TREE LAYOUT WALKER
 * ============================================================ */
static void walk_dom_layout(dom_node *node, layout_ctx_t *ctx, int depth) {
    if (!node) return;
    (void)depth;

    dom_node_type type;
    dom_node_get_node_type(node, &type);

    if (type == DOM_TEXT_NODE || type == DOM_CDATA_SECTION_NODE) {
        dom_string *val = NULL;
        dom_node_get_node_value(node, &val);
        if (val) {
            const unsigned char *data = (const unsigned char *)dom_string_data(val);
            char clean_buf[512];
            int ci = 0;
            while (*data && ci < 500) {
                if (*data == '\n' || *data == '\r' || *data == '\t') {
                    if (ci > 0 && clean_buf[ci - 1] != ' ') clean_buf[ci++] = ' ';
                    data++;
                } else if (data[0] == 0xE2 && data[1] == 0x80 && data[2] == 0xA2) {
                    clean_buf[ci++] = '|';
                    data += 3;
                } else if (*data >= 32 && *data <= 126) {
                    clean_buf[ci++] = (char)*data;
                    data++;
                } else {
                    data++;
                }
            }
            clean_buf[ci] = '\0';
            layout_add_text(ctx, clean_buf);
            dom_string_unref(val);
        }
        return;
    }

    if (type == DOM_ELEMENT_NODE) {
        dom_string *tag_name = NULL;
        dom_node_get_node_name(node, &tag_name);
        if (tag_name) {
            const char *tag = dom_string_data(tag_name);

            /* Suppress <noscript> tags completely so warnings are never displayed */
            if (strcasecmp(tag, "noscript") == 0) {
                dom_string_unref(tag_name);
                return;
            }

            /* Execute <script> contents directly via Duktape JS */
            if (strcasecmp(tag, "script") == 0) {
                dom_node *schild = NULL;
                dom_node_get_first_child(node, &schild);
                if (schild) {
                    dom_string *sval = NULL;
                    dom_node_get_node_value(schild, &sval);
                    if (sval) {
                        const char *script_code = dom_string_data(sval);
                        if (script_code && script_code[0]) {
                            serial_printf(COM1_BASE, "[JS] Executing inline page <script>...\n");
                            js_engine_eval(script_code);
                        }
                        dom_string_unref(sval);
                    }
                    dom_node_unref(schild);
                }
                dom_string_unref(tag_name);
                return;
            }

            /* Parse <style> blocks into CSS rules */
            if (strcasecmp(tag, "style") == 0) {
                dom_node *schild = NULL;
                dom_node_get_first_child(node, &schild);
                if (schild) {
                    dom_string *sval = NULL;
                    dom_node_get_node_value(schild, &sval);
                    if (sval) {
                        const char *css_text = dom_string_data(sval);
                        if (css_text && css_text[0]) {
                            css_parse_stylesheet(css_text);
                        }
                        dom_string_unref(sval);
                    }
                    dom_node_unref(schild);
                }
                dom_string_unref(tag_name);
                return;
            }

            if (strcasecmp(tag, "title") == 0) {
                doc_title[0] = '\0';
                dom_node *tchild = NULL;
                dom_node_get_first_child(node, &tchild);
                if (tchild) {
                    dom_string *tval = NULL;
                    dom_node_get_node_value(tchild, &tval);
                    if (tval) {
                        strncpy(doc_title, dom_string_data(tval), sizeof(doc_title) - 1);
                        dom_string_unref(tval);
                    }
                    dom_node_unref(tchild);
                }
                dom_string_unref(tag_name);
                return;
            }

            /* Extract Attributes via NamedNodeMap */
            char attr_href[256] = "";
            char attr_type[32] = "";
            char attr_value[64] = "";
            char attr_src[256] = "";
            char attr_poster[128] = "";
            char attr_alt[64] = "";
            char attr_class[64] = "";
            char attr_style[256] = "";
            char attr_bgcolor[32] = "";
            char attr_name[32] = "";
            char attr_placeholder[32] = "";
            char attr_onclick[128] = "";
            char attr_action[128] = "";
            char attr_method[16] = "";
            char attr_id[32] = "";
            char attr_role[32] = "";
            char attr_width[32] = "";
            char attr_height[32] = "";

            dom_namednodemap *attrs = NULL;
            dom_node_get_attributes(node, &attrs);
            if (attrs) {
                uint32_t attr_len = 0;
                dom_namednodemap_get_length(attrs, &attr_len);
                for (uint32_t i = 0; i < attr_len; i++) {
                    dom_node *an = NULL;
                    dom_namednodemap_item(attrs, i, &an);
                    if (an) {
                        dom_string *an_name = NULL;
                        dom_string *an_val = NULL;
                        dom_node_get_node_name(an, &an_name);
                        dom_attr_get_value((dom_attr *)an, &an_val);
                        if (an_name && an_val) {
                            const char *an_n = dom_string_data(an_name);
                            const char *an_v = dom_string_data(an_val);
                            if (strcasecmp(an_n, "href") == 0) strncpy(attr_href, an_v, sizeof(attr_href) - 1);
                            else if (strcasecmp(an_n, "type") == 0) strncpy(attr_type, an_v, sizeof(attr_type) - 1);
                            else if (strcasecmp(an_n, "value") == 0) strncpy(attr_value, an_v, sizeof(attr_value) - 1);
                            else if (strcasecmp(an_n, "src") == 0) strncpy(attr_src, an_v, sizeof(attr_src) - 1);
                            else if (strcasecmp(an_n, "poster") == 0) strncpy(attr_poster, an_v, sizeof(attr_poster) - 1);
                            else if (strcasecmp(an_n, "alt") == 0) strncpy(attr_alt, an_v, sizeof(attr_alt) - 1);
                            else if (strcasecmp(an_n, "class") == 0) strncpy(attr_class, an_v, sizeof(attr_class) - 1);
                            else if (strcasecmp(an_n, "style") == 0) strncpy(attr_style, an_v, sizeof(attr_style) - 1);
                            else if (strcasecmp(an_n, "bgcolor") == 0) strncpy(attr_bgcolor, an_v, sizeof(attr_bgcolor) - 1);
                            else if (strcasecmp(an_n, "name") == 0) strncpy(attr_name, an_v, sizeof(attr_name) - 1);
                            else if (strcasecmp(an_n, "placeholder") == 0) strncpy(attr_placeholder, an_v, sizeof(attr_placeholder) - 1);
                            else if (strcasecmp(an_n, "onclick") == 0) strncpy(attr_onclick, an_v, sizeof(attr_onclick) - 1);
                            else if (strcasecmp(an_n, "action") == 0) strncpy(attr_action, an_v, sizeof(attr_action) - 1);
                            else if (strcasecmp(an_n, "method") == 0) strncpy(attr_method, an_v, sizeof(attr_method) - 1);
                            else if (strcasecmp(an_n, "id") == 0) strncpy(attr_id, an_v, sizeof(attr_id) - 1);
                            else if (strcasecmp(an_n, "role") == 0) strncpy(attr_role, an_v, sizeof(attr_role) - 1);
                            else if (strcasecmp(an_n, "width") == 0) strncpy(attr_width, an_v, sizeof(attr_width) - 1);
                            else if (strcasecmp(an_n, "height") == 0) strncpy(attr_height, an_v, sizeof(attr_height) - 1);
                        }
                        if (an_name) dom_string_unref(an_name);
                        if (an_val) dom_string_unref(an_val);
                        dom_node_unref(an);
                    }
                }
                dom_namednodemap_unref(attrs);
            }

            /* Tag classification */
            bool is_h1 = (strcasecmp(tag, "h1") == 0);
            bool is_h2 = (strcasecmp(tag, "h2") == 0);
            bool is_h3 = (strcasecmp(tag, "h3") == 0);
            bool is_h = (is_h1 || is_h2 || is_h3);
            bool is_p = (strcasecmp(tag, "p") == 0);
            bool is_ol = (strcasecmp(tag, "ol") == 0);
            bool is_ul = (strcasecmp(tag, "ul") == 0);
            bool is_li = (strcasecmp(tag, "li") == 0);
            bool is_center = (strcasecmp(tag, "center") == 0);
            bool is_header = (strcasecmp(tag, "header") == 0);
            bool is_nav = (strcasecmp(tag, "nav") == 0);
            bool is_div = (strcasecmp(tag, "div") == 0 || strcasecmp(tag, "section") == 0 ||
                           strcasecmp(tag, "article") == 0 || is_header || is_nav ||
                           strcasecmp(tag, "footer") == 0 || is_center);
            bool is_form = (strcasecmp(tag, "form") == 0);
            bool is_table = (strcasecmp(tag, "table") == 0);
            bool is_tr = (strcasecmp(tag, "tr") == 0);
            bool is_td = (strcasecmp(tag, "td") == 0 || strcasecmp(tag, "th") == 0);
            bool is_bquote = (strcasecmp(tag, "blockquote") == 0);
            bool is_hr = (strcasecmp(tag, "hr") == 0);
            bool is_a = (strcasecmp(tag, "a") == 0);
            bool is_btn = (strcasecmp(tag, "button") == 0);
            bool is_inp = (strcasecmp(tag, "input") == 0);
            bool is_img = (strcasecmp(tag, "img") == 0);
            bool is_video = (strcasecmp(tag, "video") == 0);
            bool is_audio = (strcasecmp(tag, "audio") == 0);
            bool is_code = (strcasecmp(tag, "pre") == 0 || strcasecmp(tag, "code") == 0);
            bool is_mark = (strcasecmp(tag, "mark") == 0);

            /* Filter hidden inputs */
            if (is_inp && strcasecmp(attr_type, "hidden") == 0) {
                dom_string_unref(tag_name);
                return;
            }
            if (is_inp && strcasecmp(attr_type, "submit") == 0) {
                is_btn = true;
                is_inp = false;
            }

            /* Check <body> for background and text color */
            if (strcasecmp(tag, "body") == 0) {
                if (attr_bgcolor[0]) doc_bg_color = css_parse_color(attr_bgcolor, doc_bg_color);
                int has_fg = 0, has_bg = 0, has_b = 0, bw = 0, br = 0, pad = 0, mg = 0, al = -1, bold = 0, wd = 0;
                int snone = 0, sflex = 0, sinl = 0;
                css_compute_element_style(tag, attr_class, attr_id, attr_style,
                                          &doc_fg_color, &doc_bg_color, NULL,
                                          &has_fg, &has_bg, &has_b, &bw, &br, &pad, &mg, &al, &bold, &wd,
                                          &snone, &sflex, &sinl);
                ctx->cur_bg = doc_bg_color;
                ctx->cur_fg = doc_fg_color;
            }

            /* Compute Full CSS Cascade for this Element */
            uint8_t elem_fg = ctx->cur_fg;
            uint8_t elem_bg = ctx->cur_bg;
            uint8_t elem_border = GRAY(14);
            int has_fg = 0, has_bg = 0, has_border = 0;
            int border_w = 0, border_r = 0;
            int padding = 0, margin = 0;
            int elem_align = -1;
            int is_bold = 0, elem_w = 0;
            int is_none = 0, is_flex = 0, is_inline = 0;

            css_compute_element_style(tag, attr_class, attr_id, attr_style,
                                      &elem_fg, &elem_bg, &elem_border,
                                      &has_fg, &has_bg, &has_border,
                                      &border_w, &border_r,
                                      &padding, &margin, &elem_align,
                                      &is_bold, &elem_w,
                                      &is_none, &is_flex, &is_inline);

            if (attr_width[0] && elem_w <= 0) {
                int aw = atoi(attr_width);
                if (aw > 0) elem_w = aw;
            }

            /* Suppress display:none, hidden attribute, and hidden dropdown drawers */
            if (is_none || strstr(attr_style, "display:none") || strstr(attr_style, "display: none") ||
                strstr(attr_class, "hidden") || strstr(attr_class, "vector-dropdown-content") ||
                strstr(attr_id, "vector-main-menu-unpinned-container") || strstr(attr_class, "mw-empty-elt") ||
                strstr(attr_class, "navigation-drawer") || strstr(attr_id, "mw-mf-page-left") ||
                strstr(attr_class, "minerva-user-navigation") || strstr(attr_id, "main-menu-input") ||
                strstr(attr_id, "mw-mf-main-menu-button") || strstr(attr_class, "minerva-badge-container") ||
                strstr(attr_class, "main-menu-mask") || strstr(attr_class, "visually-hidden") ||
                strstr(attr_class, "screen-reader-text") || strstr(attr_class, "sr-only")) {
                dom_string_unref(tag_name);
                return;
            }

            /* Context Save for Stack Restoration */
            uint8_t prev_fg = ctx->cur_fg;
            uint8_t prev_bg = ctx->cur_bg;
            bool prev_in_link = ctx->in_link;
            char prev_href[128];
            strncpy(prev_href, ctx->link_href, sizeof(prev_href));
            int prev_align = ctx->align;
            bool prev_in_row = ctx->in_row;
            int prev_row_start_y = ctx->row_start_y;
            int prev_row_max_h = ctx->row_max_h;
            int prev_row_base_x = ctx->row_base_x;

            if (is_center) elem_align = ALIGN_CENTER;
            if (has_fg) ctx->cur_fg = elem_fg;
            if (has_bg) ctx->cur_bg = elem_bg;
            if (elem_align >= 0) ctx->align = elem_align;

            /* Horizontal Row / Flex Layout Detection (exclude header and table rows unless styled as navbar/row) */
            bool is_row_container = is_flex ||
                                    (is_form && (strstr(attr_class, "search") || strstr(attr_role, "search"))) ||
                                    strstr(attr_class, "row") != NULL ||
                                    strstr(attr_class, "navbar") != NULL ||
                                    strstr(attr_class, "search-box") != NULL ||
                                    strstr(attr_class, "input-group") != NULL;

            bool entered_new_row = false;
            if (is_row_container && !ctx->in_row) {
                layout_break_line(ctx);
                ctx->in_row = true;
                ctx->row_start_y = ctx->cur_y;
                ctx->row_max_h = ctx->line_h;
                ctx->row_base_x = ctx->cur_x;
                entered_new_row = true;
            }

            bool is_card = is_div && (strstr(attr_class, "card") || (strstr(attr_class, "box") && !strstr(attr_class, "search-box")));
            bool is_block_container = is_card || is_bquote || is_table ||
                                      (!ctx->in_row && is_div && (has_border || (has_bg && elem_bg != doc_bg_color)));

            int saved_base_x = ctx->base_x;
            int saved_view_w = ctx->view_w;
            ns_render_box_t *container_box = NULL;

            /* Container Enter: Allocate Box and Indent Inner Area */
            if (is_block_container) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    int mg = (margin > 0) ? margin : (is_card ? 4 : 0);
                    ctx->cur_y += mg;
                }

                container_box = add_render_box();
                if (container_box) {
                    container_box->x = ctx->in_row ? ctx->cur_x : ctx->base_x;
                    container_box->y = ctx->cur_y;
                    container_box->w = ctx->in_row ? (elem_w > 0 ? elem_w : ctx->view_w) : ctx->view_w;
                    container_box->is_container = true;
                    container_box->is_card = is_card;
                    container_box->is_table = is_table;
                    container_box->is_blockquote = is_bquote;
                    container_box->bg_col = (has_bg && elem_bg != doc_bg_color) ? elem_bg : doc_bg_color;
                    container_box->border_col = has_border ? elem_border : doc_bg_color;
                    container_box->has_custom_bg = (has_bg && elem_bg != doc_bg_color) || (is_card && has_bg);
                    container_box->has_custom_border = has_border;
                    container_box->border_radius = border_r;
                }

                int pad = (padding > 0) ? padding : (is_card ? 4 : (is_bquote ? 6 : 0));
                if (!ctx->in_row) {
                    ctx->base_x += pad;
                    ctx->view_w -= (pad * 2);
                    ctx->cur_x = ctx->base_x;
                }
                ctx->cur_y += pad;
            } else if (is_h) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += (is_h1 ? 8 : (is_h2 ? 6 : 4));
                }
                if (!has_fg) {
                    ctx->cur_fg = is_h1 ? RGB(0,2,5) : (is_h2 ? RGB(0,3,5) : RGB(0,4,5));
                }
            } else if (is_p) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 4;
                }
            } else if (is_div || is_tr || is_td) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 2;
                }
            } else if (is_ol || is_ul) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 4;
                }
                ctx->list_counter = 1;
                ctx->is_ordered_list = is_ol;
            } else if (is_li) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_x = ctx->base_x + 6;
                } else {
                    ctx->cur_x += 8;
                }
                ns_render_box_t *b = add_render_box();
                if (b) {
                    b->x = ctx->cur_x;
                    b->y = ctx->cur_y;
                    b->w = 14;
                    b->h = ctx->line_h;
                    b->fg_col = RGB(0,2,4);
                    if (ctx->is_ordered_list) {
                        snprintf(b->text, sizeof(b->text), "%d.", ctx->list_counter++);
                    } else {
                        strcpy(b->text, "* ");
                    }
                    if (ctx->line_box_count < 64) ctx->line_boxes[ctx->line_box_count++] = b;
                }
                ctx->cur_x += 14;
            } else if (is_hr) {
                layout_break_line(ctx);
                ctx->cur_y += 4;
                ns_render_box_t *b = add_render_box();
                if (b) {
                    b->x = ctx->base_x;
                    b->y = ctx->cur_y;
                    b->w = ctx->view_w;
                    b->h = 1;
                    b->is_hr = true;
                    b->border_col = doc_bg_color == COL_WHITE ? GRAY(16) : GRAY(8);
                }
                ctx->cur_y += 6;
                dom_string_unref(tag_name);
                return;
            } else if (is_a) {
                ctx->in_link = true;
                if (!has_fg) ctx->cur_fg = RGB(0,2,5);
                if (attr_href[0]) strncpy(ctx->link_href, attr_href, sizeof(ctx->link_href) - 1);
            } else if (is_img) {
                int parsed_w = (attr_width[0]) ? atoi(attr_width) : (elem_w > 0 ? elem_w : 0);
                int parsed_h = (attr_height[0]) ? atoi(attr_height) : 0;

                /* If image is already in cache, use its actual decoded dimensions */
                decoded_image_t *cached = image_cache_get_or_load(attr_src, NULL, 0);
                if (cached && cached->width > 0 && cached->height > 0) {
                    if (parsed_w <= 0 && parsed_h <= 0) {
                        parsed_w = cached->width;
                        parsed_h = cached->height;
                    } else if (parsed_w > 0 && parsed_h <= 0) {
                        parsed_h = (parsed_w * cached->height) / cached->width;
                    } else if (parsed_h > 0 && parsed_w <= 0) {
                        parsed_w = (parsed_h * cached->width) / cached->height;
                    }
                }

                int max_w = ctx->view_w - 12;
                if (max_w > 200) max_w = 200;
                int max_h = 90;

                int img_w = parsed_w > 0 ? parsed_w : (ctx->in_row ? 64 : 110);
                int img_h = parsed_h > 0 ? parsed_h : (ctx->in_row ? 24 : 65);

                /* Scale down proportionally to fit viewport */
                if (img_w > max_w) {
                    img_h = (img_h * max_w) / img_w;
                    img_w = max_w;
                }
                if (img_h > max_h) {
                    img_w = (img_w * max_h) / img_h;
                    img_h = max_h;
                }
                if (img_w < 30) img_w = 30;
                if (img_h < 18) img_h = 18;

                if (ctx->cur_x + img_w > ctx->base_x + ctx->view_w - 6 && ctx->cur_x > ctx->base_x) {
                    layout_break_line(ctx);
                } else if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 4;
                }

                ns_render_box_t *b = add_render_box();
                if (b) {
                    b->x = ctx->cur_x;
                    b->y = ctx->cur_y;
                    b->w = img_w;
                    b->h = img_h;
                    b->is_image = true;
                    b->bg_col = doc_bg_color == COL_WHITE ? GRAY(23) : GRAY(3);
                    b->border_col = doc_bg_color == COL_WHITE ? GRAY(15) : GRAY(7);
                    strncpy(b->src, attr_src, sizeof(b->src) - 1);
                    if (attr_alt[0]) strncpy(b->text, attr_alt, sizeof(b->text) - 1);
                    else if (attr_src[0]) strncpy(b->text, attr_src, sizeof(b->text) - 1);
                    else strcpy(b->text, "Image");

                    if (ctx->in_link) {
                        b->is_link = true;
                        strncpy(b->href, ctx->link_href, sizeof(b->href) - 1);
                    }

                    if (ctx->in_row && img_h > ctx->row_max_h) {
                        ctx->row_max_h = img_h;
                    }

                    if (ns_element_count < NS_MAX_INTERACTIVE) {
                        ns_elements[ns_element_count].x = b->x;
                        ns_elements[ns_element_count].y = b->y;
                        ns_elements[ns_element_count].w = b->w;
                        ns_elements[ns_element_count].h = b->h;
                        ns_elements[ns_element_count].is_image = true;
                        ns_elements[ns_element_count].fg_col = b->fg_col;
                        ns_elements[ns_element_count].bg_col = b->bg_col;
                        ns_elements[ns_element_count].border_col = b->border_col;
                        strncpy(ns_elements[ns_element_count].text, b->text, sizeof(ns_elements[0].text) - 1);
                        strncpy(ns_elements[ns_element_count].src, b->src, sizeof(ns_elements[0].src) - 1);
                        if (ctx->in_link) {
                            ns_elements[ns_element_count].is_link = true;
                            strncpy(ns_elements[ns_element_count].href, ctx->link_href, sizeof(ns_elements[0].href) - 1);
                        }
                        ns_element_count++;
                    }
                }
                if (ctx->in_row) {
                    ctx->cur_x += img_w + 4;
                } else {
                    ctx->cur_y += img_h + 4;
                }
                dom_string_unref(tag_name);
                return;
            } else if (is_video) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 4;
                }
                int vid_w = (elem_w > 0 && elem_w < ctx->view_w) ? elem_w : (ctx->view_w - 8);
                int vid_h = 46;
                ns_render_box_t *b = add_render_box();
                if (b) {
                    b->x = ctx->cur_x;
                    b->y = ctx->cur_y;
                    b->w = vid_w;
                    b->h = vid_h;
                    b->is_video = true;
                    b->bg_col = COL_BLACK;
                    b->border_col = RGB(1,2,3);
                    strncpy(b->src, attr_src, sizeof(b->src) - 1);
                    if (attr_poster[0]) strncpy(b->poster, attr_poster, sizeof(b->poster) - 1);
                    else strcpy(b->poster, "video_thumb.bmp");
                    if (attr_src[0]) strncpy(b->text, attr_src, sizeof(b->text) - 1);
                    else strcpy(b->text, "HTML5 Video Player");

                    if (ctx->in_row && vid_h > ctx->row_max_h) {
                        ctx->row_max_h = vid_h;
                    }

                    if (ns_element_count < NS_MAX_INTERACTIVE) {
                        ns_elements[ns_element_count].x = b->x;
                        ns_elements[ns_element_count].y = b->y;
                        ns_elements[ns_element_count].w = b->w;
                        ns_elements[ns_element_count].h = b->h;
                        ns_elements[ns_element_count].is_video = true;
                        ns_elements[ns_element_count].fg_col = COL_WHITE;
                        ns_elements[ns_element_count].bg_col = COL_BLACK;
                        ns_elements[ns_element_count].border_col = RGB(1,2,3);
                        strncpy(ns_elements[ns_element_count].text, b->text, 63);
                        strncpy(ns_elements[ns_element_count].src, b->src, 127);
                        strncpy(ns_elements[ns_element_count].poster, b->poster, 127);
                        serial_printf(COM1_BASE, "[NS_VIDEO] elem %d at (%d,%d) %dx%d src=%s\n", ns_element_count, b->x, b->y, b->w, b->h, b->src);
                        ns_element_count++;
                    }
                }
                if (ctx->in_row) {
                    ctx->cur_x += vid_w + 4;
                } else {
                    ctx->cur_y += vid_h + 4;
                }
                dom_string_unref(tag_name);
                return;
            } else if (is_audio) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 3;
                }
                int aud_w = (elem_w > 0 && elem_w < ctx->view_w) ? elem_w : (ctx->view_w - 8);
                int aud_h = 20;
                ns_render_box_t *b = add_render_box();
                if (b) {
                    b->x = ctx->cur_x;
                    b->y = ctx->cur_y;
                    b->w = aud_w;
                    b->h = aud_h;
                    b->is_audio = true;
                    b->bg_col = RGB(0,0,1);
                    b->border_col = RGB(0,2,4);
                    strncpy(b->src, attr_src, sizeof(b->src) - 1);
                    if (attr_src[0]) strncpy(b->text, attr_src, sizeof(b->text) - 1);
                    else strcpy(b->text, "HTML5 In-Browser Audio");

                    if (ctx->in_row && aud_h > ctx->row_max_h) {
                        ctx->row_max_h = aud_h;
                    }

                    if (ns_element_count < NS_MAX_INTERACTIVE) {
                        ns_elements[ns_element_count].x = b->x;
                        ns_elements[ns_element_count].y = b->y;
                        ns_elements[ns_element_count].w = b->w;
                        ns_elements[ns_element_count].h = b->h;
                        ns_elements[ns_element_count].is_audio = true;
                        ns_elements[ns_element_count].fg_col = COL_WHITE;
                        ns_elements[ns_element_count].bg_col = RGB(0,0,1);
                        ns_elements[ns_element_count].border_col = RGB(0,2,4);
                        strncpy(ns_elements[ns_element_count].text, b->text, 63);
                        strncpy(ns_elements[ns_element_count].src, b->src, 127);
                        serial_printf(COM1_BASE, "[NS_AUDIO] elem %d at (%d,%d) %dx%d src=%s\n", ns_element_count, b->x, b->y, b->w, b->h, b->src);
                        ns_element_count++;
                    }
                }
                if (ctx->in_row) {
                    ctx->cur_x += aud_w + 4;
                } else {
                    ctx->cur_y += aud_h + 4;
                }
                dom_string_unref(tag_name);
                return;
            } else if (is_btn || is_inp) {
                char val_buf[64] = "";
                if (attr_value[0]) {
                    strncpy(val_buf, attr_value, sizeof(val_buf) - 1);
                } else if (is_btn) {
                    dom_node *bchild = NULL;
                    dom_node_get_first_child(node, &bchild);
                    while (bchild) {
                        dom_node_type btype;
                        dom_node_get_node_type(bchild, &btype);
                        if (btype == DOM_TEXT_NODE) {
                            dom_string *bval = NULL;
                            dom_node_get_node_value(bchild, &bval);
                            if (bval) {
                                const char *d = dom_string_data(bval);
                                while (*d && (*d == ' ' || *d == '\t' || *d == '\r' || *d == '\n')) d++;
                                if (*d) {
                                    strncpy(val_buf, d, sizeof(val_buf) - 1);
                                    int l = strlen(val_buf);
                                    while (l > 0 && (val_buf[l-1] == ' ' || val_buf[l-1] == '\t' || val_buf[l-1] == '\r' || val_buf[l-1] == '\n')) {
                                        val_buf[--l] = '\0';
                                    }
                                }
                                dom_string_unref(bval);
                            }
                        } else if (btype == DOM_ELEMENT_NODE) {
                            dom_node *gchild = NULL;
                            dom_node_get_first_child(bchild, &gchild);
                            while (gchild) {
                                dom_node_type gtype;
                                dom_node_get_node_type(gchild, &gtype);
                                if (gtype == DOM_TEXT_NODE) {
                                    dom_string *gval = NULL;
                                    dom_node_get_node_value(gchild, &gval);
                                    if (gval) {
                                        const char *d = dom_string_data(gval);
                                        while (*d && (*d == ' ' || *d == '\t' || *d == '\r' || *d == '\n')) d++;
                                        if (*d && !val_buf[0]) {
                                            strncpy(val_buf, d, sizeof(val_buf) - 1);
                                            int l = strlen(val_buf);
                                            while (l > 0 && (val_buf[l-1] == ' ' || val_buf[l-1] == '\t' || val_buf[l-1] == '\r' || val_buf[l-1] == '\n')) {
                                                val_buf[--l] = '\0';
                                            }
                                        }
                                        dom_string_unref(gval);
                                    }
                                }
                                dom_node *next_g = NULL;
                                dom_node_get_next_sibling(gchild, &next_g);
                                dom_node_unref(gchild);
                                gchild = next_g;
                            }
                        }
                        if (val_buf[0]) {
                            dom_node_unref(bchild);
                            break;
                        }
                        dom_node *next_b = NULL;
                        dom_node_get_next_sibling(bchild, &next_b);
                        dom_node_unref(bchild);
                        bchild = next_b;
                    }
                    if (!val_buf[0]) {
                        if (strstr(attr_class, "search") || strstr(attr_id, "search")) strcpy(val_buf, "Search");
                        else strcpy(val_buf, "Submit");
                    }
                }

                int bw = 0;
                if (is_inp) {
                    bw = (elem_w > 120 && elem_w < ctx->view_w - 50) ? elem_w : (ctx->in_row ? 150 : 180);
                } else {
                    bw = (elem_w > 0 && elem_w < ctx->view_w) ? elem_w : (int)(strlen(val_buf) * 6 + 14);
                    if (bw < 34) bw = 34;
                }

                if (ctx->cur_x + bw > ctx->base_x + ctx->view_w - 6 && ctx->cur_x > ctx->base_x) {
                    layout_break_line(ctx);
                }

                ns_render_box_t *b = add_render_box();
                if (b) {
                    b->x = ctx->cur_x;
                    b->y = ctx->cur_y;
                    b->w = bw;
                    b->h = 13;
                    b->is_button = is_btn;
                    b->is_input = is_inp;
                    b->bg_col = is_btn ? (has_bg ? elem_bg : RGB(0,2,4)) : (has_bg ? elem_bg : COL_WHITE);
                    b->fg_col = is_btn ? (has_fg ? elem_fg : COL_WHITE) : (has_fg ? elem_fg : COL_BLACK);
                    b->border_col = has_border ? elem_border : (is_btn ? RGB(0,1,3) : GRAY(10));
                    b->border_radius = border_r;
                    strncpy(b->text, val_buf, sizeof(b->text) - 1);

                    if (ctx->in_row && 14 > ctx->row_max_h) {
                        ctx->row_max_h = 14;
                    }

                    if (ctx->line_box_count < 64) ctx->line_boxes[ctx->line_box_count++] = b;

                    if (ns_element_count < NS_MAX_INTERACTIVE) {
                        int elem_idx = ns_element_count++;
                        ns_interactive_elem_t *el = &ns_elements[elem_idx];
                        el->x = b->x;
                        el->y = b->y;
                        el->w = b->w;
                        el->h = b->h;
                        el->is_button = is_btn;
                        el->is_input = is_inp;
                        el->fg_col = b->fg_col;
                        el->bg_col = b->bg_col;
                        el->border_col = b->border_col;
                        strncpy(el->text, b->text, 63);
                        strncpy(el->placeholder, attr_placeholder, 31);
                        strncpy(el->name, attr_name, 31);
                        strncpy(el->onclick, attr_onclick, 127);
                        strncpy(el->form_action, ctx->form_action, 127);
                        strncpy(el->form_method, ctx->form_method, 15);

                        if (ctx->line_elem_count < 64) {
                            ctx->line_elem_indices[ctx->line_elem_count++] = elem_idx;
                        }
                    }
                }
                ctx->cur_x += bw + 4;
                if (is_btn) {
                    dom_string_unref(tag_name);
                    return;
                }
            } else if (is_mark) {
                if (!has_bg) ctx->cur_bg = RGB(5,5,0);
                if (!has_fg) ctx->cur_fg = COL_BLACK;
            } else if (is_code) {
                if (!has_fg) ctx->cur_fg = RGB(0,3,1);
            }

            /* Record Form Actions */
            if (is_form) {
                if (attr_action[0]) strncpy(ctx->form_action, attr_action, sizeof(ctx->form_action) - 1);
                if (attr_method[0]) strncpy(ctx->form_method, attr_method, sizeof(ctx->form_method) - 1);
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 2;
                }
            }

            /* Traverse children */
            dom_node *child = NULL;
            dom_node_get_first_child(node, &child);
            while (child) {
                walk_dom_layout(child, ctx, depth + 1);
                dom_node *next = NULL;
                dom_node_get_next_sibling(child, &next);
                dom_node_unref(child);
                child = next;
            }

            /* Container Exit: Compute Exact Height and Restore Layout Margins */
            if (entered_new_row) {
                layout_break_line(ctx);
                int row_bottom = ctx->row_start_y + (ctx->row_max_h > 0 ? ctx->row_max_h : ctx->line_h);
                if (ctx->cur_y < row_bottom) ctx->cur_y = row_bottom;
                ctx->cur_y += 4;
                ctx->cur_x = ctx->base_x;
                ctx->in_row = prev_in_row;
                ctx->row_start_y = prev_row_start_y;
                ctx->row_max_h = prev_row_max_h;
                ctx->row_base_x = prev_row_base_x;
            }
            if (is_block_container) {
                if (!ctx->in_row) layout_break_line(ctx);
                int pad = (padding > 0) ? padding : (is_card ? 4 : (is_bquote ? 6 : 0));
                ctx->cur_y += pad;
                if (container_box) {
                    container_box->h = ctx->cur_y - container_box->y; /* COMPUTED CONTAINER HEIGHT! */
                }
                int mg = (margin > 0) ? margin : (is_card ? 4 : 0);
                ctx->cur_y += mg;
                ctx->base_x = saved_base_x;
                if (!ctx->in_row) ctx->cur_x = ctx->base_x;
                ctx->view_w = saved_view_w;
            } else if (is_form) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 4;
                }
            } else if (is_h) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += (is_h1 ? 6 : 4);
                }
            } else if (is_p) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 4;
                }
            } else if (is_div || is_tr || is_td) {
                if (!ctx->in_row) {
                    layout_break_line(ctx);
                    ctx->cur_y += 2;
                } else if (is_td) {
                    ctx->cur_x += 6;
                }
            }

            ctx->cur_fg = prev_fg;
            ctx->cur_bg = prev_bg;
            ctx->in_link = prev_in_link;
            ctx->align = prev_align;
            strncpy(ctx->link_href, prev_href, sizeof(ctx->link_href));
            dom_string_unref(tag_name);
            return;
        }
    }

    /* Traverse any other node children */
    dom_node *child = NULL;
    dom_node_get_first_child(node, &child);
    while (child) {
        walk_dom_layout(child, ctx, depth + 1);
        dom_node *next = NULL;
        dom_node_get_next_sibling(child, &next);
        dom_node_unref(child);
        child = next;
    }
}

void ns_engine_parse_html(const char *html_data, size_t len, const char *base_url) {
    (void)base_url;
    free_render_boxes();

    if (current_doc) {
        dom_node_unref((dom_node *)current_doc);
        current_doc = NULL;
    }

    doc_bg_color = COL_WHITE;
    doc_fg_color = COL_BLACK;

    /* Initialize or reset Duktape JS engine for the new page */
    js_engine_reset();

    dom_hubbub_parser_params params;
    memset(&params, 0, sizeof(params));
    params.enc = "UTF-8";
    params.fix_enc = false;
    params.enable_script = true;

    dom_hubbub_parser *parser = NULL;
    dom_hubbub_error err = dom_hubbub_parser_create(&params, &parser, &current_doc);
    if (err == DOM_HUBBUB_OK && parser) {
        dom_hubbub_parser_parse_chunk(parser, (const uint8_t *)html_data, len);
        dom_hubbub_parser_completed(parser);
        dom_hubbub_parser_destroy(parser);

        /* Build Layout Box Tree */
        layout_ctx_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.cur_x = 6;
        ctx.cur_y = 6;
        ctx.base_x = 6;
        ctx.view_w = 276;
        ctx.line_h = 12;
        ctx.cur_fg = doc_fg_color;
        ctx.cur_bg = doc_bg_color;
        ctx.align = ALIGN_LEFT;

        dom_element *doc_elem = NULL;
        dom_document_get_document_element(current_doc, &doc_elem);
        if (doc_elem) {
            walk_dom_layout((dom_node *)doc_elem, &ctx, 0);
            dom_node_unref((dom_node *)doc_elem);
        } else {
            walk_dom_layout((dom_node *)current_doc, &ctx, 0);
        }
        layout_break_line(&ctx);
        doc_content_height = ctx.cur_y + 16;
        serial_printf(COM1_BASE, "[NetSurf] Full DOM Layout generated: height=%d interactive=%d css_rules=%d bg=%d fg=%d\n",
                      doc_content_height, ns_element_count, css_rule_count, doc_bg_color, doc_fg_color);
        for (int ei = 0; ei < ns_element_count; ei++) {
            serial_printf(COM1_BASE, "  [ELEM %d] (%d,%d) %dx%d v=%d a=%d l=%d href='%s' src='%s' txt='%s'\n",
                          ei, ns_elements[ei].x, ns_elements[ei].y, ns_elements[ei].w, ns_elements[ei].h,
                          ns_elements[ei].is_video, ns_elements[ei].is_audio, ns_elements[ei].is_link,
                          ns_elements[ei].href, ns_elements[ei].src, ns_elements[ei].text);
        }
    } else {
        serial_printf(COM1_BASE, "[NetSurf] DOM parser create error: %d\n", err);
    }
}

uint8_t ns_engine_get_bg_color(void) {
    return doc_bg_color;
}

/* ============================================================
 * UNIVERSAL IMAGE RENDERER
 * Displays decoded RGB pixels from cache/decoder or an interactive
 * card allowing the user to load the image on request.
 * ============================================================ */
static void ns_fill_rect_clip(int x, int y, int w, int h, uint8_t col, int vx, int vy, int vw, int vh) {
    int x1 = x < vx ? vx : x;
    int y1 = y < vy ? vy : y;
    int x2 = (x + w) > (vx + vw) ? (vx + vw) : (x + w);
    int y2 = (y + h) > (vy + vh) ? (vy + vh) : (y + h);
    if (x2 > x1 && y2 > y1) {
        fill_rect(x1, y1, x2 - x1, y2 - y1, col);
    }
}

static void ns_draw_rect_clip(int x, int y, int w, int h, uint8_t col, int vx, int vy, int vw, int vh) {
    /* top */
    if (y >= vy && y < vy + vh) {
        int x1 = x < vx ? vx : x;
        int x2 = (x + w) > (vx + vw) ? (vx + vw) : (x + w);
        if (x2 > x1) draw_hline(x1, y, x2 - x1, col);
    }
    /* bottom */
    int by = y + h - 1;
    if (by >= vy && by < vy + vh) {
        int x1 = x < vx ? vx : x;
        int x2 = (x + w) > (vx + vw) ? (vx + vw) : (x + w);
        if (x2 > x1) draw_hline(x1, by, x2 - x1, col);
    }
    /* left */
    if (x >= vx && x < vx + vw) {
        int y1 = y < vy ? vy : y;
        int y2 = (y + h) > (vy + vh) ? (vy + vh) : (y + h);
        for (int r = y1; r < y2; r++) draw_hline(x, r, 1, col);
    }
    /* right */
    int rx = x + w - 1;
    if (rx >= vx && rx < vx + vw) {
        int y1 = y < vy ? vy : y;
        int y2 = (y + h) > (vy + vh) ? (vy + vh) : (y + h);
        for (int r = y1; r < y2; r++) draw_hline(rx, r, 1, col);
    }
}

static void ns_draw_hline_clip(int x, int y, int w, uint8_t col, int vx, int vy, int vw, int vh) {
    if (y >= vy && y < vy + vh) {
        int x1 = x < vx ? vx : x;
        int x2 = (x + w) > (vx + vw) ? (vx + vw) : (x + w);
        if (x2 > x1) draw_hline(x1, y, x2 - x1, col);
    }
}

static void render_universal_image(int rx, int ry, int box_w, int box_h, const char *src, const char *alt,
                                  int vx, int vy, int vw, int vh) {
    int canvas_w = box_w - 4;
    int canvas_h = box_h - 4;
    if (canvas_w < 10) canvas_w = 10;
    if (canvas_h < 10) canvas_h = 10;

    /* 1. Try Real Decoded Image from Cache (decoded via stb_image) */
    decoded_image_t *img = image_cache_get_or_load(src, NULL, 0);
    if (img && img->pixels && img->width > 0 && img->height > 0) {
        ns_fill_rect_clip(rx, ry, box_w, box_h, doc_bg_color, vx, vy, vw, vh);
        ns_draw_rect_clip(rx, ry, box_w, box_h, doc_bg_color == COL_WHITE ? GRAY(16) : GRAY(8), vx, vy, vw, vh);

        int render_w = canvas_w;
        int render_h = (canvas_w * img->height) / img->width;
        if (render_h > canvas_h) {
            render_h = canvas_h;
            render_w = (canvas_h * img->width) / img->height;
        }
        int off_x = rx + 2 + (canvas_w - render_w) / 2;
        int off_y = ry + 2 + (canvas_h - render_h) / 2;
        image_scale_blit(img, off_x, off_y, render_w, render_h, vx, vy, vw, vh);
        return;
    }

    /* 2. Unloaded Image Card: Shows [IMG] tag, descriptive label, and Click to Load */
    ns_fill_rect_clip(rx, ry, box_w, box_h, doc_bg_color == COL_WHITE ? GRAY(23) : GRAY(4), vx, vy, vw, vh);
    ns_draw_rect_clip(rx, ry, box_w, box_h, doc_bg_color == COL_WHITE ? GRAY(15) : GRAY(8), vx, vy, vw, vh);

    const char *label = (alt && alt[0]) ? alt : ((src && src[0]) ? src : "Image");
    int label_y = (box_h > 20) ? ry + (box_h - 18) / 2 : ry + 2;

    if (label_y >= vy && label_y + 8 <= vy + vh) {
        draw_str_clip(rx + 4, label_y, "[IMG]", RGB(0,2,5), 0xFF, rx + box_w - 6);
        draw_str_clip(rx + 36, label_y, label, doc_bg_color == COL_WHITE ? GRAY(6) : GRAY(18), 0xFF, rx + box_w - 6);
    }
    if (box_h >= 22 && label_y + 10 >= vy && label_y + 18 <= vy + vh) {
        draw_str_clip(rx + 4, label_y + 10, "[Click to Load Image]", RGB(0,3,1), 0xFF, rx + box_w - 6);
    }
}

int ns_engine_click_fallback(int page_x, int page_y, const char *base_url, char *out_nav_url, size_t nav_sz) {
    if (!box_head) return 0;

    for (ns_render_box_t *b = box_head; b; b = b->next) {
        if (page_x >= b->x && page_x < b->x + b->w &&
            page_y >= b->y && page_y < b->y + b->h) {
            if (b->is_image) {
                decoded_image_t *existing = image_cache_get_or_load(b->src, NULL, 0);
                if (!existing) {
                    decoded_image_t *loaded = image_fetch_and_cache(b->src, base_url);
                    if (loaded && loaded->pixels) {
                        return 1;
                    }
                    return 1;
                } else if (b->is_link && b->href[0] && out_nav_url && nav_sz > 0) {
                    html_resolve_url(base_url, b->href, out_nav_url, nav_sz);
                    return 2;
                }
                return 1;
            } else if (b->is_link && b->href[0] && out_nav_url && nav_sz > 0) {
                html_resolve_url(base_url, b->href, out_nav_url, nav_sz);
                return 2;
            }
        }
    }
    return 0;
}

/* ============================================================
 * HTML5 IN-BROWSER VIDEO & AUDIO PLAYERS (Live Media Subsystem)
 * ============================================================ */
static void video_blit_frame(const uint8_t *fb, int rx, int ry, int box_w, int box_h, int vx, int vy, int vw, int vh) {
    if (!fb || box_w <= 0 || box_h <= 0) return;
    int x0 = rx < vx ? vx : rx;
    int y0 = ry < vy ? vy : ry;
    int x1 = (rx + box_w) > (vx + vw) ? (vx + vw) : (rx + box_w);
    int y1 = (ry + box_h) > (vy + vh) ? (vy + vh) : (ry + box_h);
    for (int y = y0; y < y1; y++) {
        int sy = (y - ry) * 100 / box_h;
        if (sy >= 100) sy = 99;
        const uint8_t *src_row = fb + sy * 160;
        for (int x = x0; x < x1; x++) {
            int sx = (x - rx) * 160 / box_w;
            if (sx >= 160) sx = 159;
            draw_pixel_fb(x, y, src_row[sx]);
        }
    }
}

static void render_universal_video(int rx, int ry, int box_w, int box_h, const char *src, const char *poster, const char *title, bool is_playing, int vx, int vy, int vw, int vh) {
    const uint8_t *v_fb = video_get_frame_buffer();
    bool playing = is_playing || (video_get_state() == VIDEO_STATE_PLAYING);

    /* 1. Video Frames or Thumbnail / Poster Frame Backdrop */
    if (playing && v_fb) {
        video_blit_frame(v_fb, rx, ry, box_w, box_h, vx, vy, vw, vh);
    } else {
        const char *thumb_path = (poster && poster[0]) ? poster : "video_thumb.bmp";
        const decoded_image_t *thumb = image_cache_get_or_load(thumb_path, NULL, 0);
        if (thumb && thumb->pixels && thumb->width > 0 && thumb->height > 0) {
            image_scale_blit(thumb, rx, ry, box_w, box_h, vx, vy, vw, vh);
        } else {
            ns_fill_rect_clip(rx, ry, box_w, box_h, RGB(0,1,2), vx, vy, vw, vh);
        }
    }

    /* Translucent border */
    ns_draw_rect_clip(rx, ry, box_w, box_h, playing ? RGB(0,4,5) : RGB(1,2,3), vx, vy, vw, vh);

    /* 2. Top Title Bar Overlay */
    ns_fill_rect_clip(rx, ry, box_w, 13, RGB(0,0,1), vx, vy, vw, vh);
    draw_str(rx + 4, ry + 2, playing ? "[>]" : "[*]", playing ? RGB(0,5,1) : RGB(5,4,0), 0xFF);
    const char *vlabel = (title && title[0]) ? title : ((src && src[0]) ? src : "HTML5 In-Browser Video");
    draw_str_clip(rx + 24, ry + 2, vlabel, COL_WHITE, 0xFF, rx + box_w - 6);

    /* 3. Center Floating Play / Pause Button if paused */
    if (!playing) {
        int btn_w = 68;
        int btn_h = 16;
        int bx = rx + (box_w - btn_w) / 2;
        int by = ry + (box_h - btn_h) / 2;
        ns_fill_rect_clip(bx, by, btn_w, btn_h, RGB(0,1,3), vx, vy, vw, vh);
        ns_draw_rect_clip(bx, by, btn_w, btn_h, COL_WHITE, vx, vy, vw, vh);
        draw_str(bx + 8, by + 4, "> Play", COL_WHITE, 0xFF);
    }

    /* 4. Bottom Scrubber & Progress Bar */
    int cb_y = ry + box_h - 11;
    ns_fill_rect_clip(rx, cb_y, box_w, 11, RGB(0,0,1), vx, vy, vw, vh);
    int track_w = box_w - 55;
    if (track_w > 10) {
        draw_hline(rx + 6, cb_y + 5, track_w, GRAY(8));
        uint32_t dur = video_get_duration_ms();
        uint32_t elap = video_get_elapsed_ms();
        int prog = (dur > 0) ? (int)(((uint32_t)elap * (uint32_t)track_w) / dur) : (track_w / 4);
        if (prog > track_w) prog = track_w;
        if (prog < 2) prog = 2;
        draw_hline(rx + 6, cb_y + 5, prog, COL_CYAN);
        fill_rect(rx + 6 + prog - 1, cb_y + 3, 3, 5, COL_WHITE);
    }
    uint32_t sec = video_get_elapsed_ms() / 1000;
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02u:%02u", sec / 60, sec % 60);
    draw_str(rx + box_w - 44, cb_y + 2, time_str, GRAY(18), 0xFF);
}

static void render_universal_audio(int rx, int ry, int box_w, int box_h, const char *src, const char *title, bool is_playing, int vx, int vy, int vw, int vh) {
    bool playing = is_playing || (audio_get_state() == AUDIO_STATE_PLAYING);

    /* Background pill */
    ns_fill_rect_clip(rx, ry, box_w, box_h, RGB(0,0,1), vx, vy, vw, vh);
    ns_draw_rect_clip(rx, ry, box_w, box_h, playing ? RGB(0,4,2) : RGB(1,2,3), vx, vy, vw, vh);

    /* Play/Pause Button [>] or [||] */
    int btn_x = rx + 4;
    int btn_y = ry + 3;
    int btn_w = 14;
    int btn_h = box_h - 6;
    ns_fill_rect_clip(btn_x, btn_y, btn_w, btn_h, playing ? RGB(0,3,1) : RGB(0,1,3), vx, vy, vw, vh);
    ns_draw_rect_clip(btn_x, btn_y, btn_w, btn_h, COL_WHITE, vx, vy, vw, vh);
    draw_str(btn_x + 3, btn_y + 2, playing ? "||" : ">", COL_WHITE, 0xFF);

    /* Track Title */
    const char *alabel = (title && title[0]) ? title : ((src && src[0]) ? src : "HTML5 In-Browser Audio");
    int title_max_x = rx + box_w - 95;
    if (title_max_x < rx + 25) title_max_x = rx + 25;
    draw_str_clip(rx + 22, ry + 5, alabel, playing ? RGB(0,5,2) : COL_WHITE, 0xFF, title_max_x);

    /* Mini Spectrum Bars (5 bars) when playing */
    if (playing) {
        uint8_t spec[16];
        audio_get_spectrum(spec, 16);
        for (int i = 0; i < 5; i++) {
            int bh = (spec[i * 2] * (box_h - 6)) / 32;
            if (bh < 2) bh = 2;
            if (bh > box_h - 6) bh = box_h - 6;
            int sx = rx + box_w - 85 + i * 4;
            int sy = ry + box_h - 3 - bh;
            ns_fill_rect_clip(sx, sy, 3, bh, RGB(0,5,2), vx, vy, vw, vh);
        }
    }

    /* Scrubber / Progress Bar */
    int tr_x = rx + box_w - 60;
    int tr_w = 24;
    draw_hline(tr_x, ry + box_h / 2, tr_w, GRAY(8));
    uint32_t dur = audio_get_duration_ms();
    uint32_t elap = audio_get_elapsed_ms();
    int prog = (dur > 0) ? (int)(((uint32_t)elap * (uint32_t)tr_w) / dur) : 0;
    if (prog > tr_w) prog = tr_w;
    if (prog < 2) prog = 2;
    draw_hline(tr_x, ry + box_h / 2, prog, RGB(0,4,5));

    /* Timecode */
    uint32_t sec = elap / 1000;
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02u:%02u", sec / 60, sec % 60);
    draw_str(rx + box_w - 32, ry + 5, time_str, GRAY(18), 0xFF);
}

void ns_engine_render(int vx, int vy, int vw, int vh, int scroll_y, int nav_focus_idx) {
    /* Fill page background with the WEBSITE-SPECIFIED background color */
    fill_rect(vx, vy, vw, vh, doc_bg_color);

    ns_render_box_t *b = box_head;
    while (b) {
        int ry = vy + (b->y - scroll_y);
        int rx = vx + b->x;

        /* Strict Viewport Bounds */
        if (ry + b->h >= vy && ry < vy + vh) {
            int max_bw = (vx + vw - 4) - rx;
            int box_w = b->w;
            if (box_w > max_bw) box_w = max_bw;

            if (box_w > 2 && b->h > 0) {
                if (b->is_container || b->is_card || b->is_table || b->is_blockquote) {
                    if (b->has_custom_bg) ns_fill_rect_clip(rx, ry, box_w, b->h, b->bg_col, vx, vy, vw, vh);
                    if (b->has_custom_border) {
                        ns_draw_rect_clip(rx, ry, box_w, b->h, b->border_col, vx, vy, vw, vh);
                    }
                } else if (b->is_image) {
                    render_universal_image(rx, ry, box_w, b->h, b->src, b->text, vx, vy, vw, vh);
                } else if (b->is_video) {
                    bool is_playing = false;
                    for (int k = 0; k < ns_element_count; k++) {
                        if (ns_elements[k].is_video && ns_elements[k].x == b->x && ns_elements[k].y == b->y) {
                            is_playing = ns_elements[k].is_playing;
                            break;
                        }
                    }
                    render_universal_video(rx, ry, box_w, b->h, b->src, b->poster, b->text, is_playing, vx, vy, vw, vh);
                } else if (b->is_audio) {
                    bool is_playing = false;
                    for (int k = 0; k < ns_element_count; k++) {
                        if (ns_elements[k].is_audio && ns_elements[k].x == b->x && ns_elements[k].y == b->y) {
                            is_playing = ns_elements[k].is_playing;
                            break;
                        }
                    }
                    render_universal_audio(rx, ry, box_w, b->h, b->src, b->text, is_playing, vx, vy, vw, vh);
                } else if (b->is_input) {
                    bool is_focused = (nav_focus_idx >= 0 && nav_focus_idx < ns_element_count &&
                                       ns_elements[nav_focus_idx].x == b->x && ns_elements[nav_focus_idx].y == b->y);
                    ns_fill_rect_clip(rx, ry, box_w, b->h, b->bg_col, vx, vy, vw, vh);
                    ns_draw_rect_clip(rx, ry, box_w, b->h, is_focused ? RGB(0,3,5) : b->border_col, vx, vy, vw, vh);
                    if (is_focused) {
                        ns_draw_rect_clip(rx - 1, ry - 1, box_w + 2, b->h + 2, RGB(0,4,5), vx, vy, vw, vh);
                    }

                    if (ry >= vy && ry + 10 <= vy + vh) {
                        /* Find matching ns_element for live typed text */
                        const char *inp_text = b->text;
                        const char *placeholder = "Search...";
                        for (int k = 0; k < ns_element_count; k++) {
                            if (ns_elements[k].is_input && ns_elements[k].x == b->x && ns_elements[k].y == b->y) {
                                inp_text = ns_elements[k].text;
                                if (ns_elements[k].placeholder[0]) placeholder = ns_elements[k].placeholder;
                                break;
                            }
                        }
                        if (inp_text && inp_text[0]) {
                            draw_str_clip(rx + 4, ry + 2, inp_text, b->fg_col, 0xFF, rx + box_w - 6);
                        } else if (!is_focused) {
                            draw_str_clip(rx + 4, ry + 2, placeholder, GRAY(12), 0xFF, rx + box_w - 6);
                        }
                        /* Draw cursor if focused */
                        if (is_focused) {
                            int cx = rx + 4 + strlen(inp_text) * 6;
                            if (cx < rx + box_w - 6) {
                                draw_str(cx, ry + 2, "|", RGB(0,2,5), 0xFF);
                            }
                        }
                    }
                } else if (b->is_button) {
                    ns_fill_rect_clip(rx, ry, box_w, b->h, b->bg_col, vx, vy, vw, vh);
                    ns_draw_rect_clip(rx, ry, box_w, b->h, b->border_col, vx, vy, vw, vh);
                    if (ry >= vy && ry + 10 <= vy + vh) {
                        int tw = strlen(b->text[0] ? b->text : "Search") * 6;
                        int tx = rx + (box_w - tw) / 2;
                        if (tx < rx + 2) tx = rx + 2;
                        draw_str_clip(tx, ry + 2, b->text[0] ? b->text : "Search", b->fg_col, 0xFF, rx + box_w - 2);
                    }
                } else if (b->is_hr) {
                    ns_draw_hline_clip(rx, ry, box_w, b->border_col, vx, vy, vw, vh);
                } else if (b->text[0]) {
                    if (ry >= vy && ry + 8 <= vy + vh) {
                        draw_str_clip(rx, ry + 1, b->text, b->fg_col, 0xFF, vx + vw - 4);
                    }
                }

                if (b->is_link && !b->is_image && ry + 10 >= vy && ry + 10 < vy + vh) {
                    ns_draw_hline_clip(rx, ry + 10, box_w, b->fg_col, vx, vy, vw, vh);
                }
            }
        }
        b = b->next;
    }

    /* Render Focus Ring */
    if (nav_focus_idx >= 0 && nav_focus_idx < ns_element_count) {
        ns_interactive_elem_t *el = &ns_elements[nav_focus_idx];
        int fx = vx + el->x;
        int fy = vy + (el->y - scroll_y);
        if (fy >= vy && fy + el->h <= vy + vh) {
            ns_draw_rect_clip(fx - 2, fy - 1, el->w + 4, el->h + 2, RGB(0,3,5), vx, vy, vw, vh);
        }
    }
}

int ns_engine_get_content_height(void) {
    return doc_content_height;
}

const char *ns_engine_get_title(void) {
    return doc_title;
}
