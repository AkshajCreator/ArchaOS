#include "libc.h"
#include "dirent.h"
#include <stdarg.h>

int errno = 0;

__attribute__((weak)) void __libc_init_array(void)
{
}

/* ========================================================================= */
/* Syscall Wrappers                                                          */
/* ========================================================================= */

void exit(int status)
{
    sys_call1(SYS_EXIT, (uint32_t)status);
    while (1) {
        sys_call0(SYS_YIELD);
    }
}

int read(int fd, void *buf, size_t count)
{
    return sys_call3(SYS_READ, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

int write(int fd, const void *buf, size_t count)
{
    return sys_call3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

int open(const char *path, int flags)
{
    return sys_call2(SYS_OPEN, (uint32_t)path, (uint32_t)flags);
}

int close(int fd)
{
    return sys_call1(SYS_CLOSE, (uint32_t)fd);
}

void sleep(uint32_t ms)
{
    sys_call1(SYS_SLEEP, ms);
}

void yield(void)
{
    sys_call0(SYS_YIELD);
}

int getpid(void)
{
    return sys_call0(SYS_GETPID);
}

int spawn(const char *path)
{
    return sys_call1(SYS_SPAWN, (uint32_t)path);
}

void *sbrk(intptr_t incr)
{
    return (void *)sys_call1(SYS_SBRK, (uint32_t)incr);
}

char *getcwd(char *buf, size_t size)
{
    if (!buf || size < 2) return NULL;
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

uint32_t time_ticks(void)
{
    return (uint32_t)sys_call0(SYS_TIME);
}

typedef uint32_t time_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};

time_t time(time_t *tloc)
{
    uint32_t ticks = time_ticks();
    time_t sec = (time_t)(ticks / 100);
    if (tloc) *tloc = sec;
    return sec;
}

static struct tm s_local_tm;
struct tm *localtime(const time_t *timep)
{
    (void)timep;
    memset(&s_local_tm, 0, sizeof(s_local_tm));
    s_local_tm.tm_year = 93;
    s_local_tm.tm_mon = 5;
    s_local_tm.tm_mday = 23;
    s_local_tm.tm_hour = 12;
    return &s_local_tm;
}

int lseek(int fd, long offset, int whence)
{
    return sys_call3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

/* ========================================================================= */
/* Standard Stream I/O                                                       */
/* ========================================================================= */

static FILE _stdin_file  = { 0, 0, 0 };
static FILE _stdout_file = { 1, 0, 0 };
static FILE _stderr_file = { 2, 0, 0 };
FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

FILE *fopen(const char *path, const char *mode)
{
    (void)mode;
    if (!path) return NULL;
    int fd = open(path, 0);
    if (fd < 0) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        close(fd);
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    return f;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    int bytes_read = read(stream->fd, ptr, total_bytes);
    if (bytes_read <= 0) {
        stream->eof = 1;
        return 0;
    }
    if ((size_t)bytes_read < total_bytes) {
        stream->eof = 1;
    }
    return (size_t)bytes_read / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    size_t total_bytes = size * nmemb;
    int bytes_written = write(stream->fd, ptr, total_bytes);
    if (bytes_written < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)bytes_written / size;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream) return -1;
    int res = lseek(stream->fd, offset, whence);
    if (res < 0) {
        stream->error = 1;
        return -1;
    }
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream) return -1;
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

int fclose(FILE *stream)
{
    if (!stream) return -1;
    int fd = stream->fd;
    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }
    return close(fd);
}

int feof(FILE *stream)
{
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream)
{
    return stream ? stream->error : 1;
}

void clearerr(FILE *stream)
{
    if (stream) {
        stream->eof = 0;
        stream->error = 0;
    }
}

int fileno(FILE *stream)
{
    return stream ? stream->fd : -1;
}

void rewind(FILE *stream)
{
    if (stream) fseek(stream, 0, SEEK_SET);
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}

int fgetc(FILE *stream)
{
    if (!stream) return EOF;
    unsigned char ch;
    if (fread(&ch, 1, 1, stream) == 1) return (int)ch;
    return EOF;
}

char *fgets(char *s, int size, FILE *stream)
{
    if (!s || size <= 0 || !stream) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream)
{
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) == len) return 0;
    return EOF;
}

/* ========================================================================= */
/* Memory Allocator (First-Fit with Coalescing)                              */
/* ========================================================================= */

