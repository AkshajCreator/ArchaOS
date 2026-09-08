#ifndef NETSURF_SHIM_H
#define NETSURF_SHIM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory management */
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

/* Memory manipulation */
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

/* String manipulation */
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strpbrk(const char *s, const char *accept);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strtok_r(char *str, const char *delim, char **saveptr);

/* Formatted output / input */
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int sscanf(const char *str, const char *format, ...);
int printf(const char *format, ...);
int puts(const char *s);
int putchar(int c);

/* Character classification & conversion */
int isspace(int c);
int isalpha(int c);
int isdigit(int c);
int isalnum(int c);
int isxdigit(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int isgraph(int c);
int ispunct(int c);
int tolower(int c);
int toupper(int c);

/* Utility & Sort */
int abs(int j);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *));
void abort(void);
void exit(int status);

#ifdef __cplusplus
}
#endif

#endif /* NETSURF_SHIM_H */
