// src/idt.c

#include "idt.h"
#include <stdint.h>

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

static inline void io_wait(void)
{
    outb(0x80, 0x00);
}

/* ============================================================
 * IDT STORAGE
 * ============================================================ */

static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

/* ============================================================
 * SHARED IRQ STATE
 * ============================================================ */

volatile uint8_t irq_kbd_fired = 0;
volatile uint8_t last_scancode  = 0;

volatile int      mouse_x = 160;
volatile int      mouse_y = 100;
volatile uint8_t  mouse_left_click = 0;
volatile uint8_t  mouse_right_click = 0;

/* ============================================================
 * EXTERN STUBS — defined in isr_stubs.asm
 * ============================================================ */

extern void _isr0(void);  extern void _isr1(void);
extern void _isr2(void);  extern void _isr3(void);
extern void _isr4(void);  extern void _isr5(void);
extern void _isr6(void);  extern void _isr7(void);
extern void _isr8(void);  extern void _isr9(void);
extern void _isr10(void); extern void _isr11(void);
extern void _isr12(void); extern void _isr13(void);
extern void _isr14(void); extern void _isr15(void);
extern void _isr16(void); extern void _isr17(void);
extern void _isr18(void); extern void _isr19(void);
extern void _irq0(void);  extern void _irq1(void);
extern void _irq2(void);  extern void _irq3(void);
extern void _irq4(void);  extern void _irq5(void);
extern void _irq6(void);  extern void _irq7(void);
extern void _irq8(void);  extern void _irq9(void);
extern void _irq10(void); extern void _irq11(void);
extern void _irq12(void); extern void _irq13(void);
extern void _irq14(void); extern void _irq15(void);

#include "serial.h"

/* ============================================================
 * SSE / FPU INITIALIZATION
 * ============================================================ */

void sse_init(void)
{
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); /* Clear EM (Emulation) */
    cr0 |= (1 << 1);  /* Set MP (Monitor Coprocessor) */
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  /* Set OSFXSR (FXSAVE/FXRSTOR & SSE Support) */
    cr4 |= (1 << 10); /* Set OSXMMEXCPT (Unmasked SIMD Exception Support) */
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    asm volatile("fninit");
    serial_puts(COM1_BASE, "[CPU] FPU / SSE / AES-NI extensions enabled (CR4.OSFXSR=1)\n");
}

/* ============================================================
 * C-SIDE HANDLERS — called from isr_stubs.asm
 * ============================================================ */

void isr_handler(uint32_t num, uint32_t err_code, uint32_t eip)
{
    serial_printf(COM1_BASE, "\n[CPU EXCEPTION] ISR %u (err=0x%x) at EIP=0x%x! Halting.\n", num, err_code, eip);
    for (;;) asm volatile("cli; hlt");
}

void irq1_handler(void)
{
    last_scancode = inb(0x60);
    irq_kbd_fired = 1;
    outb(0x20, 0x20);       /* EOI to master PIC */
}

void irq8_handler(void)
{
    outb(0x70, 0x0C);
    inb(0x71);
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void default_irq_handler(int irq)
{
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

/* ============================================================
 * mouse_poll_hw — parse any pending PS/2 mouse bytes.
 * Does NOT send PIC EOI — safe to call cooperatively from the
 * GUI event loop without confusing the interrupt controller.
 * ============================================================ */
void mouse_poll_hw(void)
{
    while (1)
    {
        uint8_t status = inb(0x64);
        if (!(status & 0x01) || !(status & 0x20)) break;
        uint8_t b = inb(0x60);
        switch (mouse_cycle)
        {
            case 0:
                if (b & 0x08) {
                    mouse_packet[0] = b;
                    mouse_cycle = 1;
                }
                break;
            case 1:
                mouse_packet[1] = b;
                mouse_cycle = 2;
                break;
            case 2:
                mouse_packet[2] = b;
                mouse_cycle = 0;

                int rel_x = (int)mouse_packet[1] - ((mouse_packet[0] & 0x10) ? 256 : 0);
                int rel_y = (int)mouse_packet[2] - ((mouse_packet[0] & 0x20) ? 256 : 0);

                mouse_x += rel_x;
                mouse_y -= rel_y;

                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x >= 320) mouse_x = 319;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y >= 200) mouse_y = 199;

                mouse_left_click  = mouse_packet[0] & 0x01;
                mouse_right_click = (mouse_packet[0] >> 1) & 0x01;
                break;
        }
    }
}

