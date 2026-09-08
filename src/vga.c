// src/vga.c

#include "vga.h"
#include "kernel.h"
#include "idt.h"
#include "theme.h"

#include <stdint.h>

/* Forward declaration — net_poll() lives in net/net.c */
extern void net_poll(void);

#define VGA_ADDRESS  0xB8000
#define VGA_WIDTH    80
#define VGA_HEIGHT   25

#define CMD_BUFFER_SIZE 128
#define HISTORY_SIZE    32

/* ============================================================
 * VGA COLORS
 * ============================================================ */

#define COLOR_BLACK        0x0
#define COLOR_GREEN        0x2
#define COLOR_BRIGHT_GREEN 0xA
#define COLOR_WHITE        0xF
#define COLOR_BRIGHT_WHITE 0xF

#define ATTR(fg, bg)  (((bg) << 4) | (fg))

#define ATTR_BOOT      ATTR(COLOR_BRIGHT_GREEN,  COLOR_BLACK)
#define ATTR_BORDER    ATTR(COLOR_GREEN,         COLOR_BLACK)

static uint16_t shadow_screen[VGA_WIDTH * VGA_HEIGHT];
static uint16_t *vga_buffer = (uint16_t *)VGA_ADDRESS;

void vga_write_cell(int x, int y, uint16_t cell)
{
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    int idx = y * VGA_WIDTH + x;
    shadow_screen[idx] = cell;
    vga_buffer[idx] = cell;
}

uint16_t vga_read_cell(int x, int y)
{
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return 0;
    return shadow_screen[y * VGA_WIDTH + x];
}

static int cursor_x = 0;
static int cursor_y = 0;

static int prompt_x = 0;
static int prompt_y = 0;

static int shift_pressed = 0;
static int ctrl_pressed  = 0;
static int caps_lock     = 0;
static int extended      = 0;

static uint8_t current_attr = ATTR_NORMAL;

/* ============================================================
 * SCROLLBACK BUFFER
 * Stores lines that have scrolled off the top.
 * ============================================================ */

#define SCROLLBACK_LINES 200

static uint16_t scrollback[SCROLLBACK_LINES][VGA_WIDTH];
static int      sb_count  = 0;   /* total lines stored (capped at SCROLLBACK_LINES) */
static int      sb_head   = 0;   /* ring buffer head  */
static int      sb_offset = 0;   /* 0 = live view, >0 = scrolled back N lines      */

static char cmd_buffer[CMD_BUFFER_SIZE];
static int  cmd_len    = 0;
static int  cmd_cursor = 0;

/* ============================================================
 * HISTORY
 * ============================================================ */

static char history[HISTORY_SIZE][CMD_BUFFER_SIZE];
static int  history_count = 0;
static int  history_index = 0;

/* ============================================================
 * KEYMAPS
 * ============================================================ */

static const char map_lower[128] =
{
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};

static const char map_upper[128] =
{
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
};

/* ============================================================
 * PORT I/O
 * ============================================================ */

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}

/* ============================================================
 * STRING
 * ============================================================ */

static int strlen_local(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* ============================================================
 * CURSOR
 * ============================================================ */

static void update_cursor(void)
{
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F); outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E); outb(0x3D5, (pos >> 8) & 0xFF);
}

void vga_set_cursor(int x, int y)
{
    cursor_x = x;
    cursor_y = y;
    update_cursor();
}

/* ============================================================
 * SCROLL
 * ============================================================ */

void vga_scroll(void)
{
    int y, x;

    /* Save top line to scrollback ring buffer */
    int slot = sb_head % SCROLLBACK_LINES;
    for (x = 0; x < VGA_WIDTH; x++)
        scrollback[slot][x] = shadow_screen[x];
    sb_head++;
    if (sb_count < SCROLLBACK_LINES) sb_count++;

    /* Scroll screen up */
    for (y = 1; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            vga_write_cell(x, y - 1, shadow_screen[y * VGA_WIDTH + x]);

    for (x = 0; x < VGA_WIDTH; x++)
        vga_write_cell(x, VGA_HEIGHT - 1, ' ' | ((uint16_t)ATTR_NORMAL << 8));

    cursor_y = VGA_HEIGHT - 1;
}

/* ── Scrollback view ── */

static void sb_render(void)
{
    /* Render scrollback at current offset onto screen (no cursor move) */
    for (int y = 0; y < VGA_HEIGHT; y++) {
        /* Which scrollback line maps to screen row y? */
        int line_idx = sb_head - sb_offset - VGA_HEIGHT + y;
        if (line_idx < 0 || line_idx >= sb_head ||
            (sb_head - line_idx) > sb_count) {
            /* Before start of scrollback — blank line */
            for (int x = 0; x < VGA_WIDTH; x++)
                vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
        } else {
            int slot = line_idx % SCROLLBACK_LINES;
            for (int x = 0; x < VGA_WIDTH; x++)
                vga_write_cell(x, y, scrollback[slot][x]);
        }
    }
}

void vga_scroll_up(int lines)
{
    sb_offset += lines;
    if (sb_offset > sb_count) sb_offset = sb_count;
    if (sb_offset > 0) sb_render();
}

void vga_scroll_down(int lines)
{
    sb_offset -= lines;
    if (sb_offset < 0) sb_offset = 0;
    if (sb_offset > 0) {
        sb_render();
    } else {
        update_cursor();
    }
}


int vga_in_scrollback(void) { return sb_offset > 0; }

void vga_ensure_visible(void)
{
    while (cursor_y >= VGA_HEIGHT)
        vga_scroll();
}

/* ============================================================
 * TEXT
 * ============================================================ */

void vga_clear(void)
{
    int y, x;
    for (y = 0; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));

    cursor_x = 0;
    cursor_y = 0;
    current_attr = ATTR_NORMAL;
    update_cursor();
}

