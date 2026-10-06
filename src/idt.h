// src/idt.h
#ifndef IDT_H
#define IDT_H

#include <stdint.h>



typedef struct __attribute__((packed))
{
    uint16_t base_low;      // offset bits 0-15
    uint16_t selector;      // code segment selector
    uint8_t  zero;          // always 0
    uint8_t  flags;         // type and attributes
    uint16_t base_high;     // offset bits 16-31
} idt_entry_t;



typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;



void idt_init(void);
void mouse_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
uint16_t pic_get_isr(void);

/* CPU Exception Stubs */
extern void _isr20(void);
extern void _isr21(void);
extern void _isr22(void);
extern void _isr23(void);
extern void _isr24(void);
extern void _isr25(void);
extern void _isr26(void);
extern void _isr27(void);
extern void _isr28(void);
extern void _isr29(void);
extern void _isr30(void);
extern void _isr31(void);

/* Exposed so kernel.c and gui.c can read them */
extern volatile uint8_t  irq_kbd_fired;
extern volatile uint8_t  last_scancode;

int  kbd_pop_scancode(uint8_t *sc);
void kbd_push_scancode(uint8_t sc);
int  kbd_has_scancode(void);

extern volatile int      mouse_x;
extern volatile int      mouse_y;
extern volatile uint8_t  mouse_left_click;
extern volatile uint8_t  mouse_right_click;

#endif
