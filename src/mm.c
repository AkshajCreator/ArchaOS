// src/mm.c

#include "mm.h"
#include "vmm.h"
#include <stdint.h>
#include <stddef.h>

#define ALIGN 8

#ifndef HEAP_VIRT_BASE
#define HEAP_VIRT_BASE 0xC2000000
#endif

typedef struct block
{
    size_t        size;
    uint32_t      free;
    struct block *next;
    uint32_t      padding;  /* Pad struct to 16 bytes for 8-byte alignment */
} block_t;

#define HEADER_SIZE (sizeof(block_t))

static uint8_t *heap_base      = (uint8_t *)HEAP_VIRT_BASE;
static size_t   heap_allocated = 0;
static size_t   heap_capacity  = 64 * 1024 * 1024;
static block_t *heap_head      = 0;
static int      mm_ready       = 0;
uint32_t        mm_total_ram   = 0;  /* exported — readable by kernel */

void *mm_get_heap_base(void)
{
    return (void *)heap_base;
}

static size_t align_up(size_t n)
{
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

static void split(block_t *blk, size_t size)
{
    if (blk->size <= size + HEADER_SIZE + ALIGN)
        return;

    block_t *nb = (block_t *)((uint8_t *)blk + HEADER_SIZE + size);
    nb->size    = blk->size - size - HEADER_SIZE;
    nb->free    = 1;
    nb->next    = blk->next;

    blk->size = size;
    blk->next = nb;
}

static void coalesce(void)
{
    block_t *cur = heap_head;
    while (cur && cur->next)
    {
        if (cur->free && cur->next->free &&
            (uint8_t *)cur + HEADER_SIZE + cur->size == (uint8_t *)cur->next)
        {
            cur->size += HEADER_SIZE + cur->next->size;
            cur->next  = cur->next->next;
        }
        else
        {
            cur = cur->next;
        }
    }
}

static int mm_expand_heap(size_t min_bytes)
{
    size_t needed = min_bytes + HEADER_SIZE + ALIGN;
    if (needed < 64 * 1024) {
        needed = 64 * 1024;
    }
    size_t pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;

    if (heap_allocated + (pages * PAGE_SIZE) > heap_capacity) {
        if (heap_allocated >= heap_capacity) {
            return -1;
        }
        pages = (heap_capacity - heap_allocated) / PAGE_SIZE;
        if (pages * PAGE_SIZE < min_bytes + HEADER_SIZE) {
            return -1;
        }
    }

    uint8_t *region_start = heap_base + heap_allocated;
    size_t pages_allocated = 0;

    for (size_t i = 0; i < pages; i++) {
        uint32_t paddr = pmm_alloc_frame();
        if (!paddr) {
            break;
        }
        if (vmm_map_page(NULL, (uint32_t)(heap_base + heap_allocated), paddr, PAGE_PRESENT | PAGE_WRITE) != 0) {
            pmm_free_frame(paddr);
            break;
        }
        heap_allocated += PAGE_SIZE;
        pages_allocated++;
    }

    if (pages_allocated * PAGE_SIZE < HEADER_SIZE + ALIGN) {
        return -1;
    }

    size_t mapped_bytes = pages_allocated * PAGE_SIZE;
    block_t *new_block = (block_t *)region_start;
    new_block->size = mapped_bytes - HEADER_SIZE;
    new_block->free = 1;
    new_block->next = 0;

    if (!heap_head) {
        heap_head = new_block;
    } else {
        block_t *cur = heap_head;
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = new_block;
    }

    coalesce();
    return 0;
}

void mm_init(uint32_t detected_ram_bytes)
{
    mm_total_ram = detected_ram_bytes;
    mm_expand_heap(1024 * 1024);
    mm_ready = 1;
}

void *kmalloc(size_t size)
{
    if (!mm_ready || size == 0 || size > (heap_capacity - HEADER_SIZE)) return 0;

    size = align_up(size);
    block_t *cur = heap_head;

    while (cur)
    {
        if (cur->free && cur->size >= size)
        {
            split(cur, size);
            cur->free = 0;
            return (void *)((uint8_t *)cur + HEADER_SIZE);
        }
        cur = cur->next;
    }

    /* Free list cannot satisfy size, expand heap */
    if (mm_expand_heap(size) == 0)
    {
        cur = heap_head;
        while (cur)
        {
            if (cur->free && cur->size >= size)
            {
                split(cur, size);
                cur->free = 0;
                return (void *)((uint8_t *)cur + HEADER_SIZE);
            }
            cur = cur->next;
        }
    }

    return 0;
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }
    if (!mm_ready) return 0;
    if ((uint8_t *)ptr < heap_base + HEADER_SIZE || (uint8_t *)ptr >= heap_base + heap_allocated) return 0;

    block_t *blk = (block_t *)((uint8_t *)ptr - HEADER_SIZE);
    size_t old_size = blk->size;
    if (new_size <= old_size) return ptr;

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;

    uint8_t *s = (uint8_t *)ptr;
    uint8_t *d = (uint8_t *)new_ptr;
    for (size_t i = 0; i < old_size; i++) d[i] = s[i];

    kfree(ptr);
    return new_ptr;
}

void kfree(void *ptr)
{
    if (!ptr || !mm_ready) return;
    if ((uint8_t *)ptr < heap_base + HEADER_SIZE || (uint8_t *)ptr >= heap_base + heap_allocated) return;

    block_t *blk = (block_t *)((uint8_t *)ptr - HEADER_SIZE);
    if (blk->free) return;
    blk->free = 1;
    coalesce();
}

mm_stats_t mm_stats(void)
{
    mm_stats_t s = {0,0,0,0,0};
    s.total = heap_allocated;

    block_t *cur = heap_head;
    while (cur)
    {
        if (cur->free) { s.free += cur->size; s.blocks_free++; }
        else           { s.used += cur->size; s.blocks_used++; }
        cur = cur->next;
    }
    return s;
}
