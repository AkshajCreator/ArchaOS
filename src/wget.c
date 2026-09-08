// src/wget.c — ArchaOS Network File Downloader
#include "wget.h"
#include "fs.h"
#include "vga.h"
#include "mm.h"
#include "kernel.h"
#include "net/media_fetch.h"
#include <stdint.h>
#include <stddef.h>

static int w_strncmp(const char *a, const char *b, int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (*(unsigned char*)a - *(unsigned char*)b) : 0;
}

void cmd_wget(const char *args)
{
    if (!args || !args[0]) {
        vga_print("usage: wget <url> [-O filename]\n  Downloads a file from HTTP/HTTPS and saves to VFS.\n");
        return;
    }

    /* Skip leading whitespace */
    while (*args == ' ') args++;

    char url[256] = "";
    char out_file[64] = "";

    /* Parse arguments: check for -O flag */
    const char *p = args;
    while (*p) {
        if (w_strncmp(p, "-O ", 3) == 0 || w_strncmp(p, "-o ", 3) == 0) {
            p += 3;
            while (*p == ' ') p++;
            int fi = 0;
            while (*p && *p != ' ' && fi < 63) out_file[fi++] = *p++;
            out_file[fi] = '\0';
        } else if (*p != ' ') {
            int ui = 0;
            while (*p && *p != ' ' && ui < 255) url[ui++] = *p++;
            url[ui] = '\0';
        } else {
            p++;
        }
    }

    if (!url[0]) {
        vga_print("wget: missing URL operand\n");
        return;
    }

    /* If no output filename given, derive from URL path */
    if (!out_file[0]) {
        const char *last_slash = 0;
        for (int i = 0; url[i]; i++) {
            if (url[i] == '/') last_slash = &url[i + 1];
        }
        if (last_slash && *last_slash) {
            int fi = 0;
            while (last_slash[fi] && last_slash[fi] != '?' && last_slash[fi] != '#' && fi < 63) {
                out_file[fi] = last_slash[fi];
                fi++;
            }
            out_file[fi] = '\0';
        }
        if (!out_file[0]) {
            int fi = 0;
            const char *def = "download.bin";
            while (def[fi]) { out_file[fi] = def[fi]; fi++; }
            out_file[fi] = '\0';
        }
    }

    vga_print_color("--  Connecting to: ", 0x0B);
    vga_print(url);
    vga_print("\n");

    uint8_t *data = 0;
    uint32_t len = 0;

    int ret = media_fetch(url, &data, &len);
    if (!ret || !data || len == 0) {
        vga_print_color("wget: download failed (network error or HTTP 404/500)\n", 0x0C);
        if (data) kfree(data);
        return;
    }

    /* Save to VFS */
    int w_ret = fs_write(out_file, (const char *)data, (size_t)len);
    kfree(data);

    if (w_ret < 0) {
        vga_print_color("wget: error writing to VFS target '", 0x0C);
        vga_print(out_file);
        vga_print("'\n");
        return;
    }

    char len_buf[16];
    itoa((int)len, len_buf, 10);

    vga_print_color("[200 OK] ", 0x0A);
    vga_print("Saved: '");
    vga_print_color(out_file, 0x0F);
    vga_print("' [");
    vga_print(len_buf);
    vga_print(" bytes]\n");
}
