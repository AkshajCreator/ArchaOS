// src/fs.c
// Hierarchical RAM filesystem for ArchaOS
// All nodes and file data are backed by kmalloc.

#include "fs.h"
#include "mm.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * INTERNAL HELPERS
 * ============================================================ */

static int fs_strlen(const char *s)
{
    int n = 0; while (s[n]) n++; return n;
}

static void fs_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++));
}

static void fs_strncpy(char *dst, const char *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int fs_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

static void fs_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
}

static void fs_memset(void *dst, uint8_t val, size_t n)
{
    uint8_t *d = dst;
    while (n--) *d++ = val;
}

/* Append src to dst, dst has bufsz total capacity */
static void fs_strcat(char *dst, const char *src, size_t bufsz)
{
    int dlen = fs_strlen(dst);
    int i = 0;
    while (src[i] && (size_t)(dlen + i + 1) < bufsz)
    {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
}

/* ============================================================
 * NODE POOL — 64 static nodes, allocated with a used[] flag
 * ============================================================ */

static fs_node_t node_pool[FS_MAX_NODES];
static uint8_t   node_used[FS_MAX_NODES];
static int       node_count = 0;

static fs_node_t *node_alloc(void)
{
    for (int i = 0; i < FS_MAX_NODES; i++)
    {
        if (!node_used[i])
        {
            node_used[i] = 1;
            fs_memset(&node_pool[i], 0, sizeof(fs_node_t));
            node_count++;
            return &node_pool[i];
        }
    }
    return 0;   /* out of nodes */
}

static void node_free(fs_node_t *n)
{
    for (int i = 0; i < FS_MAX_NODES; i++)
    {
        if (&node_pool[i] == n)
        {
            node_used[i] = 0;
            node_count--;
            return;
        }
    }
}

/* ============================================================
 * STATE
 * ============================================================ */

static fs_node_t *root_node = 0;
static fs_node_t *cwd_node  = 0;

/* ============================================================
 * FS_INIT
 * ============================================================ */

void fs_init(void)
{
    fs_memset(node_pool, 0, sizeof(node_pool));
    fs_memset(node_used, 0, sizeof(node_used));
    node_count = 0;

    root_node = node_alloc();
    fs_strcpy(root_node->name, "/");
    root_node->type   = FS_DIR;
    root_node->parent = root_node;  /* root's parent is itself */

    cwd_node = root_node;
}

/* ============================================================
 * NAVIGATION
 * ============================================================ */

fs_node_t *fs_root(void) { return root_node; }
fs_node_t *fs_cwd(void)  { return cwd_node;  }

void fs_pwd(char *buf, size_t bufsz)
{
    /* Build path by walking up to root */
    fs_node_t *cur = cwd_node;

    if (cur == root_node) { fs_strcpy(buf, "/"); return; }

    /* Collect path segments */
    char segments[16][FS_MAX_NAME];
    int  depth = 0;

    while (cur != root_node && depth < 16)
    {
        fs_strcpy(segments[depth++], cur->name);
        cur = cur->parent;
    }

    buf[0] = '\0';
    for (int i = depth - 1; i >= 0; i--)
    {
        fs_strcat(buf, "/", bufsz);
        fs_strcat(buf, segments[i], bufsz);
    }
}

/* ============================================================
 * PATH RESOLUTION
 * Split a path into components and walk the tree
 * ============================================================ */

/* Find a child by name in a directory node */
static fs_node_t *find_child(fs_node_t *dir, const char *name)
{
    if (!dir || dir->type != FS_DIR) return 0;
    for (int i = 0; i < dir->child_count; i++)
        if (fs_strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    return 0;
}

/* Add a child to a directory */
static int add_child(fs_node_t *dir, fs_node_t *child)
{
    if (dir->child_count >= FS_MAX_CHILDREN) return -1;
    dir->children[dir->child_count++] = child;
    child->parent = dir;
    return 0;
}

/* Remove a child from a directory */
static int remove_child(fs_node_t *dir, fs_node_t *child)
{
    for (int i = 0; i < dir->child_count; i++)
    {
        if (dir->children[i] == child)
        {
            for (int j = i; j < dir->child_count - 1; j++)
                dir->children[j] = dir->children[j+1];
            dir->child_count--;
            return 0;
        }
    }
    return -1;
}

/* Walk a path string, return the final node or NULL */
fs_node_t *fs_resolve(const char *path)
{
    if (!path || !path[0]) return cwd_node;

    fs_node_t *cur = (path[0] == '/') ? root_node : cwd_node;
    if (!path[1] && path[0] == '/') return root_node;

    /* Copy path so we can tokenize it */
    char tmp[FS_MAX_PATH];
    fs_strncpy(tmp, path, FS_MAX_PATH);

    int i = (path[0] == '/') ? 1 : 0;

    while (tmp[i])
    {
        /* Extract next component */
        char comp[FS_MAX_NAME];
        int  j = 0;
        while (tmp[i] && tmp[i] != '/' && j < FS_MAX_NAME - 1)
            comp[j++] = tmp[i++];
        comp[j] = '\0';
        if (tmp[i] == '/') i++;

        if (!comp[0] || fs_strcmp(comp, ".") == 0)
            continue;

        if (fs_strcmp(comp, "..") == 0)
        {
            cur = cur->parent;
            continue;
        }

        cur = find_child(cur, comp);
        if (!cur) return 0;
    }

    return cur;
}

/* Split a path into parent dir + final component name */
static fs_node_t *resolve_parent(const char *path, char *name_out)
{
    char tmp[FS_MAX_PATH];
    fs_strncpy(tmp, path, FS_MAX_PATH);

    int len = fs_strlen(tmp);

    /* Remove trailing slash */
    if (len > 1 && tmp[len-1] == '/') { tmp[--len] = '\0'; }

    /* Find last slash */
    int slash = -1;
    for (int i = len - 1; i >= 0; i--)
    {
        if (tmp[i] == '/') { slash = i; break; }
    }

    if (slash < 0)
    {
        /* No slash — parent is cwd */
        fs_strncpy(name_out, tmp, FS_MAX_NAME);
        return cwd_node;
    }

    fs_strncpy(name_out, tmp + slash + 1, FS_MAX_NAME);

    if (slash == 0)
        return root_node;

    tmp[slash] = '\0';
    return fs_resolve(tmp);
}

/* ============================================================
 * FS_CD
 * ============================================================ */

int fs_cd(const char *path)
{
    fs_node_t *n = fs_resolve(path);
    if (!n || n->type != FS_DIR) return -1;
    cwd_node = n;
    return 0;
}

/* ============================================================
 * FS_MKDIR
 * ============================================================ */

fs_node_t *fs_mkdir(const char *path)
{
    char name[FS_MAX_NAME];
    fs_node_t *parent = resolve_parent(path, name);
    if (!parent || !name[0]) return 0;
    if (find_child(parent, name)) return 0;  /* already exists */

        fs_node_t *n = node_alloc();
    if (!n) return 0;

    fs_strncpy(n->name, name, FS_MAX_NAME);
    n->type = FS_DIR;
    add_child(parent, n);
    return n;
}

/* ============================================================
 * FS_TOUCH
 * ============================================================ */

fs_node_t *fs_touch(const char *path)
{
    char name[FS_MAX_NAME];
    fs_node_t *parent = resolve_parent(path, name);
    if (!parent || !name[0]) return 0;

    /* Return existing file */
    fs_node_t *ex = find_child(parent, name);
    if (ex) return ex;

    fs_node_t *n = node_alloc();
    if (!n) return 0;

    fs_strncpy(n->name, name, FS_MAX_NAME);
    n->type = FS_FILE;
    n->data = 0;
    n->size = 0;
    add_child(parent, n);
    return n;
}

/* ============================================================
 * FS_WRITE
 * ============================================================ */

int fs_write(const char *path, const char *data, size_t len)
{
    fs_node_t *n = fs_resolve(path);
    if (!n) n = fs_touch(path);
    if (!n || n->type != FS_FILE) return -1;

    if (n->data) kfree(n->data);

    n->data = kmalloc(len + 1);
    if (!n->data) return -1;

    fs_memcpy(n->data, data, len);
    n->data[len] = '\0';
    n->size = len;
    return 0;
}

/* ============================================================
 * FS_CAT
 * ============================================================ */

int fs_cat(const char *path, char *buf, size_t bufsz)
{
    fs_node_t *n = fs_resolve(path);
    if (!n || n->type != FS_FILE) return -1;

    size_t copy = (n->size < bufsz - 1) ? n->size : bufsz - 1;
    if (n->data) fs_memcpy(buf, n->data, copy);
    buf[copy] = '\0';
    return 0;
}

/* ============================================================
 * FS_RM
 * ============================================================ */

int fs_rm(const char *path)
{
    fs_node_t *n = fs_resolve(path);
    if (!n || n == root_node || n == cwd_node) return -1;

    /* Don't allow deleting any ancestor of cwd_node */
    fs_node_t *p = cwd_node->parent;
    while (p && p != root_node)
    {
        if (p == n) return -1;
        p = p->parent;
    }

    /* Don't remove non-empty directories */
    if (n->type == FS_DIR && n->child_count > 0) return -2;

    if (n->type == FS_FILE && n->data) kfree(n->data);

    remove_child(n->parent, n);
    node_free(n);
    return 0;
}

/* ============================================================
 * FS_CP
 * ============================================================ */

int fs_cp(const char *src, const char *dst)
{
    fs_node_t *s = fs_resolve(src);
    if (!s || s->type != FS_FILE) return -1;

    fs_node_t *d = fs_touch(dst);
    if (!d) return -1;

    if (d->data) { kfree(d->data); d->data = 0; d->size = 0; }

    if (s->data && s->size > 0)
    {
        d->data = kmalloc(s->size + 1);
        if (!d->data) return -1;

        fs_memcpy(d->data, s->data, s->size);
        d->data[s->size] = '\0';
        d->size = s->size;
    }
    return 0;
}

/* ============================================================
 * FS_MV
 * ============================================================ */

int fs_mv(const char *src, const char *dst)
{
    fs_node_t *s = fs_resolve(src);
    if (!s || s == root_node) return -1;

    /* Resolve new parent and name */
    char new_name[FS_MAX_NAME];
    fs_node_t *new_parent = resolve_parent(dst, new_name);
    if (!new_parent || !new_name[0]) return -1;

    /* Detach from old parent, attach to new */
    remove_child(s->parent, s);
    fs_strncpy(s->name, new_name, FS_MAX_NAME);
    add_child(new_parent, s);
    return 0;
}

/* ============================================================
 * FS_LS
 * ============================================================ */

void fs_ls(const char *path, char *buf, size_t bufsz)
{
    fs_node_t *dir = path ? fs_resolve(path) : cwd_node;
    if (!dir || dir->type != FS_DIR)
    {
        fs_strcpy(buf, "not a directory\n");
        return;
    }

    buf[0] = '\0';

    if (dir->child_count == 0)
    {
        fs_strcpy(buf, "(empty)\n");
        return;
    }

    for (int i = 0; i < dir->child_count; i++)
    {
        fs_node_t *c = dir->children[i];
        fs_strcat(buf, c->type == FS_DIR ? "[d] " : "[f] ", bufsz);
        fs_strcat(buf, c->name, bufsz);
        fs_strcat(buf, "\n", bufsz);
    }
}
