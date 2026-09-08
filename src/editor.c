// src/editor.c — ArchaOS GNU nano interactive text editor

#include "editor.h"
#include "fs.h"
#include "vga.h"
#include "mm.h"
#include "idt.h"
#include <stdint.h>
#include <stddef.h>

#define ED_MAX    4096
#define VGA_W     VGA_WIDTH
#define ED_VGA_H  VGA_HEIGHT
#define ED_TOP    1                  /* first text row */
#define ED_BOT    21                 /* last text row (rows 1..21) */
#define ED_ROWS   (ED_BOT - ED_TOP + 1)
#define ED_MSG_Y  22                 /* message / status row */
#define ED_KEY_Y1 23                 /* shortcut bar row 1 */
#define ED_KEY_Y2 24                 /* shortcut bar row 2 */

#define ATTR_HEADER   0x70           /* black on light gray */
#define ATTR_KEY_INV  0x70           /* black on light gray (for "^X", "^O") */
#define ATTR_KEY_TXT  0x07           /* white on black (for labels) */
#define ATTR_MSG      0x0E           /* yellow on black */
#undef  ATTR_NORMAL
#define ATTR_NORMAL   0x07           /* white on black */
#define ATTR_BRIGHT   0x0F           /* bright white */

static void ed_cur(int x, int y)
{
    vga_set_cursor(x, y);
}

static void ed_putc(int x, int y, char c, uint8_t attr)
{
    if (x < 0 || x >= VGA_W || y < 0 || y >= ED_VGA_H) return;
    vga_write_cell(x, y, ((uint16_t)(unsigned char)c) | ((uint16_t)attr << 8));
}

static void ed_str(int x, int y, const char *s, uint8_t attr)
{
    while (*s && x < VGA_W) ed_putc(x++, y, *s++, attr);
}

static void ed_fill(int y, uint8_t attr)
{
    for (int x = 0; x < VGA_W; x++) ed_putc(x, y, ' ', attr);
}

static void draw_shortcut(int x, int y, const char *key, const char *desc)
{
    ed_str(x, y, key, ATTR_KEY_INV);
    ed_putc(x + 2, y, ' ', ATTR_KEY_TXT);
    ed_str(x + 3, y, desc, ATTR_KEY_TXT);
}

static int ed_word_len(const char *buf, int start, int total) {
    int l = 0;
    while (start + l < total && buf[start + l] != ' ' && buf[start + l] != '\t' && buf[start + l] != '\n') {
        l++;
    }
    return l;
}

