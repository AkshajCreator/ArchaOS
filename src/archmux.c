#include "archmux.h"
#include "vga.h"
#include "keyboard.h"
#include "fs.h"
#include "mm.h"
#include "pit.h"
#include "string.h"
#include "task.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_MUX_LINES 64
#define LINE_LEN      80

typedef struct {
    char lines[MAX_MUX_LINES][LINE_LEN];
    int  line_count;
    char cmd[64];
    int  cmd_len;
    int  scroll_offset;
} mux_pane_t;

static mux_pane_t panes[2];
static int active_pane = 0;
static int split_vertical = 0; /* 0: Horizontal, 1: Vertical */

static void mux_putc(int x, int y, char c, uint8_t attr)
{
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;
    vga_write_cell(x, y, ((uint16_t)(unsigned char)c) | ((uint16_t)attr << 8));
}

static void mux_puts(int x, int y, const char *s, uint8_t attr)
{
    while (*s && x < 80) mux_putc(x++, y, *s++, attr);
}

static void mux_fill(int y, uint8_t attr)
{
    for (int x = 0; x < 80; x++) mux_putc(x, y, ' ', attr);
}

static void pane_add_line(mux_pane_t *p, const char *line)
{
    if (p->line_count < MAX_MUX_LINES) {
        strncpy(p->lines[p->line_count], line, LINE_LEN - 1);
        p->lines[p->line_count][LINE_LEN - 1] = '\0';
        p->line_count++;
    } else {
        /* Shift up */
        for (int i = 0; i < MAX_MUX_LINES - 1; i++) {
            strcpy(p->lines[i], p->lines[i + 1]);
        }
        strncpy(p->lines[MAX_MUX_LINES - 1], line, LINE_LEN - 1);
        p->lines[MAX_MUX_LINES - 1][LINE_LEN - 1] = '\0';
    }
}

static void pane_execute(mux_pane_t *p, int pane_idx)
{
    char prompt_line[LINE_LEN];
    prompt_line[0] = 'A'; prompt_line[1] = 'r'; prompt_line[2] = 'c';
    prompt_line[3] = '/'; prompt_line[4] = '0' + pane_idx;
    prompt_line[5] = '>'; prompt_line[6] = ' ';
    strncpy(prompt_line + 7, p->cmd, LINE_LEN - 8);
    prompt_line[LINE_LEN - 1] = '\0';
    pane_add_line(p, prompt_line);

    char *cmd = p->cmd;
    while (*cmd == ' ') cmd++;

    if (strcmp(cmd, "help") == 0) {
        pane_add_line(p, "archmux commands: help, ls, clear, uptime, mem, ps, date, fortune, echo, exit");
    } else if (strcmp(cmd, "clear") == 0) {
        p->line_count = 0;
    } else if (strcmp(cmd, "uptime") == 0) {
        uint32_t sec = pit_ticks() / 1000;
        char ubuf[48];
        char sbuf[16];
        itoa((int)sec, sbuf, 10);
        strcpy(ubuf, "Uptime: ");
        strcat(ubuf, sbuf);
        strcat(ubuf, " seconds");
        pane_add_line(p, ubuf);
    } else if (strcmp(cmd, "mem") == 0) {
        mm_stats_t s = mm_stats();
        size_t total = s.total;
        size_t used = s.used;
        char mbuf[64], tbuf[16], ubuf[16];
        itoa((int)(total / 1024), tbuf, 10);
        itoa((int)(used / 1024), ubuf, 10);
        strcpy(mbuf, "RAM: ");
        strcat(mbuf, ubuf);
        strcat(mbuf, " KB / ");
        strcat(mbuf, tbuf);
        strcat(mbuf, " KB");
        pane_add_line(p, mbuf);
    } else if (strcmp(cmd, "ps") == 0) {
        pane_add_line(p, "PID  NAME            STATE");
        pane_add_line(p, "0    idle            RUNNING");
        pane_add_line(p, "1    archmux         RUNNING");
    } else if (strcmp(cmd, "date") == 0) {
        pane_add_line(p, "RTC: 2026-10-06 15:56:00 UTC");
    } else if (strcmp(cmd, "fortune") == 0) {
        pane_add_line(p, "\"Simplicity is prerequisite for reliability.\" - Edsger W. Dijkstra");
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        pane_add_line(p, cmd + 5);
    } else if (strcmp(cmd, "ls") == 0) {
        fs_node_t *root = fs_root();
        if (root) {
            char list_str[LINE_LEN] = "";
            for (int i = 0; i < root->child_count && i < 8; i++) {
                if (root->children[i]) {
                    strcat(list_str, root->children[i]->name);
                    strcat(list_str, "  ");
                }
            }
            pane_add_line(p, list_str);
        }
    } else if (cmd[0] != '\0') {
        char err[64];
        strcpy(err, "archmux: executed: ");
        strncat(err, cmd, 32);
        pane_add_line(p, err);
    }

    p->cmd[0] = '\0';
    p->cmd_len = 0;
}