/* ============================================================
 * ANSI ESCAPE SEQUENCE PARSER (A.4)
 * Supports SGR colors (30-37, 39, 40-47, 49, 90-97, 100-107),
 * bold (1), dim (2,22), inverse (7,27), reset (0),
 * cursor movement (A, B, C, D, H, f, s, u),
 * screen and line clears (J, K), and tab expansion.
 * ============================================================ */

#define ANSI_STATE_NORMAL 0
#define ANSI_STATE_ESC    1
#define ANSI_STATE_CSI    2

static int ansi_state = ANSI_STATE_NORMAL;
static int ansi_params[16];
static int ansi_has_param[16];
static int ansi_param_count = 0;
static int ansi_private = 0;

static uint8_t ansi_fg = 7;
static uint8_t ansi_bg = 0;
static uint8_t ansi_bold = 0;
static uint8_t ansi_inverse = 0;
static int ansi_saved_x = 0;
static int ansi_saved_y = 0;

static const uint8_t ansi_vga_map[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

void vga_print_char(char c)
{
    extern void shellext_capture_char(char c);

    if (ansi_state == ANSI_STATE_NORMAL) {
        if (c == '\033') { /* ESC (0x1B) */
            ansi_state = ANSI_STATE_ESC;
            return;
        }
    } else if (ansi_state == ANSI_STATE_ESC) {
        if (c == '[') { /* CSI */
            ansi_state = ANSI_STATE_CSI;
            ansi_param_count = 1;
            ansi_params[0] = 0;
            ansi_has_param[0] = 0;
            ansi_private = 0;
            return;
        } else if (c == 'c') { /* Reset terminal (RIS) */
            vga_clear();
            current_attr = ATTR_NORMAL;
            ansi_fg = 7; ansi_bg = 0; ansi_bold = 0; ansi_inverse = 0;
            ansi_state = ANSI_STATE_NORMAL;
            return;
        } else {
            ansi_state = ANSI_STATE_NORMAL;
            return;
        }
    } else if (ansi_state == ANSI_STATE_CSI) {
        if (c == '?') {
            ansi_private = 1;
            return;
        }
        if (c >= '0' && c <= '9') {
            ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
            ansi_has_param[ansi_param_count - 1] = 1;
            return;
        }
        if (c == ';') {
            if (ansi_param_count < 16) {
                ansi_param_count++;
                ansi_params[ansi_param_count - 1] = 0;
                ansi_has_param[ansi_param_count - 1] = 0;
            }
            return;
        }

        /* Command dispatch */
        switch (c) {
            case 'm': { /* SGR */
                if (ansi_param_count == 1 && !ansi_has_param[0]) {
                    ansi_params[0] = 0;
                    ansi_has_param[0] = 1;
                }
                for (int p = 0; p < ansi_param_count; p++) {
                    int code = ansi_params[p];
                    if (code == 0) {
                        ansi_fg = 7; ansi_bg = 0; ansi_bold = 0; ansi_inverse = 0;
                    } else if (code == 1) {
                        ansi_bold = 1;
                    } else if (code == 2 || code == 22) {
                        ansi_bold = 0;
                    } else if (code == 7) {
                        ansi_inverse = 1;
                    } else if (code == 27) {
                        ansi_inverse = 0;
                    } else if (code >= 30 && code <= 37) {
                        ansi_fg = ansi_vga_map[code - 30];
                    } else if (code == 39) {
                        ansi_fg = 7;
                    } else if (code >= 40 && code <= 47) {
                        ansi_bg = ansi_vga_map[code - 40] & 0x07;
                    } else if (code == 49) {
                        ansi_bg = 0;
                    } else if (code >= 90 && code <= 97) {
                        ansi_fg = (ansi_vga_map[code - 90] | 0x08) & 0x0F;
                    } else if (code >= 100 && code <= 107) {
                        ansi_bg = ansi_vga_map[code - 100] & 0x07;
                    }
                }
                uint8_t fg = ansi_fg;
                if (ansi_bold && fg < 8) fg |= 0x08;
                uint8_t bg = ansi_bg;
                if (ansi_inverse) current_attr = ATTR(bg, fg);
                else current_attr = ATTR(fg, bg);
                break;
            }

            case 'H':
            case 'f': { /* Cursor Position [row;col] (1-indexed) */
                int row = (ansi_has_param[0] && ansi_params[0] > 0) ? ansi_params[0] : 1;
                int col = (ansi_param_count > 1 && ansi_has_param[1] && ansi_params[1] > 0) ? ansi_params[1] : 1;
                cursor_x = col - 1;
                cursor_y = row - 1;
                if (cursor_x >= VGA_WIDTH) cursor_x = VGA_WIDTH - 1;
                if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1;
                update_cursor();
                break;
            }

            case 'A': { /* Cursor Up */
                int n = (ansi_has_param[0] && ansi_params[0] > 0) ? ansi_params[0] : 1;
                cursor_y = (cursor_y >= n) ? cursor_y - n : 0;
                update_cursor();
                break;
            }

            case 'B': { /* Cursor Down */
                int n = (ansi_has_param[0] && ansi_params[0] > 0) ? ansi_params[0] : 1;
                cursor_y = (cursor_y + n < VGA_HEIGHT) ? cursor_y + n : VGA_HEIGHT - 1;
                update_cursor();
                break;
            }

            case 'C': { /* Cursor Forward */
                int n = (ansi_has_param[0] && ansi_params[0] > 0) ? ansi_params[0] : 1;
                cursor_x = (cursor_x + n < VGA_WIDTH) ? cursor_x + n : VGA_WIDTH - 1;
                update_cursor();
                break;
            }

            case 'D': { /* Cursor Back */
                int n = (ansi_has_param[0] && ansi_params[0] > 0) ? ansi_params[0] : 1;
                cursor_x = (cursor_x >= n) ? cursor_x - n : 0;
                update_cursor();
                break;
            }

            case 'J': { /* Erase in Display */
                int mode = ansi_has_param[0] ? ansi_params[0] : 0;
                if (mode == 2) {
                    vga_clear();
                } else if (mode == 0) {
                    for (int y = cursor_y; y < VGA_HEIGHT; y++) {
                        int sx = (y == cursor_y) ? cursor_x : 0;
                        for (int x = sx; x < VGA_WIDTH; x++) {
                            vga_write_cell(x, y, ' ' | ((uint16_t)current_attr << 8));
                        }
                    }
                }
                break;
            }

            case 'K': { /* Erase in Line */
                int mode = ansi_has_param[0] ? ansi_params[0] : 0;
                if (mode == 0) {
                    for (int x = cursor_x; x < VGA_WIDTH; x++) {
                        vga_write_cell(x, cursor_y, ' ' | ((uint16_t)current_attr << 8));
                    }
                } else if (mode == 1) {
                    for (int x = 0; x <= cursor_x && x < VGA_WIDTH; x++) {
                        vga_write_cell(x, cursor_y, ' ' | ((uint16_t)current_attr << 8));
                    }
                } else if (mode == 2) {
                    for (int x = 0; x < VGA_WIDTH; x++) {
                        vga_write_cell(x, cursor_y, ' ' | ((uint16_t)current_attr << 8));
                    }
                }
                break;
            }

            case 's': { /* Save cursor position */
                ansi_saved_x = cursor_x;
                ansi_saved_y = cursor_y;
                break;
            }

            case 'u': { /* Restore cursor position */
                cursor_x = (ansi_saved_x < VGA_WIDTH) ? ansi_saved_x : VGA_WIDTH - 1;
                cursor_y = (ansi_saved_y < VGA_HEIGHT) ? ansi_saved_y : VGA_HEIGHT - 1;
                update_cursor();
                break;
            }

            default:
                break;
        }

        ansi_state = ANSI_STATE_NORMAL;
        return;
    }

    /* Process visible character */
    shellext_capture_char(c);

    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
    }
    else if (c == '\r')
    {
        cursor_x = 0;
    }
    else if (c == '\t')
    {
        int next_tab = (cursor_x + 8) & ~7;
        if (next_tab >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
        else cursor_x = next_tab;
    }
    else if (c == '\b')
    {
        /* Only block backspace if we're at the very start of input */
        int abs_pos = cursor_y * VGA_WIDTH + cursor_x;
        int prompt_abs = prompt_y * VGA_WIDTH + prompt_x;
        if (abs_pos > prompt_abs)
        {
            if (cursor_x > 0) cursor_x--;
            else { cursor_y--; cursor_x = VGA_WIDTH - 1; }
            vga_write_cell(cursor_x, cursor_y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
        }
    }
    else
    {
        vga_write_cell(cursor_x, cursor_y, ((uint16_t)c) | ((uint16_t)current_attr << 8));
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
    }

    vga_ensure_visible();
    update_cursor();
}

static int get_visible_word_len(const char *s)
{
    int len = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
        if (*s == '\033') {
            s++;
            if (*s == '[') {
                s++;
                while (*s && !((*s >= '@' && *s <= '~') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) {
                    s++;
                }
                if (*s) s++;
            }
        } else {
            len++;
            s++;
        }
    }
    return len;
}

void vga_print(const char *str)
{
    int at_word_start = 1;

    while (*str) {
        if (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
            if (*str == ' ' && cursor_x >= VGA_WIDTH - 1) {
                cursor_x = 0;
                cursor_y++;
                vga_ensure_visible();
                update_cursor();
                at_word_start = 1;
                str++;
                continue;
            }
            at_word_start = 1;
            vga_print_char(*str++);
            continue;
        }

        if (at_word_start && *str != '\033') {
            int wlen = get_visible_word_len(str);
            int remaining = VGA_WIDTH - cursor_x;
            if (wlen > remaining && wlen < VGA_WIDTH && cursor_x > 0) {
                cursor_x = 0;
                cursor_y++;
                vga_ensure_visible();
                update_cursor();
            }
            at_word_start = 0;
        }

        vga_print_char(*str++);
    }
}

void vga_print_color(const char *str, uint8_t attr)
{
    uint8_t saved = current_attr;
    current_attr = attr;
    vga_print(str);
    current_attr = saved;
}

void vga_print_center(const char *str)
{
    int len = strlen_local(str);
    vga_set_cursor((VGA_WIDTH - len) / 2, VGA_HEIGHT / 2);
    vga_print(str);
}

/* ============================================================
 * DELAY
 * ============================================================ */

static void delay(volatile unsigned int count)
{
    while (count--) asm volatile("nop");
}

/* ============================================================
 * BOOT ANIMATION
 * Pixels fill in to form a border, then logo appears
 * ============================================================ */

/* Draw one "pixel" (a block char) at vga position */
static void boot_pixel(int x, int y, char c, uint8_t attr)
{
    vga_write_cell(x, y, ((uint16_t)c) | ((uint16_t)attr << 8));
}

/* Pseudo-random fill order using LCG */
static uint32_t lcg_state = 12345;
static uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1664525 + 1013904223;
    return lcg_state;
}

void vga_show_welcome(void)
{
    int x, y, i;

    /* Clear to black */
    for (y = 0; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_BOOT << 8));

    /* ---- Phase 1: Border pixels appear one by one ---- */

    /* Collect border positions */
    static uint8_t bx[210], by[210];
    int bcount = 0;

    /* Top and bottom rows */
    for (x = 0; x < VGA_WIDTH; x++) {
        bx[bcount] = x; by[bcount] = 0;           bcount++;
    }
    for (x = 0; x < VGA_WIDTH; x++) {
        bx[bcount] = x; by[bcount] = VGA_HEIGHT-1; bcount++;
    }
    /* Left and right columns (excluding corners already added) */
    for (y = 1; y < VGA_HEIGHT-1; y++) {
        bx[bcount] = 0;           by[bcount] = y; bcount++;
        bx[bcount] = VGA_WIDTH-1; by[bcount] = y; bcount++;
    }

    /* Shuffle using LCG for random appearance order */
    for (i = bcount - 1; i > 0; i--) {
        int j = lcg_next() % (i + 1);
        uint8_t tx = bx[i]; bx[i] = bx[j]; bx[j] = tx;
        uint8_t ty = by[i]; by[i] = by[j]; by[j] = ty;
    }

    /* Draw border pixels one by one with delay */
    for (i = 0; i < bcount; i++) {
        /* Corner = double lines, edges = single lines */
        char c;
        int px = bx[i], py = by[i];
        if ((px == 0 && py == 0) || (px == VGA_WIDTH-1 && py == 0) ||
            (px == 0 && py == VGA_HEIGHT-1) || (px == VGA_WIDTH-1 && py == VGA_HEIGHT-1))
            c = '+';
        else if (py == 0 || py == VGA_HEIGHT-1)
            c = '-';
        else
            c = '|';

        boot_pixel(px, py, c, ATTR_BORDER);
        delay(800000);
    }

    /* ---- Phase 2: Logo appears in center ---- */

    /* Small delay before logo */
    delay(50000000);

    /* Logo — "ArchaOS" in pixel art by Akshaj */
    const char *logo[] = {
        " ###  ####   ###  #   #  ###   ###   #### ",
        "#   # #   # #     #   # #   # #   # #     ",
        "##### ####  #     ##### ##### #   #  ###   ",
        "#   # #  #  #     #   # #   # #   #     # ",
        "#   # #   #  ###  #   # #   #  ###  ####  ",
        0
    };

    int logo_h  = 5;
    int logo_w  = 43;
    int logo_sx = (VGA_WIDTH - logo_w) / 2;
    int logo_sy = (VGA_HEIGHT - logo_h - 2) / 2;

    /* Type in each row left to right */
    for (i = 0; logo[i]; i++) {
        int j;
        for (j = 0; logo[i][j]; j++) {
            boot_pixel(logo_sx + j, logo_sy + i, logo[i][j], ATTR_BOOT);
            delay(80000);
        }
    }

    /* Version — right-aligned under logo */
    delay(30000000);
    const char *ver = "v0.5  \"Monolith\"";
    int ver_len = strlen_local(ver);
    int ver_y   = logo_sy + logo_h + 1;
    int ver_x   = logo_sx + logo_w - ver_len;

    for (i = 0; ver[i]; i++) {
        boot_pixel(ver_x + i, ver_y, ver[i], ATTR_BORDER);
        delay(150000);
    }

    /* Hold for a moment then clear */
    delay(800000000);

    vga_clear();
}

