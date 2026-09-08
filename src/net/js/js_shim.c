#include "js_shim.h"
#include "../../mm.h"
#include "../../serial.h"

/* setjmp and longjmp assembly implementation for 32-bit x86 */
__attribute__((naked)) int setjmp(__attribute__((unused)) jmp_buf env) {
    __asm__ volatile (
        "movl 4(%esp), %edx\n"
        "movl %ebx, 0(%edx)\n"
        "movl %esi, 4(%edx)\n"
        "movl %edi, 8(%edx)\n"
        "movl %ebp, 12(%edx)\n"
        "leal 4(%esp), %ecx\n"
        "movl %ecx, 16(%edx)\n"
        "movl (%esp), %ecx\n"
        "movl %ecx, 20(%edx)\n"
        "xorl %eax, %eax\n"
        "ret\n"
    );
}

__attribute__((naked)) void longjmp(__attribute__((unused)) jmp_buf env, __attribute__((unused)) int val) {
    __asm__ volatile (
        "movl 4(%esp), %edx\n"
        "movl 8(%esp), %eax\n"
        "testl %eax, %eax\n"
        "jnz 1f\n"
        "incl %eax\n"
        "1:\n"
        "movl 0(%edx), %ebx\n"
        "movl 4(%edx), %esi\n"
        "movl 8(%edx), %edi\n"
        "movl 12(%edx), %ebp\n"
        "movl 16(%edx), %esp\n"
        "jmp *20(%edx)\n"
    );
}

/* Math shims using x87 FPU */
double fabs(double x) {
    double res;
    __asm__ volatile ("fabs" : "=t"(res) : "0"(x));
    return res;
}

double floor(double x) {
    uint16_t old_cw, new_cw;
    double res;
    __asm__ volatile ("fnstcw %0" : "=m"(old_cw));
    new_cw = (old_cw & 0xF3FF) | 0x0400; /* Round down */
    __asm__ volatile ("fldcw %0" : : "m"(new_cw));
    __asm__ volatile ("frndint" : "=t"(res) : "0"(x));
    __asm__ volatile ("fldcw %0" : : "m"(old_cw));
    return res;
}

double ceil(double x) {
    uint16_t old_cw, new_cw;
    double res;
    __asm__ volatile ("fnstcw %0" : "=m"(old_cw));
    new_cw = (old_cw & 0xF3FF) | 0x0800; /* Round up */
    __asm__ volatile ("fldcw %0" : : "m"(new_cw));
    __asm__ volatile ("frndint" : "=t"(res) : "0"(x));
    __asm__ volatile ("fldcw %0" : : "m"(old_cw));
    return res;
}

double sqrt(double x) {
    double res;
    __asm__ volatile ("fsqrt" : "=t"(res) : "0"(x));
    return res;
}

double sin(double x) {
    double res;
    __asm__ volatile ("fsin" : "=t"(res) : "0"(x));
    return res;
}

double cos(double x) {
    double res;
    __asm__ volatile ("fcos" : "=t"(res) : "0"(x));
    return res;
}

double tan(double x) {
    double res;
    __asm__ volatile ("fptan\n\tfstp %%st(0)" : "=t"(res) : "0"(x));
    return res;
}

double atan(double x) {
    double res;
    __asm__ volatile ("fld1\n\tfpatan" : "=t"(res) : "0"(x));
    return res;
}

double atan2(double y, double x) {
    double res;
    __asm__ volatile ("fpatan" : "=t"(res) : "0"(x), "u"(y) : "st(1)");
    return res;
}

double asin(double x) {
    if (x >= 1.0) return 1.5707963267948966;
    if (x <= -1.0) return -1.5707963267948966;
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x >= 1.0) return 0.0;
    if (x <= -1.0) return 3.1415926535897932;
    return atan2(sqrt(1.0 - x * x), x);
}

double fmod(double x, double y) {
    double res;
    __asm__ volatile ("1: fprem\n\tfnstsw %%ax\n\tsahf\n\tjp 1b" : "=t"(res) : "0"(x), "u"(y) : "ax", "st(1)");
    return res;
}

double exp(double x) {
    /* 2^(x * log2(e)) */
    double res;
    __asm__ volatile (
        "fldl2e\n\t"
        "fmulp\n\t"
        "fld %%st(0)\n\t"
        "frndint\n\t"
        "fsub %%st(0), %%st(1)\n\t"
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)"
        : "=t"(res) : "0"(x)
    );
    return res;
}

double log(double x) {
    double res;
    __asm__ volatile ("fldln2\n\tfxch\n\tfyl2x" : "=t"(res) : "0"(x) : "st(1)");
    return res;
}

double log2(double x) {
    return log(x) / 0.6931471805599453;
}

double log10(double x) {
    return log(x) / 2.302585092994046;
}

