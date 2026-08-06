// src/mm.c

#include "mm.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * CONFIGURATION
 * ============================================================ */

/* Static array is 8MB — safe for -m 32 and above.
 *  We use detected RAM to decide how much of it to actually hand out,
 *  so meminfo reflects real RAM without blowing GRUB's loader. */
#define HEAP_MAX  ( 8 * 1024 * 1024)
#define HEAP_MIN  ( 1 * 1024 * 1024)
#define ALIGN     8

/* ============================================================
 * HEAP STORAGE
 * ============================================================ */

static uint8_t heap[HEAP_MAX] __attribute__((aligned(ALIGN)));

/* ============================================================
 * BLOCK HEADER
 * ============================================================ */

typedef struct block
{
    size_t        size;
    uint32_t      free;
    struct block *next;
    uint32_t      padding;  /* Pad struct to 16 bytes for 8-byte alignment */
} block_t;

#define HEADER_SIZE (sizeof(block_t))

/* ============================================================
 * STATE
 * ============================================================ */

static block_t *heap_head   = 0;
static size_t   heap_size   = 0;
static int      mm_ready    = 0;
uint32_t        mm_total_ram = 0;  /* exported — readable by kernel */

/* ============================================================
 * HELPERS
 * ============================================================ */

static size_t align_up(size_t n)
{
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

/* ============================================================
 * MM_INIT
 * detected_ram_bytes: total usable RAM from multiboot map
 * We use half of it for the heap, capped at HEAP_MAX
 * ============================================================ */

void mm_init(uint32_t detected_ram_bytes)
{
    mm_total_ram = detected_ram_bytes;

    /* Use detected RAM but clamp to our static array size.
     *      This way meminfo shows real RAM while we stay within bounds. */
    size_t want = detected_ram_bytes;
    if (want < HEAP_MIN) want = HEAP_MIN;
    if (want > HEAP_MAX) want = HEAP_MAX;

    heap_size = want;

    heap_head       = (block_t *)heap;
    heap_head->size = heap_size - HEADER_SIZE;
    heap_head->free = 1;
    heap_head->next = 0;
    mm_ready        = 1;
}

/* ============================================================
 * SPLIT
 * ============================================================ */

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

/* ============================================================
 * COALESCE
 * ============================================================ */

static void coalesce(void)
{
    block_t *cur = heap_head;
    while (cur && cur->next)
    {
        if (cur->free && cur->next->free)
        {
            cur->size += HEADER_SIZE + cur->next->size;
            cur->next  = cur->next->next;
        }
        else
            cur = cur->next;
    }
}

/* ============================================================
 * KMALLOC / KFREE
 * ============================================================ */

void *kmalloc(size_t size)
{
    if (!mm_ready || size == 0) return 0;

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
    return 0;
}

void kfree(void *ptr)
{
    if (!ptr || !mm_ready) return;
    if ((uint8_t *)ptr < heap || (uint8_t *)ptr >= heap + heap_size) return;

    block_t *blk = (block_t *)((uint8_t *)ptr - HEADER_SIZE);
    blk->free = 1;
    coalesce();
}

/* ============================================================
 * STATS
 * ============================================================ */

mm_stats_t mm_stats(void)
{
    mm_stats_t s = {0,0,0,0,0};
    s.total = heap_size;

    block_t *cur = heap_head;
    while (cur)
    {
        if (cur->free) { s.free += cur->size; s.blocks_free++; }
        else           { s.used += cur->size; s.blocks_used++; }
        cur = cur->next;
    }
    return s;
}
