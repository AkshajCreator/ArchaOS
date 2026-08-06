// src/editor.c — minimal full-screen text editor

#include "editor.h"
#include "fs.h"
#include "vga.h"
#include "mm.h"
#include "idt.h"
#include <stdint.h>
#include <stddef.h>

#define ED_MAX    2048
/* VGA_WIDTH and VGA_HEIGHT come from vga.h */
#define VGA_W  VGA_WIDTH
#define ED_VGA_H VGA_HEIGHT
#define ED_TOP    1                  /* first text row (row 0 = top status)  */
#define ED_BOT    (ED_VGA_H - 2)     /* last text row  (row 24 = bot status) */
#define ED_ROWS   (ED_BOT - ED_TOP + 1)   /* = 23 usable rows                */

static uint16_t *vga = (uint16_t *)0xB8000;

#define ATTR_STATUS  0x2F   /* black on green  */
#undef  ATTR_NORMAL
#define ATTR_NORMAL  0x07   /* white on black  */
#define ATTR_BRIGHT  0x0F   /* bright white    */

/* ── port I/O ─────────────────────────────────────────── */
static inline void outb(uint16_t port, uint8_t val)
{ asm volatile("outb %0,%1"::"a"(val),"Nd"(port)); }

/* ── low-level VGA helpers ────────────────────────────── */
static void ed_cur(int x, int y)
{
    uint16_t pos = (uint16_t)(y * VGA_W + x);
    outb(0x3D4, 0x0F); outb(0x3D5,  pos & 0xFF);
    outb(0x3D4, 0x0E); outb(0x3D5, (pos >> 8) & 0xFF);
}

static void ed_putc(int x, int y, char c, uint8_t attr)
{ vga[y * VGA_W + x] = ((uint16_t)(unsigned char)c) | ((uint16_t)attr << 8); }

static void ed_str(int x, int y, const char *s, uint8_t attr)
{ while (*s) ed_putc(x++, y, *s++, attr); }

static void ed_fill(int y, uint8_t attr)
{ for (int x = 0; x < VGA_W; x++) ed_putc(x, y, ' ', attr); }

/* ── draw the whole editor screen ────────────────────── */
static void ed_draw(const char *buf, int len, int cur, const char *fname)
{
    /* ---- top status bar (row 0) ---- */
    ed_fill(0, ATTR_STATUS);
    ed_str(1,  0, "EDITOR: ",             ATTR_STATUS);
    ed_str(9,  0, fname,                  ATTR_STATUS);
    ed_str(55, 0, "Ctrl+S=Save  ESC=Quit", ATTR_STATUS);

    /* ---- clear text area (rows ED_TOP .. ED_BOT) ---- */
    for (int y = ED_TOP; y <= ED_BOT; y++) ed_fill(y, ATTR_NORMAL);

    /* ---- render text ----
     * Key rule: advance row BEFORE drawing when we hit column 80 or \n,
     * but only draw the character if it's a printable (not \n itself).       */
    int tx = 0, ty = ED_TOP;
    for (int i = 0; i < len && ty <= ED_BOT; i++)
    {
        if (buf[i] == '\n')
        {
            tx = 0; ty++;            /* newline: just move down, draw nothing */
        }
        else
        {
            if (tx >= VGA_W) { tx = 0; ty++; }   /* soft-wrap before drawing */
                if (ty <= ED_BOT)
                    ed_putc(tx, ty, buf[i], ATTR_BRIGHT);
            tx++;
        }
    }

    /* ---- compute cursor screen position ---- */
    int cx = 0, cy = ED_TOP;
    for (int i = 0; i < cur && cy <= ED_BOT; i++)
    {
        if (buf[i] == '\n')
        { cx = 0; cy++; }
        else
        {
            if (cx >= VGA_W) { cx = 0; cy++; }
            cx++;
        }
    }
    /* clamp cursor inside text area */
    if (cy > ED_BOT) { cy = ED_BOT; cx = VGA_W - 1; }

    /* ---- bottom status bar (row ED_VGA_H-1 = 24) ---- */
    ed_fill(ED_VGA_H - 1, ATTR_STATUS);

    /* "Ln N  Col N" */
    char tmp[32];
    int  ln  = cy - ED_TOP + 1;
    int  col = cx + 1;
    int  pi  = 0;

    tmp[pi++] = 'L'; tmp[pi++] = 'n'; tmp[pi++] = ' ';
    /* itoa ln */
    char digits[8]; int di = 0;
    int v = ln; do { digits[di++] = '0' + v % 10; v /= 10; } while (v);
    for (int i = di - 1; i >= 0; i--) tmp[pi++] = digits[i];

    tmp[pi++] = ' '; tmp[pi++] = ' ';
    tmp[pi++] = 'C'; tmp[pi++] = 'o'; tmp[pi++] = 'l'; tmp[pi++] = ' ';
    /* itoa col */
    di = 0; v = col;
    do { digits[di++] = '0' + v % 10; v /= 10; } while (v);
    for (int i = di - 1; i >= 0; i--) tmp[pi++] = digits[i];
    tmp[pi] = '\0';

    ed_str(1, ED_VGA_H - 1, tmp, ATTR_STATUS);

    ed_cur(cx, cy);
}

