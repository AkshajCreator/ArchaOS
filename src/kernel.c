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

static int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a -
    *(const unsigned char *)b;
}

static int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && (*a == *b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char *)a -
    *(const unsigned char *)b;
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

    if (value < 0 && base == 10)
    {
        *ptr++ = '-';
        value  = -value;
    }

    low = ptr;

    do
    {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"
        [value % base];
        value /= base;
    } while (value);

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

    while (rtc_updating());

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

    while (rtc_updating());

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
 * COMMANDS
 * ============================================================ */

void kernel_execute_command(const char *cmd)
{
    if (strcmp(cmd, "help") == 0)
    {
        vga_print("Available commands:\n");
        vga_print(" help, clear, reboot, halt, uptime,\n");
        vga_print(" echo <text>, date, neofetch, fortune,\n");
        vga_print(" theme <name>, sleep <ms>, calc <a+b>,\n");
        vga_print(" ls [path], mkdir, touch, cat, rm,\n");
        vga_print(" write <file> <text>, cp, mv, cd, pwd,\n");
        vga_print(" wc <file>, grep <pat> <file>,\n");
        vga_print(" edit <file>, ai, gui, run <script>,\n");
        vga_print(" alias [name=value], unalias, history,\n");
        vga_print(" ports, beep, credits, meminfo, memtest\n");
        vga_print("Redirection: cmd > file  cmd >> file\n");
        vga_print("Pipe:        cmd | grep <pat>  cmd | wc\n");
    }
    else if (strcmp(cmd, "clear") == 0)
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
            vga_print("usage: echo <text>\n  Prints the specified text.\n");
        else
        {
            vga_print(cmd + 5);
            vga_print("\n");
        }
    }
    else if (strcmp(cmd, "neofetch") == 0)
    {
        cmd_neofetch();
    }
    else if (strcmp(cmd, "fortune") == 0)
    {
        cmd_fortune();
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
            char buf[16]; int i = 0;
            const char *n = cmd + 6;
            while (n[i] && i < 15) { buf[i] = n[i]; i++; } buf[i] = '\0';
            int ms = 0;
            for (int j = 0; buf[j]; j++) ms = ms * 10 + (buf[j] - '0');
            if (ms > 0 && ms <= 10000) pit_sleep((uint32_t)ms);
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

                const char *text = space + 1;
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
            const char *space = rest;
            while (*space && *space != ' ') space++;
            if (!*space) { vga_print("usage: cp <src> <dst>\n"); }
            else
            {
                char src[FS_MAX_PATH], dst[FS_MAX_PATH];
                int slen = (int)(space - rest);
                int si;
                for (si = 0; si < slen && si < FS_MAX_PATH-1; si++) src[si] = rest[si];
                src[si] = '\0';
                int di = 0;
                const char *d = space + 1;
                while (*d && di < FS_MAX_PATH-1) dst[di++] = *d++;
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
            const char *space = rest;
            while (*space && *space != ' ') space++;
            if (!*space) { vga_print("usage: mv <src> <dst>\n"); }
            else
            {
                char src[FS_MAX_PATH], dst[FS_MAX_PATH];
                int slen = (int)(space - rest);
                int si;
                for (si = 0; si < slen && si < FS_MAX_PATH-1; si++) src[si] = rest[si];
                src[si] = '\0';
                int di = 0;
                const char *d = space + 1;
                while (*d && di < FS_MAX_PATH-1) dst[di++] = *d++;
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
            char op = 0;
            int i = 0;
            while (args[i] >= '0' && args[i] <= '9') { a = a * 10 + (args[i] - '0'); i++; }
            while (args[i] == ' ') i++;
            if (args[i] == '+' || args[i] == '-' || args[i] == '*' || args[i] == '/') op = args[i++];
            while (args[i] == ' ') i++;
            while (args[i] >= '0' && args[i] <= '9') { b = b * 10 + (args[i] - '0'); i++; }

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
                else if (op == '/') { if (b == 0) { vga_print("calc: div by zero\n"); res = 0; } else res = a / b; }
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
        vga_print("ArchaOS v4.0 \"Phosphor\"\n");
    }
    else if (strcmp(cmd, "ports") == 0)
    {
        vga_print("Keyboard : 0x60 / 0x64  (IRQ1)\n");
        vga_print("RTC      : 0x70 / 0x71  (IRQ8)\n");
        vga_print("Speaker  : 0x42 / 0x43 / 0x61\n");
        vga_print("VGA      : 0x3D4 / 0x3D5\n");
    }
    else if (strcmp(cmd, "beep") == 0)
    {
        beep();
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
    else if (strcmp(cmd, "edit") == 0 || strncmp(cmd, "edit ", 5) == 0)
    {
        if (strcmp(cmd, "edit") == 0)
            vga_print("usage: edit <filename>\n  Opens the full-screen interactive text editor.\n");
        else
            editor_open(cmd + 5);
    }
    else if (strcmp(cmd, "ai") == 0)
    {
        ai_chat();
    }
    else if (strcmp(cmd, "gui") == 0)
    {
        gui_enter();
    }

    else
    {
        vga_print("Unknown command. Type 'help'.\n");
    }
}

/* ============================================================
 * KERNEL MAIN
 * ============================================================ */

void kernel_main(uint32_t mb_magic, multiboot_info_t *mb_info)
{
    /* Step 1: Detect RAM from multiboot map, init memory manager */
    uint32_t detected_ram = 0;

    if (mb_magic == MULTIBOOT_MAGIC && mb_info &&
        (mb_info->flags & MULTIBOOT_FLAG_MMAP))
    {
        /* Walk the memory map, sum up usable regions */
        uint32_t offset = 0;
        while (offset < mb_info->mmap_length)
        {
            mmap_entry_t *entry = (mmap_entry_t *)
            (mb_info->mmap_addr + offset);

            if (entry->type == MMAP_TYPE_USABLE)
                detected_ram += (uint32_t)entry->length;

            offset += entry->size + sizeof(entry->size);
        }
    }
    else if (mb_magic == MULTIBOOT_MAGIC && mb_info &&
        (mb_info->flags & MULTIBOOT_FLAG_MEM))
    {
        /* Fallback: use simple mem_upper field (KB above 1MB) */
        detected_ram = (mb_info->mem_upper + 1024) * 1024;
    }
    else
    {
        /* Last resort: assume 32MB */
        detected_ram = 32 * 1024 * 1024;
    }

    mm_init(detected_ram);
    fs_init();
    ai_init();

    /* Step 2: IDT + PIC remap + IRQ enable */
    idt_init();

    /* Step 3: PIT timer (~1000 Hz) */
    pit_init();

    /* Step 4: Record boot time */
    boot_seconds = rtc_seconds_since_midnight();

    /* Step 5: Boot splash + chime */
    splash_show();
    boot_chime();

    /* Step 6: PS/2 mouse — removed (Wayland incompatibility) */

    /* Step 7: Shell */
    vga_prompt();

    /* Should never reach here */
    for (;;) asm volatile("hlt");
}
