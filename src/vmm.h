#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE           4096
#define PAGE_ENTRIES        1024
#define KERNEL_VIRT_BASE    0xC0000000   /* 3 GB Higher-Half Virtual Base */

/* Page Table / Directory Entry Flags */
#define PAGE_PRESENT        (1 << 0)   /* 0x01: Page is present in memory */
#define PAGE_WRITE          (1 << 1)   /* 0x02: Read/Write (0 = Read-Only) */
#define PAGE_USER           (1 << 2)   /* 0x04: User Mode (0 = Supervisor only) */
#define PAGE_PWT            (1 << 3)   /* 0x08: Page-level Write-Through */
#define PAGE_PCD            (1 << 4)   /* 0x10: Page-level Cache Disable */
#define PAGE_ACCESSED       (1 << 5)   /* 0x20: Accessed */
#define PAGE_DIRTY          (1 << 6)   /* 0x40: Dirty (PTE only) */

/* Page Fault Error Code Flags */
#define PF_ERR_PRESENT      (1 << 0)   /* 0 = Not Present, 1 = Protection Violation */
#define PF_ERR_WRITE        (1 << 1)   /* 0 = Read, 1 = Write */
#define PF_ERR_USER         (1 << 2)   /* 0 = Supervisor, 1 = User Mode */
#define PF_ERR_RESERVED     (1 << 3)   /* 1 = Overwritten CPU-reserved bits */
#define PF_ERR_INST_FETCH   (1 << 4)   /* 1 = Instruction Fetch violation */

/* Physical Memory Manager (PMM) API */
void     pmm_init(uint32_t mmap_addr, uint32_t mmap_length, uint32_t total_ram);
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t paddr);
uint32_t pmm_free_frames_count(void);
uint32_t pmm_total_frames_count(void);

/* Virtual Memory Manager (VMM) API */
void      vmm_init(uint32_t mmap_addr, uint32_t mmap_length, uint32_t total_ram);
uint32_t *vmm_get_kernel_pagedir(void);
int       vmm_map_page(uint32_t *pagedir, uint32_t vaddr, uint32_t paddr, uint32_t flags);
int       vmm_unmap_page(uint32_t *pagedir, uint32_t vaddr);
int       vmm_map_range(uint32_t *pagedir, uint32_t vaddr_start, uint32_t paddr_start, size_t size, uint32_t flags);
int       vmm_map_mmio(uint32_t paddr, size_t size);
uint32_t  vmm_get_physical_address(uint32_t *pagedir, uint32_t vaddr);
uint32_t *vmm_create_user_pagedir(void);
void      vmm_destroy_user_pagedir(uint32_t *pagedir);
void      vmm_switch_pagedir(uint32_t *pagedir);

/* Exception Handler Hook */
void      page_fault_handler(uint32_t err_code, uint32_t eip);

#endif /* VMM_H */
