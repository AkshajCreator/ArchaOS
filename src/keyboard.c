#include "keyboard.h"
#include "idt.h"

static const char kbd_lower[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`', 0,   '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ',
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0
};

static const char kbd_upper[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ',
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0
};

static volatile int kbd_shift = 0;
static volatile int kbd_ctrl  = 0;
static volatile int kbd_alt   = 0;
static volatile int kbd_caps  = 0;
static volatile int kbd_ext   = 0;

#define KBD_BUF_SIZE 64
static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile int  kbd_head = 0;
static volatile int  kbd_tail = 0;

void keyboard_init(void)
{
    kbd_shift = 0;
    kbd_ctrl  = 0;
    kbd_alt   = 0;
    kbd_caps  = 0;
    kbd_ext   = 0;
    kbd_head  = 0;
    kbd_tail  = 0;
}

int keyboard_is_shift(void) { return kbd_shift; }
int keyboard_is_ctrl(void)  { return kbd_ctrl; }
int keyboard_is_alt(void)   { return kbd_alt; }
int keyboard_is_caps(void)  { return kbd_caps; }

char keyboard_scancode_to_ascii(uint8_t sc, int shift, int caps)
{
    if (sc >= 128) return 0;
    char c = shift ? kbd_upper[sc] : kbd_lower[sc];
    if (caps) {
        if (!shift && (c >= 'a' && c <= 'z')) c -= 32;
        else if (shift && (c >= 'A' && c <= 'Z')) c += 32;
    }
    return c;
}

void keyboard_handle_scancode(uint8_t sc)
{
    if (sc == 0xE0) {
        kbd_ext = 1;
        return;
    }

    if (sc & 0x80) {
        uint8_t b = sc & 0x7F;
        if (b == 0x2A || b == 0x36) kbd_shift = 0;
        else if (b == 0x1D)         kbd_ctrl  = 0;
        else if (b == 0x38)         kbd_alt   = 0;
        kbd_ext = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        kbd_shift = 1;
        kbd_ext = 0;
        return;
    }
    if (sc == 0x1D) {
        kbd_ctrl = 1;
        kbd_ext = 0;
        return;
    }
    if (sc == 0x38) {
        kbd_alt = 1;
        kbd_ext = 0;
        return;
    }
    if (sc == 0x3A) {
        kbd_caps = !kbd_caps;
        kbd_ext = 0;
        return;
    }

    if (kbd_ext) {
        kbd_ext = 0;
        return;
    }

    char c = keyboard_scancode_to_ascii(sc, kbd_shift, kbd_caps);
    if (c) {
        if (kbd_ctrl && (c == 'c' || c == 'C')) {
            c = 3; /* ASCII ETX (Ctrl+C) */
        }
        int next = (kbd_head + 1) % KBD_BUF_SIZE;
        if (next != kbd_tail) {
            kbd_buf[kbd_head] = c;
            kbd_head = next;
        }
    }
}

uint8_t keyboard_get_scancode(void)
{
    while (!irq_kbd_fired) {
        asm volatile("sti; hlt");
    }
    uint8_t sc = last_scancode;
    irq_kbd_fired = 0;
    return sc;
}

int keyboard_has_char(void)
{
    return kbd_head != kbd_tail;
}

char keyboard_getchar(void)
{
    while (!keyboard_has_char()) {
        asm volatile("sti; hlt");
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}
