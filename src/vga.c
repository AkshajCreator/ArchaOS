// src/vga.c

#include "vga.h"
#include "kernel.h"
#include "idt.h"
#include "theme.h"

#include <stdint.h>

#define VGA_ADDRESS  0xB8000
#define VGA_WIDTH    80
#define VGA_HEIGHT   25

#define CMD_BUFFER_SIZE 128
#define HISTORY_SIZE    10

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

/* ============================================================
 * VGA STATE
 * ============================================================ */

static uint16_t *vga_buffer = (uint16_t *)VGA_ADDRESS;

static int cursor_x = 0;
static int cursor_y = 0;

static int prompt_x = 0;
static int prompt_y = 0;

static int shift_pressed = 0;
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
        scrollback[slot][x] = vga_buffer[x];
    sb_head++;
    if (sb_count < SCROLLBACK_LINES) sb_count++;

    /* Scroll screen up */
    for (y = 1; y < VGA_HEIGHT; y++)
        for (x = 0; x < VGA_WIDTH; x++)
            vga_buffer[(y-1)*VGA_WIDTH+x] = vga_buffer[y*VGA_WIDTH+x];

    for (x = 0; x < VGA_WIDTH; x++)
        vga_buffer[(VGA_HEIGHT-1)*VGA_WIDTH+x] = ' ' | ((uint16_t)ATTR_NORMAL << 8);

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
                vga_buffer[y*VGA_WIDTH+x] = ' ' | ((uint16_t)ATTR_NORMAL << 8);
        } else {
            int slot = line_idx % SCROLLBACK_LINES;
            for (int x = 0; x < VGA_WIDTH; x++)
                vga_buffer[y*VGA_WIDTH+x] = scrollback[slot][x];
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
        /* Back at live view — re-render the current live VGA buffer content
         * by forcing a full redraw of what vga_buffer already holds. */
        for (int y = 0; y < VGA_HEIGHT; y++)
            for (int x = 0; x < VGA_WIDTH; x++)
                vga_buffer[y * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
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
            vga_buffer[y*VGA_WIDTH+x] = ' ' | ((uint16_t)ATTR_NORMAL << 8);

    cursor_x = 0;
    cursor_y = 0;
    current_attr = ATTR_NORMAL;
    update_cursor();
}

void vga_print_char(char c)
{
    /* Feed to capture buffer if active (for redirection/pipes) */
    extern void shellext_capture_char(char c);
    shellext_capture_char(c);

    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
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
            vga_buffer[cursor_y*VGA_WIDTH+cursor_x] =
            ' ' | ((uint16_t)ATTR_NORMAL << 8);
        }
    }
    else
    {
        vga_buffer[cursor_y*VGA_WIDTH+cursor_x] =
        ((uint16_t)c) | ((uint16_t)current_attr << 8);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; }
    }

    vga_ensure_visible();
    update_cursor();
}

void vga_print(const char *str)
{
    while (*str) vga_print_char(*str++);
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
    vga_buffer[y * VGA_WIDTH + x] = ((uint16_t)c) | ((uint16_t)attr << 8);
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
            vga_buffer[y*VGA_WIDTH+x] = ' ' | ((uint16_t)ATTR_BOOT << 8);

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
    const char *ver = "v4.0  \"Phosphor\"";
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

static void redraw_line(void)
{
    int i;

    /* How many screen columns does the input occupy? */
    int total_cols = prompt_x + cmd_len;
    int total_rows = total_cols / VGA_WIDTH + 1;

    /* Clear from prompt position across all wrapped rows */
    int cx = prompt_x;
    int cy = prompt_y;
    for (i = 0; i < total_rows * VGA_WIDTH - prompt_x + VGA_WIDTH; i++) {
        vga_buffer[cy * VGA_WIDTH + cx] = ' ' | ((uint16_t)ATTR_NORMAL << 8);
        cx++;
        if (cx >= VGA_WIDTH) { cx = 0; cy++; }
        if (cy >= VGA_HEIGHT) break;
    }

    /* Redraw all characters */
    cx = prompt_x;
    cy = prompt_y;
    for (i = 0; i < cmd_len; i++) {
        vga_buffer[cy * VGA_WIDTH + cx] =
        ((uint16_t)cmd_buffer[i]) | ((uint16_t)ATTR_NORMAL << 8);
        cx++;
        if (cx >= VGA_WIDTH) { cx = 0; cy++; }
    }

    /* Place cursor at correct position accounting for wrap */
    int abs_pos = prompt_x + cmd_cursor;
    cursor_x = abs_pos % VGA_WIDTH;
    cursor_y = prompt_y + abs_pos / VGA_WIDTH;
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
    while (!irq_kbd_fired)
        asm volatile("sti; hlt");
    uint8_t sc    = last_scancode;
    irq_kbd_fired = 0;
    return sc;
}

/* ============================================================
 * SHELL
 * ============================================================ */

void vga_prompt(void)
{
    /* Print themed prompt */
    vga_print_color("Arc/> ", current_theme.prompt);

    prompt_x = cursor_x;
    prompt_y = cursor_y;

    cmd_len    = 0;
    cmd_cursor = 0;
    history_index = history_count;

    while (1)
    {
        uint8_t sc = wait_for_scancode();

        if (sc == 0xE0) { extended = 1; continue; }

        /* Key release */
        if (sc & 0x80)
        {
            uint8_t base = sc & 0x7F;
            if (base == 0x2A || base == 0x36) shift_pressed = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0x3A) { caps_lock = !caps_lock; continue; }

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
                case 0x4D:  /* Right */
                    if (cmd_cursor < cmd_len) { cmd_cursor++; redraw_line(); }
                    break;
                case 0x47:  /* Home */
                    cmd_cursor = 0; redraw_line();
                    break;
                case 0x4F:  /* End */
                    cmd_cursor = cmd_len; redraw_line();
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