double cbrt(double x) {
    if (x == 0.0) return 0.0;
    if (x < 0.0) return -pow(-x, 1.0 / 3.0);
    return pow(x, 1.0 / 3.0);
}

double sinh(double x) {
    return (exp(x) - exp(-x)) / 2.0;
}

double cosh(double x) {
    return (exp(x) + exp(-x)) / 2.0;
}

double tanh(double x) {
    double ex = exp(x);
    double emx = exp(-x);
    return (ex - emx) / (ex + emx);
}

double asinh(double x) {
    return log(x + sqrt(x * x + 1.0));
}

double acosh(double x) {
    return log(x + sqrt(x * x - 1.0));
}

double atanh(double x) {
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

double hypot(double x, double y) {
    return sqrt(x * x + y * y);
}

double trunc(double x) {
    return (x < 0.0) ? ceil(x) : floor(x);
}

double round(double x) {
    return floor(x + 0.5);
}

double pow(double x, double y) {
    if (x <= 0.0 && (double)(int)y == y) {
        /* Integer power of negative base */
        int exp_i = (int)y;
        double r = 1.0;
        double b = x;
        int n = exp_i < 0 ? -exp_i : exp_i;
        while (n > 0) {
            if (n & 1) r *= b;
            b *= b;
            n >>= 1;
        }
        return (exp_i < 0) ? (1.0 / r) : r;
    }
    return exp(y * log(x));
}

int isnan(double x) {
    union { double d; uint64_t u; } un;
    un.d = x;
    return ((un.u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) && ((un.u & 0x000FFFFFFFFFFFFFULL) != 0);
}

int isinf(double x) {
    union { double d; uint64_t u; } un;
    un.d = x;
    return ((un.u & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL);
}

int signbit(double x) {
    union { double d; uint64_t u; } un;
    un.d = x;
    return (int)(un.u >> 63);
}

/* Memory allocation shims */
void *malloc(size_t sz) {
    return kmalloc((uint32_t)sz);
}

void free(void *ptr) {
    if (ptr) kfree(ptr);
}

void *realloc(void *ptr, size_t sz) {
    return krealloc(ptr, sz);
}

void *calloc(size_t num, size_t sz) {
    size_t total = num * sz;
    void *ptr = kmalloc((uint32_t)total);
    if (ptr) {
        uint8_t *b = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++) b[i] = 0;
    }
    return ptr;
}

void abort(void) {
    serial_printf(COM1_BASE, "[JS] Abort triggered!\n");
    while (1) { __asm__ volatile ("hlt"); }
}

void exit(int status) {
    serial_printf(COM1_BASE, "[JS] Exit called with status %d\n", status);
    abort();
}

/* sprintf / snprintf helpers */
int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, 1024, format, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!str || size == 0) return 0;
    char *out = str;
    size_t left = size - 1;

    while (*format && left > 0) {
        if (*format != '%') {
            *out++ = *format++;
            left--;
            continue;
        }
        format++; /* skip '%' */

        int precision = -1;
        int width = 0;
        int pad_zero = 0;

        if (*format == '0') {
            pad_zero = 1;
            format++;
        }

        /* Width (digits or '*') */
        if (*format == '*') {
            width = va_arg(ap, int);
            format++;
        } else {
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + (*format - '0');
                format++;
            }
        }

        /* Precision */
        if (*format == '.') {
            format++;
            if (*format == '*') {
                precision = va_arg(ap, int);
                format++;
            } else {
                precision = 0;
                while (*format >= '0' && *format <= '9') {
                    precision = precision * 10 + (*format - '0');
                    format++;
                }
            }
        }

        /* Length specifiers (l, z, h) */
        while (*format == 'l' || *format == 'z' || *format == 'h') {
            format++;
        }

        if (*format == 's') {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = 0;
            while (s[slen]) slen++;
            if (precision >= 0 && precision < slen) slen = precision;
            for (int i = 0; i < slen && left > 0; i++) { *out++ = s[i]; left--; }
        } else if (*format == 'd' || *format == 'i') {
            int v = va_arg(ap, int);
            char num[16]; int np = 0;
            int neg = (v < 0);
            if (neg) v = -v;
            if (v == 0) num[np++] = '0';
            while (v > 0) { num[np++] = '0' + (v % 10); v /= 10; }
            if (neg) num[np++] = '-';
            while (np < width && pad_zero && np < 15) num[np++] = '0';
            for (int i = np - 1; i >= 0 && left > 0; i--) { *out++ = num[i]; left--; }
        } else if (*format == 'u') {
            unsigned int v = va_arg(ap, unsigned int);
            char num[16]; int np = 0;
            if (v == 0) num[np++] = '0';
            while (v > 0) { num[np++] = '0' + (v % 10); v /= 10; }
            while (np < width && pad_zero && np < 15) num[np++] = '0';
            for (int i = np - 1; i >= 0 && left > 0; i--) { *out++ = num[i]; left--; }
        } else if (*format == 'x' || *format == 'X' || *format == 'p') {
            unsigned int v = va_arg(ap, unsigned int);
            char num[16]; int np = 0;
            static const char hex[] = "0123456789abcdef";
            if (v == 0) num[np++] = '0';
            while (v > 0) { num[np++] = hex[v & 0xF]; v >>= 4; }
            while (np < width && pad_zero && np < 15) num[np++] = '0';
            for (int i = np - 1; i >= 0 && left > 0; i--) { *out++ = num[i]; left--; }
        } else if (*format == 'f' || *format == 'g') {
            double d = va_arg(ap, double);
            if (d < 0) { if (left > 0) { *out++ = '-'; left--; } d = -d; }
            int ip = (int)d;
            int fp = (int)((d - ip) * 100.0);
            char buf[32];
            int bp = snprintf(buf, sizeof(buf), "%d.%02d", ip, fp < 0 ? -fp : fp);
            for (int i = 0; i < bp && left > 0; i++) { *out++ = buf[i]; left--; }
        } else if (*format == 'c') {
            char c = (char)va_arg(ap, int);
            *out++ = c; left--;
        } else if (*format == '%') {
            *out++ = '%'; left--;
        }
        format++;
    }
    *out = '\0';
    return (int)(out - str);
}