/* ── wait for next key scancode via IRQ1 ────────────── */
static uint8_t ed_scancode(void)
{
    while (!irq_kbd_fired) asm volatile("hlt");
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

/* ── translate scancode → character ─────────────────── */
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

/* ── main editor entry point ─────────────────────────── */
void editor_open(const char *path)
{
    static char buf[ED_MAX];
    int len = 0, cur = 0;
    int shift = 0, ctrl = 0, caps = 0, ext = 0;

    /* load existing file content */
    buf[0] = '\0';
    if (fs_cat(path, buf, ED_MAX) == 0) {
        while (buf[len]) len++;
    }

    ed_draw(buf, len, cur, path);

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
            /* do NOT clear ext here — release of extended key is 0xE0 0x??|0x80 */
            continue;
        }

        /* modifier press */
        if (sc == 0x2A || sc == 0x36) { shift = 1; ext = 0; continue; }
        if (sc == 0x1D)               { ctrl  = 1; ext = 0; continue; }
        if (sc == 0x3A)               { caps  = !caps; ext = 0; continue; } /* Caps Lock */

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
                    case 0x48:  /* Up — move to same column on previous line */
                        if (cur > 0)
                        {
                            cur--;
                            while (cur > 0 && buf[cur - 1] != '\n') cur--;
                        }
                        break;
                    case 0x50:  /* Down — move to start of next line */
                        while (cur < len && buf[cur] != '\n') cur++;
                        if (cur < len) cur++;
                        break;
                    case 0x47:  /* Home — start of line */
                        while (cur > 0 && buf[cur - 1] != '\n') cur--;
                        break;
                    case 0x4F:  /* End — end of line */
                        while (cur < len && buf[cur] != '\n') cur++;
                        break;
                }
                ed_draw(buf, len, cur, path);
                continue;
            }

            /* ---- ESC: quit without save ---- */
            if (sc == 0x01) break;

            /* ---- Ctrl+S: save and quit ---- */
            if (ctrl && sc == 0x1F)
            {
                size_t slen = 0;
                while (buf[slen]) slen++;
                fs_write(path, buf, slen);
                break;
            }

            /* ---- Backspace ---- */
            if (sc == 0x0E)
            {
                if (cur > 0)
                {
                    for (int i = cur - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                    len--; cur--;
                    buf[len] = '\0';
                    ed_draw(buf, len, cur, path);
                }
                continue;
            }

            /* ---- Delete (sc 0x53, non-extended on some keyboards) ---- */
            if (sc == 0x53)
            {
                if (cur < len)
                {
                    for (int i = cur; i < len - 1; i++) buf[i] = buf[i + 1];
                    len--;
                    buf[len] = '\0';
                    ed_draw(buf, len, cur, path);
                }
                continue;
            }

            /* ---- Enter ---- */
            if (sc == 0x1C && len < ED_MAX - 1)
            {
                for (int i = len; i > cur; i--) buf[i] = buf[i - 1];
                buf[cur++] = '\n'; len++;
                buf[len] = '\0';
                ed_draw(buf, len, cur, path);
                continue;
            }

            /* ---- Regular printable character ---- */
            char c = ed_translate(sc, shift, caps);
            if (c && c != '\b' && len < ED_MAX - 1)
            {
                for (int i = len; i > cur; i--) buf[i] = buf[i - 1];
                buf[cur++] = c; len++;
                buf[len] = '\0';
                ed_draw(buf, len, cur, path);
            }
    }

    /* restore shell */
    vga_clear();
    /* Return to vga_prompt() which is already on the call stack —
     * do NOT call vga_prompt() here or it creates an infinite recursive loop. */
}

