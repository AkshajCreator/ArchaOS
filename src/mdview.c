#include "mdview.h"
#include "fs.h"
#include "vga.h"
#include "string.h"
#include "mm.h"
#include <stdint.h>
#include <stddef.h>

static void print_formatted_line(const char *line, int in_code_block) {
    if (in_code_block) {
        vga_print_color("  | ", 0x08); /* Dark gray border */
        vga_print_color(line, 0x0A);   /* Green monospace text */
        vga_print("\n");
        return;
    }

    /* Skip leading whitespace */
    while (*line == ' ') line++;
    if (!*line) {
        vga_print("\n");
        return;
    }

    /* Horizontal rule --- or === */
    if ((line[0] == '-' && line[1] == '-' && line[2] == '-') ||
        (line[0] == '=' && line[1] == '=' && line[2] == '=')) {
        vga_print_color("  ------------------------------------------------------------\n", 0x08);
        return;
    }

    /* Headers */
    if (line[0] == '#' && line[1] == ' ') {
        vga_print("\n");
        vga_print_color("  === ", 0x0B);
        vga_print_color(line + 2, 0x0F); /* Bright white */
        vga_print_color(" ===\n", 0x0B);
        return;
    }
    if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
        vga_print("\n");
        vga_print_color("  -- ", 0x0E); /* Yellow */
        vga_print_color(line + 3, 0x0E);
        vga_print_color(" --\n", 0x0E);
        return;
    }
    if (line[0] == '#' && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
        vga_print_color("  * ", 0x0A); /* Light green */
        vga_print_color(line + 4, 0x0A);
        vga_print("\n");
        return;
    }

    /* Blockquote */
    if (line[0] == '>' && (line[1] == ' ' || line[1] == '\0')) {
        vga_print_color("  | ", 0x03); /* Cyan bar */
        const char *p = line + 1;
        while (*p == ' ') p++;
        vga_print_color(p, 0x07);
        vga_print("\n");
        return;
    }

    /* Bullet list */
    if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
        vga_print_color("   \xFE ", 0x0E); /* Bullet glyph in yellow */
        vga_print(line + 2);
        vga_print("\n");
        return;
    }

    /* Numbered list e.g. "1. " */
    if (line[0] >= '0' && line[0] <= '9' && line[1] == '.' && line[2] == ' ') {
        char nbuf[8];
        nbuf[0] = ' '; nbuf[1] = line[0]; nbuf[2] = '.'; nbuf[3] = ' '; nbuf[4] = '\0';
        vga_print_color(nbuf, 0x0B);
        vga_print(line + 3);
        vga_print("\n");
        return;
    }

    /* Regular paragraph text */
    vga_print("  ");
    const char *p = line;
    while (*p) {
        /* Bold **text** */
        if (p[0] == '*' && p[1] == '*') {
            p += 2;
            const char *end = strstr(p, "**");
            if (end) {
                while (p < end) {
                    char cstr[2] = { *p++, '\0' };
                    vga_print_color(cstr, 0x0F); /* Bright white */
                }
                p += 2;
                continue;
            }
        }
        /* Inline code `code` */
        if (*p == '`') {
            p++;
            const char *end = strchr(p, '`');
            if (end) {
                while (p < end) {
                    char cstr[2] = { *p++, '\0' };
                    vga_print_color(cstr, 0x0A); /* Light green */
                }
                p++;
                continue;
            }
        }
        char cstr[2] = { *p++, '\0' };
        vga_print(cstr);
    }
    vga_print("\n");
}

void cmd_mdview(const char *args) {
    if (!args || !args[0]) {
        vga_print("Usage: mdview <file.md>\n");
        return;
    }
    while (*args == ' ') args++;

    fs_node_t *node = fs_resolve(args);
    if (!node || node->type != FS_FILE || !node->data || node->size == 0) {
        vga_print_color("mdview: cannot open markdown file: ", 0x0C);
        vga_print(args);
        vga_print("\n");
        return;
    }

    vga_print_color("\n=== Markdown Viewer: ", 0x03);
    vga_print_color(args, 0x0F);
    vga_print_color(" ===\n\n", 0x03);

    const char *data = (const char *)node->data;
    size_t size = node->size;
    size_t pos = 0;
    int in_code_block = 0;

    char line_buf[256];
    while (pos < size) {
        size_t lidx = 0;
        while (pos < size && data[pos] != '\n' && lidx < sizeof(line_buf) - 1) {
            if (data[pos] != '\r') {
                line_buf[lidx++] = data[pos];
            }
            pos++;
        }
        if (pos < size && data[pos] == '\n') pos++;
        line_buf[lidx] = '\0';

        /* Code fence ``` toggle */
        if (line_buf[0] == '`' && line_buf[1] == '`' && line_buf[2] == '`') {
            in_code_block = !in_code_block;
            if (in_code_block) {
                vga_print_color("  +-- [Code Block] ---------------------------------\n", 0x08);
            } else {
                vga_print_color("  +-------------------------------------------------\n", 0x08);
            }
            continue;
        }

        print_formatted_line(line_buf, in_code_block);
    }
    vga_print_color("\n=== End of Document ===\n\n", 0x08);
}
