// src/vga.c

#include "vga.h"
#include "kernel.h"

#include <stdint.h>

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

#define CMD_BUFFER_SIZE 128
#define HISTORY_SIZE    10

/* ============================================================
 * VGA STATE
 * ============================================================ */

static uint16_t *vga_buffer = (uint16_t *)VGA_ADDRESS;

static int cursor_x = 0;
static int cursor_y = 0;

static int prompt_x = 0;
static int prompt_y = 0;

static int shift_pressed = 0;
static int caps_lock = 0;
static int extended = 0;

/* ============================================================
 * COMMAND BUFFER
 * ============================================================ */

static char cmd_buffer[CMD_BUFFER_SIZE];

static int cmd_len = 0;
static int cmd_cursor = 0;

/* ============================================================
 * HISTORY
 * ============================================================ */

static char history[HISTORY_SIZE][CMD_BUFFER_SIZE];

static int history_count = 0;
static int history_index = 0;

/* ============================================================
 * KEYMAPS
 * ============================================================ */

static const char map_lower[128] =
{
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};

static const char map_upper[128] =
{
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
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
    asm volatile("inb %1,%0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0,%1" : : "a"(val), "Nd"(port));
}

static inline int kbd_has_data(void)
{
    return inb(0x64) & 1;
}

/* ============================================================
 * STRING
 * ============================================================ */

static int strlen_local(const char *s)
{
    int len = 0;

    while (s[len])
        len++;

    return len;
}

/* ============================================================
 * CURSOR
 * ============================================================ */

static void update_cursor(void)
{
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);

    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
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
    for (int y = 1; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            vga_buffer[(y - 1) * VGA_WIDTH + x] =
            vga_buffer[y * VGA_WIDTH + x];
        }
    }

    for (int x = 0; x < VGA_WIDTH; x++)
    {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
        ' ' | 0x0700;
    }

    cursor_y = VGA_HEIGHT - 1;
}

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
    for (int y = 0; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            vga_buffer[y * VGA_WIDTH + x] =
            ' ' | 0x0700;
        }
    }

    cursor_x = 0;
    cursor_y = 0;

    update_cursor();
}

void vga_print_char(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
    }
    else if (c == '\b')
    {
        if (!(cursor_y == prompt_y &&
            cursor_x <= prompt_x))
        {
            if (cursor_x > 0)
            {
                cursor_x--;
            }

            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
            ' ' | 0x0700;
        }
    }
    else
    {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
        ((uint16_t)c) | 0x0700;

        cursor_x++;

        if (cursor_x >= VGA_WIDTH)
        {
            cursor_x = 0;
            cursor_y++;
        }
    }

    vga_ensure_visible();
    update_cursor();
}

void vga_print(const char *str)
{
    while (*str)
        vga_print_char(*str++);
}

void vga_print_center(const char *str)
{
    int len = strlen_local(str);

    int x = (VGA_WIDTH - len) / 2;
    int y = VGA_HEIGHT / 2;

    vga_set_cursor(x, y);
    vga_print(str);
}

/* ============================================================
 * WELCOME
 * ============================================================ */

static void delay(volatile unsigned int count)
{
    while (count--)
        asm volatile("nop");
}

void vga_show_welcome(void)
{
    vga_clear();

    vga_print_center(
        "Welcome to ArchaOS v0.3 \"Terminal\""
    );

    delay(500000000);

    vga_clear();
}

/* ============================================================
 * HISTORY
 * ============================================================ */

static void history_save(const char *cmd)
{
    if (!cmd[0])
        return;

    if (history_count < HISTORY_SIZE)
    {
        int dst = history_count++;

        for (int i = 0;; i++)
        {
            history[dst][i] = cmd[i];

            if (!cmd[i])
                break;
        }
    }
    else
    {
        for (int i = 1; i < HISTORY_SIZE; i++)
        {
            for (int j = 0;; j++)
            {
                history[i - 1][j] =
                history[i][j];

                if (!history[i][j])
                    break;
            }
        }

        for (int j = 0;; j++)
        {
            history[HISTORY_SIZE - 1][j] =
            cmd[j];

            if (!cmd[j])
                break;
        }
    }

    history_index = history_count;
}

