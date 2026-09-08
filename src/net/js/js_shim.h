#ifndef ARCHA_JS_SHIM_H
#define ARCHA_JS_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "string.h"

/* jmp_buf definition for x86 (ebx, esi, edi, ebp, esp, eip) */
typedef uint32_t jmp_buf[6];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

/* Standard C math shims using x87 FPU */
double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double pow(double x, double y);
double cbrt(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double hypot(double x, double y);
double trunc(double x);
double round(double x);

int isnan(double x);
int isinf(double x);
int signbit(double x);

/* Standard string / memory / stdio helpers */
void *malloc(size_t sz);
void free(void *ptr);
void *realloc(void *ptr, size_t sz);
void *calloc(size_t num, size_t sz);

int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int sscanf(const char *str, const char *format, ...);
int vsscanf(const char *str, const char *format, va_list ap);

void abort(void);
void exit(int status);

#endif /* ARCHA_JS_SHIM_H */