/* ============================================================
 * HISTORY
 * ============================================================ */

static void history_save(const char *cmd)
{
    int i, j;
    if (!cmd[0]) return;

    if (history_count < HISTORY_SIZE)
    {
        int dst = history_count++;
        for (i = 0;; i++) { history[dst][i] = cmd[i]; if (!cmd[i]) break; }
    }
    else
    {
        for (i = 1; i < HISTORY_SIZE; i++) {
            for (j = 0;; j++) { history[i-1][j] = history[i][j]; if (!history[i][j]) break; }
        }
        for (j = 0;; j++) { history[HISTORY_SIZE-1][j] = cmd[j]; if (!cmd[j]) break; }
    }

    history_index = history_count;
}

#include "fs.h"

static const char *known_cmds[] = {
    "help", "new", "general", "clear", "echo", "reboot", "halt", "uptime", "date", "neofetch",
    "fortune", "theme", "sleep", "calc", "matrix", "ls", "tree", "pwd", "cd",
    "mkdir", "touch", "cat", "less", "more", "head", "tail", "find", "stat", "hexdump", "xxd",
    "rm", "write", "cp", "mv", "edit", "nano", "top", "sysmon", "ports", "beep", "credits",
    "meminfo", "memtest", "ai", "gui", "serial", "pci", "ata", "run", "audio", "video", "play",
    "wget", "curl", "fetch", "export", "env", "unset", "alias", "unalias", "history", "ansi", "colors", 0
};

