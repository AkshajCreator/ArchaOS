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
    if (!dst || !src || bufsz == 0) return;
    int dlen = fs_strlen(dst);
    if ((size_t)dlen >= bufsz) { dst[bufsz - 1] = '\0'; return; }
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

static void fs_init_default_files(void);

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

    fs_init_default_files();
}

/* ============================================================
 * NAVIGATION
 * ============================================================ */

fs_node_t *fs_root(void) { return root_node; }
fs_node_t *fs_cwd(void)  { return cwd_node;  }

void fs_pwd(char *buf, size_t bufsz)
{
    if (!buf || bufsz == 0) return;
    /* Build path by walking up to root */
    fs_node_t *cur = cwd_node;

    if (!cur || cur == root_node) { fs_strncpy(buf, "/", bufsz); return; }

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
        while (tmp[i] && tmp[i] != '/') i++; /* Skip remaining chars of oversized component */
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
    if (!path || !name_out) return 0;
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
    if (!parent || !name[0] || parent->type != FS_DIR) return 0;
    if (find_child(parent, name)) return 0;  /* already exists */
    if (parent->child_count >= FS_MAX_CHILDREN) return 0;

    fs_node_t *n = node_alloc();
    if (!n) return 0;

    fs_strncpy(n->name, name, FS_MAX_NAME);
    n->type = FS_DIR;
    if (add_child(parent, n) < 0) {
        node_free(n);
        return 0;
    }
    return n;
}

/* ============================================================
 * FS_TOUCH
 * ============================================================ */

