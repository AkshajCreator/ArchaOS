// src/less.c — ArchaOS Interactive Paginated File Viewer
#include "less.h"
#include "fs.h"
#include "vga.h"
#include "pit.h"
#include "idt.h"
#include "kernel.h"
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VIEW_ROWS   23
#define MAX_LINES   1024
#define BUF_SIZE    8192

static uint8_t wait_key_less(int *extended)
{
    while (1) {
        if (irq_kbd_fired) {
            irq_kbd_fired = 0;
            uint8_t sc = last_scancode;
            if (sc == 0xE0) {
                *extended = 1;
                continue;
            }
            if (sc & 0x80) {
                continue;
            }
            return sc;
        }
        pit_sleep(10);
    }
}

void cmd_less(const char *path)
{
    if (!path || !path[0]) {
        vga_print("usage: less <filename> or more <filename>\n");
        return;
    }

    fs_node_t *node = fs_resolve(path);
    if (!node || node->type != FS_FILE || !node->data) {
        vga_print("less: cannot open '");
        vga_print(path);
        vga_print("': No such file\n");
        return;
    }

    static char file_buf[BUF_SIZE];
    size_t sz = node->size;
    if (sz >= BUF_SIZE - 1) sz = BUF_SIZE - 2;

    for (size_t i = 0; i < sz; i++) file_buf[i] = (char)node->data[i];
    file_buf[sz] = '\0';

    typedef struct {
        const char *str;
        int len;
    } less_line_t;

    static less_line_t lines[MAX_LINES];
    int line_count = 0;

    size_t i = 0;
    while (i < sz && line_count < MAX_LINES) {
        const char *line_start = &file_buf[i];
        size_t start_idx = i;
        int last_space = -1;
        int col = 0;

        while (i < sz && file_buf[i] != '\n' && col < (VGA_WIDTH - 2)) {
            if (file_buf[i] == ' ') last_space = (int)i;
            i++;
            col++;
        }

        if (i < sz && file_buf[i] == '\n') {
            /* Clean line ending with \n */
            lines[line_count].str = line_start;
            lines[line_count].len = (int)(i - start_idx);
            line_count++;
            i++; /* skip \n */
        } else if (col >= (VGA_WIDTH - 2) && i < sz && file_buf[i] != '\n') {
            /* Line is longer than 78 columns: wrap at word boundary */
            if (last_space > (int)start_idx) {
                lines[line_count].str = line_start;
                lines[line_count].len = last_space - (int)start_idx;
                line_count++;
                i = (size_t)(last_space + 1); /* Next line starts after space */
            } else {
                /* Long word without space: hard split at width limit */
                lines[line_count].str = line_start;
                lines[line_count].len = (int)(i - start_idx);
                line_count++;
            }
        } else {
            /* End of buffer */
            lines[line_count].str = line_start;
            lines[line_count].len = (int)(i - start_idx);
            line_count++;
            break;
        }
    }

    int top_line = 0;
    int extended = 0;

    while (1) {
        vga_clear();

        /* Render 23 lines */
        for (int r = 0; r < VIEW_ROWS; r++) {
            int cur_idx = top_line + r;
            vga_set_cursor(0, r);
            if (cur_idx < line_count) {
                const char *ln = lines[cur_idx].str;
                int len = lines[cur_idx].len;
                for (int c = 0; c < len && c < VGA_WIDTH - 1; c++) {
                    vga_print_char(ln[c]);
                }
            } else {
                vga_print_color("~", 0x08); /* Vim/less tilde on empty trailing rows */
            }
        }

        /* Render Reverse-Video Status Bar on row 24 */
        vga_set_cursor(0, 24);
        int bottom_idx = top_line + VIEW_ROWS;
        if (bottom_idx > line_count) bottom_idx = line_count;
        int pct = line_count > 0 ? ((bottom_idx * 100) / line_count) : 100;

        char bar[VGA_WIDTH + 1];
        for (int i = 0; i < VGA_WIDTH; i++) bar[i] = ' ';
        bar[VGA_WIDTH] = '\0';

        char num1[8], num2[8], num3[8], pct_str[8];
        itoa(top_line + 1, num1, 10);
        itoa(bottom_idx, num2, 10);
        itoa(line_count, num3, 10);
        itoa(pct, pct_str, 10);

        /* Format status line */
        int p = 0;
        const char *prefix = ": ";
        while (prefix[p]) { bar[p] = prefix[p]; p++; }
        int pi = 0;
        while (path[pi] && p < 24) { bar[p++] = path[pi++]; }
        const char *l1 = " (lines ";
        while (*l1) { bar[p++] = *l1++; }
        int ni = 0; while (num1[ni]) bar[p++] = num1[ni++];
        bar[p++] = '-';
        ni = 0; while (num2[ni]) bar[p++] = num2[ni++];
        bar[p++] = '/';
        ni = 0; while (num3[ni]) bar[p++] = num3[ni++];
        bar[p++] = ')';
        bar[p++] = ' ';
        ni = 0; while (pct_str[ni]) bar[p++] = pct_str[ni++];
        bar[p++] = '%';

        const char *hint = " [Space/Enter/Down/q:quit]";
        int hp = VGA_WIDTH - 27;
        while (*hint && hp < VGA_WIDTH) { bar[hp++] = *hint++; }

        vga_print_color(bar, 0x70); /* Black on Light Gray */

        /* Await navigation key */
        extended = 0;
        uint8_t sc = wait_key_less(&extended);

        if (sc == 0x10 || sc == 0x01) { /* 'q' or ESC */
            break;
        } else if (sc == 0x1C) { /* Enter: advance 1 line, or exit if at end */
            if (top_line + VIEW_ROWS < line_count) {
                top_line++;
            } else {
                break;
            }
        } else if (sc == 0x39) { /* Space: page down, or exit if at end */
            if (top_line + VIEW_ROWS < line_count) {
                top_line += 20;
                if (top_line + VIEW_ROWS > line_count) top_line = line_count - VIEW_ROWS;
                if (top_line < 0) top_line = 0;
            } else {
                break;
            }
        } else if (sc == 0x50 || sc == 0x24) { /* Down Arrow or 'j' */
            if (top_line + VIEW_ROWS < line_count) top_line++;
        } else if (sc == 0x48 || sc == 0x25) { /* Up Arrow or 'k' */
            if (top_line > 0) top_line--;
        } else if (sc == 0x51) { /* PgDn */
            top_line += 20;
            if (top_line + VIEW_ROWS > line_count) top_line = line_count - VIEW_ROWS;
            if (top_line < 0) top_line = 0;
        } else if (sc == 0x49 || sc == 0x30) { /* PgUp or 'b' */
            top_line -= 20;
            if (top_line < 0) top_line = 0;
        } else if (sc == 0x47 || sc == 0x22) { /* Home or 'g' */
            top_line = 0;
        } else if (sc == 0x4F) { /* End */
            top_line = line_count - VIEW_ROWS;
            if (top_line < 0) top_line = 0;
        }
    }

    vga_clear();
    vga_set_cursor(0, 0);
}