static char suggestion_buf[CMD_BUFFER_SIZE];
static int  suggestion_len = 0;

static int prefix_match(const char *s, const char *prefix, int n) {
    for (int i = 0; i < n; i++) {
        if (!s[i] || s[i] != prefix[i]) return 0;
    }
    return 1;
}

static void compute_suggestion(void)
{
    suggestion_len = 0;
    suggestion_buf[0] = '\0';
    if (cmd_len == 0) return;

    /* Check history backwards first */
    for (int h = history_count - 1; h >= 0; h--) {
        int hl = 0;
        while (history[h][hl]) hl++;
        if (hl > cmd_len && prefix_match(history[h], cmd_buffer, cmd_len)) {
            int si = 0;
            while (history[h][si] && si < CMD_BUFFER_SIZE - 1) {
                suggestion_buf[si] = history[h][si];
                si++;
            }
            suggestion_buf[si] = '\0';
            suggestion_len = si;
            return;
        }
    }

    /* Fallback to known_cmds */
    for (int k = 0; known_cmds[k]; k++) {
        int kl = 0;
        while (known_cmds[k][kl]) kl++;
        if (kl > cmd_len && prefix_match(known_cmds[k], cmd_buffer, cmd_len)) {
            int si = 0;
            while (known_cmds[k][si] && si < CMD_BUFFER_SIZE - 1) {
                suggestion_buf[si] = known_cmds[k][si];
                si++;
            }
            suggestion_buf[si] = '\0';
            suggestion_len = si;
            return;
        }
    }
}

static int get_cmd_word_len(const char *buf, int start, int total) {
    int l = 0;
    while (start + l < total && buf[start + l] != ' ' && buf[start + l] != '\t') {
        l++;
    }
    return l;
}

