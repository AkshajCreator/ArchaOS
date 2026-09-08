#include "netsurf_shim.h"
#include "../../mm.h"
#include "../../serial.h"

static int errno_val = 0;
int *__errno_location(void) {
    return &errno_val;
}

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    serial_printf(COM1_BASE, "[ASSERT FAIL] %s:%u (%s): %s\n", file, line, function ? function : "?", assertion);
}

extern uint32_t pit_ticks(void);
long time(long *t) {
    long sec = (long)(pit_ticks() / 100);
    if (t) *t = sec;
    return sec;
}

/* iconv dummy implementation for UTF-8 / ASCII transparent pass */
typedef void *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode) {
    (void)tocode; (void)fromcode;
    return (iconv_t)1;
}

size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) {
    (void)cd;
    if (!inbuf || !*inbuf) return 0;
    size_t count = 0;
    while (*inbytesleft > 0 && *outbytesleft > 0) {
        **outbuf = **inbuf;
        (*inbuf)++;
        (*outbuf)++;
        (*inbytesleft)--;
        (*outbytesleft)--;
        count++;
    }
    return count;
}

int iconv_close(iconv_t cd) {
    (void)cd;
    return 0;
}

/* ctype tables */
static unsigned short ctype_b_arr[384];
static int32_t ctype_lower_arr[384];
static int32_t ctype_upper_arr[384];
static int ctype_init_done = 0;

static void init_ctype_tables(void) {
    if (ctype_init_done) return;
    for (int i = 0; i < 384; i++) {
        int c = i - 128;
        unsigned short mask = 0;
        if (c >= '0' && c <= '9') mask |= 0x0800 | 0x0004; /* digit | alnum */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) mask |= 0x0400 | 0x0004; /* alpha | alnum */
        if (c >= 'A' && c <= 'Z') mask |= 0x0100; /* upper */
        if (c >= 'a' && c <= 'z') mask |= 0x0200; /* lower */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') mask |= 0x2000 | 0x0001; /* space | blank */
        if (c >= 32 && c <= 126) mask |= 0x4000; /* print */
        ctype_b_arr[i] = mask;
        ctype_lower_arr[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        ctype_upper_arr[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    ctype_init_done = 1;
}

const unsigned short **__ctype_b_loc(void) {
    init_ctype_tables();
    static const unsigned short *p = ctype_b_arr + 128;
    return &p;
}

const int32_t **__ctype_tolower_loc(void) {
    init_ctype_tables();
    static const int32_t *p = ctype_lower_arr + 128;
    return &p;
}

const int32_t **__ctype_toupper_loc(void) {
    init_ctype_tables();
    static const int32_t *p = ctype_upper_arr + 128;
    return &p;
}

/* strtol & strtoul */
long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16; s += 2;
    } else if (base == 0 && s[0] == '0') {
        base = 8; s++;
    } else if (base == 0) {
        base = 10;
    }

    long val = 0;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        if (d < 0 || d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16; s += 2;
    } else if (base == 0 && s[0] == '0') {
        base = 8; s++;
    } else if (base == 0) {
        base = 10;
    }
    unsigned long val = 0;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        if (d < 0 || d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return val;
}

long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return strtoul(nptr, endptr, base);
}

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

long long __isoc23_strtoll(const char *nptr, char **endptr, int base) {
    return strtoll(nptr, endptr, base);
}

/* Memory operations */
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t *)s;
    uint8_t uc = (uint8_t)c;
    while (n--) {
        if (*p == uc) return (void *)p;
        p++;
    }
    return NULL;
}

/* String operations */
size_t strlen(const char *s) {
    size_t len = 0;
    if (!s) return 0;
    while (*s++) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline char to_lower_c(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (to_lower_c(*s1) == to_lower_c(*s2))) { s1++; s2++; }
    return (unsigned char)to_lower_c(*s1) - (unsigned char)to_lower_c(*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (to_lower_c(*s1) == to_lower_c(*s2))) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return (unsigned char)to_lower_c(*s1) - (unsigned char)to_lower_c(*s2);
}

char *strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && (*h == *n)) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept) {
    size_t count = 0;
    while (*s) {
        int match = 0;
        for (const char *a = accept; *a; a++) {
            if (*s == *a) { match = 1; break; }
        }
        if (!match) break;
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    while (*s) {
        for (const char *r = reject; *r; r++) {
            if (*s == *r) return count;
        }
        count++;
        s++;
    }
    return count;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        for (const char *a = accept; *a; a++) {
            if (*s == *a) return (char *)s;
        }
        s++;
    }
    return NULL;
}

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

char *strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s = str ? str : *saveptr;
    if (!s) return NULL;
    while (*s && strchr(delim, *s)) s++;
    if (!*s) { *saveptr = NULL; return NULL; }
    char *tok = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) {
        *s = '\0';
        *saveptr = s + 1;
    } else {
        *saveptr = NULL;
    }
    return tok;
}

/* Character classification */
int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isalnum(int c) {
    return (isalpha(c) || isdigit(c));
}

int isxdigit(int c) {
    return (isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int isprint(int c) {
    return (c >= 32 && c <= 126);
}

int isgraph(int c) {
    return (c > 32 && c <= 126);
}

int ispunct(int c) {
    return (isprint(c) && !isalnum(c) && !isspace(c));
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

int abs(int j) {
    return (j < 0) ? -j : j;
}

/* Quick Sort Implementation */
static void swap_bytes(uint8_t *a, uint8_t *b, size_t width) {
    while (width--) {
        uint8_t t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *)) {
    if (nel < 2 || width == 0) return;
    uint8_t *b = (uint8_t *)base;
    uint8_t *pivot = b + (nel / 2) * width;
    uint8_t *i = b;
    uint8_t *j = b + (nel - 1) * width;

    while (i <= j) {
        while (compar(i, pivot) < 0) i += width;
        while (compar(j, pivot) > 0) j -= width;
        if (i <= j) {
            if (i != j) {
                if (pivot == i) pivot = j;
                else if (pivot == j) pivot = i;
                swap_bytes(i, j, width);
            }
            i += width;
            j -= width;
        }
    }
    if (j > b) qsort(b, (j - b) / width + 1, width, compar);
    if (b + nel * width > i) qsort(i, ((b + nel * width) - i) / width, width, compar);
}

int puts(const char *s) {
    if (s) serial_printf(COM1_BASE, "%s\n", s);
    return 0;
}

int putchar(int c) {
    serial_printf(COM1_BASE, "%c", (char)c);
    return c;
}

int printf(const char *format, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    serial_printf(COM1_BASE, "%s", buf);
    return len;
}

int fprintf(void *stream, const char *format, ...) {
    (void)stream;
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    serial_printf(COM1_BASE, "%s", buf);
    return len;
}
