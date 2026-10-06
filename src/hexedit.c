#include "hexedit.h"
#include "fs.h"
#include "vga.h"
#include "keyboard.h"
#include "pit.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

#define HEX_MAX_SIZE  32768
#define ROWS_PER_PAGE 22
#define BYTES_PER_ROW 16

#define ATTR_HEADER   0x70  /* Black on light gray */
#define ATTR_OFFSET   0x0E  /* Yellow on black */
#define ATTR_HEX      0x07  /* Light gray on black */
#define ATTR_HEX_CUR  0x1F  /* Bright white on blue */
#define ATTR_ASCII    0x0A  /* Light green on black */
#define ATTR_BAR      0x70  /* Black on light gray */
#define ATTR_MSG      0x0F  /* Bright white */

static void hex_putc(int x, int y, char c, uint8_t attr)
{
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;
    vga_write_cell(x, y, ((uint16_t)(unsigned char)c) | ((uint16_t)attr << 8));
}

static void hex_puts(int x, int y, const char *s, uint8_t attr)
{
    while (*s && x < 80) hex_putc(x++, y, *s++, attr);
}

static void hex_fill(int y, uint8_t attr)
{
    for (int x = 0; x < 80; x++) hex_putc(x, y, ' ', attr);
}

static const char hex_chars[] = "0123456789ABCDEF";

