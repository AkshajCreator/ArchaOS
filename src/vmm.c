#include "vmm.h"
#include "serial.h"
#include "task.h"
#include "string.h"
#include "multiboot.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;



#define PMM_MAX_FRAMES   131072  /* Supports up to 512 MB physical RAM */
#define PMM_BITMAP_SIZE  (PMM_MAX_FRAMES / 8)

static uint8_t  pmm_bitmap[PMM_BITMAP_SIZE];
static uint32_t pmm_total_frames = 0;
static uint32_t pmm_free_frames  = 0;

static inline void pmm_set_bit(uint32_t frame)
{
    pmm_bitmap[frame / 8] |= (1 << (frame % 8));
}

static inline void pmm_clear_bit(uint32_t frame)
{
    pmm_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static inline int pmm_test_bit(uint32_t frame)
{
    return (pmm_bitmap[frame / 8] & (1 << (frame % 8))) != 0;
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_length, uint32_t total_ram)
{
    pmm_total_frames = total_ram / PAGE_SIZE;
    if (pmm_total_frames > PMM_MAX_FRAMES) {
        pmm_total_frames = PMM_MAX_FRAMES;
    }

    /* Mark all frames in pmm_bitmap as used (0xFF) and pmm_free_frames = 0 */
    for (size_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }
    pmm_free_frames = 0;

    /* Determine reserved_boundary (protect 0 to at least max(_kernel_end + 4KB, 4MB)) */
    uint32_t reserved_boundary = 4 * 1024 * 1024;
    uint32_t kernel_end_addr = (uint32_t)&_kernel_end;
    if (kernel_end_addr >= KERNEL_VIRT_BASE) {
        kernel_end_addr -= KERNEL_VIRT_BASE;
    }
    if (kernel_end_addr + PAGE_SIZE > reserved_boundary) {
        reserved_boundary = (kernel_end_addr + PAGE_SIZE + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
    }

    int mmap_parsed = 0;
    if (mmap_addr != 0 && mmap_length != 0) {
        uint32_t offset = 0;
        while (offset < mmap_length && offset < 8192) {
            mmap_entry_t *entry = (mmap_entry_t *)(mmap_addr + offset);
            if (entry->size == 0) break;
            if (entry->type == 1) { /* Usable RAM */
                uint64_t start = entry->base_addr;
                uint64_t end = entry->base_addr + entry->length;
                uint32_t start_frame = (uint32_t)(start / PAGE_SIZE);
                uint32_t end_frame = (uint32_t)(end / PAGE_SIZE);
                if (end_frame > pmm_total_frames) end_frame = pmm_total_frames;
                for (uint32_t f = start_frame; f < end_frame; f++) {
                    if (f * PAGE_SIZE >= reserved_boundary && pmm_test_bit(f)) {
                        pmm_clear_bit(f);
                        pmm_free_frames++;
                    }
                }
            }
            offset += entry->size + sizeof(entry->size);
        }
        if (pmm_free_frames > 0) {
            mmap_parsed = 1;
        }
    }

    if (!mmap_parsed || pmm_free_frames == 0) {
        uint32_t start_frame = reserved_boundary / PAGE_SIZE;
        for (uint32_t frame = start_frame; frame < pmm_total_frames; frame++) {
            if (pmm_test_bit(frame)) {
                pmm_clear_bit(frame);
                pmm_free_frames++;
            }
        }
    }

    serial_printf(COM1_BASE, "[PMM] Initialized: %u total frames (%u MB), %u free frames (%u MB), reserved 0-%u KB, mmap=%s\n",
                  pmm_total_frames, (pmm_total_frames * PAGE_SIZE) / (1024 * 1024),
                  pmm_free_frames, (pmm_free_frames * PAGE_SIZE) / (1024 * 1024),
                  reserved_boundary / 1024,
                  mmap_parsed ? "E820" : "fallback");
}

uint32_t pmm_alloc_frame(void)
{
    if (pmm_free_frames == 0) {
        serial_puts(COM1_BASE, "[PMM] Error: Out of physical memory frames!\n");
        return 0;
    }

    for (uint32_t i = 0; i < pmm_total_frames; i++) {
        if (!pmm_test_bit(i)) {
            pmm_set_bit(i);
            pmm_free_frames--;
            return i * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_frame(uint32_t paddr)
{
    uint32_t frame = paddr / PAGE_SIZE;
    if (frame < pmm_total_frames) {
        if (pmm_test_bit(frame)) {
            pmm_clear_bit(frame);
            pmm_free_frames++;
        }
    }
}

uint32_t pmm_free_frames_count(void)
{
    return pmm_free_frames;
}

uint32_t pmm_total_frames_count(void)
{
    return pmm_total_frames;
}



/* Kernel Page Directory (aligned to 4KB) */
static uint32_t kernel_pagedir[PAGE_ENTRIES] __attribute__((aligned(4096)));

/* 32 Page Tables identity-mapping lower 128 MB (32 * 4MB = 128MB) */
#define KERNEL_IDENTITY_TABLES 32
static uint32_t kernel_pts[KERNEL_IDENTITY_TABLES][PAGE_ENTRIES] __attribute__((aligned(4096)));

/* 8 Page Tables identity-mapping PCI MMIO window (0xFE000000 to 0xFFFFFFFF, 32MB) */
#define MMIO_PDE_START 1016
#define MMIO_TABLES    8
static uint32_t mmio_pts[MMIO_TABLES][PAGE_ENTRIES] __attribute__((aligned(4096)));



void vmm_init(uint32_t mmap_addr, uint32_t mmap_length, uint32_t total_ram)
{
    /* 1. Initialize PMM */
    pmm_init(mmap_addr, mmap_length, total_ram);

    /* 2. Clear Kernel Page Directory */
    memset(kernel_pagedir, 0, sizeof(kernel_pagedir));

    /* 3. Map the first 128 MB using pre-allocated page tables into BOTH lower memory (0-128MB) and higher half (0xC0000000-0xC8000000) */
    for (uint32_t t = 0; t < KERNEL_IDENTITY_TABLES; t++) {
        for (uint32_t p = 0; p < PAGE_ENTRIES; p++) {
            uint32_t paddr = (t * PAGE_ENTRIES + p) * PAGE_SIZE;
            kernel_pts[t][p] = paddr | PAGE_PRESENT | PAGE_WRITE;
        }
        /* Connect table to directory in physical memory */
        uint32_t pt_paddr = (uint32_t)&kernel_pts[t][0];
        if (pt_paddr >= KERNEL_VIRT_BASE) pt_paddr -= KERNEL_VIRT_BASE;

        /* Lower identity mapping (0-128 MB) for direct hardware buffers */
        kernel_pagedir[t] = pt_paddr | PAGE_PRESENT | PAGE_WRITE;
        /* Higher-Half mapping (0xC0000000 - 0xC8000000) for kernel code/data/stacks/heap */
        kernel_pagedir[768 + t] = pt_paddr | PAGE_PRESENT | PAGE_WRITE;
    }

    /* 3.5. Identity-map PCI MMIO window (0xFE000000 - 0xFFFFFFFF, 32 MB) */
    for (uint32_t t = 0; t < MMIO_TABLES; t++) {
        uint32_t pde_idx = MMIO_PDE_START + t;
        for (uint32_t p = 0; p < PAGE_ENTRIES; p++) {
            uint32_t paddr = ((pde_idx * PAGE_ENTRIES) + p) * PAGE_SIZE;
            mmio_pts[t][p] = paddr | PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_PWT;
        }
        uint32_t mmio_pt_paddr = (uint32_t)&mmio_pts[t][0];
        if (mmio_pt_paddr >= KERNEL_VIRT_BASE) mmio_pt_paddr -= KERNEL_VIRT_BASE;
        kernel_pagedir[pde_idx] = mmio_pt_paddr | PAGE_PRESENT | PAGE_WRITE;
    }

    /* 4. Load CR3 with Kernel Page Directory physical address */
    uint32_t pd_paddr = (uint32_t)kernel_pagedir;
    if (pd_paddr >= KERNEL_VIRT_BASE) pd_paddr -= KERNEL_VIRT_BASE;
    asm volatile("mov %0, %%cr3" :: "r"(pd_paddr) : "memory");

    /* 5. Enable Paging in CR0: Bit 31 (PG) + Bit 16 (WP: Write Protect supervisor) */
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80010000;
    asm volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");

    serial_printf(COM1_BASE, "[VMM] Higher-Half Two-Tier Paging MMU enabled (CR0.PG=1, CR3=0x%x)\n", pd_paddr);
    serial_puts(COM1_BASE, "[VMM] Mapped Higher-Half 0xC0000000-0xC8000000 (Kernel code, data, BSS, heap)\n");
    serial_puts(COM1_BASE, "[VMM] Identity-mapped 0-128 MB (Hardware DMA, VGA 0xA0000/0xB8000)\n");
    serial_puts(COM1_BASE, "[VMM] Identity-mapped 0xFE000000-0xFFFFFFFF (PCI MMIO, E1000, AC97)\n");
}

uint32_t *vmm_get_kernel_pagedir(void)
{
    return kernel_pagedir;
}

int vmm_map_page(uint32_t *pagedir, uint32_t vaddr, uint32_t paddr, uint32_t flags)
{
    if (!pagedir) pagedir = kernel_pagedir;

    uint32_t pde_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;

    uint32_t *pt = NULL;

    if (!(pagedir[pde_idx] & PAGE_PRESENT)) {
        /* Allocate a new page table frame from PMM */
        uint32_t pt_frame = pmm_alloc_frame();
        if (!pt_frame) {
            serial_puts(COM1_BASE, "[VMM] Error: Failed to allocate frame for page table!\n");
            return -1;
        }
        memset((void *)pt_frame, 0, PAGE_SIZE);
        pagedir[pde_idx] = pt_frame | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        pt = (uint32_t *)pt_frame;
    } else {
        if (flags & PAGE_USER) {
            pagedir[pde_idx] |= PAGE_USER;
        }
        pt = (uint32_t *)(pagedir[pde_idx] & ~0xFFF);
    }

    pt[pte_idx] = (paddr & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;

    /* Invalidate TLB */
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}

int vmm_unmap_page(uint32_t *pagedir, uint32_t vaddr)
{
    if (!pagedir) pagedir = kernel_pagedir;

    uint32_t pde_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;

    if (!(pagedir[pde_idx] & PAGE_PRESENT)) {
        return -1;
    }

    uint32_t *pt = (uint32_t *)(pagedir[pde_idx] & ~0xFFF);
    pt[pte_idx] = 0;

    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    return 0;
}

int vmm_map_range(uint32_t *pagedir, uint32_t vaddr_start, uint32_t paddr_start, size_t size, uint32_t flags)
{
    uint32_t v = vaddr_start & ~0xFFF;
    uint32_t p = paddr_start & ~0xFFF;
    size_t count = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < count; i++) {
        if (vmm_map_page(pagedir, v + i * PAGE_SIZE, p + i * PAGE_SIZE, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

int vmm_map_mmio(uint32_t paddr, size_t size)
{
    /* MMIO mapped identity-wise with cache disable & write-through */
    return vmm_map_range(kernel_pagedir, paddr, paddr, size, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_PWT);
}

uint32_t vmm_get_physical_address(uint32_t *pagedir, uint32_t vaddr)
{
    if (!pagedir) pagedir = kernel_pagedir;

    uint32_t pde_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;

    if (!(pagedir[pde_idx] & PAGE_PRESENT)) return 0;

    uint32_t *pt = (uint32_t *)(pagedir[pde_idx] & ~0xFFF);
    if (!(pt[pte_idx] & PAGE_PRESENT)) return 0;

    return (pt[pte_idx] & ~0xFFF) | (vaddr & 0xFFF);
}

uint32_t *vmm_create_user_pagedir(void)
{
    uint32_t pd_frame = pmm_alloc_frame();
    if (!pd_frame) return NULL;

    uint32_t *user_pd = (uint32_t *)pd_frame;
    memset(user_pd, 0, PAGE_SIZE);

    /* 1. Identity-map physical memory (0-128 MB: entries 0 to 31) for kernel supervisor access (VGA, DMA, kstack) */
    /* Note: PAGE_USER is NOT set, so Ring 3 access is forbidden and will fault */
    for (int i = 0; i < KERNEL_IDENTITY_TABLES; i++) {
        if (kernel_pagedir[i] & PAGE_PRESENT) {
            user_pd[i] = kernel_pagedir[i];
        }
    }

    /* 2. Clone Higher-Half Kernel mappings (PDE entries 768 to 1023) so syscalls & ISRs are active */
    /* Note: All kernel entries have PAGE_USER = 0 (Supervisor only!) */
    for (int i = 768; i < PAGE_ENTRIES; i++) {
        if (kernel_pagedir[i] & PAGE_PRESENT) {
            user_pd[i] = kernel_pagedir[i];
        }
    }

    /* Entries 32 to 767 are completely ZERO, reserving 128 MB to 3 GB exclusively for isolated Ring 3 */
    return user_pd;
}

void vmm_destroy_user_pagedir(uint32_t *pagedir)
{
    if (!pagedir || pagedir == kernel_pagedir) return;

    /* Free all user-allocated pages (entries 32 to 767 only, preserving kernel identity 0-31) */
    for (int i = KERNEL_IDENTITY_TABLES; i < 768; i++) {
        if ((pagedir[i] & PAGE_PRESENT) && (pagedir[i] & PAGE_USER)) {
            uint32_t *pt = (uint32_t *)(pagedir[i] & ~0xFFF);
            for (int j = 0; j < PAGE_ENTRIES; j++) {
                if (pt[j] & PAGE_PRESENT) {
                    pmm_free_frame(pt[j] & ~0xFFF);
                    pt[j] = 0;
                }
            }
            pmm_free_frame((uint32_t)pt);
            pagedir[i] = 0;
        }
    }

    pmm_free_frame((uint32_t)pagedir);
}

void vmm_switch_pagedir(uint32_t *pagedir)
{
    if (!pagedir) pagedir = kernel_pagedir;
    uint32_t paddr = (uint32_t)pagedir;
    if (paddr >= KERNEL_VIRT_BASE) paddr -= KERNEL_VIRT_BASE;
    asm volatile("mov %0, %%cr3" :: "r"(paddr) : "memory");
}



void page_fault_handler(uint32_t err_code, uint32_t eip)
{
    uint32_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

    serial_puts(COM1_BASE, "\n=======================================================\n");
    serial_puts(COM1_BASE, " [PAGE FAULT] CPU Vector 14 Intercepted!\n");
    serial_printf(COM1_BASE, " Faulting Address (CR2): 0x%x | EIP: 0x%x | Err: 0x%x\n",
                  fault_addr, eip, err_code);
    serial_printf(COM1_BASE, " Reason: [%s] [%s] [%s]\n",
                  (err_code & PF_ERR_PRESENT) ? "Protection Violation" : "Page Not Present",
                  (err_code & PF_ERR_WRITE) ? "Write Access" : "Read Access",
                  (err_code & PF_ERR_USER) ? "User Mode (Ring 3)" : "Supervisor Mode (Ring 0)");
    serial_puts(COM1_BASE, "=======================================================\n");

    task_t *cur = task_get_current();
    if ((err_code & PF_ERR_USER) || (cur && cur->is_user)) {
        serial_printf(COM1_BASE, "[SHIELD] Segmentation fault in user task '%s' (PID %u) at 0x%x. Safely terminating task.\n",
                      cur ? cur->name : "unknown", cur ? cur->pid : 0, fault_addr);
        if (cur && cur->pid > 0) {
            task_kill(cur->pid);
            task_yield();
            return;
        }
    }

    /* Auto on-demand map MMIO accesses (PCI/APIC/AC97/E1000) above 0xE0000000 in kernel mode */
    if (!(err_code & PF_ERR_USER) && fault_addr >= 0xE0000000) {
        serial_printf(COM1_BASE, "[VMM] Auto on-demand mapping MMIO page 0x%x for kernel driver\n", fault_addr & ~0xFFF);
        vmm_map_page(kernel_pagedir, fault_addr & ~0xFFF, fault_addr & ~0xFFF, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_PWT);
        return;
    }

    serial_puts(COM1_BASE, "[VMM] Fatal Kernel Page Fault! System halted safely.\n");
    for (;;) {
        asm volatile("cli; hlt");
    }
}