/* ── draw the whole nano screen ────────────────────── */
static void ed_draw(const char *buf, int len, int cur, const char *fname, int modified, const char *msg)
{
    /* ---- Row 0: nano header bar ---- */
    ed_fill(0, ATTR_HEADER);
    ed_str(2,  0, "GNU nano 0.5.0", ATTR_HEADER);
    ed_str(26, 0, "File: ",        ATTR_HEADER);
    ed_str(32, 0, fname,           ATTR_HEADER);
    if (modified) {
        ed_str(68, 0, "[Modified]", ATTR_HEADER);
    }

    /* ---- Rows 1..21: clear text area ---- */
    for (int y = ED_TOP; y <= ED_BOT; y++) ed_fill(y, ATTR_NORMAL);

    /* ---- Render text buffer with word wrap ---- */
    int tx = 0, ty = ED_TOP;
    for (int i = 0; i < len && ty <= ED_BOT; i++)
    {
        if (buf[i] == '\n')
        {
            tx = 0; ty++;
        }
        else if (buf[i] == ' ' || buf[i] == '\t')
        {
            if (tx >= VGA_W - 1) { tx = 0; ty++; }
            else {
                if (ty <= ED_BOT) ed_putc(tx, ty, buf[i], ATTR_BRIGHT);
                tx++;
            }
        }
        else
        {
            if (i == 0 || buf[i - 1] == ' ' || buf[i - 1] == '\t' || buf[i - 1] == '\n') {
                int wlen = ed_word_len(buf, i, len);
                if (tx + wlen > VGA_W && wlen < VGA_W && tx > 0) {
                    tx = 0; ty++;
                }
            }
            if (tx >= VGA_W) { tx = 0; ty++; }
            if (ty <= ED_BOT)
                ed_putc(tx, ty, buf[i], ATTR_BRIGHT);
            tx++;
        }
    }

    /* ---- Compute cursor screen position with word wrap ---- */
    int cx = 0, cy = ED_TOP;
    int cur_line = 1, cur_col = 1;
    for (int i = 0; i < cur; i++)
    {
        if (buf[i] == '\n') {
            cur_line++;
            cur_col = 1;
            cx = 0;
            cy++;
        } else if (buf[i] == ' ' || buf[i] == '\t') {
            cur_col++;
            if (cx >= VGA_W - 1) { cx = 0; cy++; }
            else cx++;
        } else {
            cur_col++;
            if (i == 0 || buf[i - 1] == ' ' || buf[i - 1] == '\t' || buf[i - 1] == '\n') {
                int wlen = ed_word_len(buf, i, len);
                if (cx + wlen > VGA_W && wlen < VGA_W && cx > 0) {
                    cx = 0; cy++;
                }
            }
            if (cx >= VGA_W) { cx = 0; cy++; }
            cx++;
        }
    }
    if (cx >= VGA_W) cx = VGA_W - 1;
    if (cy > ED_BOT) cy = ED_BOT;

    /* ---- Row 22: Message / Status Bar ---- */
    ed_fill(ED_MSG_Y, ATTR_NORMAL);
    if (msg && msg[0]) {
        ed_str(2, ED_MSG_Y, msg, ATTR_MSG);
    } else {
        char status[64];
        char num[16];
        int pos = 0;
        status[pos++] = '['; status[pos++] = ' ';
        status[pos++] = 'L'; status[pos++] = 'n'; status[pos++] = ' ';
        int v = cur_line, di = 0;
        do { num[di++] = '0' + (v % 10); v /= 10; } while (v);
        for (int k = di - 1; k >= 0; k--) status[pos++] = num[k];

        status[pos++] = ','; status[pos++] = ' ';
        status[pos++] = 'C'; status[pos++] = 'o'; status[pos++] = 'l'; status[pos++] = ' ';
        v = cur_col; di = 0;
        do { num[di++] = '0' + (v % 10); v /= 10; } while (v);
        for (int k = di - 1; k >= 0; k--) status[pos++] = num[k];

        status[pos++] = ' '; status[pos++] = ']';
        status[pos] = '\0';
        ed_str(2, ED_MSG_Y, status, ATTR_KEY_TXT);
    }

    /* ---- Rows 23 & 24: Classic GNU nano Shortcut Bar ---- */
    ed_fill(ED_KEY_Y1, ATTR_NORMAL);
    draw_shortcut(1,  ED_KEY_Y1, "^G", "Get Help");
    draw_shortcut(20, ED_KEY_Y1, "^O", "WriteOut");
    draw_shortcut(40, ED_KEY_Y1, "^W", "Where Is");
    draw_shortcut(60, ED_KEY_Y1, "^K", "Cut Text");

    ed_fill(ED_KEY_Y2, ATTR_NORMAL);
    draw_shortcut(1,  ED_KEY_Y2, "^X", "Exit");
    draw_shortcut(20, ED_KEY_Y2, "^R", "Read File");
    draw_shortcut(40, ED_KEY_Y2, "^\\", "Replace");
    draw_shortcut(60, ED_KEY_Y2, "^U", "Paste Text");

    ed_cur(cx, cy);
}

/* ── wait for next key scancode via IRQ1 ────────────── */
static uint8_t ed_scancode(void)
{
    while (!irq_kbd_fired) asm volatile("sti; hlt");
    uint8_t sc    = last_scancode;
    irq_kbd_fired = 0;
    return sc;
}

/* ── keymaps ─────────────────────────────────────────── */
static const char map_lo[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',
};
static const char map_hi[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',
};

static char ed_translate(uint8_t sc, int shift, int caps)
{
    if (sc >= 128) return 0;
    char c = shift ? map_hi[sc] : map_lo[sc];
    if (caps && !shift)
    {
        if (c >= 'a' && c <= 'z') c -= 32;
    }
    return c;
}

