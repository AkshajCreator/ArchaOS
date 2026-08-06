#include "keyboard.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

#define BUF_SIZE 128

struct {
    char buf[BUF_SIZE];
    uint32_t head;
    uint32_t tail;
} kbd;

static const char keymap[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};

void keyboard_isr(void) {
    uint8_t sc = inb(0x60);

    if (sc & 0x80)
        return;

    char c = keymap[sc];
    if (!c) return;

    uint32_t next = (kbd.head + 1) % BUF_SIZE;
    if (next != kbd.tail) {
        kbd.buf[kbd.head] = c;
        kbd.head = next;
    }
}

int keyboard_has_char(void) {
    return kbd.head != kbd.tail;
}

char keyboard_get_char(void) {
    if (!keyboard_has_char()) return 0;
    char c = kbd.buf[kbd.tail];
    kbd.tail = (kbd.tail + 1) % BUF_SIZE;
    return c;
}

void keyboard_init(void) {
    kbd.head = kbd.tail = 0;
}
