// src/ai.c — ArchaOS Assistant AI (Live Online Copilot)
// Connected directly to internet AI via E1000 and NIM/DeepSeek runtime.
// Zero offline weights bloat.

#include "ai.h"
#include "vga.h"
#include "idt.h"
#include "kernel.h"
#include "net/net.h"
#include "net/nim.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * HELPERS
 * ============================================================ */

static void ai_strcpy(char *d, const char *s)
{
    while ((*d++ = *s++));
}

static void ai_tolower(char *s)
{
    for (int i = 0; s[i]; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
    }
}

/* ============================================================
 * KEYBOARD INPUT FOR INTERACTIVE CHAT
 * ============================================================ */

static const char kb_lo[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};

static const char kb_hi[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
};

static uint8_t ai_wait_sc(void)
{
    while (!irq_kbd_fired) asm volatile("hlt");
    uint8_t sc = last_scancode;
    irq_kbd_fired = 0;
    return sc;
}

static char ai_translate(uint8_t sc, int shift, int caps)
{
    if (sc >= 128) return 0;
    char c = shift ? kb_hi[sc] : kb_lo[sc];
    if (caps && !shift) {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return c;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void ai_init(void)
{
    // Live internet AI copilot — ready
}

void ai_get_response(const char *input, char *output, size_t max_len)
{
    if (!input || !output || max_len == 0) return;
    output[0] = '\0';

    if (net_if.up) {
        if (nim_query(input, output, max_len) && output[0]) {
            return;
        }
    }

    const char *fallback = "AI Copilot requires an active internet connection. Check ifconfig / network.";
    size_t oi = 0;
    while (fallback[oi] && oi < max_len - 1) {
        output[oi] = fallback[oi];
        oi++;
    }
    output[oi] = '\0';
}

void ai_chat(void)
{
    #define AI_INPUT_MAX 128
    char input[AI_INPUT_MAX];
    char low[AI_INPUT_MAX];
    int  shift = 0, caps = 0, ext = 0;

    vga_print_color("\n ArchaOS Assistant AI (Live Online Copilot)\n", 0x0B);
    vga_print_color(" Connected to live AI via NIM & E1000 networking.\n", 0x07);
    vga_print_color(" Type 'exit' to return to shell.\n\n", 0x08);

    while (1)
    {
        vga_print_color(" AI> ", 0x0E);

        int len = 0;
        while (1)
        {
            uint8_t sc = ai_wait_sc();

            if (sc == 0xE0) { ext = 1; continue; }

            if (sc & 0x80) {
                uint8_t b = sc & 0x7F;
                if (b == 0x2A || b == 0x36) shift = 0;
                ext = 0;
                continue;
            }

            if (sc == 0x2A || sc == 0x36) { shift = 1; ext = 0; continue; }
            if (sc == 0x3A)               { caps = !caps; ext = 0; continue; }
            if (ext)                       { ext = 0; continue; }

            if (sc == 0x1C) { input[len] = '\0'; vga_print("\n"); break; }

            if (sc == 0x0E) {
                if (len > 0) {
                    len--;
                    vga_print_char('\b');
                    vga_print_char(' ');
                    vga_print_char('\b');
                }
                continue;
            }

            char c = ai_translate(sc, shift, caps);
            if (c && c != '\b' && c != '\t' && len < AI_INPUT_MAX - 1) {
                input[len++] = c;
                vga_print_char(c);
            }
        }

        if (len == 0) continue;

        ai_strcpy(low, input);
        ai_tolower(low);

        if ((low[0]=='e' && low[1]=='x' && low[2]=='i' && low[3]=='t' && low[4]=='\0') ||
            (low[0]=='q' && low[1]=='u' && low[2]=='i' && low[3]=='t' && low[4]=='\0')) {
            vga_print_color(" Goodbye! Returning to shell.\n\n", 0x0B);
            break;
        }

        vga_print_color("    ", 0x07);

        if (net_if.up) {
            static char live_ai_resp[4096];
            vga_print_color("[Thinking...]\r    ", 0x08);
            if (nim_query(input, live_ai_resp, sizeof(live_ai_resp))) {
                vga_print(live_ai_resp);
                vga_print("\n\n");
                continue;
            }
        }

        vga_print("ArchaOS AI requires an active internet connection. Please configure networking via 'ifconfig'.\n\n");
    }
}
