// src/idt.c

#include "idt.h"
#include "keyboard.h"
#include <stdint.h>



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



static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;



volatile uint8_t irq_kbd_fired = 0;
volatile uint8_t last_scancode  = 0;

#define KBD_RAW_BUF_SIZE 128
static volatile uint8_t kbd_raw_buf[KBD_RAW_BUF_SIZE];
static volatile int kbd_raw_head = 0;
static volatile int kbd_raw_tail = 0;
static int kbd_ext_prefix = 0;

void kbd_push_scancode(uint8_t sc)
{
    int next = (kbd_raw_head + 1) % KBD_RAW_BUF_SIZE;
    if (next != kbd_raw_tail) {
        kbd_raw_buf[kbd_raw_head] = sc;
        kbd_raw_head = next;
    }
}

int kbd_pop_scancode(uint8_t *sc)
{
    if (kbd_raw_head == kbd_raw_tail) return 0;
    if (sc) *sc = kbd_raw_buf[kbd_raw_tail];
    kbd_raw_tail = (kbd_raw_tail + 1) % KBD_RAW_BUF_SIZE;
    return 1;
}

int kbd_has_scancode(void)
{
    return (kbd_raw_head != kbd_raw_tail);
}

volatile int      mouse_x = 160;
volatile int      mouse_y = 100;
volatile uint8_t  mouse_left_click = 0;
volatile uint8_t  mouse_right_click = 0;



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
extern void _isr20(void); extern void _isr21(void);
extern void _isr22(void); extern void _isr23(void);
extern void _isr24(void); extern void _isr25(void);
extern void _isr26(void); extern void _isr27(void);
extern void _isr28(void); extern void _isr29(void);
extern void _isr30(void); extern void _isr31(void);
extern void _irq0(void);  extern void _irq1(void);
extern void _irq2(void);  extern void _irq3(void);
extern void _irq4(void);  extern void _irq5(void);
extern void _irq6(void);  extern void _irq7(void);
extern void _irq8(void);  extern void _irq9(void);
extern void _irq10(void); extern void _irq11(void);
extern void _irq12(void); extern void _irq13(void);
extern void _irq14(void); extern void _irq15(void);
extern void _isr128(void);

#include "serial.h"
#include "gdt.h"
#include "task.h"



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



void isr_handler(uint32_t num, uint32_t err_code, uint32_t eip)
{
    static const char *exception_names[32] = {
        "Division By Zero",                 /* 0 */
        "Debug",                            /* 1 */
        "Non-Maskable Interrupt",           /* 2 */
        "Breakpoint",                       /* 3 */
        "Into Detected Overflow",           /* 4 */
        "Out of Bounds",                    /* 5 */
        "Invalid Opcode",                   /* 6 */
        "No Coprocessor",                   /* 7 */
        "Double Fault",                     /* 8 */
        "Coprocessor Segment Overrun",      /* 9 */
        "Bad TSS",                          /* 10 */
        "Segment Not Present",              /* 11 */
        "Stack Fault",                      /* 12 */
        "General Protection Fault",         /* 13 */
        "Page Fault",                       /* 14 */
        "Unknown Interrupt",                /* 15 */
        "Coprocessor Fault",                /* 16 */
        "Alignment Check",                  /* 17 */
        "Machine Check",                    /* 18 */
        "SIMD Floating-Point Exception",    /* 19 */
        "Virtualization Exception",         /* 20 */
        "Control Protection Exception",     /* 21 */
        "Reserved",                         /* 22 */
        "Reserved",                         /* 23 */
        "Reserved",                         /* 24 */
        "Reserved",                         /* 25 */
        "Reserved",                         /* 26 */
        "Reserved",                         /* 27 */
        "Hypervisor Injection",             /* 28 */
        "VMM Communication",                /* 29 */
        "Security Exception",               /* 30 */
        "Reserved"                          /* 31 */
    };

    const char *name = (num < 32) ? exception_names[num] : "Unknown";
    serial_printf(COM1_BASE, "\n[CPU EXCEPTION] ISR %u (%s, err=0x%x) at EIP=0x%x!\n", num, name, err_code, eip);
    task_t *cur = task_get_current();
    if (cur && cur->pid > 0) {
        serial_printf(COM1_BASE, "[SHIELD] Fault caught in task '%s' (PID %u). Terminating to prevent crash.\n",
                      cur->name, cur->pid);
        task_kill(cur->pid);
        task_yield();
        return;
    }
    serial_printf(COM1_BASE, "[CPU EXCEPTION] Fatal kernel exception: %s. System halted.\n", name);
    for (;;) asm volatile("cli; hlt");
}

