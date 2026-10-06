#ifndef _DIRENT_H
#define _DIRENT_H

#include <stdint.h>
#include <stddef.h>

#define DT_UNKNOWN  0
#define DT_REG      1
#define DT_DIR      2

struct dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    char     d_name[64];
    uint32_t d_size;
};

typedef struct {
    char path[256];
    int  index;
} DIR;

#ifdef __cplusplus
extern "C" {
#endif

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* _DIRENT_H */