static void redraw_line(void)
{
    for (int x = prompt_x;
         x < VGA_WIDTH;
    x++)
         {
             vga_buffer[prompt_y * VGA_WIDTH + x] =
             ' ' | 0x0700;
         }

         for (int i = 0; i < cmd_len; i++)
         {
             vga_buffer[
                 prompt_y * VGA_WIDTH +
                 prompt_x + i
             ] =
             ((uint16_t)cmd_buffer[i]) | 0x0700;
         }

         cursor_x = prompt_x + cmd_cursor;
         cursor_y = prompt_y;

         update_cursor();
}

static void history_load(int index)
{
    if (index < 0 ||
        index >= history_count)
        return;

    cmd_len = 0;
    cmd_cursor = 0;

    for (int i = 0;
         history[index][i];
    i++)
         {
             cmd_buffer[i] =
             history[index][i];

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
    char c;

    if (sc >= 128)
        return 0;

    c = shift_pressed
    ? map_upper[sc]
    : map_lower[sc];

    if (c >= 'a' && c <= 'z')
    {
        if (caps_lock)
            c -= 32;
    }
    else if (c >= 'A' && c <= 'Z')
    {
        if (caps_lock)
            c += 32;
    }

    return c;
}

/* ============================================================
 * SHELL
 * ============================================================ */

void vga_prompt(void)
{
    vga_print("Arc/> ");

    prompt_x = cursor_x;
    prompt_y = cursor_y;

    cmd_len = 0;
    cmd_cursor = 0;

    history_index = history_count;

    while (1)
    {
        if (!kbd_has_data())
            continue;

        uint8_t sc = inb(0x60);

        if (sc == 0xE0)
        {
            extended = 1;
            continue;
        }

        if (sc & 0x80)
        {
            if ((sc & 0x7F) == 0x2A ||
                (sc & 0x7F) == 0x36)
            {
                shift_pressed = 0;
            }

            continue;
        }

        if (sc == 0x2A || sc == 0x36)
        {
            shift_pressed = 1;
            continue;
        }

        if (sc == 0x3A)
        {
            caps_lock = !caps_lock;
            continue;
        }

        if (extended)
        {
            switch (sc)
            {
                case 0x48:
                    if (history_index > 0)
                        history_load(--history_index);
                break;

                case 0x50:
                    if (history_index <
                        history_count - 1)
                    {
                        history_load(++history_index);
                    }
                    break;

                case 0x4B:
                    if (cmd_cursor > 0)
                        cmd_cursor--;
                redraw_line();
                break;

                case 0x4D:
                    if (cmd_cursor < cmd_len)
                        cmd_cursor++;
                redraw_line();
                break;
            }

            extended = 0;
            continue;
        }

        char c = translate_key(sc);

        if (sc == 0x0E)
        {
            if (cmd_cursor > 0)
            {
                for (int i = cmd_cursor - 1;
                     i < cmd_len - 1;
                i++)
                     {
                         cmd_buffer[i] =
                         cmd_buffer[i + 1];
                     }

                     cmd_len--;
                     cmd_cursor--;

                     redraw_line();
            }

            continue;
        }

        if (sc == 0x1C)
        {
            cmd_buffer[cmd_len] = '\0';

            vga_print("\n");

            history_save(cmd_buffer);

            kernel_execute_command(cmd_buffer);

            vga_print("\n");
            vga_print("Arc/> ");

            prompt_x = cursor_x;
            prompt_y = cursor_y;

            cmd_len = 0;
            cmd_cursor = 0;

            history_index = history_count;

            continue;
        }

        if (c &&
            cmd_len < CMD_BUFFER_SIZE - 1)
        {
            for (int i = cmd_len;
                 i > cmd_cursor;
            i--)
                 {
                     cmd_buffer[i] =
                     cmd_buffer[i - 1];
                 }

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

void vga_execute_command(const char *cmd)
{
    kernel_execute_command(cmd);
}