typedef struct block_header {
    size_t size;                /* Size of payload data */
    int is_free;                /* 1 = free, 0 = allocated */
    struct block_header *next;  /* Linked list pointer */
    uint32_t _reserved;         /* Pad header to 16 bytes on 32-bit x86 */
} block_header_t;

static block_header_t *heap_head = NULL;
static size_t s_heap_allocations = 0;
static size_t s_heap_frees = 0;

void *malloc(size_t size)
{
    if (size == 0) return NULL;

    /* Align payload size to 8-byte boundary */
    size = (size + 7) & ~7;

    /* Search for first fitting free block */
    block_header_t *prev = NULL;
    block_header_t *curr = heap_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Check if block can be split */
            if (curr->size >= size + sizeof(block_header_t) + 8) {
                block_header_t *split = (block_header_t *)((char *)(curr + 1) + size);
                split->size = curr->size - size - sizeof(block_header_t);
                split->is_free = 1;
                split->next = curr->next;
                split->_reserved = 0;

                curr->size = size;
                curr->next = split;
            }
            curr->is_free = 0;
            s_heap_allocations++;
            return (void *)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    /* No block found, request more memory from kernel via sbrk */
    size_t need = sizeof(block_header_t) + size;
    size_t chunk_size = (need < 4096) ? 4096 : ((need + 4095) & ~4095);

    void *raw = sbrk((intptr_t)chunk_size);
    if (raw == (void *)-1 || raw == NULL) {
        return NULL;
    }

    block_header_t *new_block = (block_header_t *)raw;
    new_block->size = chunk_size - sizeof(block_header_t);
    new_block->is_free = 0;
    new_block->next = NULL;
    new_block->_reserved = 0;

    if (prev) {
        prev->next = new_block;
    } else {
        heap_head = new_block;
    }

    /* Split new block if there is excess space */
    if (new_block->size >= size + sizeof(block_header_t) + 8) {
        block_header_t *split = (block_header_t *)((char *)(new_block + 1) + size);
        split->size = new_block->size - size - sizeof(block_header_t);
        split->is_free = 1;
        split->next = NULL;
        split->_reserved = 0;

        new_block->size = size;
        new_block->next = split;
    }

    s_heap_allocations++;
    return (void *)(new_block + 1);
}

void free(void *ptr)
{
    if (!ptr) return;

    s_heap_frees++;
    block_header_t *block = ((block_header_t *)ptr) - 1;
    block->is_free = 1;

    /* Coalesce adjacent free blocks */
    block_header_t *curr = heap_head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            char *curr_end = (char *)(curr + 1) + curr->size;
            if (curr_end == (char *)curr->next) {
                curr->size += sizeof(block_header_t) + curr->next->size;
                curr->next = curr->next->next;
                continue;
            }
        }
        curr = curr->next;
    }
}

void malloc_stats(void)
{
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    size_t free_bytes = 0;
    size_t used_blocks = 0;
    size_t free_blocks = 0;

    block_header_t *curr = heap_head;
    while (curr) {
        total_bytes += sizeof(block_header_t) + curr->size;
        if (curr->is_free) {
            free_bytes += curr->size;
            free_blocks++;
        } else {
            used_bytes += curr->size;
            used_blocks++;
        }
        curr = curr->next;
    }

    printf("=== Heap / Malloc Diagnostics ===\n");
    printf("  Total Heap: %u bytes\n", (uint32_t)total_bytes);
    printf("  Used:       %u bytes in %u blocks\n", (uint32_t)used_bytes, (uint32_t)used_blocks);
    printf("  Free:       %u bytes in %u blocks\n", (uint32_t)free_bytes, (uint32_t)free_blocks);
    printf("  Allocs:     %u | Frees: %u\n", (uint32_t)s_heap_allocations, (uint32_t)s_heap_frees);
    printf("=================================\n");
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    block_header_t *block = ((block_header_t *)ptr) - 1;
    if (block->size >= size) return ptr;
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, block->size < size ? block->size : size);
    free(ptr);
    return new_ptr;
}

/* ========================================================================= */
/* String and Memory Utilities                                               */
/* ========================================================================= */

size_t strlen(const char *s)
{
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
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
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0')
            return 0;
    }
    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

int atoi(const char *str)
{
    int res = 0;
    int sign = 1;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return sign * res;
}

