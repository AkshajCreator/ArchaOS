// src/idt.h
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* ============================================================
 * IDT ENTRY
 * ============================================================ */

typedef struct __attribute__((packed))
{
    uint16_t base_low;      // offset bits 0-15
    uint16_t selector;      // code segment selector
    uint8_t  zero;          // always 0
    uint8_t  flags;         // type and attributes
    uint16_t base_high;     // offset bits 16-31
} idt_entry_t;

/* ============================================================
 * IDT POINTER
 * ============================================================ */

typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void idt_init(void);
void mouse_init(void);

/* Exposed so kernel.c and gui.c can read them */
extern volatile uint8_t  irq_kbd_fired;
extern volatile uint8_t  last_scancode;

extern volatile int      mouse_x;
extern volatile int      mouse_y;
extern volatile uint8_t  mouse_left_click;
extern volatile uint8_t  mouse_right_click;

#endif