void irq1_handler(void)
{
    uint8_t sc = inb(0x60);
    outb(0x20, 0x20);       /* EOI to master PIC */
    keyboard_handle_scancode(sc);

    /* 0xE0 is an extended key prefix — remember it and do not emit a ghost event */
    if (sc == 0xE0) {
        kbd_ext_prefix = 1;
        return;
    }

    extern int gui_active;
    if (!gui_active && (sc & 0x80) && sc != 0xAA && sc != 0xB6 && sc != 0x9D && sc != 0xB8) {
        /* Release of non-modifier key in CLI: preserve make code in last_scancode */
        kbd_ext_prefix = 0;
        return;
    }

    last_scancode = sc;
    irq_kbd_fired = 1;
    kbd_push_scancode(sc);
    kbd_ext_prefix = 0;
}

void irq8_handler(void)
{
    outb(0x70, 0x0C);
    inb(0x71);
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

uint16_t pic_get_isr(void)
{
    outb(0x20, 0x0B);
    outb(0xA0, 0x0B);
    return (uint16_t)((inb(0xA0) << 8) | inb(0x20));
}

void default_irq_handler(int irq)
{
    if (irq == 7) {
        if (!(pic_get_isr() & 0x80)) {
            /* Spurious IRQ 7: do NOT send EOI to PIC */
            return;
        }
    } else if (irq == 15) {
        if (!(pic_get_isr() & 0x8000)) {
            /* Spurious IRQ 15: send EOI ONLY to Master PIC */
            outb(0x20, 0x20);
            return;
        }
    }

    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

__attribute__((weak)) int vga_is_vesa(void)
{
    extern int vesa_is_active(void) __attribute__((weak));
    return vesa_is_active ? vesa_is_active() : 0;
}

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

                extern int gui_active;
                extern int vga_is_vesa(void);
                int max_x = (gui_active || !vga_is_vesa()) ? 319 : 799;
                int max_y = (gui_active || !vga_is_vesa()) ? 199 : 599;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x > max_x) mouse_x = max_x;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y > max_y) mouse_y = max_y;

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



void idt_set_gate(uint8_t num,
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



static void irq_unmask(uint8_t irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1 << irq));
}



static void rtc_init(void)
{
    outb(0x70, 0x8B);
    uint8_t prev = inb(0x71);
    outb(0x70, 0x8B);
    outb(0x71, prev | 0x40);
    outb(0x70, 0x0C);
    inb(0x71);
}



void idt_init(void)
{
    /* 0. Initialize GDT with Kernel TSS and Double Fault Task TSS */
    gdt_init();

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
    /* Vector 8: Hardware Task Gate (0x85) pointing to Double Fault TSS (0x20) */
    idt_set_gate(8,  0, GDT_DF_TSS_SEL, 0x85);
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
    idt_set_gate(20, (uint32_t)_isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)_isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)_isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)_isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)_isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)_isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)_isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)_isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)_isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)_isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)_isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)_isr31, 0x08, 0x8E);

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

    /* 5.5. Vector 128 (0x80): POSIX Syscall Gate with DPL 3 (0xEE) */
    idt_set_gate(128, (uint32_t)_isr128, cs_sel, 0xEE);

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
