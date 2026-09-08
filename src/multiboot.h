// src/multiboot.h
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002

/* Flags telling us which fields are valid */
#define MULTIBOOT_FLAG_MEM     (1 << 0)   /* mem_lower/mem_upper valid */
#define MULTIBOOT_FLAG_MMAP    (1 << 6)   /* full memory map valid     */
#define MULTIBOOT_FLAG_VBE     (1 << 11)  /* VBE info valid            */
#define MULTIBOOT_FLAG_FB      (1 << 12)  /* Framebuffer info valid    */

#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED  0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB      1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2

/* ============================================================
 * MULTIBOOT INFO STRUCT
 * Exactly as GRUB fills it in memory
 * ============================================================ */

typedef struct __attribute__((packed))
{
    uint32_t flags;             /* 0: which fields below are valid */
    uint32_t mem_lower;         /* 4: KB of lower memory (usually 640) */
    uint32_t mem_upper;         /* 8: KB of upper memory (RAM above 1MB) */
    uint32_t boot_device;       /* 12 */
    uint32_t cmdline;           /* 16 */
    uint32_t mods_count;        /* 20 */
    uint32_t mods_addr;         /* 24 */
    uint32_t syms[4];           /* 28..40 */
    uint32_t mmap_length;       /* 44: bytes in memory map */
    uint32_t mmap_addr;         /* 48: physical address of map */
    uint32_t drives_length;     /* 52 */
    uint32_t drives_addr;       /* 56 */
    uint32_t config_table;      /* 60 */
    uint32_t boot_loader_name;  /* 64 */
    uint32_t apm_table;         /* 68 */
    uint32_t vbe_control_info;  /* 72 */
    uint32_t vbe_mode_info;     /* 76 */
    uint16_t vbe_mode;          /* 80 */
    uint16_t vbe_interface_seg; /* 82 */
    uint16_t vbe_interface_off; /* 84 */
    uint16_t vbe_interface_len; /* 86 */
    uint64_t framebuffer_addr;  /* 88..95: Physical 64-bit linear FB address */
    uint32_t framebuffer_pitch; /* 96: Bytes per scanline */
    uint32_t framebuffer_width; /* 100: Horizontal resolution */
    uint32_t framebuffer_height;/* 104: Vertical resolution */
    uint8_t  framebuffer_bpp;   /* 108: Bits per pixel (usually 32) */
    uint8_t  framebuffer_type;  /* 109: 1 = direct RGB */
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
