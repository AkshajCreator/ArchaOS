// src/multiboot.h
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002

/* Flags telling us which fields are valid */
#define MULTIBOOT_FLAG_MEM     (1 << 0)   /* mem_lower/mem_upper valid */
#define MULTIBOOT_FLAG_MMAP    (1 << 6)   /* full memory map valid     */

/* ============================================================
 * MULTIBOOT INFO STRUCT
 * Exactly as GRUB fills it in memory
 * ============================================================ */

typedef struct __attribute__((packed))
{
    uint32_t flags;         /* which fields below are valid */
    uint32_t mem_lower;     /* KB of lower memory (usually 640) */
    uint32_t mem_upper;     /* KB of upper memory (RAM above 1MB) */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;   /* bytes in memory map */
    uint32_t mmap_addr;     /* physical address of map */
    /* ... more fields we don't need yet */
} multiboot_info_t;

/* ============================================================
 * MEMORY MAP ENTRY
 * ============================================================ */

typedef struct __attribute__((packed))
{
    uint32_t size;          /* size of this entry (not counting this field) */
    uint64_t base_addr;     /* start of region */
    uint64_t length;        /* length of region */
    uint32_t type;          /* 1 = usable RAM, anything else = reserved */
} mmap_entry_t;

#define MMAP_TYPE_USABLE 1

#endif
