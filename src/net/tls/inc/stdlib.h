// src/net/tls/inc/stdlib.h — Minimal freestanding stdlib.h for BearSSL in ArchaOS
#ifndef STDLIB_H
#define STDLIB_H

#define _MM_MALLOC_H_INCLUDED 1

#include <stddef.h>
#include <stdint.h>

static inline void *malloc(size_t sz) { (void)sz; return NULL; }
static inline void free(void *ptr) { (void)ptr; }

#endif /* STDLIB_H */
