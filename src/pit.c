// src/pit.c — Programmable Interval Timer (IRQ0, ~1000 Hz)

#include "pit.h"
#include "idt.h"
#include <stdint.h>

/* ============================================================
 * PORT I/O
 * ============================================================ */

static inline void outb(uint16_t port, uint8_t val)
{ asm volatile("outb %0,%1"::"a"(val),"Nd"(port)); }

static inline uint8_t inb(uint16_t port)
{ uint8_t r; asm volatile("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

/* ============================================================
 * STATE
 * ============================================================ */

volatile uint32_t pit_tick_count = 0;

extern void audio_step(uint32_t now_ticks);
static volatile int in_audio_step = 0;

void irq0_handler(void)
{
    pit_tick_count++;

    /* Background Audio Mixer Daemon: tick audio engine at 100 Hz (~every 10ms) */
    if ((pit_tick_count % 10) == 0 && !in_audio_step) {
        in_audio_step = 1;
        audio_step(pit_tick_count);
        in_audio_step = 0;
    }

    outb(0x20, 0x20);   /* EOI to master PIC */
}

/* ============================================================
 * PIT INIT — set channel 0 to ~1000 Hz
 * ============================================================ */

void pit_init(void)
{
    /* Divisor for 1000 Hz: 1193180 / 1000 = 1193 */
    uint16_t divisor = 1193;

    /* Channel 0, lobyte/hibyte, rate generator */
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));

    /* Unmask IRQ0 */
    uint8_t mask = inb(0x21);
    outb(0x21, mask & ~0x01);
}

/* ============================================================
 * SLEEP — busy-wait on tick counter
 * ============================================================ */

void pit_sleep(uint32_t ms)
{
    uint32_t start = pit_tick_count;
    uint32_t end = start + ms;
    uint32_t safety_cycles = ms * 50000;

    /* If ticks advance, use them; if IRQ0 is masked by UEFI, fall back to cycle count */
    while (pit_tick_count < end && safety_cycles > 0) {
        safety_cycles--;
        asm volatile("pause");
    }
}

uint32_t pit_ticks(void) { return pit_tick_count; }