static void format_hex8(uint32_t val, char *out)
{
    for (int i = 7; i >= 0; i--) {
        out[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    out[8] = '\0';
}

static void format_hex2(uint8_t val, char *out)
{
    out[0] = hex_chars[(val >> 4) & 0xF];
    out[1] = hex_chars[val & 0xF];
    out[2] = '\0';
}

void cmd_hexedit(const char *path)
{
    if (!path || !path[0]) {
        vga_print("usage: hexedit <filename>\n");
        return;
    }
    while (*path == ' ') path++;

    fs_node_t *node = fs_resolve(path);
    if (!node || node->type != FS_FILE || !node->data) {
        vga_print_color("hexedit: cannot open '", 0x0C);
        vga_print(path);
        vga_print("': No such file\n");
        return;
    }

    static uint8_t file_buf[HEX_MAX_SIZE];
    size_t file_size = node->size;
    if (file_size > HEX_MAX_SIZE) file_size = HEX_MAX_SIZE;
    if (file_size == 0) file_size = 1; /* At least 1 editable byte */

    for (size_t i = 0; i < file_size; i++) {
        file_buf[i] = ((uint8_t *)node->data)[i];
    }

    size_t cur_pos = 0;
    int pane_mode = 0;   /* 0: Hex Pane, 1: ASCII Pane */
    int hex_nibble = 0;  /* 0: High nibble, 1: Low nibble */
    int modified = 0;
    char status_msg[64] = "Ready";

    int shift = 0, ctrl = 0, ext = 0;

    while (1) {
        size_t top_row = (cur_pos / BYTES_PER_ROW);
        if (top_row >= ROWS_PER_PAGE) {
            top_row -= (ROWS_PER_PAGE - 1);
        } else {
            top_row = 0;
        }
        size_t top_offset = top_row * BYTES_PER_ROW;

        /* Row 0: Header */
        hex_fill(0, ATTR_HEADER);
        hex_puts(1, 0, "HEXEDIT 1.0", ATTR_HEADER);
        hex_puts(15, 0, "File: ", ATTR_HEADER);
        hex_puts(21, 0, path, ATTR_HEADER);

        char sz_str[16];
        itoa((int)file_size, sz_str, 10);
        hex_puts(46, 0, "Size: ", ATTR_HEADER);
        hex_puts(52, 0, sz_str, ATTR_HEADER);
        hex_puts(52 + strlen(sz_str), 0, "B", ATTR_HEADER);

        if (modified) {
            hex_puts(65, 0, "[MODIFIED]", 0x74); /* Red on light gray */
        }

        /* Rows 1..22: Hex & ASCII View */
        for (int r = 0; r < ROWS_PER_PAGE; r++) {
            int y = 1 + r;
            size_t row_offset = top_offset + r * BYTES_PER_ROW;
            hex_fill(y, 0x07);

            if (row_offset < file_size) {
                /* Address offset */
                char off_str[9];
                format_hex8((uint32_t)row_offset, off_str);
                hex_puts(1, y, off_str, ATTR_OFFSET);
                hex_putc(9, y, ':', ATTR_OFFSET);

                /* 16 bytes */
                for (int b = 0; b < BYTES_PER_ROW; b++) {
                    size_t byte_pos = row_offset + b;
                    int hex_x = 12 + b * 3 + (b >= 8 ? 1 : 0);
                    int asc_x = 62 + b;

                    if (byte_pos < file_size) {
                        uint8_t byte_val = file_buf[byte_pos];
                        char h2[3];
                        format_hex2(byte_val, h2);

                        int is_cur = (byte_pos == cur_pos);
                        uint8_t hattr = is_cur ? (pane_mode == 0 ? ATTR_HEX_CUR : 0x30) : ATTR_HEX;
                        uint8_t aattr = is_cur ? (pane_mode == 1 ? ATTR_HEX_CUR : 0x30) : ATTR_ASCII;

                        hex_putc(hex_x, y, h2[0], hattr);
                        hex_putc(hex_x + 1, y, h2[1], hattr);

                        char ac = (byte_val >= 32 && byte_val <= 126) ? (char)byte_val : '.';
                        hex_putc(asc_x, y, ac, aattr);
                    } else {
                        hex_puts(hex_x, y, "  ", 0x08);
                        hex_putc(asc_x, y, ' ', 0x08);
                    }
                }
                hex_putc(60, y, '|', 0x08);
            }
        }

        /* Row 23: Shortcut Bar */
        hex_fill(23, ATTR_BAR);
        hex_puts(1, 23, "[Tab] Mode   [F2/Ctrl+S] Save   [Esc/Ctrl+Q] Quit   [Arrows] Navigate", ATTR_BAR);

        /* Row 24: Status & Cursor details */
        hex_fill(24, 0x00);
        char cur_off_str[9];
        format_hex8((uint32_t)cur_pos, cur_off_str);
        hex_puts(1, 24, "Offset: 0x", 0x0B);
        hex_puts(11, 24, cur_off_str, 0x0F);
        hex_puts(22, 24, "Pane: ", 0x0B);
        hex_puts(28, 24, pane_mode == 0 ? "HEX" : "ASCII", 0x0E);
        hex_puts(36, 24, "Status: ", 0x0B);
        hex_puts(44, 24, status_msg, ATTR_MSG);

        /* Set hardware cursor */
        int cur_row = (int)((cur_pos - top_offset) / BYTES_PER_ROW);
        int cur_col = (int)((cur_pos - top_offset) % BYTES_PER_ROW);
        if (pane_mode == 0) {
            int cx = 12 + cur_col * 3 + (cur_col >= 8 ? 1 : 0) + hex_nibble;
            vga_set_cursor(cx, 1 + cur_row);
        } else {
            vga_set_cursor(62 + cur_col, 1 + cur_row);
        }

        /* Keyboard polling */
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

        if (ext) {
            ext = 0;
            if (sc == 0x4B) { /* Left Arrow */
                if (pane_mode == 0) {
                    if (hex_nibble > 0) {
                        hex_nibble = 0;
                    } else if (cur_pos > 0) {
                        cur_pos--;
                        hex_nibble = 1;
                    }
                } else if (cur_pos > 0) {
                    cur_pos--;
                }
            } else if (sc == 0x4D) { /* Right Arrow */
                if (pane_mode == 0) {
                    if (hex_nibble == 0) {
                        hex_nibble = 1;
                    } else if (cur_pos + 1 < file_size) {
                        cur_pos++;
                        hex_nibble = 0;
                    }
                } else if (cur_pos + 1 < file_size) {
                    cur_pos++;
                }
            } else if (sc == 0x48) { /* Up Arrow */
                if (cur_pos >= BYTES_PER_ROW) cur_pos -= BYTES_PER_ROW;
            } else if (sc == 0x50) { /* Down Arrow */
                if (cur_pos + BYTES_PER_ROW < file_size) cur_pos += BYTES_PER_ROW;
            } else if (sc == 0x49) { /* PgUp */
                if (cur_pos >= ROWS_PER_PAGE * BYTES_PER_ROW)
                    cur_pos -= ROWS_PER_PAGE * BYTES_PER_ROW;
                else
                    cur_pos = 0;
            } else if (sc == 0x51) { /* PgDn */
                if (cur_pos + ROWS_PER_PAGE * BYTES_PER_ROW < file_size)
                    cur_pos += ROWS_PER_PAGE * BYTES_PER_ROW;
                else
                    cur_pos = file_size - 1;
            } else if (sc == 0x47) { /* Home */
                cur_pos = (cur_pos / BYTES_PER_ROW) * BYTES_PER_ROW;
                hex_nibble = 0;
            } else if (sc == 0x4F) { /* End */
                cur_pos = (cur_pos / BYTES_PER_ROW) * BYTES_PER_ROW + (BYTES_PER_ROW - 1);
                if (cur_pos >= file_size) cur_pos = file_size - 1;
                hex_nibble = 1;
            }
            continue;
        }

        /* Non-extended keys */
        if (sc == 0x01 || (ctrl && sc == 0x10)) { /* Esc or Ctrl+Q: Exit */
            break;
        }

        if (sc == 0x0F) { /* Tab: switch pane */
            pane_mode = !pane_mode;
            hex_nibble = 0;
            continue;
        }

        /* F2 or Ctrl+S: Save */
        if (sc == 0x3C || (ctrl && sc == 0x1F)) {
            fs_write(path, (const char *)file_buf, file_size);
            modified = 0;
            strncpy(status_msg, "File saved successfully", sizeof(status_msg) - 1);
            continue;
        }

        /* Editing logic */
        char ascii = keyboard_scancode_to_ascii(sc, shift, 0);
        if (pane_mode == 0) {
            /* Hex input 0-9, a-f */
            int val = -1;
            if (ascii >= '0' && ascii <= '9') val = ascii - '0';
            else if (ascii >= 'a' && ascii <= 'f') val = ascii - 'a' + 10;
            else if (ascii >= 'A' && ascii <= 'F') val = ascii - 'A' + 10;

            if (val >= 0) {
                if (hex_nibble == 0) {
                    file_buf[cur_pos] = (file_buf[cur_pos] & 0x0F) | (val << 4);
                    hex_nibble = 1;
                } else {
                    file_buf[cur_pos] = (file_buf[cur_pos] & 0xF0) | (val & 0x0F);
                    hex_nibble = 0;
                    if (cur_pos + 1 < file_size) cur_pos++;
                }
                modified = 1;
                strncpy(status_msg, "Byte edited", sizeof(status_msg) - 1);
            }
        } else {
            /* ASCII input */
            if (ascii >= 32 && ascii <= 126) {
                file_buf[cur_pos] = (uint8_t)ascii;
                modified = 1;
                if (cur_pos + 1 < file_size) cur_pos++;
                strncpy(status_msg, "Char edited", sizeof(status_msg) - 1);
            }
        }
    }

    vga_clear();
    vga_set_cursor(0, 0);
}
