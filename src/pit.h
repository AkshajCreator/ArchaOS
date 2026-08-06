// src/pit.h
#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void     pit_init(void);
void     pit_sleep(uint32_t ms);
uint32_t pit_ticks(void);

extern volatile uint32_t pit_tick_count;

#endif
