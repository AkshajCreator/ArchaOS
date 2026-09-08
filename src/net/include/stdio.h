#ifndef STDIO_H
#define STDIO_H
#include "../netsurf_shim.h"
#define FILE void
#define stdout ((FILE*)1)
#define stderr ((FILE*)2)
#define stdin  ((FILE*)0)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static inline void *fopen(const char *p, const char *m) { (void)p; (void)m; return NULL; }
static inline int fseek(void *f, long o, int w) { (void)f; (void)o; (void)w; return -1; }
static inline long ftell(void *f) { (void)f; return -1; }
static inline size_t fread(void *p, size_t s, size_t n, void *f) { (void)p; (void)s; (void)n; (void)f; return 0; }
static inline int fclose(void *f) { (void)f; return 0; }

#endif