/* ISR wrapper — called by hardware IRQ 12. Parses data then ACKs PIC. */
void irq12_handler(void)
{
    mouse_poll_hw();
    outb(0xA0, 0x20);  /* EOI to Slave PIC  */
    outb(0x20, 0x20);  /* EOI to Master PIC */
}

static int mouse_wait(uint8_t type)
{
    uint32_t timeout = 100000;
    if (type == 0) {
        while (!(inb(0x64) & 1) && --timeout);
    } else {
        while ((inb(0x64) & 2) && --timeout);
    }
    return timeout > 0; /* 1 = success, 0 = timed out */
}

static void mouse_write(uint8_t write)
{
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
}

static uint8_t mouse_read(void)
{
    mouse_wait(0);
    return inb(0x60);
}

static void irq_unmask(uint8_t irq);

void mouse_init(void)
{
    uint8_t status;

    /* Disable interrupts during PS/2 hardware handshake so ACKs (0xFA)
     * are read synchronously and not intercepted by irq12_handler */
    asm volatile("cli");

    /* Flush output buffer first */
    while (inb(0x64) & 1) inb(0x60);

    /* Pre-check: if PS/2 controller doesn't exist (common on pure UEFI
     * laptops with I2C touchpads), the status register reads 0xFF.
     * Bail out immediately to avoid hanging. */
    status = inb(0x64);
    if (status == 0xFF) {
        asm volatile("sti");
        return;
    }

    if (!mouse_wait(1)) { asm volatile("sti"); return; }
    outb(0x64, 0xA8);

    if (!mouse_wait(1)) { asm volatile("sti"); return; }
    outb(0x64, 0x20);
    if (!mouse_wait(0)) { asm volatile("sti"); return; }
    status = (inb(0x60) | 0x03) & ~0x20;
    if (!mouse_wait(1)) { asm volatile("sti"); return; }
    outb(0x64, 0x60);
    if (!mouse_wait(1)) { asm volatile("sti"); return; }
    outb(0x60, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();

    /* Flush any leftover response bytes */
    while (inb(0x64) & 1) inb(0x60);

    mouse_cycle = 0;
    mouse_packet[0] = 0;
    mouse_packet[1] = 0;
    mouse_packet[2] = 0;
    mouse_left_click = 0;
    mouse_right_click = 0;

    irq_unmask(2);  /* Unmask cascade on Master PIC */
    irq_unmask(12); /* Unmask IRQ 12 on Slave PIC */

    asm volatile("sti");
}

/* ============================================================
 * SET ONE IDT GATE
 * ============================================================ */

static void idt_set_gate(uint8_t num,
                         uint32_t base,
                         uint16_t sel,
                         uint8_t  flags)
{
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector  = sel;
    idt[num].zero      = 0;
    idt[num].flags     = flags;
}

/* ============================================================
 * PIC REMAP
 * ============================================================ */

static void pic_remap(void)
{
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);

    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();   /* master → INT 32 */
    outb(0xA1, 0x28); io_wait();   /* slave  → INT 40 */
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    outb(0x21, mask1);
    outb(0xA1, mask2);
}

/* ============================================================
 * UNMASK SPECIFIC IRQ
 * ============================================================ */

static void irq_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1 << irq));
}

/* ============================================================
 * RTC INIT
 * ============================================================ */

static void rtc_init(void)
{
    outb(0x70, 0x8B);
    uint8_t prev = inb(0x71);
    outb(0x70, 0x8B);
    outb(0x71, prev | 0x40);
    outb(0x70, 0x0C);
    inb(0x71);
}

/* ============================================================
 * IDT INIT
 * ============================================================ */

