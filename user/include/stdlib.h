#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);
void  exit(int status);
int   atoi(const char *str);
long  strtol(const char *nptr, char **endptr, int base);
double atof(const char *str);
char *itoa(int value, char *str, int base);
int   abs(int n);
char *getenv(const char *name);
int   system(const char *command);
int   rand(void);
void  srand(unsigned int seed);
void  qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void  malloc_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
