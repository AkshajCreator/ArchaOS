// src/kernel.c

#include "kernel.h"
#include "vga.h"
#include "idt.h"
#include "mm.h"
#include "multiboot.h"
#include "fs.h"
#include "editor.h"
#include "ai.h"
#include "pit.h"
#include "splash.h"
#include "neofetch.h"
#include "fortune.h"
#include "theme.h"
#include "gui.h"
#include "serial.h"
#include "pci.h"
#include "ata.h"
#include "net/e1000.h"
#include "net/net.h"
#include "net/arp.h"
#include "net/ip.h"
#include "net/dhcp.h"
#include "net/dns.h"
#include "net/tcp.h"
#include "net/http.h"
#include "net/nim.h"
#include "net/browse.h"
#include "net/js/js_engine.h"
#include "audio.h"
#include "video.h"
#include "matrix.h"
#include "less.h"
#include "wget.h"
#include "shellext.h"

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * PORT I/O
 * ============================================================ */

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ============================================================
 * STRINGS
 * ============================================================ */

int atoi(const char *s)
{
    int v = 0;
    while (*s == ' ') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sign;
}

/* ============================================================
 * ITOA
 * ============================================================ */

char *itoa(int value, char *str, int base)
{
    char *rc  = str;
    char *ptr = str;
    char *low;

    if (base < 2 || base > 36) { *str = '\0'; return str; }

    unsigned int uval = (unsigned int)value;
    if (value < 0 && base == 10)
    {
        *ptr++ = '-';
        uval = (unsigned int)(-value);
    }

    low = ptr;

    do
    {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"
        [uval % base];
        uval /= base;
    } while (uval);

    *ptr-- = '\0';

    while (low < ptr)
    {
        char tmp = *low;
        *low++   = *ptr;
        *ptr--   = tmp;
    }

    return rc;
}

/* ============================================================
 * REBOOT
 * ============================================================ */

void reboot(void)
{
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
}

/* ============================================================
 * RTC — polled read (used only for uptime baseline at boot)
 * The IRQ8 handler in idt.c re-arms the RTC each tick.
 * ============================================================ */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static inline uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg | 0x80);
    return inb(CMOS_DATA);
}

static inline int rtc_updating(void)
{
    outb(CMOS_ADDR, 0x0A | 0x80);
    return inb(CMOS_DATA) & 0x80;
}

static inline uint8_t bcd_to_bin(uint8_t value)
{
    return (value & 0x0F) + ((value >> 4) * 10);
}

static uint32_t rtc_seconds_since_midnight(void)
{
    uint8_t sec, min, hour, regB, last;

    uint32_t to = 100000;
    while (rtc_updating() && --to);

    do
    {
        last = cmos_read(0x00);
        sec  = cmos_read(0x00);
    } while (last != sec);

    min  = cmos_read(0x02);
    hour = cmos_read(0x04);
    regB = cmos_read(0x0B);

    if (!(regB & 0x04))
    {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = bcd_to_bin(hour);
    }

    if (!(regB & 0x02) && (hour & 0x80))
        hour = ((hour & 0x7F) + 12) % 24;

    return (uint32_t)hour * 3600U +
    (uint32_t)min  * 60U   +
    (uint32_t)sec;
}

/* ============================================================
 * UPTIME
 * ============================================================ */

static uint32_t boot_seconds = 0;

/* ============================================================
 * SPEAKER
 * ============================================================ */