static void redraw_line(void)
{
    compute_suggestion();
    int i;
    int display_len = (suggestion_len > cmd_len) ? suggestion_len : cmd_len;
    int total_rows = (prompt_x + display_len) / VGA_WIDTH + 2;

    /* If wrapping would push off bottom, scroll proactively */
    while (prompt_y + total_rows > VGA_HEIGHT) {
        vga_scroll();
        if (prompt_y > 0) prompt_y--;
    }

    /* Clear from prompt position across all wrapped rows */
    for (int y = prompt_y; y <= prompt_y + total_rows && y < VGA_HEIGHT; y++) {
        int start_x = (y == prompt_y) ? prompt_x : 0;
        for (int x = start_x; x < VGA_WIDTH; x++) {
            vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
        }
    }

    /* Redraw all typed characters with word wrap */
    int cx = prompt_x;
    int cy = prompt_y;
    int target_cx = prompt_x;
    int target_cy = prompt_y;

    for (i = 0; i < cmd_len; i++) {
        if (i == cmd_cursor) {
            target_cx = cx;
            target_cy = cy;
        }

        if (cmd_buffer[i] == ' ') {
            if (cx >= VGA_WIDTH - 1) {
                cx = 0; cy++;
            } else {
                if (cy < VGA_HEIGHT) vga_write_cell(cx, cy, ' ' | ((uint16_t)ATTR_NORMAL << 8));
                cx++;
            }
        } else {
            if (i == 0 || cmd_buffer[i - 1] == ' ') {
                int wlen = get_cmd_word_len(cmd_buffer, i, cmd_len);
                if (cx + wlen > VGA_WIDTH && wlen < VGA_WIDTH && cx > 0) {
                    cx = 0; cy++;
                    if (i == cmd_cursor) {
                        target_cx = cx;
                        target_cy = cy;
                    }
                }
            }
            if (cx >= VGA_WIDTH) { cx = 0; cy++; }
            if (cy < VGA_HEIGHT) {
                vga_write_cell(cx, cy, ((uint16_t)cmd_buffer[i]) | ((uint16_t)ATTR_NORMAL << 8));
            }
            cx++;
        }
    }

    if (cmd_cursor == cmd_len) {
        target_cx = cx;
        target_cy = cy;
    }

    /* Render inline suggestion in dimmed dark gray (0x08) */
    if (cmd_cursor == cmd_len && suggestion_len > cmd_len) {
        for (i = cmd_len; i < suggestion_len; i++) {
            if (suggestion_buf[i] == ' ') {
                if (cx >= VGA_WIDTH - 1) { cx = 0; cy++; }
                else {
                    if (cy < VGA_HEIGHT) vga_write_cell(cx, cy, ' ' | ((uint16_t)0x08 << 8));
                    cx++;
                }
            } else {
                if (i == cmd_len || suggestion_buf[i - 1] == ' ') {
                    int wlen = get_cmd_word_len(suggestion_buf, i, suggestion_len);
                    if (cx + wlen > VGA_WIDTH && wlen < VGA_WIDTH && cx > 0) {
                        cx = 0; cy++;
                    }
                }
                if (cx >= VGA_WIDTH) { cx = 0; cy++; }
                if (cy < VGA_HEIGHT) {
                    vga_write_cell(cx, cy, ((uint16_t)suggestion_buf[i]) | ((uint16_t)0x08 << 8));
                }
                cx++;
            }
        }
    }

    cursor_x = target_cx;
    cursor_y = target_cy;
    if (cursor_x >= VGA_WIDTH) cursor_x = VGA_WIDTH - 1;
    if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1;
    update_cursor();
}

static void history_load(int index)
{
    int i;
    if (index < 0 || index >= history_count) return;

    cmd_len = 0; cmd_cursor = 0;
    for (i = 0; history[index][i]; i++)
    {
        cmd_buffer[i] = history[index][i];
        cmd_len++;
        cmd_cursor++;
    }
    redraw_line();
}

/* ============================================================
 * KEY TRANSLATION
 * ============================================================ */

static char translate_key(uint8_t sc)
{
    if (sc >= 128) return 0;
    char c = shift_pressed ? map_upper[sc] : map_lower[sc];
    if      (c >= 'a' && c <= 'z' && caps_lock) c -= 32;
    else if (c >= 'A' && c <= 'Z' && caps_lock) c += 32;
    return c;
}

/* ============================================================
 * WAIT FOR IRQ KEYPRESS
 * ============================================================ */

static uint8_t wait_for_scancode(void)
{
    while (!irq_kbd_fired) {
        uint8_t st = inb(0x64);
        if ((st & 0x01) && !(st & 0x20)) {
            last_scancode = inb(0x60);
            irq_kbd_fired = 1;
            break;
        }
        asm volatile("sti; hlt");
    }
    uint8_t sc    = last_scancode;
    irq_kbd_fired = 0;
    return sc;
}