fs_node_t *fs_touch(const char *path)
{
    char name[FS_MAX_NAME];
    fs_node_t *parent = resolve_parent(path, name);
    if (!parent || !name[0] || parent->type != FS_DIR) return 0;

    /* Return existing file */
    fs_node_t *ex = find_child(parent, name);
    if (ex) return (ex->type == FS_FILE) ? ex : 0;
    if (parent->child_count >= FS_MAX_CHILDREN) return 0;

    fs_node_t *n = node_alloc();
    if (!n) return 0;

    fs_strncpy(n->name, name, FS_MAX_NAME);
    n->type = FS_FILE;
    n->data = 0;
    n->size = 0;
    if (add_child(parent, n) < 0) {
        node_free(n);
        return 0;
    }
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

    if (n->data) { kfree(n->data); n->data = 0; n->size = 0; }

    if (len == 0) return 0;

    n->data = kmalloc(len + 1);
    if (!n->data) { n->size = 0; return -1; }

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
    if (!buf || bufsz == 0) return -1;
    fs_node_t *n = fs_resolve(path);
    if (!n || n->type != FS_FILE) return -1;

    size_t copy = (n->size < bufsz - 1) ? n->size : bufsz - 1;
    if (n->data && copy > 0) fs_memcpy(buf, n->data, copy);
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

    fs_node_t *d = fs_resolve(dst);
    if (d == s) return 0; /* Self-copy is a no-op */

    if (!d) d = fs_touch(dst);
    if (!d || d->type != FS_FILE) return -1;

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
    if (!new_parent || !new_name[0] || new_parent->type != FS_DIR) return -1;

    /* Prevent moving a directory inside itself or its descendants */
    if (s->type == FS_DIR) {
        fs_node_t *chk = new_parent;
        while (chk && chk != root_node) {
            if (chk == s) return -1;
            chk = chk->parent;
        }
    }

    /* Check destination capacity */
    if (new_parent != s->parent && new_parent->child_count >= FS_MAX_CHILDREN) return -1;

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
    if (!buf || bufsz == 0) return;
    fs_node_t *dir = path ? fs_resolve(path) : cwd_node;
    if (!dir || dir->type != FS_DIR)
    {
        fs_strncpy(buf, "not a directory\n", bufsz);
        return;
    }

    buf[0] = '\0';

    if (dir->child_count == 0)
    {
        fs_strncpy(buf, "(empty)\n", bufsz);
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

/* ============================================================
 * DEFAULT SYSTEM & USER DOCUMENTATION FILES
 * ============================================================ */

static void fs_init_default_files(void)
{
    static const char NEW_CONTENT[] =
        "=== WHAT'S NEW IN ARCHAOS v0.5 ===\n\n"
        "1. ADVANCED CLI SHELL & TERMINAL ENGINE\n"
        "   - Fish-style inline autosuggestions: lookahead in dim gray;\n"
        "     press Right Arrow or Tab to accept.\n"
        "   - Reverse Incremental History Search (Ctrl+R): interactive\n"
        "     history query with cycling, Enter to run, Tab/Right to edit,\n"
        "     ESC to cancel.\n"
        "   - Line editing shortcuts: Ctrl+L (clear screen), Ctrl+C (cancel),\n"
        "     Ctrl+U (erase line before cursor).\n"
        "   - Full ANSI escape sequence engine: SGR colors (fg/bg 30-37,\n"
        "     90-97, 40-47, 100-107), bold, dim, inverse, cursor movements,\n"
        "     erase line/display, tab stops. Test with 'ansi' or 'echo -e'.\n"
        "   - Intelligent ANSI-aware word wrap: words wrap cleanly at space\n"
        "     boundaries in terminal output and interactive prompt.\n\n"
        "2. AUTHENTIC GNU NANO 0.5.0 MICRO-EDITOR\n"
        "   - Full interactive terminal text editor ('nano <f>' or 'edit <f>').\n"
        "   - Inverted title header, 21-line text canvas with word wrap,\n"
        "     status bar, and two-row shortcut legend (^O WriteOut, ^K Cut,\n"
        "     ^U UnCut, ^X Exit).\n\n"
        "3. UNIVERSAL UNIX PIPELINES & SCRIPTING\n"
        "   - Inter-process communication via in-memory buffer: 'cmd1 | cmd2'.\n"
        "   - Semicolon command sequencing: 'cmd1; cmd2'.\n"
        "   - Environment variables: export VAR=val, env, unset VAR, $VAR.\n\n"
        "4. MODE 13h MODERN DESKTOP & IN-BROWSER MEDIA\n"
        "   - Multi-tier window drop shadows with active window cyan glow.\n"
        "   - Window split-screen edge snapping (left/right/top) and\n"
        "     maximize (Alt+Enter).\n"
        "   - Desktop file icons with MIME associations (.txt, .wav, .vid, .py).\n"
        "   - Consolidated in-browser universal media player with scrubber.\n"
        "   - Speed dial bookmarks bar and Ctrl+Tab browser tab cycling.\n"
        "   - PIT interrupt-driven background audio synthesizer (100 Hz).\n\n"
        "5. MODERN NETWORKING TOOLS\n"
        "   - 'wget <url> [-O file]': download web assets directly into VFS.\n"
        "   - 'curl <url>', 'ping <host>', 'ifconfig'.\n\n"
        "Type \"general\" to read the complete system manual.\n";

    static const char GENERAL_CONTENT[] =
        "=== ARCHAOS OPERATING SYSTEM — USER MANUAL & ARCHITECTURE ===\n\n"
        "OVERVIEW:\n"
        "ArchaOS is an advanced, lightweight 32-bit x86 monolithic OS\n"
        "running in Protected Mode (Ring 0) with a Virtual Memory Manager,\n"
        "VFS RAM disk, Mode 13h desktop compositor, and integrated AI.\n"
        "It is 100% in-memory and ephemeral (zero persistent disk footprint).\n\n"
        "SHELL COMMANDS:\n"
        "- System:       help, new, general, clear/cls, reboot, halt,\n"
        "                uptime, date, top, neofetch, fortune, theme <name>,\n"
        "                matrix, credits, meminfo, memtest\n"
        "- Filesystem:   ls, tree, cd, pwd, mkdir, touch, cat, less, nano,\n"
        "                head, tail, stat, hexdump, write, rm, cp, mv, wc,\n"
        "                grep, find\n"
        "- Networking:   ifconfig, ping <host>, curl <url>, wget <url>,\n"
        "                ports\n"
        "- Shell & Env:  export VAR=val, env, unset VAR, $VAR, cmd1; cmd2,\n"
        "                pipes |, Ctrl+R (history search), ansi / colors\n"
        "- AI Assistant: ai <question> (multi-turn chat), ai clear, ai history\n"
        "- Multimedia:   audio [play|stop|pause|next|prev|list], video, beep\n"
        "- Hardware:     pci [list|scan], serial [com1|com2], ata\n"
        "- Graphical:    gui (enters Mode 13h desktop environment)\n\n"
        "KEYBOARD SHORTCUTS:\n"
        "- Shell:        Tab / Right Arrow: accept inline suggestion\n"
        "                Ctrl+R: reverse history search | Ctrl+L: clear screen\n"
        "                Ctrl+C: cancel current line    | Ctrl+U: erase line\n"
        "- Nano:         Ctrl+O: save file              | Ctrl+X: exit editor\n"
        "                Ctrl+K: cut line               | Ctrl+U: uncut / paste\n"
        "- GUI Desktop:  Alt+Enter / F11: maximize      | Ctrl+W / Alt+F4: close\n"
        "                Ctrl+Tab: cycle browser tabs   | Ctrl++ / Ctrl+-: volume\n"
        "                ESC: return to CLI shell\n\n"
        "GRAPHICAL DESKTOP APPLICATIONS:\n"
        "- Web Browser:  NetSurf engine + JS runtime + NanoSVG vector decoder\n"
        "                Docked universal media player & Speed Dial bookmarks\n"
        "- Terminal:     Mode 13h terminal window with command capture\n"
        "- App Studio:   Integrated code editor and C/Python script runner\n"
        "- Utilities:    File Manager, Notepad, Painter, Calculator, Snake,\n"
        "                Minesweeper, Control Panel, Task Manager, Image Viewer\n";

    fs_write("/new", NEW_CONTENT, sizeof(NEW_CONTENT) - 1);
    fs_write("/general", GENERAL_CONTENT, sizeof(GENERAL_CONTENT) - 1);

    fs_mkdir("/sys");
    fs_write("/sys/ai_chat.log", "", 0);

    fs_mkdir("/audio");
    fs_mkdir("/media");
}
