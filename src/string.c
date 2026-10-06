#include "string.h"

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s && s[len]) {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d) d++;
    while (n-- && *src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline char to_lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && (to_lower_char(*s1) == to_lower_char(*s2))) {
        s1++;
        s2++;
    }
    return (unsigned char)to_lower_char(*s1) - (unsigned char)to_lower_char(*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (to_lower_char(*s1) == to_lower_char(*s2))) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)to_lower_char(*s1) - (unsigned char)to_lower_char(*s2);
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    char ch = (char)c;
    const char *last = NULL;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char ch = (unsigned char)c;
    while (n--) {
        if (*p == ch) return (void *)p;
        p++;
    }
    return NULL;
}

int atoi(const char *s)
{
    int v = 0;
    while (*s == ' ') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
    }
    return v * sign;
}

char *itoa(int value, char *str, int base)
{
    char *rc  = str;
    char *ptr = str;
    char *low;

    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }

    unsigned int uval = (unsigned int)value;
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        uval = (unsigned int)(-value);
    }

    low = ptr;
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[uval % base];
        uval /= base;
    } while (uval);

    *ptr-- = '\0';

    while (low < ptr) {
        char tmp = *low;
        *low++   = *ptr;
        *ptr--   = tmp;
    }

    return rc;
}