static void vga_tab_complete(void)
{
    if (cmd_len == 0) return;

    cmd_buffer[cmd_len] = '\0';

    /* Find last space in cmd_buffer */
    int space_idx = -1;
    for (int i = 0; i < cmd_len; i++) {
        if (cmd_buffer[i] == ' ') {
            space_idx = i;
        }
    }

    if (space_idx == -1) {
        /* Auto-completing command name */
        const char *match = 0;
        int match_count = 0;

        for (int i = 0; known_cmds[i]; i++) {
            int len = 0;
            while (known_cmds[i][len] && len < cmd_len && known_cmds[i][len] == cmd_buffer[len]) {
                len++;
            }
            if (len == cmd_len) {
                match = known_cmds[i];
                match_count++;
            }
        }

        if (match_count == 1 && match) {
            cmd_len = 0;
            while (match[cmd_len]) {
                cmd_buffer[cmd_len] = match[cmd_len];
                cmd_len++;
            }
            cmd_buffer[cmd_len++] = ' ';
            cmd_buffer[cmd_len] = '\0';
            cmd_cursor = cmd_len;
            redraw_line();
        } else if (match_count > 1) {
            vga_print("\n");
            for (int i = 0; known_cmds[i]; i++) {
                int len = 0;
                while (known_cmds[i][len] && len < cmd_len && known_cmds[i][len] == cmd_buffer[len]) len++;
                if (len == cmd_len) {
                    vga_print(known_cmds[i]); vga_print("  ");
                }
            }
            vga_print("\n");
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            redraw_line();
        }
    } else {
        /* Auto-completing filename in current directory */
        const char *prefix = cmd_buffer + space_idx + 1;
        int prefix_len = cmd_len - (space_idx + 1);

        fs_node_t *cwd = fs_cwd();
        if (!cwd) return;

        const char *match = 0;
        int match_count = 0;

        for (int i = 0; i < cwd->child_count; i++) {
            fs_node_t *child = cwd->children[i];
            if (!child) continue;
            int len = 0;
            while (child->name[len] && len < prefix_len && child->name[len] == prefix[len]) len++;
            if (len == prefix_len) {
                match = child->name;
                match_count++;
            }
        }

        if (match_count == 1 && match) {
            cmd_len = space_idx + 1;
            while (*match) {
                cmd_buffer[cmd_len++] = *match++;
            }
            cmd_buffer[cmd_len] = '\0';
            cmd_cursor = cmd_len;
            redraw_line();
        } else if (match_count > 1) {
            vga_print("\n");
            for (int i = 0; i < cwd->child_count; i++) {
                fs_node_t *child = cwd->children[i];
                if (!child) continue;
                int len = 0;
                while (child->name[len] && len < prefix_len && child->name[len] == prefix[len]) len++;
                if (len == prefix_len) {
                    vga_print(child->name); vga_print("  ");
                }
            }
            vga_print("\n");
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            redraw_line();
        }
    }
}

/* ============================================================
 * REVERSE INCREMENTAL HISTORY SEARCH (A.2: Ctrl+R)
 * ============================================================ */

static int substring_match(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    if (!haystack) return 0;
    int hlen = 0, nlen = 0;
    while (haystack[hlen]) hlen++;
    while (needle[nlen]) nlen++;
    if (nlen > hlen) return 0;
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

static void redraw_search_prompt(const char *q, int matched, int failed)
{
    for (int y = prompt_y; y < prompt_y + 2 && y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
        }
    }
    cursor_x = 0;
    cursor_y = prompt_y;

    if (failed) {
        vga_print_color("(failed reverse-i-search)`", ATTR(0x0C, 0x00)); /* red */
    } else {
        vga_print_color("(reverse-i-search)`", ATTR(0x0B, 0x00)); /* cyan */
    }
    vga_print_color(q, ATTR(0x0E, 0x00)); /* yellow */
    vga_print_color("': ", ATTR(0x07, 0x00)); /* light gray */

    int q_cursor_x = cursor_x;
    int q_cursor_y = cursor_y;

    if (matched >= 0 && matched < history_count) {
        vga_print_color(history[matched], ATTR(0x0F, 0x00)); /* bright white */
    }

    vga_set_cursor(q_cursor_x, q_cursor_y);
}

