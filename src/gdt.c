#include "gdt.h"
#include "serial.h"
#include "task.h"
#include <stdint.h>
#include <stddef.h>

#define GDT_ENTRIES 7

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

tss_entry_t kernel_tss;
tss_entry_t df_tss;

/* Dedicated isolated 16KB stack for Double Fault handler */
static uint8_t df_stack[16384] __attribute__((aligned(16)));

/* Helper to set a GDT entry */
static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access      = access;
}

/* Hardware Double Fault Task Entry Point */
void double_fault_entry(void)
{
    uint32_t prev_sel = df_tss.prev_tss;
    uint32_t fault_eip = kernel_tss.eip;
    uint32_t fault_esp = kernel_tss.esp;

    serial_puts(COM1_BASE, "\n=======================================================\n");
    serial_puts(COM1_BASE, " [TRIPLE FAULT SHIELD] CRITICAL: Double Fault (Vector 8)!\n");
    serial_puts(COM1_BASE, " Intercepted via hardware TSS Task Gate on clean stack.\n");
    serial_printf(COM1_BASE, " Prev Task Selector: 0x%x | Faulting EIP: 0x%x | ESP: 0x%x\n", prev_sel, fault_eip, fault_esp);
    serial_puts(COM1_BASE, " Hardware reset prevented! Recovering system state...\n");
    serial_puts(COM1_BASE, "=======================================================\n");

    /* Reset TSS busy bits in GDT so gates can be re-entered if needed */
    gdt[4].access = 0x89; /* DF TSS back to Available */
    gdt[3].access = 0x89; /* Kernel TSS back to Available */

    /* Reset TSS stack pointer for next fault */
    df_tss.esp = (uint32_t)df_stack + sizeof(df_stack) - 16;
    df_tss.prev_tss = 0;

    /* Reload task register with kernel TSS */
    asm volatile("ltr %%ax" :: "a"(GDT_KERNEL_TSS_SEL));

    /* Terminate current offending task if under multitasking */
    task_t *cur = task_get_current();
    if (cur && cur->pid > 0) {
        serial_printf(COM1_BASE, "[SHIELD] Terminating faulted worker task '%s' (PID %u)\n", cur->name, cur->pid);
        task_kill(cur->pid);
        task_yield();
    } else {
        serial_puts(COM1_BASE, "[SHIELD] Kernel main context faulted. Entering safe halt.\n");
        for (;;) {
            asm volatile("hlt");
        }
    }
}

/* Update kernel stack in TSS for privilege transitions */
void tss_set_kernel_stack(uint32_t esp0)
{
    kernel_tss.esp0 = esp0;
}

/* Assembly helper to reload GDT, segment registers, and LTR */
static void gdt_flush_internal(uint32_t gdt_ptr_addr)
{
    asm volatile (
        "mov %0, %%eax\n"
        "lgdt (%%eax)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $.flush\n"
        ".flush:\n"
        "mov $0x18, %%ax\n"
        "ltr %%ax\n"
        :
        : "r"(gdt_ptr_addr)
        : "eax"
    );
}

void gdt_init(void)
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    /* 0x00: Null Descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code Segment (Base=0, Limit=4GB, Exec/Read, Ring 0) */
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);

    /* 0x10: Kernel Data Segment (Base=0, Limit=4GB, Read/Write, Ring 0) */
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);

    /* Setup Kernel Main TSS (0x18) */
    for (size_t i = 0; i < sizeof(tss_entry_t); i++) ((uint8_t*)&kernel_tss)[i] = 0;
    kernel_tss.ss0  = GDT_KERNEL_DATA_SEL;
    kernel_tss.esp0 = (uint32_t)df_stack + sizeof(df_stack) - 16;
    kernel_tss.cs   = GDT_KERNEL_CODE_SEL | 0;
    kernel_tss.ss   = kernel_tss.ds = kernel_tss.es = kernel_tss.fs = kernel_tss.gs = GDT_KERNEL_DATA_SEL | 0;
    kernel_tss.iomap_base = sizeof(tss_entry_t);
    gdt_set_gate(3, (uint32_t)&kernel_tss, sizeof(tss_entry_t) - 1, 0x89, 0x00);

    /* Setup Double Fault Hardware TSS (0x20) */
    for (size_t i = 0; i < sizeof(tss_entry_t); i++) ((uint8_t*)&df_tss)[i] = 0;
    df_tss.ss0  = GDT_KERNEL_DATA_SEL;
    df_tss.esp0 = (uint32_t)df_stack + sizeof(df_stack) - 16;
    df_tss.esp  = (uint32_t)df_stack + sizeof(df_stack) - 16;
    df_tss.eip  = (uint32_t)double_fault_entry;
    df_tss.cs   = GDT_KERNEL_CODE_SEL;
    df_tss.ss   = df_tss.ds = df_tss.es = df_tss.fs = df_tss.gs = GDT_KERNEL_DATA_SEL;
    df_tss.eflags = 0x00000002; /* Interrupts disabled */
    df_tss.iomap_base = sizeof(tss_entry_t);
    gdt_set_gate(4, (uint32_t)&df_tss, sizeof(tss_entry_t) - 1, 0x89, 0x00);

    /* 0x28: User Code Segment (Base=0, Limit=4GB, Exec/Read, Ring 3) */
    gdt_set_gate(5, 0, 0xFFFFF, 0xFA, 0xCF);

    /* 0x30: User Data Segment (Base=0, Limit=4GB, Read/Write, Ring 3) */
    gdt_set_gate(6, 0, 0xFFFFF, 0xF2, 0xCF);

    /* Flush GDT and load LTR */
    gdt_flush_internal((uint32_t)&gdt_ptr);

    serial_puts(COM1_BASE, "[GDT] Initialized flat 4GB segments with Kernel TSS (0x18) and DF TSS (0x20)\n");
}