long strtol(const char *nptr, char **endptr, int base)
{
    if (!nptr) return 0;
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }

    if (base == 0) {
        if (*s == '0') {
            if (*(s + 1) == 'x' || *(s + 1) == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) {
            s += 2;
        }
    }

    long val = 0;
    const char *start = s;
    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        if (digit < 0 || digit >= base) break;
        val = val * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = (char *)((s == start) ? nptr : s);
    }
    return val * sign;
}

double atof(const char *str)
{
    if (!str) return 0.0;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    double sign = 1.0;
    if (*str == '-') { sign = -1.0; str++; }
    else if (*str == '+') { str++; }
    double val = 0.0;
    while (*str >= '0' && *str <= '9') {
        val = val * 10.0 + (*str - '0');
        str++;
    }
    if (*str == '.') {
        str++;
        double frac = 0.1;
        while (*str >= '0' && *str <= '9') {
            val += (*str - '0') * frac;
            frac *= 0.1;
            str++;
        }
    }
    return val * sign;
}

char *itoa(int value, char *str, int base)
{
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }

    char *ptr = str;
    char *ptr1 = str;
    int is_negative = 0;
    unsigned int uval;

    if (value < 0 && base == 10) {
        is_negative = 1;
        uval = (unsigned int)(-value);
    } else {
        uval = (unsigned int)value;
    }

    do {
        unsigned int rem = uval % (unsigned int)base;
        uval /= (unsigned int)base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[rem];
    } while (uval != 0);

    if (is_negative) {
        *ptr++ = '-';
    }
    *ptr-- = '\0';

    while (ptr1 < ptr) {
        char tmp = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp;
    }
    return str;
}

/* ========================================================================= */
/* Standard I/O Utilities                                                    */
/* ========================================================================= */

int putchar(char c)
{
    int ret = write(1, &c, 1);
    if (ret <= 0) return -1;
    return (unsigned char)c;
}

int puts(const char *str)
{
    if (!str) return -1;
    size_t len = strlen(str);
    if (len > 0) {
        write(1, str, len);
    }
    char nl = '\n';
    write(1, &nl, 1);
    return (int)(len + 1);
}