static void play_sound(uint32_t frequency)
{
    if (frequency == 0) return;
    uint32_t divisor = 1193180 / frequency;
    uint8_t  tmp;

    outb(0x43, 0xB6);
    outb(0x42,  divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    tmp = inb(0x61);
    if ((tmp | 3) != tmp) outb(0x61, tmp | 3);
}

static void no_sound(void)
{
    outb(0x61, inb(0x61) & 0xFC);
}

void beep(void)
{
    play_sound(1000);
    pit_sleep(150);
    no_sound();
}

/* Boot chime — two rising tones like early PC/Windows */
void boot_chime(void)
{
    play_sound(523);  /* C5  */
    pit_sleep(120);
    no_sound();
    pit_sleep(40);
    play_sound(659);  /* E5  */
    pit_sleep(120);
    no_sound();
    pit_sleep(40);
    play_sound(784);  /* G5  */
    pit_sleep(180);
    no_sound();
}

/* ============================================================
 * DATE/TIME COMMAND
 * ============================================================ */

static void cmd_datetime(void)
{
    uint8_t sec, min, hour, day, month;
    uint8_t regB;
    char buf[4];

    uint32_t to = 100000;
    while (rtc_updating() && --to);

    sec   = cmos_read(0x00);
    min   = cmos_read(0x02);
    hour  = cmos_read(0x04);
    day   = cmos_read(0x07);
    month = cmos_read(0x08);
    regB  = cmos_read(0x0B);

    if (!(regB & 0x04))
    {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
    }

    /* Print DD/MM  HH:MM:SS */
    itoa(day,   buf, 10); if (day   < 10) vga_print("0"); vga_print(buf);
    vga_print("/");
    itoa(month, buf, 10); if (month < 10) vga_print("0"); vga_print(buf);
    vga_print("  ");
    itoa(hour,  buf, 10); if (hour  < 10) vga_print("0"); vga_print(buf);
    vga_print(":");
    itoa(min,   buf, 10); if (min   < 10) vga_print("0"); vga_print(buf);
    vga_print(":");
    itoa(sec,   buf, 10); if (sec   < 10) vga_print("0"); vga_print(buf);
    vga_print("\n");
}

/* ============================================================
 * TREE COMMAND
 * ============================================================ */

static void tree_recurse(fs_node_t *dir, char *prefix, int prefix_len, int *d_count, int *f_count, int depth)
{
    if (!dir || depth >= 8) return;

    for (int i = 0; i < dir->child_count; i++) {
        fs_node_t *c = dir->children[i];
        if (!c) continue;
        int is_last = (i == dir->child_count - 1);

        vga_print(prefix);
        if (is_last) {
            vga_print("`-- ");
        } else {
            vga_print("|-- ");
        }

        if (c->type == FS_DIR) {
            vga_print_color(c->name, 0x0B); /* Cyan for directories */
            vga_print("\n");
            (*d_count)++;

            char new_prefix[64];
            int p = 0;
            for (; p < prefix_len && p < 58; p++) new_prefix[p] = prefix[p];
            if (is_last) {
                new_prefix[p++] = ' '; new_prefix[p++] = ' '; new_prefix[p++] = ' '; new_prefix[p++] = ' ';
            } else {
                new_prefix[p++] = '|'; new_prefix[p++] = ' '; new_prefix[p++] = ' '; new_prefix[p++] = ' ';
            }
            new_prefix[p] = '\0';

            tree_recurse(c, new_prefix, p, d_count, f_count, depth + 1);
        } else {
            vga_print_color(c->name, 0x0F); /* Bright white for files */
            vga_print("\n");
            (*f_count)++;
        }
    }
}

static void cmd_tree(const char *path)
{
    fs_node_t *target = 0;
    if (!path || !path[0] || (path[0] == '.' && path[1] == '\0')) {
        target = fs_cwd();
    } else {
        target = fs_resolve(path);
    }

    if (!target) {
        vga_print("tree: [error opening dir]\n");
        return;
    }

    if (target->type != FS_DIR) {
        vga_print(target->name);
        vga_print("\n\n0 directories, 1 file\n");
        return;
    }

    vga_print_color(target->name[0] ? target->name : "/", 0x0B);
    vga_print("\n");

    int d_count = 0, f_count = 0;
    char prefix[64] = "";
    tree_recurse(target, prefix, 0, &d_count, &f_count, 0);

    char buf[16];
    vga_print("\n");
    itoa(d_count, buf, 10);
    vga_print(buf);
    vga_print(d_count == 1 ? " directory, " : " directories, ");
    itoa(f_count, buf, 10);
    vga_print(buf);
    vga_print(f_count == 1 ? " file\n" : " files\n");
}

/* ============================================================
 * HEXDUMP / XXD COMMAND
 * ============================================================ */

static void cmd_hexdump(const char *path)
{
    if (!path || !path[0]) {
        vga_print("usage: hexdump <file> or xxd <file>\n  Displays canonical hex and ASCII dump of a file.\n");
        return;
    }
    fs_node_t *node = fs_resolve(path);
    if (!node || node->type != FS_FILE || !node->data) {
        vga_print("hexdump: no such file or cannot read\n");
        return;
    }

    size_t sz = node->size;
    const uint8_t *data = node->data;
    static const char hex_chars[] = "0123456789ABCDEF";

    for (size_t offset = 0; offset < sz; offset += 16) {
        char off_str[12];
        for (int b = 7; b >= 0; b--) {
            off_str[7 - b] = hex_chars[(offset >> (b * 4)) & 0xF];
        }
        off_str[8] = ':';
        off_str[9] = ' ';
        off_str[10] = '\0';
        vga_print_color(off_str, 0x08);

        for (int i = 0; i < 16; i++) {
            if (i == 8) vga_print(" ");
            if (offset + i < sz) {
                uint8_t b = data[offset + i];
                char hb[4];
                hb[0] = hex_chars[(b >> 4) & 0xF];
                hb[1] = hex_chars[b & 0xF];
                hb[2] = ' ';
                hb[3] = '\0';
                vga_print(hb);
            } else {
                vga_print("   ");
            }
        }

        vga_print(" |");
        for (int i = 0; i < 16 && (offset + i) < sz; i++) {
            uint8_t b = data[offset + i];
            char ac = (b >= 32 && b <= 126) ? (char)b : '.';
            vga_print_char(ac);
        }
        vga_print("|\n");

        if (offset >= 512 && (offset + 16) < sz) {
            vga_print("... [truncated, remaining ");
            char rem[16];
            itoa((int)(sz - offset - 16), rem, 10);
            vga_print(rem);
            vga_print(" bytes]\n");
            break;
        }
    }
}

/* ============================================================
 * STAT COMMAND
 * ============================================================ */

static void cmd_stat(const char *path)
{
    if (!path || !path[0]) {
        vga_print("usage: stat <file_or_dir>\n  Displays detailed node status and metadata.\n");
        return;
    }
    fs_node_t *n = fs_resolve(path);
    if (!n) {
        vga_print("stat: cannot stat '");
        vga_print(path);
        vga_print("': No such file or directory\n");
        return;
    }

    char buf[32];
    vga_print("  File: \""); vga_print(n->name); vga_print("\"\n");
    vga_print("  Type: ");
    if (n->type == FS_DIR) {
        vga_print_color("directory (FS_DIR)\n", 0x0B);
        vga_print("Entries: "); itoa(n->child_count, buf, 10); vga_print(buf);
        vga_print(" children (max 24)\n");
    } else {
        vga_print_color("regular file (FS_FILE)\n", 0x0A);
        vga_print("  Size: "); itoa((int)n->size, buf, 10); vga_print(buf);
        vga_print(" bytes\n");
        int blks = ((int)n->size + 511) / 512;
        vga_print("Blocks: "); itoa(blks, buf, 10); vga_print(buf);
        vga_print(" (512-byte blocks)\n");
    }

    vga_print(" Inode: 0x");
    uint32_t addr = (uint32_t)n;
    static const char hc[] = "0123456789ABCDEF";
    for (int b = 7; b >= 0; b--) {
        char ch[2] = { hc[(addr >> (b * 4)) & 0xF], '\0' };
        vga_print(ch);
    }
    vga_print(" | Parent: ");
    vga_print((n->parent && n->parent->name[0]) ? n->parent->name : "/");
    vga_print("\nAccess: (0755/drwxr-xr-x)  Uid: (0/root)  Gid: (0/root)\n");
}

/* ============================================================
 * TOP / SYSMON COMMAND
 * ============================================================ */

static void cmd_top(void)
{
    uint32_t now = rtc_seconds_since_midnight();
    uint32_t up = (now >= boot_seconds) ? (now - boot_seconds) : ((86400 - boot_seconds) + now);
    char buf[32];

    vga_print_color("=== ArchaOS System Resource Monitor (top) ===\n", 0x0E);
    
    vga_print_color("Kernel:   ", 0x0B);
    vga_print("ArchaOS v0.5 [x86 Ring 0 IA-32 Protected Mode]\n");
    vga_print_color("Uptime:   ", 0x0B);
    itoa((int)up, buf, 10); vga_print(buf); vga_print(" seconds | PIT Frequency: 100 Hz\n");

    mm_stats_t ms = mm_stats();
    vga_print_color("Memory:   ", 0x0B);
    itoa((int)(ms.used / 1024), buf, 10); vga_print(buf); vga_print(" KB used / ");
    itoa((int)(ms.total / 1024), buf, 10); vga_print(buf); vga_print(" KB total (");
    itoa((int)(ms.free / 1024), buf, 10); vga_print(buf); vga_print(" KB free)\n");

    vga_print("          [");
    int total_bars = 32;
    int used_bars = (ms.total > 0) ? (int)((ms.used * total_bars) / ms.total) : 0;
    if (used_bars < 1 && ms.used > 0) used_bars = 1;
    for (int i = 0; i < total_bars; i++) {
        if (i < used_bars) vga_print_color("|", 0x0A);
        else vga_print_color(".", 0x08);
    }
    vga_print("] ");
    int pct = (ms.total > 0) ? (int)((ms.used * 100) / ms.total) : 0;
    itoa(pct, buf, 10); vga_print(buf); vga_print("%\n");

    vga_print_color("Network:  ", 0x0B);
    if (net_if.up) {
        char ip_str[16], mac_str[20];
        ip_to_str(net_if.ip, ip_str);
        mac_to_str(net_if.mac, mac_str);
        vga_print_color("Intel E1000 Gigabit [ONLINE]\n", 0x0A);
        vga_print("          IP: "); vga_print(ip_str);
        vga_print(" | MAC: "); vga_print(mac_str);
        vga_print("\n");
    } else {
        vga_print("No network interface active\n");
    }

    vga_print_color("Audio:    ", 0x0B);
    vga_print("PC Speaker / Sound Blaster Universal Synth [");
    vga_print(audio_get_state() == AUDIO_STATE_PLAYING ? "PLAYING" : "IDLE");
    vga_print("] Vol: ");
    itoa((int)audio_get_volume(), buf, 10); vga_print(buf); vga_print("%\n");

    vga_print_color("\n  PID  USER   RING  STATE   PROCESS / SUBSYSTEM\n", 0x0F);
    vga_print("    0  root      0  RUN     kernel_core (IDT, GDT, Paging)\n");
    vga_print("    1  root      0  WAIT    pit_timer (IRQ0 100Hz Heartbeat)\n");
    vga_print("    2  root      0  RUN     vga_shell (Command Interpreter)\n");
    vga_print("    3  root      0  POLL    e1000_nic (PCI Ethernet Loop)\n");
    vga_print("    4  root      0  IDLE    audio_mixer (Universal Synthesizer)\n");
}

static void cmd_ansi_demo(void)
{
    vga_print("\033[1;36m=== ArchaOS ANSI Color & Terminal Engine ===\033[0m\n\n");

    vga_print("Standard 8 Colors (SGR 30..37):\n  ");
    const char *names[8] = { "Black", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White" };
    for (int i = 0; i < 8; i++) {
        char buf[32];
        if (i == 0) {
            vga_print("\033[30;47m[Black]\033[0m ");
        } else {
            snprintf(buf, sizeof(buf), "\033[%dm[%s]\033[0m ", 30 + i, names[i]);
            vga_print(buf);
        }
    }
    vga_print("\n\n");

    vga_print("High-Intensity / Bold Colors (SGR 90..97 / SGR 1):\n  ");
    for (int i = 0; i < 8; i++) {
        char buf[32];
        if (i == 0) {
            vga_print("\033[90;47m[Gray]\033[0m ");
        } else {
            snprintf(buf, sizeof(buf), "\033[1;%dm[%s]\033[0m ", 30 + i, names[i]);
            vga_print(buf);
        }
    }
    vga_print("\n\n");

    vga_print("Background Palette (SGR 40..47):\n  ");
    for (int i = 1; i < 8; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "\033[4%d;37m %s \033[0m ", i, names[i]);
        vga_print(buf);
    }
    vga_print("\n\n");

    vga_print("Text Styles & Composite Modes:\n  ");
    vga_print("\033[1m[Bold Text]\033[0m  ");
    vga_print("\033[7m[Inverse Video]\033[0m  ");
    vga_print("\033[1;32;44m[Bold Green on Blue]\033[0m  ");
    vga_print("\033[1;33;41m[Warning Banner]\033[0m\n");
}

/* ============================================================
 * COMMANDS
 * ============================================================ */

void kernel_execute_command(const char *cmd)
{
    if (strcmp(cmd, "help") == 0)
    {
        vga_print("=== ArchaOS Command Reference ===\n");
        vga_print("System:      help, new, general, clear, reboot, halt, uptime, date, top,\n");
        vga_print("             neofetch, fortune, theme <name>, matrix, credits, meminfo\n");
        vga_print("Filesystem:  ls, tree, cd, pwd, mkdir, touch, cat, less, nano, head,\n");
        vga_print("             tail, stat, hexdump, write, rm, cp, mv, wc, grep, find\n");
        vga_print("Multimedia:  audio [play|stop|pause|next|prev|list], video, beep\n");
        vga_print("Networking:  ifconfig, ping <host>, curl <url>, wget <url>, ports\n");
        vga_print("Shell/Env:   export VAR=val, env, unset VAR, $VAR, cmd1; cmd2, pipes |\n");
        vga_print("             Ctrl+R (history search), ansi / colors (palette test)\n");
        vga_print("GUI Desktop: gui (Mode 13h desktop with Web Browser & In-Browser Media)\n");
        vga_print("Hardware:    pci [list|scan], serial [com1|com2] [write|read|status]\n");
    }
    else if (strcmp(cmd, "new") == 0)
    {
        if (!gui_active && !shellext_is_capturing()) {
            cmd_less("/new");
        } else {
            fs_node_t *f = fs_resolve("/new");
            if (!f) f = fs_resolve("new");
            if (f && f->data) {
                vga_print((const char *)f->data);
                vga_print("\n");
            } else {
                vga_print("File /new not found.\n");
            }
        }
    }
    else if (strcmp(cmd, "general") == 0)
    {
        if (!gui_active && !shellext_is_capturing()) {
            cmd_less("/general");
        } else {
            fs_node_t *f = fs_resolve("/general");
            if (!f) f = fs_resolve("general");
            if (f && f->data) {
                vga_print((const char *)f->data);
                vga_print("\n");
            } else {
                vga_print("File /general not found.\n");
            }
        }
    }
    else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0)
    {
        vga_clear();
    }
    else if (strcmp(cmd, "reboot") == 0)
    {
        reboot();
    }
    else if (strcmp(cmd, "halt") == 0)
    {
        vga_print("System halted.\n");
        asm volatile("cli");
        for (;;) asm volatile("hlt");
    }
    else if (strcmp(cmd, "uptime") == 0)
    {
        uint32_t now = rtc_seconds_since_midnight();
        uint32_t up;
        char buf[16];

        if (now >= boot_seconds)
            up = now - boot_seconds;
        else
            up = (86400 - boot_seconds) + now;

        itoa((int)up, buf, 10);
        vga_print("Uptime: ");
        vga_print(buf);
        vga_print(" seconds\n");
    }
    else if (strcmp(cmd, "date") == 0)
    {
        cmd_datetime();
    }
    else if (strcmp(cmd, "echo") == 0 || strncmp(cmd, "echo ", 5) == 0)
    {
        if (strcmp(cmd, "echo") == 0)
            vga_print("usage: echo [-e] <text>\n  Prints the specified text. Use -e for ANSI escapes (\\e, \\033, \\x1b, \\n, \\t).\n");
        else
        {
            const char *arg = cmd + 5;
            int is_escape = 0;
            if (strncmp(arg, "-e ", 3) == 0) {
                is_escape = 1;
                arg += 3;
            }
            while (*arg == ' ') arg++;
            static char unquoted[256];
            int alen = strlen(arg);
            if (alen >= 2 && ((arg[0] == '"' && arg[alen - 1] == '"') || (arg[0] == '\'' && arg[alen - 1] == '\''))) {
                int ui = 0;
                for (int i = 1; i < alen - 1 && ui < 255; i++) unquoted[ui++] = arg[i];
                unquoted[ui] = '\0';
                arg = unquoted;
            }

            if (is_escape) {
                while (*arg) {
                    if (*arg == '\\' && *(arg + 1)) {
                        arg++;
                        if (*arg == 'e') vga_print_char('\033');
                        else if (*arg == '0' && *(arg+1) == '3' && *(arg+2) == '3') {
                            vga_print_char('\033');
                            arg += 2;
                        }
                        else if (*arg == 'x' && *(arg+1) == '1' && (*(arg+2) == 'b' || *(arg+2) == 'B')) {
                            vga_print_char('\033');
                            arg += 2;
                        }
                        else if (*arg == 'n') vga_print_char('\n');
                        else if (*arg == 'r') vga_print_char('\r');
                        else if (*arg == 't') vga_print_char('\t');
                        else if (*arg == '\\') vga_print_char('\\');
                        else vga_print_char(*arg);
                    } else {
                        vga_print_char(*arg);
                    }
                    arg++;
                }
            } else {
                vga_print(arg);
            }
            vga_print("\n");
        }
    }
    else if (strcmp(cmd, "ansi") == 0 || strcmp(cmd, "colors") == 0)
    {
        cmd_ansi_demo();
    }
    else if (strcmp(cmd, "top") == 0 || strcmp(cmd, "sysmon") == 0)
    {
        cmd_top();
    }
    else if (strcmp(cmd, "neofetch") == 0)
    {
        cmd_neofetch();
    }
    else if (strcmp(cmd, "fortune") == 0)
    {
        cmd_fortune();
    }
    else if (strcmp(cmd, "matrix") == 0)
    {
        cmd_matrix();
    }
    else if (strncmp(cmd, "theme ", 6) == 0)
    {
        theme_set(cmd + 6);
    }
    else if (strcmp(cmd, "theme") == 0)
    {
        theme_list();
    }
    else if (strcmp(cmd, "sleep") == 0 || strncmp(cmd, "sleep ", 6) == 0)
    {
        if (strcmp(cmd, "sleep") == 0)
            vga_print("usage: sleep <ms>\n  Pauses execution for specified milliseconds (1-10000).\n");
        else
        {
            const char *n = cmd + 6;
            while (*n == ' ') n++;
            int ms = 0;
            int valid = 0;
            while (*n >= '0' && *n <= '9') {
                ms = ms * 10 + (*n - '0');
                valid = 1;
                n++;
            }
            if (valid && ms > 0 && ms <= 10000) pit_sleep((uint32_t)ms);
            else vga_print("usage: sleep <ms> (1-10000)\n");
        }
    }
    else if (strcmp(cmd, "ls") == 0 ||
        strncmp(cmd, "ls ", 3) == 0)
    {
        const char *path = (cmd[2] == ' ') ? cmd + 3 : 0;
        char buf[1024];
        fs_ls(path, buf, sizeof(buf));
        vga_print(buf);
    }
    else if (strcmp(cmd, "tree") == 0 || strncmp(cmd, "tree ", 5) == 0)
    {
        const char *path = (cmd[4] == ' ') ? cmd + 5 : 0;
        cmd_tree(path);
    }

    else if (strcmp(cmd, "pwd") == 0)
    {
        char buf[FS_MAX_PATH];
        fs_pwd(buf, sizeof(buf));
        vga_print(buf);
        vga_print("\n");
    }

    else if (strncmp(cmd, "cd ", 3) == 0)
    {
        if (fs_cd(cmd + 3) < 0)
            vga_print("cd: no such directory\n");
    }

    else if (strcmp(cmd, "cd") == 0)
    {
        fs_cd("/");
    }

    else if (strcmp(cmd, "mkdir") == 0 || strncmp(cmd, "mkdir ", 6) == 0)
    {
        if (strcmp(cmd, "mkdir") == 0)
            vga_print("usage: mkdir <directory>\n  Creates a new directory in the RAM filesystem.\n");
        else if (!fs_mkdir(cmd + 6))
            vga_print("mkdir: failed\n");
    }

    else if (strcmp(cmd, "touch") == 0 || strncmp(cmd, "touch ", 6) == 0)
    {
        if (strcmp(cmd, "touch") == 0)
            vga_print("usage: touch <filename>\n  Creates a new empty file in the current directory.\n");
        else if (!fs_touch(cmd + 6))
            vga_print("touch: failed\n");
    }

    else if (strcmp(cmd, "cat") == 0 || strncmp(cmd, "cat ", 4) == 0)
    {
        if (strcmp(cmd, "cat") == 0)
            vga_print("usage: cat <filename>\n  Displays the contents of a file.\n");
        else
        {
            char buf[2048];
            if (fs_cat(cmd + 4, buf, sizeof(buf)) < 0)
                vga_print("cat: no such file\n");
            else
            {
                vga_print(buf);
                vga_print("\n");
            }
        }
    }
    else if (strcmp(cmd, "less") == 0 || strncmp(cmd, "less ", 5) == 0 ||
             strcmp(cmd, "more") == 0 || strncmp(cmd, "more ", 5) == 0)
    {
        const char *arg = (strncmp(cmd, "less ", 5) == 0) ? cmd + 5 :
                          (strncmp(cmd, "more ", 5) == 0) ? cmd + 5 : 0;
        cmd_less(arg);
    }

    else if (strcmp(cmd, "head") == 0 || strncmp(cmd, "head ", 5) == 0)
    {
        if (strcmp(cmd, "head") == 0)
            vga_print("usage: head <filename>\n  Displays top 10 lines of a file.\n");
        else {
            char buf[2048];
            if (fs_cat(cmd + 5, buf, sizeof(buf)) < 0) vga_print("head: no such file\n");
            else {
                int lines = 0;
                for (int i = 0; buf[i]; i++) {
                    char c[2] = { buf[i], '\0' };
                    vga_print(c);
                    if (buf[i] == '\n') { lines++; if (lines >= 10) break; }
                }
                vga_print("\n");
            }
        }
    }

    else if (strcmp(cmd, "tail") == 0 || strncmp(cmd, "tail ", 5) == 0)
    {
        if (strcmp(cmd, "tail") == 0)
            vga_print("usage: tail <filename>\n  Displays last 10 lines of a file.\n");
        else {
            char buf[2048];
            if (fs_cat(cmd + 5, buf, sizeof(buf)) < 0) vga_print("tail: no such file\n");
            else {
                int total_lines = 0;
                for (int i = 0; buf[i]; i++) if (buf[i] == '\n') total_lines++;
                int skip_lines = (total_lines > 10) ? (total_lines - 10) : 0;
                int cur_line = 0;
                for (int i = 0; buf[i]; i++) {
                    if (cur_line >= skip_lines) {
                        char c[2] = { buf[i], '\0' };
                        vga_print(c);
                    }
                    if (buf[i] == '\n') cur_line++;
                }
                vga_print("\n");
            }
        }
    }

    else if (strcmp(cmd, "find") == 0 || strncmp(cmd, "find ", 5) == 0)
    {
        if (strcmp(cmd, "find") == 0)
            vga_print("usage: find <query>\n  Searches files/dirs matching query.\n");
        else {
            const char *query = cmd + 5;
            fs_node_t *dir = fs_cwd();
            int count = 0;
            if (dir) {
                for (int i = 0; i < dir->child_count; i++) {
                    if (dir->children[i]) {
                        /* Substring search */
                        const char *n = dir->children[i]->name;
                        const char *q = query;
                        int hit = 0;
                        for (int j = 0; n[j]; j++) {
                            int k = 0;
                            while (n[j+k] && q[k] && n[j+k] == q[k]) k++;
                            if (!q[k]) { hit = 1; break; }
                        }
                        if (hit) {
                            vga_print(dir->children[i]->name);
                            vga_print(dir->children[i]->type == FS_DIR ? "/" : "");
                            vga_print("\n");
                            count++;
                        }
                    }
                }
            }
            if (!count) vga_print("find: no matches found.\n");
        }
    }
    else if (strcmp(cmd, "stat") == 0 || strncmp(cmd, "stat ", 5) == 0)
    {
        const char *arg = (strncmp(cmd, "stat ", 5) == 0) ? cmd + 5 : 0;
        cmd_stat(arg);
    }
    else if (strcmp(cmd, "hexdump") == 0 || strncmp(cmd, "hexdump ", 8) == 0 ||
             strcmp(cmd, "xxd") == 0 || strncmp(cmd, "xxd ", 4) == 0)
    {
        const char *arg = (strncmp(cmd, "hexdump ", 8) == 0) ? cmd + 8 :
                          (strncmp(cmd, "xxd ", 4) == 0) ? cmd + 4 : 0;
        cmd_hexdump(arg);
    }

    else if (strcmp(cmd, "snake") == 0)
    {
        gui_enter();
    }

    else if (strcmp(cmd, "rm") == 0 || strncmp(cmd, "rm ", 3) == 0)
    {
        if (strcmp(cmd, "rm") == 0)
            vga_print("usage: rm <path>\n  Removes a file or empty directory.\n");
        else
        {
            int r = fs_rm(cmd + 3);
            if (r == -1) vga_print("rm: no such file or directory\n");
            else if (r == -2) vga_print("rm: directory not empty\n");
        }
    }

    else if (strcmp(cmd, "write") == 0 || strncmp(cmd, "write ", 6) == 0)
    {
        if (strcmp(cmd, "write") == 0)
        {
            vga_print("usage: write <file> <text>\n  Writes text to a file.\n");
        }
        else
        {
            const char *rest = cmd + 6;
            while (*rest == ' ') rest++;
            const char *space = rest;
            while (*space && *space != ' ') space++;
            if (!*space)
            {
                vga_print("usage: write <file> <text>\n");
            }
            else
            {
                char fname[FS_MAX_NAME];
                int flen = (int)(space - rest);
                if (flen >= FS_MAX_NAME) flen = FS_MAX_NAME - 1;
                int fi;
                for (fi = 0; fi < flen; fi++) fname[fi] = rest[fi];
                fname[fi] = '\0';

                const char *text = space;
                while (*text == ' ') text++;
                size_t tlen = 0;
                while (text[tlen]) tlen++;
                if (fs_write(fname, text, tlen) < 0)
                    vga_print("write: failed\n");
            }
        }
    }

    else if (strcmp(cmd, "cp") == 0 || strncmp(cmd, "cp ", 3) == 0)
    {
        if (strcmp(cmd, "cp") == 0)
        {
            vga_print("usage: cp <src> <dst>\n  Copies a file to a new destination.\n");
        }
        else
        {
            const char *rest = cmd + 3;
            while (*rest == ' ') rest++;
            const char *space = rest;
            while (*space && *space != ' ') space++;
            if (!*space) { vga_print("usage: cp <src> <dst>\n"); }
            else
            {
                char src[FS_MAX_PATH], dst[FS_MAX_PATH];
                int slen = (int)(space - rest);
                if (slen >= FS_MAX_PATH) slen = FS_MAX_PATH - 1;
                int si;
                for (si = 0; si < slen; si++) src[si] = rest[si];
                src[si] = '\0';
                const char *d = space;
                while (*d == ' ') d++;
                int di = 0;
                while (*d && di < FS_MAX_PATH - 1) dst[di++] = *d++;
                dst[di] = '\0';
                if (fs_cp(src, dst) < 0) vga_print("cp: failed\n");
            }
        }
    }

    else if (strcmp(cmd, "mv") == 0 || strncmp(cmd, "mv ", 3) == 0)
    {
        if (strcmp(cmd, "mv") == 0)
        {
            vga_print("usage: mv <src> <dst>\n  Moves or renames a file or directory.\n");
        }
        else
        {
            const char *rest = cmd + 3;
            while (*rest == ' ') rest++;
            const char *space = rest;
            while (*space && *space != ' ') space++;
            if (!*space) { vga_print("usage: mv <src> <dst>\n"); }
            else
            {
                char src[FS_MAX_PATH], dst[FS_MAX_PATH];
                int slen = (int)(space - rest);
                if (slen >= FS_MAX_PATH) slen = FS_MAX_PATH - 1;
                int si;
                for (si = 0; si < slen; si++) src[si] = rest[si];
                src[si] = '\0';
                const char *d = space;
                while (*d == ' ') d++;
                int di = 0;
                while (*d && di < FS_MAX_PATH - 1) dst[di++] = *d++;
                dst[di] = '\0';
                if (fs_mv(src, dst) < 0) vga_print("mv: failed\n");
            }
        }
    }
    else if (strcmp(cmd, "calc") == 0 || strncmp(cmd, "calc ", 5) == 0)
    {
        if (strcmp(cmd, "calc") == 0)
        {
            vga_print("usage: calc <num1> <+|-|*|/> <num2>\n  Calculates mathematical expressions.\n");
        }
        else
        {
            const char *args = cmd + 5;
            while (*args == ' ') args++;
            int a = 0, b = 0;
            int sign_a = 1, sign_b = 1;
            char op = 0;
            int i = 0;
            if (args[i] == '-') { sign_a = -1; i++; }
            else if (args[i] == '+') { i++; }
            while (args[i] >= '0' && args[i] <= '9') { a = a * 10 + (args[i] - '0'); i++; }
            a *= sign_a;
            while (args[i] == ' ') i++;
            if (args[i] == '+' || args[i] == '-' || args[i] == '*' || args[i] == '/') op = args[i++];
            while (args[i] == ' ') i++;
            if (args[i] == '-') { sign_b = -1; i++; }
            else if (args[i] == '+') { i++; }
            while (args[i] >= '0' && args[i] <= '9') { b = b * 10 + (args[i] - '0'); i++; }
            b *= sign_b;

            if (!op)
            {
                vga_print("usage: calc <num1> <+|-|*|/> <num2>\n");
            }
            else
            {
                int res = 0;
                if (op == '+') res = a + b;
                else if (op == '-') res = a - b;
                else if (op == '*') res = a * b;
                else if (op == '/') {
                    if (b == 0) {
                        vga_print("calc: div by zero\n");
                        return;
                    }
                    res = a / b;
                }
                char buf[32];
                itoa(res, buf, 10);
                vga_print("= ");
                vga_print(buf);
                vga_print("\n");
            }
        }
    }
    else if (strcmp(cmd, "$VERSION") == 0)
    {
        vga_print("ArchaOS v0.5 \"Monolith\"\n");
    }
    else if (strcmp(cmd, "ports") == 0)
    {
        vga_print("Keyboard : 0x60 / 0x64  (IRQ1)\n");
        vga_print("RTC      : 0x70 / 0x71  (IRQ8)\n");
        vga_print("Speaker  : 0x42 / 0x43 / 0x61\n");
        vga_print("VGA      : 0x3D4 / 0x3D5\n");
        vga_print("Serial1  : 0x3F8 (COM1)  115200 8N1\n");
        vga_print("Serial2  : 0x2F8 (COM2)  115200 8N1\n");
    }
    else if (strcmp(cmd, "serial") == 0 || strncmp(cmd, "serial ", 7) == 0)
    {
        /* Parse optional COM port: serial [com1|com2|com3|com4] <subcommand> */
        uint16_t base = COM1_BASE;
        const char *subcmd = cmd;

        if (strncmp(cmd, "serial ", 7) == 0) {
            subcmd = cmd + 7;
            if (strncmp(subcmd, "com1 ", 5) == 0) { base = COM1_BASE; subcmd += 5; }
            else if (strncmp(subcmd, "com2 ", 5) == 0) { base = COM2_BASE; subcmd += 5; }
            else if (strncmp(subcmd, "com3 ", 5) == 0) { base = COM3_BASE; subcmd += 5; }
            else if (strncmp(subcmd, "com4 ", 5) == 0) { base = COM4_BASE; subcmd += 5; }
        }

        char port_name[8] = "COM1";
        if (base == COM2_BASE) port_name[3] = '2';
        else if (base == COM3_BASE) port_name[3] = '3';
        else if (base == COM4_BASE) port_name[3] = '4';

        if (subcmd[0] == '\0' || strcmp(subcmd, "help") == 0)
        {
            vga_print("Serial port test. Usage:\n");
            vga_print("  serial [com1|com2|com3|com4] write <text>\n");
            vga_print("  serial [com1|com2|com3|com4] read\n");
            vga_print("  serial [com1|com2|com3|com4] echo\n");
            vga_print("  serial [com1|com2|com3|com4] status\n");
            vga_print("  serial init [com1|com2|com3|com4] [baud]\n");
            vga_print("Default: COM1 @ 115200\n");
        }
        else if (strncmp(subcmd, "init", 4) == 0)
        {
            uint32_t baud = 115200;
            const char *ptr = subcmd + 4;
            while (*ptr == ' ') ptr++;

            if (strncmp(ptr, "com1", 4) == 0) { base = COM1_BASE; port_name[3] = '1'; ptr += 4; }
            else if (strncmp(ptr, "com2", 4) == 0) { base = COM2_BASE; port_name[3] = '2'; ptr += 4; }
            else if (strncmp(ptr, "com3", 4) == 0) { base = COM3_BASE; port_name[3] = '3'; ptr += 4; }
            else if (strncmp(ptr, "com4", 4) == 0) { base = COM4_BASE; port_name[3] = '4'; ptr += 4; }

            while (*ptr == ' ') ptr++;
            if (*ptr >= '0' && *ptr <= '9') {
                baud = 0;
                while (*ptr >= '0' && *ptr <= '9') {
                    baud = baud * 10 + (*ptr - '0');
                    ptr++;
                }
            }
            if (baud == 0) baud = 115200;

            serial_init(base, baud);
            vga_print("Initialized "); vga_print(port_name); vga_print(" @ ");
            char buf[16]; itoa(baud, buf, 10); vga_print(buf); vga_print(" baud (8N1)\n");
        }
        else if (strncmp(subcmd, "write ", 6) == 0)
        {
            serial_puts(base, subcmd + 6);
            serial_puts(base, "\n");
            vga_print("Sent to "); vga_print(port_name); vga_print("\n");
        }
        else if (strcmp(subcmd, "read") == 0 || strcmp(subcmd, "listen") == 0)
        {
            vga_print("Listening on "); vga_print(port_name); vga_print(". Press ESC on keyboard to exit.\n");
            while (1)
            {
                if (irq_kbd_fired) {
                    uint8_t sc = last_scancode;
                    irq_kbd_fired = 0;
                    if (sc == 0x01) break; // ESC
                }

                char c = serial_try_getc(base);
                if (c) {
                    if (c == '\r') vga_print_char('\n');
                    else vga_print_char(c);
                }
            }
            vga_print("\nExited "); vga_print(port_name); vga_print(" listener.\n");
        }
        else if (strcmp(subcmd, "echo") == 0 || strcmp(subcmd, "terminal") == 0)
        {
            vga_print("Serial Terminal mode on "); vga_print(port_name); vga_print(". Press ESC to exit.\n");
            vga_print("Incoming data is printed on screen & echoed back to "); vga_print(port_name); vga_print(".\n");
            while (1)
            {
                /* Check keyboard for ESC */
                if (irq_kbd_fired)
                {
                    uint8_t sc = last_scancode;
                    irq_kbd_fired = 0;
                    if (sc == 0x01) break; // ESC
                }

                /* Check serial for incoming data */
                char c = serial_try_getc(base);
                if (c)
                {
                    /* Echo back to serial */
                    serial_putc(base, c);
                    if (c == '\r') serial_putc(base, '\n');

                    /* Print to VGA screen */
                    if (c == '\r') vga_print_char('\n');
                    else vga_print_char(c);
                }
            }
            vga_print("\nExited "); vga_print(port_name); vga_print(" terminal mode.\n");
        }
        else if (strcmp(subcmd, "status") == 0)
        {
            uint8_t lsr = inb(base + UART_LSR);
            uint8_t msr = inb(base + UART_MSR);
            uint8_t ier = inb(base + UART_IER);
            uint8_t iir = inb(base + UART_IIR);
            vga_print(port_name); vga_print(" Registers:\n");
            vga_print("  LSR: 0x"); char buf[3]; itoa(lsr, buf, 16); vga_print(buf); vga_print("\n");
            vga_print("  MSR: 0x"); itoa(msr, buf, 16); vga_print(buf); vga_print("\n");
            vga_print("  IER: 0x"); itoa(ier, buf, 16); vga_print(buf); vga_print("\n");
            vga_print("  IIR: 0x"); itoa(iir, buf, 16); vga_print(buf); vga_print("\n");
            vga_print("  Data Ready: "); vga_print(lsr & LSR_DR ? "YES" : "NO"); vga_print("\n");
            vga_print("  TX Empty:   "); vga_print(lsr & LSR_THRE ? "YES" : "NO"); vga_print("\n");
        }
        else
        {
            vga_print("Unknown serial subcommand. Type 'serial' for help.\n");
        }
    }
    else if (strcmp(cmd, "pci") == 0 || strncmp(cmd, "pci ", 4) == 0)
    {
        if (strcmp(cmd, "pci") == 0 || strcmp(cmd, "pci list") == 0)
        {
            pci_list_all();
        }
        else if (strcmp(cmd, "pci scan") == 0)
        {
            pci_scan();
            vga_print("PCI scan complete.\n");
        }
        else if (strncmp(cmd, "pci find ", 9) == 0)
        {
            /* pci find class subclass [progif] */
            const char *rest = cmd + 9;
            int class_code = 0, subclass = 0, prog_if = 0;
            while (*rest == ' ') rest++;
            if (!*rest) {
                vga_print("Usage: pci find <class> <subclass> [progif]\n");
            } else {
                while ((*rest >= '0' && *rest <= '9') || (*rest >= 'a' && *rest <= 'f') || (*rest >= 'A' && *rest <= 'F')) {
                    int d = (*rest >= '0' && *rest <= '9') ? (*rest - '0') :
                            ((*rest >= 'a' && *rest <= 'f') ? (*rest - 'a' + 10) : (*rest - 'A' + 10));
                    class_code = class_code * 16 + d;
                    rest++;
                }
                while (*rest == ' ') rest++;
                while ((*rest >= '0' && *rest <= '9') || (*rest >= 'a' && *rest <= 'f') || (*rest >= 'A' && *rest <= 'F')) {
                    int d = (*rest >= '0' && *rest <= '9') ? (*rest - '0') :
                            ((*rest >= 'a' && *rest <= 'f') ? (*rest - 'a' + 10) : (*rest - 'A' + 10));
                    subclass = subclass * 16 + d;
                    rest++;
                }
                while (*rest == ' ') rest++;
                if (*rest) {
                    while ((*rest >= '0' && *rest <= '9') || (*rest >= 'a' && *rest <= 'f') || (*rest >= 'A' && *rest <= 'F')) {
                        int d = (*rest >= '0' && *rest <= '9') ? (*rest - '0') :
                                ((*rest >= 'a' && *rest <= 'f') ? (*rest - 'a' + 10) : (*rest - 'A' + 10));
                        prog_if = prog_if * 16 + d;
                        rest++;
                    }
                } else {
                    prog_if = 0;
                }
                pci_device_t *dev = pci_find_device((uint8_t)class_code, (uint8_t)subclass, (uint8_t)prog_if);
                if (dev) pci_print_device(dev);
                else vga_print("No matching device found.\n");
            }
        }
        else
        {
            vga_print("Usage: pci [list|scan|find <class> <subclass> [progif]]\n");
        }
    }
    else if (strcmp(cmd, "ata") == 0 || strncmp(cmd, "ata ", 4) == 0)
    {
        if (strcmp(cmd, "ata") == 0 || strcmp(cmd, "ata list") == 0)
        {
            ata_list_all();
        }
        else if (strcmp(cmd, "ata scan") == 0)
        {
            ata_init();
            vga_print("ATA scan complete.\n");
        }
        else if (strncmp(cmd, "ata read ", 9) == 0)
        {
            /* ata read <device_index> <lba> <count> */
            const char *rest = cmd + 9;
            int dev_idx = 0, lba = 0, count = 0;
            while (*rest == ' ') rest++;
            while (*rest >= '0' && *rest <= '9') { dev_idx = dev_idx * 10 + (*rest - '0'); rest++; }
            while (*rest == ' ') rest++;
            while (*rest >= '0' && *rest <= '9') { lba = lba * 10 + (*rest - '0'); rest++; }
            while (*rest == ' ') rest++;
            while (*rest >= '0' && *rest <= '9') { count = count * 10 + (*rest - '0'); rest++; }
            if (count <= 0) count = 1;
            if (count > 256) count = 256;
            ata_device_t *dev = ata_get_device(dev_idx);
            if (!dev) { vga_print("Invalid device index\n"); }
            else {
                void *buf = kmalloc(count * 512);
                if (!buf) { vga_print("Out of memory\n"); }
                else {
                    int ret = ata_read_sectors(dev, lba, count, buf);
                    if (ret < 0) vga_print("Read failed\n");
                    else {
                        vga_print("Read ");
                        char buf2[16];
                        itoa(count * 512, buf2, 10); vga_print(buf2);
                        vga_print(" bytes from LBA ");
                        itoa(lba, buf2, 10); vga_print(buf2);
                        vga_print("\n");
                        // Hex dump first sector
                        uint8_t *data = (uint8_t *)buf;
                        for (int i = 0; i < 512 && i < count * 512; i++) {
                            if (i % 16 == 0) { vga_print("\n"); char h[3]; itoa(i, h, 16); vga_print(h); vga_print(": "); }
                            char h[3]; itoa(data[i], h, 16); if (h[1] == '\0') vga_print("0"); vga_print(h); vga_print(" ");
                        }
                        vga_print("\n");
                    }
                    kfree(buf);
                }
            }
        }
        else if (strncmp(cmd, "ata write ", 9) == 0)
        {
            /* ata write <device_index> <lba> <hex_bytes...> - simple test write */
            vga_print("Write not implemented in shell yet (use code)\n");
        }
        else
        {
            vga_print("Usage: ata [list|scan|read <dev> <lba> <count>]\n");
        }
    }
    else if (strcmp(cmd, "beep") == 0 || strncmp(cmd, "beep ", 5) == 0)
    {
        if (strcmp(cmd, "beep") == 0) {
            beep();
        } else {
            const char *p = cmd + 5;
            while (*p == ' ') p++;
            int freq = atoi(p);
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            int ms = atoi(p);
            if (freq <= 0) freq = 750;
            if (ms <= 0) ms = 100;
            play_sound((uint32_t)freq);
            pit_sleep((uint32_t)ms);
            no_sound();
        }
    }
    else if (strcmp(cmd, "credits") == 0)
    {
        vga_print("Made by Akshaj\n");
    }

    else if (strcmp(cmd, "memtest") == 0)
    {
        char buf[16];
        vga_print("Allocating 4 blocks...\n");

        void *a = kmalloc(1024);
        void *b = kmalloc(512);
        void *c = kmalloc(2048);
        void *d = kmalloc(256);

        mm_stats_t s = mm_stats();
        vga_print("Used: ");
        itoa((int)(s.used / 1024), buf, 10); vga_print(buf);
        vga_print(" KB in ");
        itoa((int)s.blocks_used, buf, 10); vga_print(buf);
        vga_print(" blocks\n");

        vga_print("Freeing 2 blocks...\n");
        kfree(b);
        kfree(c);

        s = mm_stats();
        vga_print("Used: ");
        itoa((int)(s.used / 1024), buf, 10); vga_print(buf);
        vga_print(" KB in ");
        itoa((int)s.blocks_used, buf, 10); vga_print(buf);
        vga_print(" blocks\n");

        vga_print("Freeing rest...\n");
        kfree(a);
        kfree(d);

        s = mm_stats();
        vga_print("Used: ");
        itoa((int)(s.used), buf, 10); vga_print(buf);
        vga_print(" bytes — heap fully coalesced: ");
        vga_print(s.blocks_free == 1 ? "YES" : "NO");
        vga_print("\n");

        (void)buf;
    }

    else if (strcmp(cmd, "meminfo") == 0)
    {
        mm_stats_t s = mm_stats();
        char buf[16];

        /* Show total detected RAM */
        vga_print("Total RAM  : ");
        itoa((int)(mm_total_ram / 1024), buf, 10);
        vga_print(buf); vga_print(" KB\n");

        vga_print("Heap total : ");
        itoa((int)(s.total / 1024), buf, 10);
        vga_print(buf); vga_print(" KB\n");

        vga_print("Used       : ");
        itoa((int)(s.used / 1024), buf, 10);
        vga_print(buf); vga_print(" KB (");
        itoa((int)s.blocks_used, buf, 10);
        vga_print(buf); vga_print(" blocks)\n");

        vga_print("Free       : ");
        itoa((int)(s.free / 1024), buf, 10);
        vga_print(buf); vga_print(" KB (");
        itoa((int)s.blocks_free, buf, 10);
        vga_print(buf); vga_print(" blocks)\n");
    }
    else if (strcmp(cmd, "edit") == 0 || strncmp(cmd, "edit ", 5) == 0 ||
             strcmp(cmd, "nano") == 0 || strncmp(cmd, "nano ", 5) == 0)
    {
        const char *arg = 0;
        if (strncmp(cmd, "nano ", 5) == 0) arg = cmd + 5;
        else if (strncmp(cmd, "edit ", 5) == 0) arg = cmd + 5;

        if (!arg || !*arg)
            vga_print("usage: nano <filename> (or edit <filename>)\n  Opens the GNU nano interactive text editor.\n");
        else
            editor_open(arg);
    }
    else if (strcmp(cmd, "ai") == 0 || strncmp(cmd, "ai ", 3) == 0 ||
             strncmp(cmd, "deepseek ", 9) == 0 || strncmp(cmd, "ask ", 4) == 0)
    {
        const char *prompt = 0;
        if (strncmp(cmd, "deepseek ", 9) == 0) prompt = cmd + 9;
        else if (strncmp(cmd, "ask ", 4) == 0) prompt = cmd + 4;
        else if (strncmp(cmd, "ai ", 3) == 0)  prompt = cmd + 3;

        if (prompt) {
            if (strcmp(prompt, "clear") == 0 || strcmp(prompt, "reset") == 0) {
                nim_history_clear();
                vga_print("[AI] Conversation memory cleared.\n");
            } else if (strcmp(prompt, "history") == 0) {
                int cnt = nim_history_count();
                if (cnt == 0) {
                    vga_print("[AI] No conversation history recorded.\n");
                } else {
                    vga_print("=== AI Conversation History ===\n");
                    for (int h = 0; h < cnt; h++) {
                        const nim_message_t *m = nim_history_get(h);
                        vga_print("["); vga_print(m->role); vga_print("] ");
                        vga_print(m->content);
                        vga_print("\n");
                    }
                }
            } else {
                int online_ok = 0;
                if (net_if.up) {
                    vga_print("[AI] Thinking...\n");
                    static char ai_resp[4096];
                    if (nim_query(prompt, ai_resp, sizeof(ai_resp)) && ai_resp[0]) {
                        vga_print(ai_resp);
                        vga_print("\n");
                        online_ok = 1;
                    }
                }
                if (!online_ok) {
                    char resp[128];
                    ai_get_response(prompt, resp, sizeof(resp));
                    vga_print("AI > ");
                    vga_print(resp);
                    vga_print("\n");
                    nim_history_add("user", prompt);
                    nim_history_add("assistant", resp);
                }
            }
        } else if (gui_active) {
            vga_print("AI > ArchaOS Assistant ready. Type 'ai <question>' (e.g. 'ai hello').\n");
        } else {
            ai_chat();
        }
    }
    else if (strcmp(cmd, "audio") == 0 || strncmp(cmd, "audio ", 6) == 0 ||
             strcmp(cmd, "play") == 0 || strncmp(cmd, "play ", 5) == 0)
    {
        const char *arg = 0;
        if (strncmp(cmd, "audio ", 6) == 0) arg = cmd + 6;
        else if (strncmp(cmd, "play ", 5) == 0) arg = cmd + 5;

        if (!arg || strcmp(arg, "status") == 0) {
            vga_print("=== Universal Audio Player Status ===\n");
            vga_print("Source: "); vga_print(audio_get_source()); vga_print("\n");
            vga_print("Title:  "); vga_print(audio_get_title()); vga_print("\n");
            vga_print("Format: "); vga_print(audio_get_format()); vga_print("\n");
            char buf[32]; itoa((int)audio_get_volume(), buf, 10);
            vga_print("Volume: "); vga_print(buf); vga_print("%\n");
            itoa((int)(audio_get_elapsed_ms() / 1000), buf, 10);
            vga_print("Time:   "); vga_print(buf); vga_print("s / ");
            itoa((int)(audio_get_duration_ms() / 1000), buf, 10);
            vga_print(buf); vga_print("s\n");
        } else if (strcmp(arg, "play") == 0) {
            audio_play();
            vga_print("Playing: "); vga_print(audio_get_title()); vga_print("\n");
        } else if (strcmp(arg, "stop") == 0) {
            audio_stop();
            vga_print("Audio stopped.\n");
        } else if (strcmp(arg, "pause") == 0) {
            audio_pause();
            vga_print("Audio paused.\n");
        } else if (strcmp(arg, "list") == 0) {
            vga_print("=== ArchaOS Audio Subsystem ===\n");
            vga_print("Supported: Ogg Vorbis (.ogg), MP3 (.mp3), RIFF WAV (.wav), Sun AU (.au)\n");
            vga_print("Tip: Type 'audio <url_or_path>' to play any media stream or local file!\n");
        } else {
            /* Open arbitrary URL or local file path */
            vga_print("Opening audio: "); vga_print(arg); vga_print("...\n");
            if (audio_open(arg)) {
                vga_print("Now playing: "); vga_print(audio_get_title()); vga_print("\n");
                vga_print("Format:      "); vga_print(audio_get_format()); vga_print("\n");
            } else {
                vga_print("Failed to load audio from: "); vga_print(arg); vga_print("\n");
            }
        }
    }
    else if (strcmp(cmd, "video") == 0 || strncmp(cmd, "video ", 6) == 0)
    {
        const char *arg = (strncmp(cmd, "video ", 6) == 0) ? cmd + 6 : 0;
        if (!arg || strcmp(arg, "status") == 0) {
            vga_print("=== Universal Video Player Status ===\n");
            vga_print("Source: "); vga_print(video_get_source()); vga_print("\n");
            vga_print("Title:  "); vga_print(video_get_title()); vga_print("\n");
            vga_print("Format: "); vga_print(video_get_format()); vga_print("\n");
            char buf[32]; itoa((int)video_get_fps(), buf, 10);
            vga_print("FPS:    "); vga_print(buf); vga_print("\n");
            vga_print("Tip: Media plays inside the Web Browser with a single click.\n");
        } else if (strcmp(arg, "list") == 0) {
            vga_print("=== ArchaOS Video Subsystem ===\n");
            vga_print("Supported: Animated GIF (.gif), MPEG-1 Video (.mpg), AVID (.vid)\n");
            vga_print("Tip: Type 'video <url_or_path>' or open the Web Browser to stream video!\n");
        } else if (strcmp(arg, "stop") == 0) {
            video_stop();
            vga_print("Video stopped.\n");
        } else {
            vga_print("Opening video: "); vga_print(arg); vga_print("...\n");
            if (video_open(arg)) {
                vga_print("Loaded video: "); vga_print(video_get_title()); vga_print("\n");
                vga_print("Launching Web Browser In-Browser Media Player...\n");
                gui_enter();
            } else {
                vga_print("Failed to load video from: "); vga_print(arg); vga_print("\n");
            }
        }
    }
    else if (strcmp(cmd, "gui") == 0)
    {
        gui_enter();
    }

    /* ============================================================
     * NETWORK COMMANDS
     * ============================================================ */
    else if (strcmp(cmd, "ifconfig") == 0 || strcmp(cmd, "ipconfig") == 0)
    {
        if (!e1000_is_active()) {
            vga_print("No network adapter detected.\n");
        } else {
            char mac_str[18], ip_str[16], gw_str[16], dns_str[16];
            uint8_t mac[6];
            e1000_get_mac(mac);
            mac_to_str(mac, mac_str);
            ip_to_str(net_if.ip,      ip_str);
            ip_to_str(net_if.gateway, gw_str);
            ip_to_str(net_if.dns,     dns_str);
            vga_print("eth0  MAC : "); vga_print(mac_str);  vga_print("\n");
            vga_print("      IP  : "); vga_print(ip_str);   vga_print("\n");
            vga_print("      GW  : "); vga_print(gw_str);   vga_print("\n");
            vga_print("      DNS : "); vga_print(dns_str);  vga_print("\n");
            vga_print(net_if.up ? "      Status: UP\n" : "      Status: DOWN\n");
        }
    }
    else if (strncmp(cmd, "ping ", 5) == 0 || strcmp(cmd, "ping") == 0)
    {
        if (!net_if.up) { vga_print("Network not configured. Run DHCP first.\n"); }
        else {
            const char *host = (strcmp(cmd, "ping") == 0) ? "10.0.2.2" : cmd + 5;
            uint32_t dst_ip = 0;
            if (!dns_resolve(host, &dst_ip)) { vga_print("ping: could not resolve host\n"); }
            else {
                char ip_str[16]; ip_to_str(dst_ip, ip_str);
                vga_print("PING "); vga_print(host); vga_print(" ("); vga_print(ip_str); vga_print(")\n");
                for (int i = 0; i < 4; i++) {
                    int rtt = icmp_ping(dst_ip, 2000);
                    if (rtt < 0) vga_print("  Request timeout\n");
                    else {
                        char rtt_str[12]; itoa(rtt, rtt_str, 10);
                        vga_print("  reply time="); vga_print(rtt_str); vga_print(" ms\n");
                    }
                }
            }
        }
    }
    else if (strncmp(cmd, "nslookup ", 9) == 0)
    {
        if (!net_if.up) { vga_print("Network not configured.\n"); }
        else {
            const char *host = cmd + 9;
            uint32_t ip = 0;
            if (dns_resolve(host, &ip)) {
                char ip_str[16]; ip_to_str(ip, ip_str);
                vga_print(host); vga_print(" -> "); vga_print(ip_str); vga_print("\n");
            } else {
                vga_print("nslookup: resolution failed\n");
            }
        }
    }
    else if (strncmp(cmd, "curl ", 5) == 0)
    {
        if (!net_if.up) { vga_print("Network not configured.\n"); }
        else {
            const char *url = cmd + 5;
            while (*url == ' ') url++;
            int is_https = 0;
            const char *hoststart = url;
            uint16_t port = 80;

            if (strncmp(url, "https://", 8) == 0) {
                is_https = 1;
                port = 443;
                hoststart = url + 8;
            } else if (strncmp(url, "http://", 7) == 0) {
                is_https = 0;
                port = 80;
                hoststart = url + 7;
            }

            const char *slash = hoststart;
            while (*slash && *slash != '/' && *slash != ':') slash++;
            char hostname[64]; int hn = (int)(slash - hoststart);
            if (hn > 63) hn = 63;
            for (int i = 0; i < hn; i++) hostname[i] = hoststart[i];
            hostname[hn] = '\0';

            if (*slash == ':') {
                port = 0;
                slash++;
                while (*slash >= '0' && *slash <= '9') {
                    port = port * 10 + (*slash - '0');
                    slash++;
                }
            }
            const char *path = *slash == '/' ? slash : "/";
            uint32_t hip = 0;
            if (!dns_resolve(hostname, &hip)) {
                vga_print("curl: could not resolve host '");
                vga_print(hostname);
                vga_print("'\n");
            } else {
                if (is_https) {
                    vga_print_color("[TLS 1.2] Connecting HTTPS to ", 0x0B);
                    vga_print(hostname);
                    vga_print(":443...\n");
                }
                static char curl_buf[4096];
                int sc = http_get(hostname, hip, port, path, curl_buf, sizeof(curl_buf));
                if (sc == 0) { vga_print("curl: connection failed\n"); }
                else { vga_print(curl_buf); vga_print("\n"); }
            }
        }
    }
    else if (strcmp(cmd, "wget") == 0 || strncmp(cmd, "wget ", 5) == 0)
    {
        const char *arg = (strncmp(cmd, "wget ", 5) == 0) ? cmd + 5 : 0;
        cmd_wget(arg);
    }
    else if (strcmp(cmd, "netstat") == 0)
    {
        if (!e1000_is_active()) { vga_print("No network adapter.\n"); }
        else {
            vga_print("E1000 Intel PRO/1000 (82540EM)\n");
            vga_print(net_if.up ? "Link: UP\n" : "Link: DOWN\n");
            char ip_str[16]; ip_to_str(net_if.ip, ip_str);
            vga_print("IP: "); vga_print(ip_str); vga_print("\n");
        }
    }
    else if (strncmp(cmd, "browse", 6) == 0 && (cmd[6] == ' ' || cmd[6] == '\0'))
    {
        cmd_browse(cmd[6] == ' ' ? cmd + 7 : 0);
    }
    else if (strncmp(cmd, "js", 2) == 0 && (cmd[2] == ' ' || cmd[2] == '\0'))
    {
        if (cmd[2] == '\0') {
            vga_print("usage: js <code> | js <filename.js>\n  Executes JavaScript code in the Bare-Metal ECMAScript VM.\n");
        } else {
            const char *arg = cmd + 3;
            while (*arg == ' ') arg++;
            fs_node_t *f = fs_resolve(arg);
            if (f && f->type == FS_FILE && f->data) {
                vga_print("Executing JS file: "); vga_print(arg); vga_print("\n");
                js_engine_eval((const char*)f->data);
            } else {
                js_engine_eval(arg);
            }
        }
    }

    else
    {
        vga_print("Unknown command. Type 'help'.\n");
    }
}