static void vga_reverse_search(void)
{
    char saved_cmd[CMD_BUFFER_SIZE];
    for (int i = 0; i <= cmd_len; i++) saved_cmd[i] = cmd_buffer[i];
    int saved_len = cmd_len;
    int saved_cursor = cmd_cursor;

    char query[64];
    int qlen = 0;
    query[0] = '\0';

    int match_idx = -1;
    if (history_count > 0) {
        match_idx = history_count - 1;
    }

    if (prompt_y >= VGA_HEIGHT - 1) {
        vga_scroll();
        if (prompt_y > 0) prompt_y--;
    }

    redraw_search_prompt(query, match_idx, (match_idx < 0));

    while (1) {
        net_poll();
        uint8_t sc = wait_for_scancode();

        if (sc == 0xE0) { extended = 1; continue; }

        if (sc & 0x80) {
            uint8_t base = sc & 0x7F;
            if (base == 0x2A || base == 0x36) shift_pressed = 0;
            if (base == 0x1D) ctrl_pressed = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0x1D) { ctrl_pressed = 1; extended = 0; continue; }

        /* Ctrl+R again -> cycle to earlier match */
        if (ctrl_pressed && sc == 0x13) {
            if (history_count > 0) {
                int start = (match_idx > 0) ? match_idx - 1 : history_count - 1;
                int found = -1;
                for (int h = start; h >= 0; h--) {
                    if (substring_match(history[h], query)) {
                        found = h;
                        break;
                    }
                }
                if (found == -1 && start < history_count - 1) {
                    for (int h = history_count - 1; h > start; h--) {
                        if (substring_match(history[h], query)) {
                            found = h;
                            break;
                        }
                    }
                }
                if (found >= 0) match_idx = found;
            }
            redraw_search_prompt(query, match_idx, (match_idx < 0));
            continue;
        }

        /* Cancel search: Esc (0x01), Ctrl+C (0x2E), Ctrl+G (0x22) */
        if (sc == 0x01 || (ctrl_pressed && (sc == 0x2E || sc == 0x22))) {
            for (int i = 0; i <= saved_len; i++) cmd_buffer[i] = saved_cmd[i];
            cmd_len = saved_len;
            cmd_cursor = saved_cursor;

            for (int y = prompt_y; y < prompt_y + 2 && y < VGA_HEIGHT; y++) {
                for (int x = 0; x < VGA_WIDTH; x++)
                    vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
            }
            cursor_x = 0; cursor_y = prompt_y;
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            redraw_line();
            return;
        }

        /* Enter (0x1C): Execute matched command */
        if (sc == 0x1C) {
            if (match_idx >= 0 && match_idx < history_count) {
                int i = 0;
                while (history[match_idx][i] && i < CMD_BUFFER_SIZE - 1) {
                    cmd_buffer[i] = history[match_idx][i];
                    i++;
                }
                cmd_buffer[i] = '\0';
                cmd_len = i;
                cmd_cursor = i;
            } else {
                for (int i = 0; i <= saved_len; i++) cmd_buffer[i] = saved_cmd[i];
                cmd_len = saved_len;
                cmd_cursor = saved_cursor;
            }

            for (int y = prompt_y; y < prompt_y + 2 && y < VGA_HEIGHT; y++) {
                for (int x = 0; x < VGA_WIDTH; x++)
                    vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
            }
            cursor_x = 0; cursor_y = prompt_y;
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            vga_print(cmd_buffer);
            vga_print("\n");

            history_save(cmd_buffer);
            current_attr = ATTR_NORMAL;
            extern void shell_exec(const char *cmd);
            shell_exec(cmd_buffer);

            vga_print("\n");
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            cmd_len = 0;
            cmd_cursor = 0;
            history_index = history_count;
            return;
        }

        /* Accept match into command line for editing: Right Arrow, Tab, Left Arrow, Home, End */
        if (sc == 0x0F || (extended && (sc == 0x4D || sc == 0x4B || sc == 0x47 || sc == 0x4F)) || sc == 0x4D) {
            extended = 0;
            if (match_idx >= 0 && match_idx < history_count) {
                int i = 0;
                while (history[match_idx][i] && i < CMD_BUFFER_SIZE - 1) {
                    cmd_buffer[i] = history[match_idx][i];
                    i++;
                }
                cmd_buffer[i] = '\0';
                cmd_len = i;
                cmd_cursor = i;
            }
            for (int y = prompt_y; y < prompt_y + 2 && y < VGA_HEIGHT; y++) {
                for (int x = 0; x < VGA_WIDTH; x++)
                    vga_write_cell(x, y, ' ' | ((uint16_t)ATTR_NORMAL << 8));
            }
            cursor_x = 0; cursor_y = prompt_y;
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            redraw_line();
            return;
        }

        /* Backspace (0x0E) */
        if (sc == 0x0E) {
            if (qlen > 0) {
                query[--qlen] = '\0';
                match_idx = -1;
                for (int h = history_count - 1; h >= 0; h--) {
                    if (substring_match(history[h], query)) {
                        match_idx = h;
                        break;
                    }
                }
                redraw_search_prompt(query, match_idx, (match_idx < 0));
            }
            continue;
        }

        /* Regular character typed into query */
        if (!ctrl_pressed) {
            char c = translate_key(sc);
            if (c && qlen < (int)sizeof(query) - 1) {
                query[qlen++] = c;
                query[qlen] = '\0';
                match_idx = -1;
                for (int h = history_count - 1; h >= 0; h--) {
                    if (substring_match(history[h], query)) {
                        match_idx = h;
                        break;
                    }
                }
                redraw_search_prompt(query, match_idx, (match_idx < 0));
            }
        }
    }
}

/* ============================================================
 * SHELL
 * ============================================================ */