/* ── main nano entry point ───────────────────────────── */
void editor_open(const char *path)
{
    static char buf[ED_MAX];
    static char cut_buf[512] = "";
    static char msg_buf[64] = "";
    int len = 0, cur = 0;
    int shift = 0, ctrl = 0, caps = 0, ext = 0;
    int modified = 0;

    /* Load existing file content */
    buf[0] = '\0';
    msg_buf[0] = '\0';
    if (fs_cat(path, buf, ED_MAX) == 0) {
        while (buf[len]) len++;
    }

    ed_draw(buf, len, cur, path, modified, msg_buf);

    while (1)
    {
        uint8_t sc = ed_scancode();

        /* extended prefix — set flag, wait for actual key */
        if (sc == 0xE0) { ext = 1; continue; }

        /* key-release events */
        if (sc & 0x80)
        {
            uint8_t b = sc & 0x7F;
            if (b == 0x2A || b == 0x36) shift = 0;
            if (b == 0x1D)              ctrl  = 0;
            ext = 0;
            continue;
        }

        /* modifier press */
        if (sc == 0x2A || sc == 0x36) { shift = 1; ext = 0; continue; }
        if (sc == 0x1D)               { ctrl  = 1; ext = 0; continue; }
        if (sc == 0x3A)               { caps  = !caps; ext = 0; continue; }

        /* Clear transient message on typing or movement */
        msg_buf[0] = '\0';

        /* ---- extended (arrow / nav) keys ---- */
        if (ext)
        {
            ext = 0;
            switch (sc)
            {
                case 0x4B:  /* Left */
                    if (cur > 0) cur--;
                    break;
                case 0x4D:  /* Right */
                    if (cur < len) cur++;
                    break;
                case 0x48:  /* Up */
                    if (cur > 0)
                    {
                        cur--;
                        while (cur > 0 && buf[cur - 1] != '\n') cur--;
                    }
                    break;
                case 0x50:  /* Down */
                    while (cur < len && buf[cur] != '\n') cur++;
                    if (cur < len) cur++;
                    break;
                case 0x47:  /* Home */
                    while (cur > 0 && buf[cur - 1] != '\n') cur--;
                    break;
                case 0x4F:  /* End */
                    while (cur < len && buf[cur] != '\n') cur++;
                    break;
                case 0x53:  /* Delete */
                    if (cur < len)
                    {
                        for (int i = cur; i < len - 1; i++) buf[i] = buf[i + 1];
                        len--;
                        buf[len] = '\0';
                        modified = 1;
                    }
                    break;
            }
            ed_draw(buf, len, cur, path, modified, msg_buf);
            continue;
        }

        /* ---- ESC or Ctrl+X: Exit nano ---- */
        if (sc == 0x01 || (ctrl && sc == 0x2D)) break;

        /* ---- Ctrl+O or Ctrl+S: WriteOut / Save to disk ---- */
        if (ctrl && (sc == 0x18 || sc == 0x1F))
        {
            size_t slen = 0;
            while (buf[slen]) slen++;
            fs_write(path, buf, slen);
            modified = 0;

            /* Set status message */
            int di = 0;
            char num[16];
            int v = (int)slen;
            do { num[di++] = '0' + (v % 10); v /= 10; } while (v);
            int mi = 0;
            const char *prefix = "[ Wrote ";
            while (*prefix) msg_buf[mi++] = *prefix++;
            for (int k = di - 1; k >= 0; k--) msg_buf[mi++] = num[k];
            const char *suffix = " bytes ]";
            while (*suffix) msg_buf[mi++] = *suffix++;
            msg_buf[mi] = '\0';

            ed_draw(buf, len, cur, path, modified, msg_buf);
            continue;
        }

        /* ---- Ctrl+K: Cut line ---- */
        if (ctrl && sc == 0x25)
        {
            /* Find start and end of current line */
            int ls = cur;
            while (ls > 0 && buf[ls - 1] != '\n') ls--;
            int le = cur;
            while (le < len && buf[le] != '\n') le++;
            if (le < len && buf[le] == '\n') le++;

            int clen = le - ls;
            if (clen > 0) {
                int ci = 0;
                for (int i = ls; i < le && ci < 510; i++) cut_buf[ci++] = buf[i];
                cut_buf[ci] = '\0';

                for (int i = ls; i < len - clen; i++) buf[i] = buf[i + clen];
                len -= clen;
                buf[len] = '\0';
                cur = ls;
                modified = 1;
                const char *kmsg = "[ Cut 1 line ]";
                int mi = 0; while (*kmsg) msg_buf[mi++] = *kmsg++; msg_buf[mi] = '\0';
            }
            ed_draw(buf, len, cur, path, modified, msg_buf);
            continue;
        }

        /* ---- Ctrl+U: Paste line / buffer ---- */
        if (ctrl && sc == 0x16)
        {
            int clen = 0;
            while (cut_buf[clen]) clen++;
            if (clen > 0 && len + clen < ED_MAX - 1) {
                for (int i = len; i >= cur; i--) buf[i + clen] = buf[i];
                for (int i = 0; i < clen; i++) buf[cur + i] = cut_buf[i];
                len += clen;
                cur += clen;
                buf[len] = '\0';
                modified = 1;
            }
            ed_draw(buf, len, cur, path, modified, msg_buf);
            continue;
        }

        /* ---- Backspace ---- */
        if (sc == 0x0E)
        {
            if (cur > 0)
            {
                for (int i = cur - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; cur--;
                buf[len] = '\0';
                modified = 1;
                ed_draw(buf, len, cur, path, modified, msg_buf);
            }
            continue;
        }

        /* ---- Enter ---- */
        if (sc == 0x1C && len < ED_MAX - 1)
        {
            for (int i = len; i > cur; i--) buf[i] = buf[i - 1];
            buf[cur++] = '\n'; len++;
            buf[len] = '\0';
            modified = 1;
            ed_draw(buf, len, cur, path, modified, msg_buf);
            continue;
        }

        /* ---- Regular printable character ---- */
        char c = ed_translate(sc, shift, caps);
        if (c && c != '\b' && len < ED_MAX - 1)
        {
            for (int i = len; i > cur; i--) buf[i] = buf[i - 1];
            buf[cur++] = c; len++;
            buf[len] = '\0';
            modified = 1;
            ed_draw(buf, len, cur, path, modified, msg_buf);
        }
    }

    /* Restore VGA text mode & clear */
    vga_clear();
}