int getchar(void)
{
    char c = 0;
    while (1) {
        int n = read(0, &c, 1);
        if (n > 0) return (unsigned char)c;
        if (n < 0) return -1;
        yield();
    }
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    if (!str || size == 0) return 0;
    size_t written = 0;
    const char *p = fmt;

    while (*p && written + 1 < size) {
        if (*p != '%') {
            str[written++] = *p++;
            continue;
        }
        p++; // skip '%'
        if (!*p) break;

        int zero_pad = 0;
        int left_align = 0;
        int width = 0;
        int precision = -1;

        // Flags
        while (*p == '0' || *p == '-' || *p == '+' || *p == ' ') {
            if (*p == '0') zero_pad = 1;
            if (*p == '-') left_align = 1;
            p++;
        }

        // Width
        if (*p >= '0' && *p <= '9') {
            width = 0;
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
        }

        // Precision (e.g. %.3d)
        if (*p == '.') {
            p++;
            precision = 0;
            while (*p >= '0' && *p <= '9') {
                precision = precision * 10 + (*p - '0');
                p++;
            }
        }

        // Length modifiers (e.g. 'l', 'h', 'z')
        while (*p == 'l' || *p == 'h' || *p == 'z') p++;

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                size_t slen = strlen(s);
                if (precision >= 0 && (size_t)precision < slen) slen = precision;
                if (!left_align && width > (int)slen) {
                    for (int w = 0; w < width - (int)slen && written + 1 < size; w++)
                        str[written++] = ' ';
                }
                for (size_t i = 0; i < slen && written + 1 < size; i++) {
                    str[written++] = s[i];
                }
                if (left_align && width > (int)slen) {
                    for (int w = 0; w < width - (int)slen && written + 1 < size; w++)
                        str[written++] = ' ';
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                if (written + 1 < size) str[written++] = c;
                break;
            }
            case 'd':
            case 'i': {
                int val = va_arg(ap, int);
                int is_neg = 0;
                if (val < 0) {
                    is_neg = 1;
                    val = -val;
                }
                char buf[32];
                int bi = 0;
                do {
                    buf[bi++] = (char)('0' + (val % 10));
                    val /= 10;
                } while (val > 0 && bi < 31);

                int pad_count = 0;
                if (precision >= 0) {
                    if (precision > bi) pad_count = precision - bi;
                } else if (zero_pad && width > bi + is_neg) {
                    pad_count = width - (bi + is_neg);
                }

                if (!left_align && !zero_pad && width > bi + is_neg + pad_count) {
                    for (int w = 0; w < width - (bi + is_neg + pad_count) && written + 1 < size; w++)
                        str[written++] = ' ';
                }
                if (is_neg && written + 1 < size) str[written++] = '-';
                for (int z = 0; z < pad_count && written + 1 < size; z++) str[written++] = '0';
                while (bi > 0 && written + 1 < size) str[written++] = buf[--bi];
                if (left_align && width > bi + is_neg + pad_count) {
                    for (int w = 0; w < width - (bi + is_neg + pad_count) && written + 1 < size; w++)
                        str[written++] = ' ';
                }
                break;
            }
            case 'u': {
                unsigned int val = va_arg(ap, unsigned int);
                char buf[32];
                int bi = 0;
                do {
                    buf[bi++] = (char)('0' + (val % 10));
                    val /= 10;
                } while (val > 0 && bi < 31);

                int pad_count = 0;
                if (precision >= 0 && precision > bi) pad_count = precision - bi;
                else if (zero_pad && width > bi) pad_count = width - bi;

                for (int z = 0; z < pad_count && written + 1 < size; z++) str[written++] = '0';
                while (bi > 0 && written + 1 < size) str[written++] = buf[--bi];
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                unsigned int val;
                if (*p == 'p') {
                    val = (uint32_t)va_arg(ap, void *);
                    if (written + 2 < size) {
                        str[written++] = '0';
                        str[written++] = 'x';
                    }
                } else {
                    val = va_arg(ap, unsigned int);
                }
                const char *hex = (*p == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char buf[32];
                int bi = 0;
                do {
                    buf[bi++] = hex[val & 0xF];
                    val >>= 4;
                } while (val > 0 && bi < 31);

                int pad_count = 0;
                if (precision >= 0 && precision > bi) pad_count = precision - bi;
                else if (zero_pad && width > bi) pad_count = width - bi;

                for (int z = 0; z < pad_count && written + 1 < size; z++) str[written++] = '0';
                while (bi > 0 && written + 1 < size) str[written++] = buf[--bi];
                break;
            }
            case '%': {
                if (written + 1 < size) str[written++] = '%';
                break;
            }
            default:
                if (written + 1 < size) str[written++] = *p;
                break;
        }
        p++;
    }
    str[written] = '\0';
    return (int)written;
}

int printf(const char *fmt, ...)
{
    if (!fmt) return 0;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
        write(1, buf, len);
    }
    return len;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, 65536, fmt, ap);
    va_end(ap);
    return ret;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

int abs(int n) {
    return n < 0 ? -n : n;
}

int strcasecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (n > 0 && *s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

char *strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? (char *)s : (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

void *memmove(void *dest, const void *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
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

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

char *strpbrk(const char *s, const char *accept)
{
    if (!s || !accept) return NULL;
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*s == *a) return (char *)s;
            a++;
        }
        s++;
    }
    return NULL;
}

static char *s_strtok_last = NULL;
char *strtok(char *str, const char *delim)
{
    if (!str) str = s_strtok_last;
    if (!str) return NULL;

    while (*str && strchr(delim, *str)) str++;
    if (*str == '\0') {
        s_strtok_last = NULL;
        return NULL;
    }

    char *token_start = str;
    while (*str && !strchr(delim, *str)) str++;
    if (*str != '\0') {
        *str = '\0';
        s_strtok_last = str + 1;
    } else {
        s_strtok_last = NULL;
    }
    return token_start;
}

static unsigned long int s_next_rand = 1;

int rand(void) {
    s_next_rand = s_next_rand * 1103515245 + 12345;
    return (unsigned int)(s_next_rand / 65536) % 32768;
}

void srand(unsigned int seed) {
    s_next_rand = seed;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0 || !compar) return;
    char *a = (char *)base;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - 1 - i; j++) {
            char *p1 = a + j * size;
            char *p2 = a + (j + 1) * size;
            if (compar(p1, p2) > 0) {
                for (size_t k = 0; k < size; k++) {
                    char tmp = p1[k];
                    p1[k] = p2[k];
                    p2[k] = tmp;
                }
            }
        }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    if (!key || !base || nmemb == 0 || size == 0 || !compar) return NULL;
    size_t l = 0;
    size_t r = nmemb;
    while (l < r) {
        size_t m = l + (r - l) / 2;
        const void *elem = (const char *)base + m * size;
        int res = compar(key, elem);
        if (res == 0) return (void *)elem;
        if (res < 0) r = m;
        else l = m + 1;
    }
    return NULL;
}