/* ============================================================
 * KERNEL MAIN
 * ============================================================ */

void kernel_main(uint32_t mb_magic, void *mb_info)
{
    /* Step 0: Clear screen */
    vga_clear();

    /* Step 1: Detect RAM from multiboot map, init memory manager */
    uint32_t detected_ram = 0;

    if (mb_magic == MULTIBOOT_MAGIC && mb_info)
    {
        multiboot_info_t *mbi = (multiboot_info_t *)mb_info;
        if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
            uint32_t offset = 0;
            uint64_t total_usable = 0;
            while (offset < mbi->mmap_length && offset < 8192) {
                mmap_entry_t *entry = (mmap_entry_t *)(mbi->mmap_addr + offset);
                if (entry->type == MMAP_TYPE_USABLE)
                    total_usable += entry->length;
                if (entry->size == 0) break;
                offset += entry->size + sizeof(entry->size);
            }
            if (total_usable > 0xFFFFFFFFULL) detected_ram = 0xFFFFFFFF;
            else detected_ram = (uint32_t)total_usable;
        } else if (mbi->flags & MULTIBOOT_FLAG_MEM) {
            detected_ram = (mbi->mem_upper + 1024) * 1024;
        }
    }

    if (detected_ram == 0)
    {
        /* Fallback: assume 32MB */
        detected_ram = 32 * 1024 * 1024;
    }

    mm_init(detected_ram);
    fs_init();
    ai_init();

    /* Step 1.5: Initialize serial ports for debugging (COM1 @ 115200, COM2 @ 115200) */
    serial_init(COM1_BASE, 115200);
    serial_puts(COM1_BASE, "\n=== ArchaOS v0.5 \"Monolith\" Serial Debug ===\n");
    serial_printf(COM1_BASE, "Detected RAM: %u KB\n", detected_ram / 1024);
    serial_puts(COM1_BASE, "VGA Text Mode: 80x25 at 0xB8000\n");

    serial_init(COM2_BASE, 115200);
    serial_puts(COM2_BASE, "\n=== ArchaOS v0.5 \"Monolith\" COM2 Ready ===\n");

    /* Step 2: IDT + PIC remap + IRQ enable */
    idt_init();
    serial_puts(COM1_BASE, "IDT initialized\n");

    /* Step 3: PIT timer (~1000 Hz) */
    pit_init();
    serial_puts(COM1_BASE, "PIT initialized\n");

    /* Step 3.5: PCI enumeration */
    pci_scan();
    serial_puts(COM1_BASE, "PCI enumerated\n");

    /* Step 3.6: ATA/IDE initialization */
    ata_init();
    serial_puts(COM1_BASE, "ATA initialized\n");

    /* Step 3.65: Universal Audio & Video Subsystems */
    audio_init();
    video_init();
    serial_puts(COM1_BASE, "Universal Audio & Video Subsystems initialized\n");

    /* Step 3.7: Network subsystem */
    if (e1000_init()) {
        net_init();
        serial_puts(COM1_BASE, "Requesting IP via DHCP...\n");
        dhcp_request();
        nim_init();
        char ip_str[16];
        ip_to_str(net_if.ip, ip_str);
        serial_printf(COM1_BASE, "Network ready: %s\n", ip_str);
    } else {
        serial_puts(COM1_BASE, "No network adapter found\n");
    }

    /* Step 4: Record boot time */
    boot_seconds = rtc_seconds_since_midnight();

    /* Step 5: Boot splash + chime */
    splash_show();
    boot_chime();

    /* Step 6: PS/2 mouse — removed (Wayland incompatibility) */

    /* Step 7: Shell */
    serial_puts(COM1_BASE, "Entering shell...\n");
    vga_prompt();

    /* Should never reach here */
    for (;;) asm volatile("hlt");
}