void vga_prompt(void)
{
    /* Guidance banner: exactly 2 lines before Arc/> prompt */
    vga_print_color("Type \"new\" to learn what's new and\n\"general\" to learn about ArchaOS in its entirety!\n\n", 0x0E);

    /* Print themed prompt */
    vga_print_color("Arc/> ", current_theme.prompt);

    prompt_x = cursor_x;
    prompt_y = cursor_y;

    cmd_len    = 0;
    cmd_cursor = 0;
    history_index = history_count;

    while (1)
    {
        net_poll();
        uint8_t sc = wait_for_scancode();

        if (sc == 0xE0) { extended = 1; continue; }

        /* Key release */
        if (sc & 0x80)
        {
            uint8_t base = sc & 0x7F;
            if (base == 0x2A || base == 0x36) shift_pressed = 0;
            if (base == 0x1D) ctrl_pressed = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0x1D) { ctrl_pressed = 1; extended = 0; continue; }
        if (sc == 0x3A) { caps_lock = !caps_lock; continue; }

        /* Control shortcuts */
        if (ctrl_pressed)
        {
            if (sc == 0x13) /* Ctrl+R -> Reverse incremental history search */
            {
                vga_reverse_search();
                continue;
            }
            if (sc == 0x26) /* Ctrl+L -> Clear screen */
            {
                vga_clear();
                vga_print_color("Arc/> ", current_theme.prompt);
                prompt_x = cursor_x;
                prompt_y = cursor_y;
                redraw_line();
                continue;
            }
            if (sc == 0x2E) /* Ctrl+C -> Cancel current line */
            {
                vga_print("^C\n");
                vga_print_color("Arc/> ", current_theme.prompt);
                prompt_x = cursor_x;
                prompt_y = cursor_y;
                cmd_len = 0;
                cmd_cursor = 0;
                history_index = history_count;
                continue;
            }
            if (sc == 0x16) /* Ctrl+U -> Clear line before cursor */
            {
                for (int i = 0; i < cmd_len - cmd_cursor; i++)
                    cmd_buffer[i] = cmd_buffer[cmd_cursor + i];
                cmd_len -= cmd_cursor;
                cmd_cursor = 0;
                redraw_line();
                continue;
            }
            continue;
        }

        /* Navigation keys */
        if (extended || sc == 0x48 || sc == 0x50 ||
            sc == 0x4B || sc == 0x4D || sc == 0x49 ||
            sc == 0x51 || sc == 0x47 || sc == 0x4F)
        {
            switch (sc)
            {
                case 0x49:  /* PgUp — scroll back if shift held, else history */
                    if (shift_pressed) { vga_scroll_up(VGA_HEIGHT - 2); extended = 0; continue; }
                    if (history_index > 0) history_load(--history_index);
                    break;
                case 0x51:  /* PgDn — scroll forward if shift held, else history */
                    if (shift_pressed) { vga_scroll_down(VGA_HEIGHT - 2); extended = 0; continue; }
                    if (history_index < history_count - 1) history_load(++history_index);
                    break;
                case 0x48:  /* Up — history prev */
                    if (history_index > 0) history_load(--history_index);
                    break;
                case 0x50:  /* Down — history next */
                    if (history_index < history_count - 1) history_load(++history_index);
                    break;
                case 0x4B:  /* Left */
                    if (cmd_cursor > 0) { cmd_cursor--; redraw_line(); }
                    break;
                case 0x4D:  /* Right — advance cursor or accept suggestion */
                    if (cmd_cursor < cmd_len) {
                        cmd_cursor++; redraw_line();
                    } else if (cmd_cursor == cmd_len && suggestion_len > cmd_len) {
                        for (int si = 0; si < suggestion_len; si++) cmd_buffer[si] = suggestion_buf[si];
                        cmd_len = suggestion_len;
                        cmd_cursor = cmd_len;
                        redraw_line();
                    }
                    break;
                case 0x47:  /* Home */
                    cmd_cursor = 0; redraw_line();
                    break;
                case 0x4F:  /* End — jump to end of line or accept suggestion */
                    if (cmd_cursor == cmd_len && suggestion_len > cmd_len) {
                        for (int si = 0; si < suggestion_len; si++) cmd_buffer[si] = suggestion_buf[si];
                        cmd_len = suggestion_len;
                    }
                    cmd_cursor = cmd_len;
                    redraw_line();
                    break;
            }
            extended = 0;
            continue;
        }

        /* Backspace */
        if (sc == 0x0E)
        {
            if (cmd_cursor > 0)
            {
                int i;
                for (i = cmd_cursor - 1; i < cmd_len - 1; i++)
                    cmd_buffer[i] = cmd_buffer[i+1];
                cmd_len--;
                cmd_cursor--;
                redraw_line();
            }
            continue;
        }

        /* Tab key auto-completion / suggestion acceptance */
        if (sc == 0x0F)
        {
            if (cmd_cursor == cmd_len && suggestion_len > cmd_len)
            {
                for (int si = 0; si < suggestion_len; si++) cmd_buffer[si] = suggestion_buf[si];
                cmd_len = suggestion_len;
                cmd_cursor = cmd_len;
                redraw_line();
            }
            else
            {
                vga_tab_complete();
            }
            continue;
        }

        /* Enter */
        if (sc == 0x1C)
        {
            cmd_buffer[cmd_len] = '\0';
            vga_print("\n");
            history_save(cmd_buffer);

            current_attr = ATTR_NORMAL;
            extern void shell_exec(const char *cmd);
            shell_exec(cmd_buffer);

            vga_print("\n");
            vga_print_color("Arc/> ", current_theme.prompt);
            prompt_x = cursor_x;
            prompt_y = cursor_y;
            cmd_len    = 0;
            cmd_cursor = 0;
            history_index = history_count;
            continue;
        }

        /* Regular character */
        char c = translate_key(sc);
        if (c && cmd_len < CMD_BUFFER_SIZE - 1)
        {
            int i;
            for (i = cmd_len; i > cmd_cursor; i--)
                cmd_buffer[i] = cmd_buffer[i-1];
            cmd_buffer[cmd_cursor] = c;
            cmd_len++;
            cmd_cursor++;
            redraw_line();
        }
    }
}

/* ============================================================
 * COMPAT
 * ============================================================ */

void vga_print_history(void)
{
    char buf[8];
    for (int i = 0; i < history_count; i++) {
        itoa(i + 1, buf, 10);
        vga_print("  "); vga_print(buf);
        vga_print("  "); vga_print(history[i]);
        vga_print("\n");
    }
    if (!history_count) vga_print("No history.\n");
}

void vga_execute_command(const char *cmd)
{
    kernel_execute_command(cmd);
}

static inline uint8_t vga_inb(uint16_t port)
{ uint8_t r; asm volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

void vga_kbd_flush(void)
{
    while (vga_inb(0x64) & 1) (void)vga_inb(0x60);
    shift_pressed = 0;
    caps_lock     = 0;
    extended      = 0;
    irq_kbd_fired = 0;
    last_scancode = 0;
    cmd_len       = 0;
    cmd_cursor    = 0;
    cmd_buffer[0] = '\0';
}

void vga_get_input(const char *prompt_str, char *out_buf, int max_len)
{
    if (!out_buf || max_len <= 0) return;
    if (prompt_str) vga_print_color(prompt_str, current_theme.prompt);
    int len = 0;
    out_buf[0] = '\0';

    while (1) {
        net_poll();
        uint8_t sc = wait_for_scancode();
        if (sc == 0xE0) continue;
        if (sc & 0x80) {
            uint8_t base = sc & 0x7F;
            if (base == 0x2A || base == 0x36) shift_pressed = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0x3A) { caps_lock = !caps_lock; continue; }
        if (sc == 0x1C) { /* Enter */
            vga_print("\n");
            out_buf[len] = '\0';
            break;
        }
        if (sc == 0x0E) { /* Backspace */
            if (len > 0) {
                len--;
                out_buf[len] = '\0';
                if (cursor_x > 0) {
                    cursor_x--;
                    vga_write_cell(cursor_x, cursor_y, (current_theme.normal << 8) | ' ');
                    vga_set_cursor(cursor_x, cursor_y);
                }
            }
            continue;
        }
        char ch = translate_key(sc);
        if (ch && len < max_len - 1) {
            out_buf[len++] = ch;
            out_buf[len] = '\0';
            char s[2] = { ch, '\0' };
            vga_print(s);
        }
    }
}