void cmd_archmux(void)
{
    /* Initialize both panes */
    for (int i = 0; i < 2; i++) {
        panes[i].line_count = 0;
        panes[i].cmd[0] = '\0';
        panes[i].cmd_len = 0;
        panes[i].scroll_offset = 0;
    }
    active_pane = 0;
    split_vertical = 0;

    pane_add_line(&panes[0], "ArchaOS archmux Pane 0 ready. Type 'help' for commands.");
    pane_add_line(&panes[1], "ArchaOS archmux Pane 1 ready. Type 'help' for commands.");

    int shift = 0, ctrl = 0, ext = 0;
    int ctrl_b_prefix = 0;

    while (1) {
        /* Render Header (Row 0) */
        mux_fill(0, 0x70);
        mux_puts(1, 0, "[archmux 1.0]", 0x70);
        mux_puts(16, 0, active_pane == 0 ? "[0: shell*]" : "[0: shell]", active_pane == 0 ? 0x71 : 0x70);
        mux_puts(28, 0, active_pane == 1 ? "[1: shell*]" : "[1: shell]", active_pane == 1 ? 0x71 : 0x70);

        if (split_vertical) {
            mux_puts(42, 0, "Split: Vertical | [Tab]: Toggle Pane", 0x70);
        } else {
            mux_puts(42, 0, "Split: Horizontal | [Tab]: Toggle Pane", 0x70);
        }

        /* Render Panes */
        if (!split_vertical) {
            /* Horizontal Split:
               Pane 0: rows 1..11 (11 rows)
               Divider: row 12
               Pane 1: rows 13..23 (11 rows)
            */
            for (int pi = 0; pi < 2; pi++) {
                int start_y = (pi == 0) ? 1 : 13;
                int max_r = 10; /* 10 lines of scrollback, last line is prompt */
                mux_pane_t *p = &panes[pi];

                int visible_lines = p->line_count < max_r ? p->line_count : max_r;
                int start_line = p->line_count - visible_lines;
                if (start_line < 0) start_line = 0;

                for (int r = 0; r < max_r; r++) {
                    int y = start_y + r;
                    mux_fill(y, 0x07);
                    int line_idx = start_line + r;
                    if (line_idx < p->line_count) {
                        mux_puts(1, y, p->lines[line_idx], 0x07);
                    }
                }

                /* Prompt Line */
                int py = start_y + max_r;
                mux_fill(py, 0x07);
                char pbuf[8];
                pbuf[0] = 'A'; pbuf[1] = 'r'; pbuf[2] = 'c';
                pbuf[3] = '/'; pbuf[4] = '0' + pi;
                pbuf[5] = '>'; pbuf[6] = ' '; pbuf[7] = '\0';
                mux_puts(1, py, pbuf, (pi == active_pane) ? 0x0A : 0x08);
                mux_puts(8, py, p->cmd, 0x0F);
            }

            /* Divider at Row 12 */
            mux_fill(12, active_pane == 0 ? 0x1F : 0x2F);
            mux_puts(1, 12, "--- [Pane 0] ----------------------------------------------------------------", 0x0B);

        } else {
            /* Vertical Split:
               Pane 0: cols 0..38, rows 1..23
               Divider: col 39
               Pane 1: cols 40..79, rows 1..23
            */
            for (int y = 1; y <= 23; y++) {
                mux_fill(y, 0x07);
                mux_putc(39, y, '|', 0x0B);
            }

            for (int pi = 0; pi < 2; pi++) {
                int start_x = (pi == 0) ? 1 : 41;
                int max_w = 37;
                int max_r = 22;
                mux_pane_t *p = &panes[pi];

                int visible_lines = p->line_count < max_r ? p->line_count : max_r;
                int start_line = p->line_count - visible_lines;
                if (start_line < 0) start_line = 0;

                for (int r = 0; r < max_r; r++) {
                    int y = 1 + r;
                    int line_idx = start_line + r;
                    if (line_idx < p->line_count) {
                        char sub[40];
                        strncpy(sub, p->lines[line_idx], max_w);
                        sub[max_w] = '\0';
                        mux_puts(start_x, y, sub, 0x07);
                    }
                }

                /* Prompt line */
                int py = 23;
                char pbuf[8];
                pbuf[0] = 'A'; pbuf[1] = 'r'; pbuf[2] = 'c';
                pbuf[3] = '/'; pbuf[4] = '0' + pi;
                pbuf[5] = '>'; pbuf[6] = ' '; pbuf[7] = '\0';
                mux_puts(start_x, py, pbuf, (pi == active_pane) ? 0x0A : 0x08);
                mux_puts(start_x + 7, py, p->cmd, 0x0F);
            }
        }

        /* Status Footer (Row 24) */
        mux_fill(24, 0x70);
        if (ctrl_b_prefix) {
            mux_puts(1, 24, "PREFIX: [o] Switch  [\"] Horiz Split  [%] Vert Split  [x] Exit", 0x74);
        } else {
            mux_puts(1, 24, "[Tab] Toggle Pane   [Ctrl+B] Prefix Menu   [Ctrl+B x] Exit   'exit' to close", 0x70);
        }

        /* Update hardware cursor position */
        if (!split_vertical) {
            int cur_y = (active_pane == 0) ? 11 : 23;
            vga_set_cursor(8 + panes[active_pane].cmd_len, cur_y);
        } else {
            int cur_x = (active_pane == 0) ? 8 : 48;
            vga_set_cursor(cur_x + panes[active_pane].cmd_len, 23);
        }

        /* Keyboard handling */
        uint8_t sc = keyboard_get_scancode();
        if (sc == 0xE0) { ext = 1; continue; }
        if (sc & 0x80) {
            uint8_t rel = sc & 0x7F;
            if (rel == 0x2A || rel == 0x36) shift = 0;
            if (rel == 0x1D) ctrl = 0;
            ext = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (sc == 0x1D) { ctrl = 1; continue; }

        if (ext) { ext = 0; continue; }

        /* Handle Ctrl+B prefix */
        if (ctrl && sc == 0x30) { /* 0x30 is scancode for 'b' */
            ctrl_b_prefix = 1;
            continue;
        }

        if (ctrl_b_prefix) {
            ctrl_b_prefix = 0;
            if (sc == 0x2D) { /* 'x' */
                break;
            } else if (sc == 0x18) { /* 'o' */
                active_pane = !active_pane;
                continue;
            } else if (sc == 0x28 || sc == 0x03) { /* '"' or '2' */
                split_vertical = 0;
                continue;
            } else if (sc == 0x06) { /* '%' or '5' */
                split_vertical = 1;
                continue;
            }
        }

        if (sc == 0x0F) { /* Tab: switch pane */
            active_pane = !active_pane;
            continue;
        }

        if (sc == 0x01) { /* Esc: cancel prefix or exit */
            if (ctrl_b_prefix) {
                ctrl_b_prefix = 0;
            } else {
                break;
            }
            continue;
        }

        mux_pane_t *cur_p = &panes[active_pane];

        if (sc == 0x1C) { /* Enter */
            if (strcmp(cur_p->cmd, "exit") == 0) {
                break;
            }
            pane_execute(cur_p, active_pane);
            continue;
        }

        if (sc == 0x0E) { /* Backspace */
            if (cur_p->cmd_len > 0) {
                cur_p->cmd_len--;
                cur_p->cmd[cur_p->cmd_len] = '\0';
            }
            continue;
        }

        char ascii = keyboard_scancode_to_ascii(sc, shift, 0);
        if (ascii >= 32 && ascii <= 126) {
            if (cur_p->cmd_len < 60) {
                cur_p->cmd[cur_p->cmd_len++] = ascii;
                cur_p->cmd[cur_p->cmd_len] = '\0';
            }
        }
    }

    vga_clear();
    vga_set_cursor(0, 0);
}
