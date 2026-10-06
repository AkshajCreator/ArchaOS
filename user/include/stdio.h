#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef EOF
#define EOF (-1)
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct {
    int fd;
    int eof;
    int error;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

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
void     clearerr(FILE *stream);

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, __builtin_va_list ap);
int sprintf(char *str, const char *fmt, ...);
int vsprintf(char *str, const char *fmt, __builtin_va_list ap);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, __builtin_va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int remove(const char *pathname);
int rename(const char *oldpath, const char *newpath);
int puts(const char *s);
int putchar(char c);
int getchar(void);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
