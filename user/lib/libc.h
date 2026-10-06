#ifndef USER_LIBC_H
#define USER_LIBC_H

#include <stdint.h>
#include <stddef.h>
#include "syscall.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifndef EOF
#define EOF (-1)
#endif

typedef struct {
    int fd;
    int eof;
    int error;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* POSIX-like System Call Wrappers */
void     exit(int status);
int      read(int fd, void *buf, size_t count);
int      write(int fd, const void *buf, size_t count);
int      open(const char *path, int flags);
int      close(int fd);
int      lseek(int fd, long offset, int whence);
void     sleep(uint32_t ms);
void     yield(void);
int      getpid(void);
int      spawn(const char *path);
void    *sbrk(intptr_t incr);
uint32_t time_ticks(void);

/* Standard Stream I/O */
FILE    *fopen(const char *path, const char *mode);
size_t   fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t   fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int      fseek(FILE *stream, long offset, int whence);
long     ftell(FILE *stream);
int      fclose(FILE *stream);
int      feof(FILE *stream);
int      ferror(FILE *stream);
int      fileno(FILE *stream);
void     rewind(FILE *stream);
int      fflush(FILE *stream);
int      fgetc(FILE *stream);
char    *fgets(char *s, int size, FILE *stream);
int      fputs(const char *s, FILE *stream);

/* Dynamic Memory Allocation */
void    *malloc(size_t size);
void    *calloc(size_t nmemb, size_t size);
void    *realloc(void *ptr, size_t size);
void     free(void *ptr);
void     malloc_stats(void);

/* String & Char Utilities */
size_t   strlen(const char *s);
int      strcmp(const char *s1, const char *s2);
int      strncmp(const char *s1, const char *s2, size_t n);
int      strcasecmp(const char *s1, const char *s2);
int      strncasecmp(const char *s1, const char *s2, size_t n);
char    *strcpy(char *dest, const char *src);
char    *strncpy(char *dest, const char *src, size_t n);
char    *strcat(char *dest, const char *src);
char    *strncat(char *dest, const char *src, size_t n);
char    *strchr(const char *s, int c);
char    *strrchr(const char *s, int c);
char    *strstr(const char *haystack, const char *needle);
char    *strdup(const char *s);
void    *memset(void *s, int c, size_t n);
void    *memcpy(void *dest, const void *src, size_t n);
void    *memmove(void *dest, const void *src, size_t n);
int      memcmp(const void *s1, const void *s2, size_t n);
int      atoi(const char *str);
double   atof(const char *str);
char    *itoa(int value, char *str, int base);
int      toupper(int c);
int      tolower(int c);
int      abs(int n);
char    *getenv(const char *name);
int      system(const char *command);
int      rand(void);
void     srand(unsigned int seed);
void     qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

/* Standard I/O */
int      putchar(char c);
int      puts(const char *str);
int      getchar(void);
int      printf(const char *fmt, ...);
int      fprintf(FILE *stream, const char *fmt, ...);
int      vfprintf(FILE *stream, const char *fmt, __builtin_va_list ap);
int      vprintf(const char *fmt, __builtin_va_list ap);
int      sprintf(char *str, const char *fmt, ...);
int      snprintf(char *str, size_t size, const char *fmt, ...);
int      vsnprintf(char *str, size_t size, const char *fmt, __builtin_va_list ap);
int      vsprintf(char *str, const char *fmt, __builtin_va_list ap);
int      sscanf(const char *str, const char *fmt, ...);
int      remove(const char *pathname);
int      rename(const char *oldpath, const char *newpath);

#endif /* USER_LIBC_H */
