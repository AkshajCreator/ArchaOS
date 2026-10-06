#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <stddef.h>

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

struct stat {
    size_t st_size;
    int st_mode;
};

static inline int mkdir(const char *path, int mode) { (void)path; (void)mode; return 0; }
static inline int stat(const char *path, struct stat *buf) { (void)path; (void)buf; return -1; }

#endif /* _SYS_STAT_H */
