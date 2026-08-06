// src/fs.h
#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

#define FS_MAX_NODES     64
#define FS_MAX_NAME      32
#define FS_MAX_PATH     256
#define FS_MAX_CHILDREN  16   /* max entries per directory */

typedef enum {
    FS_FILE = 1,
    FS_DIR  = 2
} fs_type_t;

typedef struct fs_node
{
    char          name[FS_MAX_NAME];
    fs_type_t     type;
    uint8_t      *data;         /* kmalloc'd, NULL for dirs  */
    size_t        size;         /* bytes of content          */
    struct fs_node *parent;
    struct fs_node *children[FS_MAX_CHILDREN];
    int            child_count;
} fs_node_t;

/* ── API ─────────────────────────────────────────────── */

void        fs_init(void);

/* Navigation */
fs_node_t  *fs_root(void);
fs_node_t  *fs_cwd(void);
int         fs_cd(const char *path);
void        fs_pwd(char *buf, size_t bufsz);

/* Lookup */
fs_node_t  *fs_resolve(const char *path);   /* abs or relative */

/* Operations */
fs_node_t  *fs_mkdir(const char *path);
fs_node_t  *fs_touch(const char *path);
int         fs_write(const char *path, const char *data, size_t len);
int         fs_cat(const char *path, char *buf, size_t bufsz);
int         fs_rm(const char *path);
int         fs_cp(const char *src, const char *dst);
int         fs_mv(const char *src, const char *dst);
void        fs_ls(const char *path, char *buf, size_t bufsz);

#endif