int vsscanf(const char *str, const char *format, va_list ap) {
    if (!str || !format) return 0;
    int count = 0;
    const char *s = str;
    const char *f = format;

    while (*f) {
        if (*f == ' ') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            while (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r') f++;
            continue;
        }

        if (*f != '%') {
            if (*s != *f) return count;
            s++;
            f++;
            continue;
        }

        f++; /* Skip '%' */
        if (*f == '%') {
            if (*s != '%') return count;
            s++;
            f++;
            continue;
        }

        /* Parse width (e.g. %2x, %1x) */
        int width = -1;
        if (*f >= '0' && *f <= '9') {
            width = 0;
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }
        }

        /* Length modifiers */
        int is_long = 0;
        if (*f == 'l') {
            is_long = 1;
            f++;
            if (*f == 'l') { is_long = 2; f++; }
        } else if (*f == 'h') {
            f++;
        }

        char conv = *f;
        if (!conv) break;
        f++;

        if (conv == 'd' || conv == 'i') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            int neg = 0;
            if (*s == '-') { neg = 1; s++; }
            else if (*s == '+') { s++; }
            if (*s < '0' || *s > '9') return count;
            long long val = 0;
            int n = 0;
            while (*s >= '0' && *s <= '9' && (width < 0 || n < width)) {
                val = val * 10 + (*s - '0');
                s++;
                n++;
            }
            if (neg) val = -val;
            if (is_long == 2) *va_arg(ap, long long *) = val;
            else if (is_long == 1) *va_arg(ap, long *) = (long)val;
            else *va_arg(ap, int *) = (int)val;
            count++;
        } else if (conv == 'u') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            if (*s < '0' || *s > '9') return count;
            unsigned long long val = 0;
            int n = 0;
            while (*s >= '0' && *s <= '9' && (width < 0 || n < width)) {
                val = val * 10 + (*s - '0');
                s++;
                n++;
            }
            if (is_long == 2) *va_arg(ap, unsigned long long *) = val;
            else if (is_long == 1) *va_arg(ap, unsigned long *) = (unsigned long)val;
            else *va_arg(ap, unsigned int *) = (unsigned int)val;
            count++;
        } else if (conv == 'x' || conv == 'X') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
            int n = 0;
            unsigned long val = 0;
            while ((width < 0 || n < width) &&
                   ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))) {
                int d = (*s >= '0' && *s <= '9') ? (*s - '0') :
                        (*s >= 'a' && *s <= 'f') ? (*s - 'a' + 10) : (*s - 'A' + 10);
                val = (val << 4) | (unsigned long)d;
                s++;
                n++;
            }
            if (n == 0) return count;
            if (is_long) *va_arg(ap, unsigned long *) = val;
            else *va_arg(ap, unsigned int *) = (unsigned int)val;
            count++;
        } else if (conv == 's') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            char *out_s = va_arg(ap, char *);
            int n = 0;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && (width < 0 || n < width)) {
                *out_s++ = *s++;
                n++;
            }
            *out_s = '\0';
            if (n == 0) return count;
            count++;
        } else if (conv == 'c') {
            char *out_c = va_arg(ap, char *);
            *out_c = *s++;
            count++;
        }
    }
    return count;
}

int sscanf(const char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf(str, format, ap);
    va_end(ap);
    return ret;
}