void idt_init(void)
{
    /* 1. Remap PIC first */
    pic_remap();

    /* 2. Mask all IRQs */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    /* 3. IDT pointer */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    /* 4. Exception stubs */
    uint16_t cs_sel;
    asm volatile("mov %%cs, %0" : "=r"(cs_sel));

    idt_set_gate(0,  (uint32_t)_isr0,  cs_sel, 0x8E);
    idt_set_gate(1,  (uint32_t)_isr1,  cs_sel, 0x8E);
    idt_set_gate(2,  (uint32_t)_isr2,  cs_sel, 0x8E);
    idt_set_gate(3,  (uint32_t)_isr3,  cs_sel, 0x8E);
    idt_set_gate(4,  (uint32_t)_isr4,  cs_sel, 0x8E);
    idt_set_gate(5,  (uint32_t)_isr5,  cs_sel, 0x8E);
    idt_set_gate(6,  (uint32_t)_isr6,  cs_sel, 0x8E);
    idt_set_gate(7,  (uint32_t)_isr7,  cs_sel, 0x8E);
    idt_set_gate(8,  (uint32_t)_isr8,  cs_sel, 0x8E);
    idt_set_gate(9,  (uint32_t)_isr9,  cs_sel, 0x8E);
    idt_set_gate(10, (uint32_t)_isr10, cs_sel, 0x8E);
    idt_set_gate(11, (uint32_t)_isr11, cs_sel, 0x8E);
    idt_set_gate(12, (uint32_t)_isr12, cs_sel, 0x8E);
    idt_set_gate(13, (uint32_t)_isr13, cs_sel, 0x8E);
    idt_set_gate(14, (uint32_t)_isr14, cs_sel, 0x8E);
    idt_set_gate(15, (uint32_t)_isr15, cs_sel, 0x8E);
    idt_set_gate(16, (uint32_t)_isr16, cs_sel, 0x8E);
    idt_set_gate(17, (uint32_t)_isr17, cs_sel, 0x8E);
    idt_set_gate(18, (uint32_t)_isr18, cs_sel, 0x8E);
    idt_set_gate(19, (uint32_t)_isr19, cs_sel, 0x8E);

    /* 5. IRQ handlers (all 16 PIC IRQs mapped to vectors 32..47) */
    idt_set_gate(32, (uint32_t)_irq0,  cs_sel, 0x8E);
    idt_set_gate(33, (uint32_t)_irq1,  cs_sel, 0x8E);
    idt_set_gate(34, (uint32_t)_irq2,  cs_sel, 0x8E);
    idt_set_gate(35, (uint32_t)_irq3,  cs_sel, 0x8E);
    idt_set_gate(36, (uint32_t)_irq4,  cs_sel, 0x8E);
    idt_set_gate(37, (uint32_t)_irq5,  cs_sel, 0x8E);
    idt_set_gate(38, (uint32_t)_irq6,  cs_sel, 0x8E);
    idt_set_gate(39, (uint32_t)_irq7,  cs_sel, 0x8E);
    idt_set_gate(40, (uint32_t)_irq8,  cs_sel, 0x8E);
    idt_set_gate(41, (uint32_t)_irq9,  cs_sel, 0x8E);
    idt_set_gate(42, (uint32_t)_irq10, cs_sel, 0x8E);
    idt_set_gate(43, (uint32_t)_irq11, cs_sel, 0x8E);
    idt_set_gate(44, (uint32_t)_irq12, cs_sel, 0x8E);
    idt_set_gate(45, (uint32_t)_irq13, cs_sel, 0x8E);
    idt_set_gate(46, (uint32_t)_irq14, cs_sel, 0x8E);
    idt_set_gate(47, (uint32_t)_irq15, cs_sel, 0x8E);

    /* 6. Load IDT */
    asm volatile("lidt %0" : : "m"(idt_ptr));

    /* 7. Init RTC & Mouse */
    rtc_init();
    mouse_init();

    /* 8. Unmask PIT, keyboard, cascade, RTC, Mouse */
    irq_unmask(0);
    irq_unmask(1);
    irq_unmask(2);
    irq_unmask(8);
    irq_unmask(12);

    /* 8.5. Enable FPU & SSE/AES-NI */
    sse_init();

    /* 9. Enable interrupts — always last */
    asm volatile("sti");
}
