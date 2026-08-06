// src/mm.h
#ifndef MM_H
#define MM_H

#include <stdint.h>
#include <stddef.h>

/* Call once at boot with total detected RAM in bytes */
void  mm_init(uint32_t detected_ram_bytes);
void *kmalloc(size_t size);
void  kfree(void *ptr);

/* Detected RAM (set by mm_init, readable by kernel) */
extern uint32_t mm_total_ram;

typedef struct {
    size_t total;
    size_t used;
    size_t free;
    size_t blocks_used;
    size_t blocks_free;
} mm_stats_t;

mm_stats_t mm_stats(void);

#endif