char *getenv(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "DOOMWADDIR") == 0) return "/DOOM";
    if (strcmp(name, "HOME") == 0) return "/";
    return NULL;
}

int system(const char *command) {
    if (!command) return 1;
    return -1;
}

int remove(const char *pathname) {
    (void)pathname;
    return 0;
}

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    return 0;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    if (!stream) return -1;
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len > 0) {
        if (stream == stdout || stream == stderr) {
            write(stream->fd, buf, len);
        } else {
            fwrite(buf, 1, len, stream);
        }
    }
    return len;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int vsprintf(char *str, const char *fmt, va_list ap) {
    return vsnprintf(str, 65536, fmt, ap);
}

int sscanf(const char *str, const char *fmt, ...) {
    if (!str || !fmt) return 0;
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;
    const char *s = str;
    const char *f = fmt;

    while (*f && *s) {
        if (*f == ' ') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            f++;
            continue;
        }
        if (*f != '%') {
            if (*f != *s) break;
            f++;
            s++;
            continue;
        }
        f++;
        if (*f == '\0') break;

        if (*f != 'c') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        }

        if (*f == 'd' || *f == 'i') {
            int sign = 1;
            if (*s == '-') { sign = -1; s++; }
            else if (*s == '+') { s++; }
            int base = 10;
            if (*f == 'i' && *s == '0') {
                if (*(s + 1) == 'x' || *(s + 1) == 'X') {
                    base = 16;
                    s += 2;
                } else {
                    base = 8;
                }
            }
            long val = 0;
            int any = 0;
            while (1) {
                int digit = -1;
                if (*s >= '0' && *s <= '9') digit = *s - '0';
                else if (base == 16 && *s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
                else if (base == 16 && *s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
                if (digit < 0 || digit >= base) break;
                val = val * base + digit;
                s++;
                any = 1;
            }
            if (!any) break;
            int *out = va_arg(ap, int *);
            if (out) *out = (int)(val * sign);
            matched++;
        } else if (*f == 'x' || *f == 'X') {
            if (*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')) s += 2;
            unsigned int val = 0;
            int any = 0;
            while (1) {
                int digit = -1;
                if (*s >= '0' && *s <= '9') digit = *s - '0';
                else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
                else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
                if (digit < 0) break;
                val = val * 16 + digit;
                s++;
                any = 1;
            }
            if (!any) break;
            unsigned int *out = va_arg(ap, unsigned int *);
            if (out) *out = val;
            matched++;
        } else if (*f == 's') {
            char *out = va_arg(ap, char *);
            int len = 0;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
                if (out) out[len++] = *s;
                s++;
            }
            if (out) out[len] = '\0';
            if (len > 0) matched++;
            else break;
        } else if (*f == 'c') {
            char *out = va_arg(ap, char *);
            if (out) *out = *s;
            s++;
            matched++;
        }
        f++;
    }

    va_end(ap);
    return matched;
}

/* ========================================================================= */
/* Directory Operations (POSIX opendir / readdir / closedir)                 */
/* ========================================================================= */

static struct dirent s_static_dirent;

DIR *opendir(const char *name)
{
    if (!name) return NULL;
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return NULL;
    strncpy(d->path, name, sizeof(d->path) - 1);
    d->path[sizeof(d->path) - 1] = '\0';
    d->index = 0;

    /* Verify directory exists by attempting to read index 0 */
    struct dirent temp;
    int res = sys_call3(SYS_READDIR, (uint32_t)d->path, 0, (uint32_t)&temp);
    if (res < 0) {
        free(d);
        return NULL;
    }
    return d;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp) return NULL;
    int res = sys_call3(SYS_READDIR, (uint32_t)dirp->path, (uint32_t)dirp->index, (uint32_t)&s_static_dirent);
    if (res <= 0) {
        return NULL; /* 0 = EOF, <0 = error */
    }
    dirp->index++;
    return &s_static_dirent;
}

int closedir(DIR *dirp)
{
    if (!dirp) return -1;
    free(dirp);
    return 0;
}

void rewinddir(DIR *dirp)
{
    if (dirp) {
        dirp->index = 0;
    }
}


