#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_KERNEL_CODE_SEL  0x08
#define GDT_KERNEL_DATA_SEL  0x10
#define GDT_KERNEL_TSS_SEL   0x18
#define GDT_DF_TSS_SEL       0x20
#define GDT_USER_CODE_SEL    0x28
#define GDT_USER_DATA_SEL    0x30

/* 32-bit x86 Task State Segment */
typedef struct __attribute__((packed)) {
    uint32_t prev_tss;   /* Backlink pointer to previous TSS */
    uint32_t esp0;       /* Stack pointer for Ring 0 */
    uint32_t ss0;        /* Stack segment for Ring 0 */
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;        /* Page Directory Base Register */
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} tss_entry_t;

/* GDT Entry */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} gdt_entry_t;

/* GDT Pointer */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

/* Public API */
void gdt_init(void);
void tss_set_kernel_stack(uint32_t esp0);

/* Exported TSS instances */
extern tss_entry_t kernel_tss;
extern tss_entry_t df_tss;

#endif /* GDT_H */
