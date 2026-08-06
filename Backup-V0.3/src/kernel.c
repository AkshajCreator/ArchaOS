// src/kernel.c

#include "kernel.h"
#include "vga.h"

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
    while (*a && (*a == *b))
    {
        a++;
        b++;
    }

    return *(const unsigned char *)a -
    *(const unsigned char *)b;
}

static int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && (*a == *b))
    {
        a++;
        b++;
        n--;
    }

    if (n == 0)
        return 0;

    return *(const unsigned char *)a -
    *(const unsigned char *)b;
}

/* ============================================================
 * ITOA
 * ============================================================ */

char *itoa(int value, char *str, int base)
{
    char *rc = str;
    char *ptr = str;
    char *low;

    if (base < 2 || base > 36)
    {
        *str = '\0';
        return str;
    }

    if (value < 0 && base == 10)
    {
        *ptr++ = '-';
        value = -value;
    }

    low = ptr;

    do
    {
        *ptr++ =
        "0123456789abcdefghijklmnopqrstuvwxyz"
        [value % base];

        value /= base;

    } while (value);

    *ptr-- = '\0';

    while (low < ptr)
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }

    return rc;
}

/* ============================================================
 * REBOOT
 * ============================================================ */

void reboot(void)
{
    uint8_t good = 0x02;

    while (good & 0x02)
        good = inb(0x64);

    outb(0x64, 0xFE);
}

/* ============================================================
 * RTC
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
    return (value & 0x0F) +
    ((value >> 4) * 10);
}

static uint32_t rtc_seconds_since_midnight(void)
{
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t regB;
    uint8_t last;

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
    {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    return (uint32_t)hour * 3600U +
    (uint32_t)min  * 60U +
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
    uint32_t divisor;
    uint8_t tmp;

    divisor = 1193180 / frequency;

    outb(0x43, 0xB6);

    outb(0x42, divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    tmp = inb(0x61);

    if ((tmp | 3) != tmp)
    {
        outb(0x61, tmp | 3);
    }
}

static void no_sound(void)
{
    uint8_t tmp;

    tmp = inb(0x61) & 0xFC;

    outb(0x61, tmp);
}

void beep(void)
{
    play_sound(1000);

    for (volatile int i = 0;
         i < 0xFFFFF;
    i++);

    no_sound();
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
        vga_print(" echo <text>,\n");
        vga_print(" settings about,\n");
        vga_print(" ls, ports, beep, credits\n");
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

        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    else if (strcmp(cmd, "uptime") == 0)
    {
        uint32_t now;
        uint32_t up;
        char buf[16];

        now = rtc_seconds_since_midnight();

        if (now >= boot_seconds)
            up = now - boot_seconds;
        else
            up = (86400 - boot_seconds) + now;

        itoa((int)up, buf, 10);

        vga_print("Uptime: ");
        vga_print(buf);
        vga_print(" seconds\n");
    }

    else if (strncmp(cmd, "echo ", 5) == 0)
    {
        vga_print(cmd + 5);
        vga_print("\n");
    }

    else if (strcmp(cmd, "settings about") == 0)
    {
        vga_print("ArchaOS v0.3 \"Terminal\"\n");
        vga_print("Author: Akshaj\n");
        vga_print("32-bit x86 kernel\n");
    }

    else if (strcmp(cmd, "about") == 0)
    {
        vga_print("32-bit x86 kernel\n");
    }

    else if (strcmp(cmd, "ls") == 0)
    {
        vga_print("/ArchaOS\n");
    }

    else if (strcmp(cmd, "$VERSION") == 0)
    {
        vga_print("ArchaOS v0.3 \"Terminal\"\n");
    }

    else if (strcmp(cmd, "ports") == 0)
    {
        vga_print("I/O Ports: stub\n");
    }

    else if (strcmp(cmd, "beep") == 0)
    {
        beep();
    }

    else if (strcmp(cmd, "credits") == 0)
    {
        vga_print("Made by Akshaj\n");
    }

    else
    {
        vga_print("Unknown command. Type 'help'.\n");
    }
}

/* ============================================================
 * KERNEL MAIN
 * ============================================================ */

void kernel_main(void)
{
    boot_seconds = rtc_seconds_since_midnight();

    vga_show_welcome();

    vga_clear();

    vga_prompt();

    for (;;)
    {
        asm volatile ("hlt");
    }
}
