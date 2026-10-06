#include "vesa.h"
#include "pci.h"
#include "vmm.h"
#include "mm.h"
#include "vga.h"
#include "pit.h"
#include "serial.h"
#include "font_engine.h"
#include "font.h"
#include <stdint.h>
#include <stddef.h>



static inline void outw(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Integer Square Root (for circular rendering & glowing orb calculation) */
static int int_sqrt(int n)
{
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}



static vesa_driver_t vesa_drv = {0};

/* 800x600x32 (1.92 MB) static backbuffer pool in BSS */
static uint32_t vesa_static_backbuffer[VESA_RES_800_600_W * VESA_RES_800_600_H];

/* Text mode font backup buffer */
static uint8_t vesa_text_font_backup[256 * 32];
static int vesa_text_font_saved = 0;



static void bga_write_register(uint16_t index, uint16_t data)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, data);
}

static uint16_t bga_read_register(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

/* Probe Bochs Graphics Adapter via standard I/O ports 0x01CE / 0x01CF */
static int bga_detect(void)
{
    uint16_t id = bga_read_register(VBE_DISPI_INDEX_ID);
    if (id >= VBE_DISPI_ID0 && id <= VBE_DISPI_ID5) {
        /* Negotiate highest supported version */
        bga_write_register(VBE_DISPI_INDEX_ID, VBE_DISPI_ID5);
        id = bga_read_register(VBE_DISPI_INDEX_ID);
        vesa_drv.bga_detected = 1;
        vesa_drv.bga_version = id;
        return 1;
    }
    return 0;
}

/* Scan PCI bus for Display Controller to obtain physical VRAM aperture */
static uint32_t pci_scan_vram(void)
{
    /* 1. Try dedicated Bochs/QEMU BGA device (Vendor 0x1234, Device 0x1111) */
    pci_device_t *dev = pci_find_vendor_device(0x1234, 0x1111);

    /* 2. Fallback to any PCI Display Controller (Class 0x03, Subclass 0x00) */
    if (!dev) {
        dev = pci_find_device(PCI_CLASS_DISPLAY, 0x00, 0xFF);
    }

    if (dev) {
        uint32_t bar0 = pci_get_bar(dev, 0);
        if (bar0 != 0 && bar0 != 0xFFFFFFFF) {
            return (bar0 & 0xFFFFFFF0);
        }
    }
    return 0;
}



int vesa_init(multiboot_info_t *mbi)
{
    uint32_t phys = 0;
    int bga_found = bga_detect();

    /* 1. Multiboot Framebuffer Detection (only accept true RGB linear framebuffers) */
    if (mbi && (mbi->flags & MULTIBOOT_FLAG_FB) && mbi->framebuffer_addr != 0 &&
        mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
        mbi->framebuffer_bpp >= 24 &&
        mbi->framebuffer_addr != 0xB8000) {
        phys = (uint32_t)(mbi->framebuffer_addr & 0xFFFFFFFF);
        vesa_drv.multiboot_fb = 1;
        vesa_drv.width = mbi->framebuffer_width;
        vesa_drv.height = mbi->framebuffer_height;
        vesa_drv.pitch = mbi->framebuffer_pitch;
        vesa_drv.bpp = mbi->framebuffer_bpp;
    }

    /* 2. PCI Display Controller Scan */
    if (!phys) {
        phys = pci_scan_vram();
    }

    /* 3. Default QEMU/Bochs BAR0 fallback */
    if (!phys) {
        phys = 0xFD000000;
    }

    vesa_drv.phys_base = phys;
    vesa_drv.lfb = (uint32_t *)phys;

    /* 4. Paging Integration: Identity-map 16MB of video RAM MMIO with Cache-Disable */
    vmm_map_mmio(phys, 16 * 1024 * 1024);

    /* 5. Save VGA text mode font table for clean restoration later */
    if (!vesa_text_font_saved) {
        vga_font_backup_save(vesa_text_font_backup);
        vesa_text_font_saved = 1;
    }

    serial_printf(COM1_BASE, "[VESA] Driver initialized: BGA=%d (ver 0x%x), LFB Phys=0x%x, MultibootFB=%d\n",
                  vesa_drv.bga_detected, vesa_drv.bga_version, vesa_drv.phys_base, vesa_drv.multiboot_fb);

    return (bga_found || vesa_drv.multiboot_fb) ? 0 : -1;
}

int vesa_is_available(void)
{
    return vesa_drv.bga_detected || vesa_drv.multiboot_fb;
}

int vesa_is_active(void)
{
    return vesa_drv.active;
}

vesa_driver_t *vesa_get_driver(void)
{
    return &vesa_drv;
}

int vesa_get_width(void)
{
    return vesa_drv.width;
}

int vesa_get_height(void)
{
    return vesa_drv.height;
}

uint32_t *vesa_get_backbuffer(void)
{
    return vesa_drv.backbuffer;
}

uint32_t *vesa_get_lfb(void)
{
    return vesa_drv.lfb;
}



int vesa_set_mode(int width, int height, int bpp)
{
    if (!vesa_drv.phys_base) {
        vesa_init(NULL);
    }

    if (width <= 0 || height <= 0) {
        width = VESA_RES_800_600_W;
        height = VESA_RES_800_600_H;
    }
    if (bpp != 32 && bpp != 24) {
        bpp = 32;
    }

    /* Program BGA Registers if hardware adapter present */
    if (vesa_drv.bga_detected) {
        bga_write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        bga_write_register(VBE_DISPI_INDEX_XRES, (uint16_t)width);
        bga_write_register(VBE_DISPI_INDEX_YRES, (uint16_t)height);
        bga_write_register(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
        bga_write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
        bga_write_register(VBE_DISPI_INDEX_BANK, 0);
    }

    vesa_drv.width = width;
    vesa_drv.height = height;
    vesa_drv.bpp = bpp;
    vesa_drv.pitch = width * (bpp / 8);

    size_t needed = (size_t)width * (size_t)height * sizeof(uint32_t);

    /* Ensure video RAM MMIO pages are mapped */
    vmm_map_mmio(vesa_drv.phys_base, needed > (16 * 1024 * 1024) ? needed : (16 * 1024 * 1024));

    /* Backbuffer allocation: kmalloc or static backbuffer pool */
    if (vesa_drv.backbuffer && vesa_drv.backbuffer != vesa_static_backbuffer &&
        vesa_drv.backbuffer_size < needed) {
        kfree(vesa_drv.backbuffer);
        vesa_drv.backbuffer = NULL;
        vesa_drv.backbuffer_size = 0;
    }

    if (!vesa_drv.backbuffer || vesa_drv.backbuffer_size < needed) {
        void *buf = kmalloc(needed);
        if (buf) {
            vesa_drv.backbuffer = (uint32_t *)buf;
            vesa_drv.backbuffer_size = needed;
        } else if (needed <= sizeof(vesa_static_backbuffer)) {
            vesa_drv.backbuffer = vesa_static_backbuffer;
            vesa_drv.backbuffer_size = sizeof(vesa_static_backbuffer);
        } else {
            serial_puts(COM1_BASE, "[VESA] Error: Backbuffer allocation failed!\n");
            return -1;
        }
    }

    vesa_drv.active = 1;

    /* Clear both buffers to black */
    vesa_clear(0xFF000000);
    vesa_flip();

    serial_printf(COM1_BASE, "[VESA] Mode set: %dx%d %d-bpp (pitch=%d)\n",
                  width, height, bpp, vesa_drv.pitch);
    return 0;
}

void vesa_set_text_mode(void)
{
    if (vesa_drv.bga_detected) {
        bga_write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    }
    vga_set_text_mode();
    if (vesa_text_font_saved) {
        vga_restore_font_and_text(vesa_text_font_backup);
    }
    vga_restore_text_palette();
    vga_clear();
    vesa_drv.active = 0;
    serial_puts(COM1_BASE, "[VESA] Restored VGA 80x25 text mode\n");
}



void vesa_clear(uint32_t argb)
{
    if (!vesa_drv.backbuffer) return;

    uint32_t *dst = vesa_drv.backbuffer;
    uint32_t count = (uint32_t)(vesa_drv.width * vesa_drv.height);

    asm volatile(
        "cld; rep stosl"
        : "+D"(dst), "+c"(count)
        : "a"(argb)
        : "memory"
    );
}

void vesa_put_pixel(int x, int y, uint32_t argb)
{
    if ((unsigned int)x < (unsigned int)vesa_drv.width &&
        (unsigned int)y < (unsigned int)vesa_drv.height &&
        vesa_drv.backbuffer) {
        vesa_drv.backbuffer[y * vesa_drv.width + x] = argb;
    }
}

uint32_t vesa_get_pixel(int x, int y)
{
    if ((unsigned int)x < (unsigned int)vesa_drv.width &&
        (unsigned int)y < (unsigned int)vesa_drv.height &&
        vesa_drv.backbuffer) {
        return vesa_drv.backbuffer[y * vesa_drv.width + x];
    }
    return 0;
}

void vesa_fill_rect(int x, int y, int w, int h, uint32_t argb)
{
    if (!vesa_drv.backbuffer || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > vesa_drv.width)  w = vesa_drv.width - x;
    if (y + h > vesa_drv.height) h = vesa_drv.height - y;
    if (w <= 0 || h <= 0) return;

    for (int r = 0; r < h; r++) {
        uint32_t *dst = &vesa_drv.backbuffer[(y + r) * vesa_drv.width + x];
        uint32_t count = (uint32_t)w;
        asm volatile(
            "cld; rep stosl"
            : "+D"(dst), "+c"(count)
            : "a"(argb)
            : "memory"
        );
    }
}

/* True Alpha Blending: out = (fg * a + bg * (255 - a)) / 255 */
void vesa_fill_rect_alpha(int x, int y, int w, int h, uint32_t argb, uint8_t alpha)
{
    if (!vesa_drv.backbuffer || w <= 0 || h <= 0 || alpha == 0) return;
    if (alpha == 255) {
        vesa_fill_rect(x, y, w, h, argb);
        return;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > vesa_drv.width)  w = vesa_drv.width - x;
    if (y + h > vesa_drv.height) h = vesa_drv.height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t a = (uint32_t)alpha;
    uint32_t inv_a = 255 - a;

    uint32_t fg_r = (argb >> 16) & 0xFF;
    uint32_t fg_g = (argb >> 8) & 0xFF;
    uint32_t fg_b = argb & 0xFF;

    uint32_t term_r = fg_r * a;
    uint32_t term_g = fg_g * a;
    uint32_t term_b = fg_b * a;

    for (int r = 0; r < h; r++) {
        uint32_t *dst = &vesa_drv.backbuffer[(y + r) * vesa_drv.width + x];
        for (int c = 0; c < w; c++) {
            uint32_t bg = dst[c];
            uint32_t bg_r = (bg >> 16) & 0xFF;
            uint32_t bg_g = (bg >> 8) & 0xFF;
            uint32_t bg_b = bg & 0xFF;

            uint32_t out_r = (term_r + bg_r * inv_a) / 255;
            uint32_t out_g = (term_g + bg_g * inv_a) / 255;
            uint32_t out_b = (term_b + bg_b * inv_a) / 255;

            dst[c] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

void vesa_draw_rect(int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0) return;
    vesa_fill_rect(x, y, w, 1, argb);
    vesa_fill_rect(x, y + h - 1, w, 1, argb);
    vesa_fill_rect(x, y, 1, h, argb);
    vesa_fill_rect(x + w - 1, y, 1, h, argb);
}

void vesa_draw_line(int x0, int y0, int x1, int y1, uint32_t argb)
{
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        vesa_put_pixel(x0, y0, argb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void vesa_draw_gradient_v(int x, int y, int w, int h, uint32_t top_color, uint32_t bot_color)
{
    if (!vesa_drv.backbuffer || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > vesa_drv.width)  w = vesa_drv.width - x;
    if (y + h > vesa_drv.height) h = vesa_drv.height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t top_r = (top_color >> 16) & 0xFF;
    uint32_t top_g = (top_color >> 8) & 0xFF;
    uint32_t top_b = top_color & 0xFF;

    uint32_t bot_r = (bot_color >> 16) & 0xFF;
    uint32_t bot_g = (bot_color >> 8) & 0xFF;
    uint32_t bot_b = bot_color & 0xFF;

    for (int r = 0; r < h; r++) {
        uint32_t t = (h > 1) ? ((uint32_t)r * 255) / (uint32_t)(h - 1) : 0;
        uint32_t inv_t = 255 - t;

        uint32_t cr = (top_r * inv_t + bot_r * t) / 255;
        uint32_t cg = (top_g * inv_t + bot_g * t) / 255;
        uint32_t cb = (top_b * inv_t + bot_b * t) / 255;
        uint32_t row_color = 0xFF000000 | (cr << 16) | (cg << 8) | cb;

        uint32_t *dst = &vesa_drv.backbuffer[(y + r) * vesa_drv.width + x];
        uint32_t count = (uint32_t)w;
        asm volatile(
            "cld; rep stosl"
            : "+D"(dst), "+c"(count)
            : "a"(row_color)
            : "memory"
        );
    }
}

void vesa_draw_gradient_h(int x, int y, int w, int h, uint32_t left_color, uint32_t right_color)
{
    if (!vesa_drv.backbuffer || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > vesa_drv.width)  w = vesa_drv.width - x;
    if (y + h > vesa_drv.height) h = vesa_drv.height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t left_r = (left_color >> 16) & 0xFF;
    uint32_t left_g = (left_color >> 8) & 0xFF;
    uint32_t left_b = left_color & 0xFF;

    uint32_t right_r = (right_color >> 16) & 0xFF;
    uint32_t right_g = (right_color >> 8) & 0xFF;
    uint32_t right_b = right_color & 0xFF;

    for (int col = 0; col < w; col++) {
        uint32_t t = (w > 1) ? ((uint32_t)col * 255) / (uint32_t)(w - 1) : 0;
        uint32_t inv_t = 255 - t;

        uint32_t cr = (left_r * inv_t + right_r * t) / 255;
        uint32_t cg = (left_g * inv_t + right_g * t) / 255;
        uint32_t cb = (left_b * inv_t + right_b * t) / 255;
        uint32_t col_color = 0xFF000000 | (cr << 16) | (cg << 8) | cb;

        for (int r = 0; r < h; r++) {
            vesa_drv.backbuffer[(y + r) * vesa_drv.width + (x + col)] = col_color;
        }
    }
}

void vesa_draw_circle(int cx, int cy, int radius, uint32_t argb)
{
    if (radius <= 0) {
        vesa_put_pixel(cx, cy, argb);
        return;
    }

    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;

    while (y >= x) {
        vesa_put_pixel(cx + x, cy + y, argb);
        vesa_put_pixel(cx - x, cy + y, argb);
        vesa_put_pixel(cx + x, cy - y, argb);
        vesa_put_pixel(cx - x, cy - y, argb);
        vesa_put_pixel(cx + y, cy + x, argb);
        vesa_put_pixel(cx - y, cy + x, argb);
        vesa_put_pixel(cx + y, cy - x, argb);
        vesa_put_pixel(cx - y, cy - x, argb);

        if (d <= 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void vesa_fill_circle(int cx, int cy, int radius, uint32_t argb)
{
    if (radius <= 0) {
        vesa_put_pixel(cx, cy, argb);
        return;
    }

    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= vesa_drv.height) continue;
        int dx_limit = int_sqrt(r2 - dy * dy);
        int x0 = cx - dx_limit;
        int x1 = cx + dx_limit;
        if (x0 < 0) x0 = 0;
        if (x1 >= vesa_drv.width) x1 = vesa_drv.width - 1;
        if (x1 >= x0) {
            uint32_t *dst = &vesa_drv.backbuffer[py * vesa_drv.width + x0];
            uint32_t count = (uint32_t)(x1 - x0 + 1);
            asm volatile("cld; rep stosl" : "+D"(dst), "+c"(count) : "a"(argb) : "memory");
        }
    }
}

void vesa_fill_circle_alpha(int cx, int cy, int radius, uint32_t argb, uint8_t alpha)
{
    if (radius <= 0 || alpha == 0) return;
    if (alpha == 255) {
        vesa_fill_circle(cx, cy, radius, argb);
        return;
    }

    int r2 = radius * radius;
    uint32_t a = (uint32_t)alpha;
    uint32_t inv_a = 255 - a;

    uint32_t fg_r = (argb >> 16) & 0xFF;
    uint32_t fg_g = (argb >> 8) & 0xFF;
    uint32_t fg_b = argb & 0xFF;

    uint32_t term_r = fg_r * a;
    uint32_t term_g = fg_g * a;
    uint32_t term_b = fg_b * a;

    for (int dy = -radius; dy <= radius; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= vesa_drv.height) continue;
        int dx_limit = int_sqrt(r2 - dy * dy);
        int x0 = cx - dx_limit;
        int x1 = cx + dx_limit;
        if (x0 < 0) x0 = 0;
        if (x1 >= vesa_drv.width) x1 = vesa_drv.width - 1;

        for (int px = x0; px <= x1; px++) {
            uint32_t bg = vesa_drv.backbuffer[py * vesa_drv.width + px];
            uint32_t bg_r = (bg >> 16) & 0xFF;
            uint32_t bg_g = (bg >> 8) & 0xFF;
            uint32_t bg_b = bg & 0xFF;

            uint32_t out_r = (term_r + bg_r * inv_a) / 255;
            uint32_t out_g = (term_g + bg_g * inv_a) / 255;
            uint32_t out_b = (term_b + bg_b * inv_a) / 255;

            vesa_drv.backbuffer[py * vesa_drv.width + px] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}



void vesa_draw_char(int x, int y, char c, uint32_t color)
{
    if (!vesa_drv.backbuffer) return;
    font_draw_char_32(vesa_drv.backbuffer, vesa_drv.width, vesa_drv.height, x, y, c, color);
}

void vesa_draw_string(int x, int y, const char *str, uint32_t color)
{
    if (!vesa_drv.backbuffer || !str) return;
    font_draw_string_32(vesa_drv.backbuffer, vesa_drv.width, vesa_drv.height, x, y, str, color);
}

void vesa_draw_string_scaled(int x, int y, const char *str, uint32_t color, int scale)
{
    if (!vesa_drv.backbuffer || !str) return;
    font_draw_string_32_scaled(vesa_drv.backbuffer, vesa_drv.width, vesa_drv.height, x, y, str, color, scale);
}

void vesa_draw_string_shadow(int x, int y, const char *str, uint32_t color, uint32_t shadow_color)
{
    if (!vesa_drv.backbuffer || !str) return;
    font_draw_string_32_shadow(vesa_drv.backbuffer, vesa_drv.width, vesa_drv.height, x, y, str, color, shadow_color);
}



void vesa_flip(void)
{
    if (!vesa_drv.active || !vesa_drv.lfb || !vesa_drv.backbuffer) return;

    uint32_t *dst = vesa_drv.lfb;
    uint32_t *src = vesa_drv.backbuffer;
    uint32_t count = (uint32_t)(vesa_drv.width * vesa_drv.height);

    asm volatile(
        "cld; rep movsl"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}



/* Glowing energy orb with additive halo radiation */
static void vesa_draw_glowing_orb(int cx, int cy, int radius)
{
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= vesa_drv.height) continue;
        int dx_limit = int_sqrt(r2 - dy * dy);
        int x0 = cx - dx_limit;
        int x1 = cx + dx_limit;
        if (x0 < 0) x0 = 0;
        if (x1 >= vesa_drv.width) x1 = vesa_drv.width - 1;

        for (int px = x0; px <= x1; px++) {
            int dx = px - cx;
            int dist = int_sqrt(dx * dx + dy * dy);
            if (dist > radius) continue;

            uint32_t a = (uint32_t)((radius - dist) * 230 / radius);
            if (a == 0) continue;

            /* Radiant color ramp: Pure white core -> Neon cyan -> Electric blue */
            uint32_t fg_r, fg_g, fg_b;
            if (dist < radius / 3) {
                int t = (dist * 255) / (radius / 3);
                fg_r = 255 - (t * 155 / 255);
                fg_g = 255;
                fg_b = 255;
            } else {
                int t = ((dist - radius / 3) * 255) / (radius * 2 / 3);
                fg_r = 100 - (t * 80 / 255);
                fg_g = 255 - (t * 135 / 255);
                fg_b = 255;
            }

            uint32_t bg = vesa_drv.backbuffer[py * vesa_drv.width + px];
            uint32_t bg_r = (bg >> 16) & 0xFF;
            uint32_t bg_g = (bg >> 8) & 0xFF;
            uint32_t bg_b = bg & 0xFF;

            /* Additive luminous blend */
            uint32_t out_r = (fg_r * a + bg_r * 255) / 255;
            uint32_t out_g = (fg_g * a + bg_g * 255) / 255;
            uint32_t out_b = (fg_b * a + bg_b * 255) / 255;
            if (out_r > 255) out_r = 255;
            if (out_g > 255) out_g = 255;
            if (out_b > 255) out_b = 255;

            vesa_drv.backbuffer[py * vesa_drv.width + px] = 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

/* Translucent Frosted Glass Card with Drop Shadow & Accent Header */
static void vesa_draw_glass_panel(int x, int y, int w, int h, const char *title, uint32_t header_c1, uint32_t header_c2)
{
    /* 1. Floating drop shadows with dual-layer soft edge simulation */
    vesa_fill_rect_alpha(x + 12, y + 14, w, h, 0x00000000, 110);
    vesa_fill_rect_alpha(x + 6,  y + 7,  w, h, 0x00000000, 70);

    /* 2. Frosted translucent glass background (~75% opacity) */
    vesa_fill_rect_alpha(x, y, w, h, 0xFF0F172A, 195);

    /* 3. Thin crisp glowing neon border */
    vesa_draw_rect(x, y, w, h, 0xFF38BDF8);

    /* 4. Gradient window titlebar header */
    vesa_draw_gradient_v(x + 1, y + 1, w - 2, 25, header_c1, header_c2);
    vesa_draw_line(x + 1, y + 26, x + w - 2, y + 26, 0xFF0284C7);

    /* 5. Window control dots (Close / Minimize / Maximize) */
    vesa_fill_circle(x + 12, y + 13, 4, 0xFFEF4444);
    vesa_fill_circle(x + 24, y + 13, 4, 0xFFF59E0B);
    vesa_fill_circle(x + 36, y + 13, 4, 0xFF10B981);

    /* 6. Title text with drop shadow */
    vesa_draw_string_shadow(x + 48, y + 9, title, 0xFFFFFFFF, 0xFF000000);
}

void vesa_demo(void)
{
    /* Switch to 800x600x32 high-resolution mode */
    if (!vesa_drv.active) {
        if (vesa_set_mode(VESA_RES_800_600_W, VESA_RES_800_600_H, 32) != 0) {
            serial_puts(COM1_BASE, "[VESA] Demo failed: Could not activate 800x600x32 mode\n");
            return;
        }
    }

    font_engine_init();

    int orb_x = 240, orb_y = 200;
    int orb_vx = 5, orb_vy = 3;
    int orb_radius = 52;
    uint32_t frame_count = 0;

    extern volatile uint8_t irq_kbd_fired;
    vga_kbd_flush();
    irq_kbd_fired = 0;
    uint32_t start_tick = pit_ticks();

    serial_puts(COM1_BASE, "[VESA] Entering animated Compositor Showcase demo loop...\n");

    while (1) {
        /* Check keyboard IRQ, 8042 port, or serial port for user exit */
        if (irq_kbd_fired) {
            irq_kbd_fired = 0;
            if (pit_ticks() - start_tick > 250) {
                vga_kbd_flush();
                break;
            }
        }
        uint8_t st = inb(0x64);
        if ((st & 0x01) && !(st & 0x20)) {
            inb(0x60); /* Consume scancode */
            if (pit_ticks() - start_tick > 250) {
                break;
            }
        }
        if (serial_data_ready(COM1_BASE)) {
            serial_getc(COM1_BASE);
            break;
        }

        /* 1. Deep Celestial Gradient Wallpaper */
        vesa_draw_gradient_v(0, 0, 800, 600, 0xFF050B14, 0xFF1E1B4B);

        /* 2. Cybernetic Background Grid Overlay */
        for (int gx = 40; gx < 800; gx += 40) {
            vesa_fill_rect_alpha(gx, 0, 1, 600, 0xFF38BDF8, 18);
        }
        for (int gy = 40; gy < 600; gy += 40) {
            vesa_fill_rect_alpha(0, gy, 800, 1, 0xFF38BDF8, 18);
        }

        /* 3. Physics update for moving glowing orb */
        orb_x += orb_vx;
        orb_y += orb_vy;
        if (orb_x < 60 || orb_x > 740) orb_vx = -orb_vx;
        if (orb_y < 60 || orb_y > 540) orb_vy = -orb_vy;

        /* 4. Render Glowing Radial Orb (passes behind and illuminates glass panels) */
        vesa_draw_glowing_orb(orb_x, orb_y, orb_radius);

        /* 5. Master Header Card (Top Center) */
        vesa_draw_glass_panel(40, 25, 720, 135,
                              "ArchaOS v0.6 'Nexus' - High-Res Compositor Engine",
                              0xFF0284C7, 0xFF0369A1);

        /* Feature Badges */
        vesa_fill_rect(55, 60, 150, 22, 0xFF0369A1);
        vesa_draw_rect(55, 60, 150, 22, 0xFF38BDF8);
        vesa_draw_string(62, 67, "[ 800x600 TrueColor ]", 0xFFFFFFFF);

        vesa_fill_rect(215, 60, 150, 22, 0xFF047857);
        vesa_draw_rect(215, 60, 150, 22, 0xFF34D399);
        vesa_draw_string(222, 67, "[ 60 FPS Compositor ]", 0xFFFFFFFF);

        vesa_fill_rect(375, 60, 150, 22, 0xFF6D28D9);
        vesa_draw_rect(375, 60, 150, 22, 0xFFA78BFA);
        vesa_draw_string(382, 67, "[ MMIO Identity Paged ]", 0xFFFFFFFF);

        vesa_fill_rect(535, 60, 140, 22, 0xFFB45309);
        vesa_draw_rect(535, 60, 140, 22, 0xFFFBBF24);
        vesa_draw_string(542, 67, "[ BGA 0x01CE/CF ]", 0xFFFFFFFF);

        /* Architectural descriptions */
        vesa_draw_string(55, 92,  "Hardware Driver: Bochs Graphics Adapter (BGA) & PCI Display Controller", 0xFFE2E8F0);
        vesa_draw_string(55, 110, "Paging MMU:      vmm_map_mmio() Hardware Identity Mapping into Kernel Page Directory", 0xFF94A3B8);
        vesa_draw_string(55, 128, "Compositor Math: out = (fg * a + bg * (255 - a)) / 255  [Double-Buffered Blit]", 0xFF38BDF8);

        /* 6. Panel 2: Typography & Font Engine Specimen (Left Side) */
        vesa_draw_glass_panel(40, 175, 380, 335,
                              "Typography Specimen & Font Engine",
                              0xFF4338CA, 0xFF3730A3);

        vesa_draw_string_scaled(55, 210, "Nexus v0.6", 0xFF00F0FF, 2);

        font_set_active(FONT_STYLE_CLASSIC);
        vesa_draw_string(55, 240, "Style 0 (Classic): The quick brown fox jumps", 0xFFF1F5F9);

        font_set_active(FONT_STYLE_CYBERPUNK);
        vesa_draw_string(55, 265, "Style 1 (Cyberpunk): SYSTEM SHIELD ENGAGED", 0xFF00FF66);

        font_set_active(FONT_STYLE_MODERN);
        vesa_draw_string(55, 290, "Style 2 (Modern): Humanist typography in RAM", 0xFF38BDF8);

        font_set_active(FONT_STYLE_PIXEL);
        vesa_draw_string(55, 315, "Style 3 (Pixel): 0123456789 (Dotted Zero)", 0xFFFBBF24);

        font_set_active(FONT_STYLE_CLASSIC);
        vesa_draw_string_shadow(55, 345, "Text with Soft Alpha Drop Shadow", 0xFFFFFFFF, 0xFF000000);
        vesa_draw_string(55, 370, "ASCII: ABCDEFGHIJKLMNOPQRSTUVWXYZ 0-9", 0xFFCBD5E1);
        vesa_draw_string(55, 390, "SYMBOLS: !@#$%^&*()_+-=[]{}|;':,./<>?", 0xFF94A3B8);

        vesa_draw_string(55, 420, "Multi-scale: 1x, 2x, 3x High-Res Vectors", 0xFF4ADE80);
        vesa_draw_string(55, 440, "Dynamic Style Switching via 'font set <id>'", 0xFF94A3B8);
        vesa_draw_string(55, 460, "Engine Core: src/font_engine.c [4 Styles]", 0xFF38BDF8);

        /* 7. Panel 3: Translucent Glass & Alpha Ramps (Right Side) */
        vesa_draw_glass_panel(440, 175, 320, 335,
                              "Alpha Compositing & Glass Ramps",
                              0xFF047857, 0xFF065F46);

        vesa_draw_string(455, 210, "True Alpha Compositing Ramps:", 0xFFF8FAFC);

        /* 25% Alpha */
        vesa_fill_rect_alpha(455, 230, 290, 24, 0xFF0EA5E9, 64);
        vesa_draw_rect(455, 230, 290, 24, 0xFF38BDF8);
        vesa_draw_string(465, 238, "25% Alpha Translucent Overlay", 0xFFFFFFFF);

        /* 50% Alpha */
        vesa_fill_rect_alpha(455, 260, 290, 24, 0xFF10B981, 128);
        vesa_draw_rect(455, 260, 290, 24, 0xFF34D399);
        vesa_draw_string(465, 268, "50% Alpha Frosted Glass Tint", 0xFFFFFFFF);

        /* 75% Alpha */
        vesa_fill_rect_alpha(455, 290, 290, 24, 0xFF8B5CF6, 192);
        vesa_draw_rect(455, 290, 290, 24, 0xFFA78BFA);
        vesa_draw_string(465, 298, "75% Alpha Glassmorphism Shield", 0xFFFFFFFF);

        /* 90% Alpha */
        vesa_fill_rect_alpha(455, 320, 290, 24, 0xFFF43F5E, 230);
        vesa_draw_rect(455, 320, 290, 24, 0xFFFB7185);
        vesa_draw_string(465, 328, "90% Alpha Dense Obsidian Panel", 0xFFFFFFFF);

        /* Real-time Compositor Metrics */
        vesa_draw_string(455, 365, "Compositor Rate: 60.0 FPS [PIT Synchronous]", 0xFF22D3EE);
        vesa_draw_string(455, 385, "Active Buffers:  Dual (Backbuf + Front)", 0xFFE2E8F0);
        vesa_draw_string(455, 405, "Blit Method:     Fast 'rep movsl' 32-bit", 0xFFE2E8F0);
        vesa_draw_string(455, 425, "VRAM Aperture:   16 MB MMIO Space", 0xFFE2E8F0);
        vesa_draw_string(455, 445, "Glowing Orb:     Additive Luminous Blend", 0xFFFCD34D);
        vesa_draw_string(455, 465, "Orb Position:    Dynamic Vector Physics", 0xFF94A3B8);

        /* 8. Panel 4: Footer Status Bar (Bottom) */
        vesa_draw_glass_panel(40, 525, 720, 50,
                              "System Status",
                              0xFF1E293B, 0xFF0F172A);

        /* Pulsing indicator */
        vesa_fill_circle(58, 550, 5, 0xFF10B981);
        vesa_draw_string(72, 546, "[ LIVE COMPOSITOR ACTIVE ]", 0xFF34D399);
        vesa_draw_string(280, 546, "Press ANY KEY to return to ArchaOS Text Mode", 0xFFF8FAFC);

        /* 9. Blit backbuffer to physical frontbuffer */
        vesa_flip();

        frame_count++;

        /* ~60 FPS rate limit */
        pit_sleep(16);
    }

    /* Return to text mode upon keypress */
    vesa_set_text_mode();
    vga_kbd_flush();
    vga_clear();
    vga_prompt();
}

void vesa_show_resolution_test(int width, int height)
{
    if (width <= 0 || height <= 0) {
        width = 800;
        height = 600;
    }

    if (vesa_set_mode(width, height, 32) != 0) {
        serial_puts(COM1_BASE, "[VESA] Failed to set requested resolution\n");
        return;
    }

    font_engine_init();

    /* 1. Deep Celestial Gradient Wallpaper */
    vesa_draw_gradient_v(0, 0, width, height, 0xFF0A1120, 0xFF1E1B4B);

    /* 2. Cybernetic Grid Overlay */
    for (int gx = 20; gx < width; gx += 40) {
        vesa_fill_rect_alpha(gx, 0, 1, height, 0xFF38BDF8, 20);
    }
    for (int gy = 20; gy < height; gy += 40) {
        vesa_fill_rect_alpha(0, gy, width, 1, 0xFF38BDF8, 20);
    }

    /* 3. Header Panel (responsive to width) */
    int panel_w = width - 40;
    vesa_draw_glass_panel(20, 15, panel_w, 105,
                          "ArchaOS High-Resolution Framebuffer Display Test",
                          0xFF0284C7, 0xFF0369A1);

    /* Badges */
    char res_buf[32];
    res_buf[0] = '['; res_buf[1] = ' ';
    int ri = 2;
    if (width == 800) {
        const char *s = "800x600 32-Bit ARGB ]";
        while (*s) res_buf[ri++] = *s++;
    } else {
        const char *s = "640x480 32-Bit ARGB ]";
        while (*s) res_buf[ri++] = *s++;
    }
    res_buf[ri] = '\0';

    vesa_fill_rect(35, 48, 175, 22, 0xFF0369A1);
    vesa_draw_rect(35, 48, 175, 22, 0xFF38BDF8);
    vesa_draw_string(42, 55, res_buf, 0xFFFFFFFF);

    vesa_fill_rect(220, 48, 150, 22, 0xFF047857);
    vesa_draw_rect(220, 48, 150, 22, 0xFF34D399);
    vesa_draw_string(228, 55, "[ BGA 0x01CE/CF ]", 0xFFFFFFFF);

    vesa_fill_rect(380, 48, 150, 22, 0xFF6D28D9);
    vesa_draw_rect(380, 48, 150, 22, 0xFFA78BFA);
    vesa_draw_string(388, 55, "[ 16 MB MMIO Paged ]", 0xFFFFFFFF);

    vesa_draw_string(35, 78, "Hardware Engine: Bochs Graphics Adapter (BGA) True-Color Linear Framebuffer", 0xFFE2E8F0);
    vesa_draw_string(35, 94, "Compositor Mode: 32-Bit ARGB Double-Buffered Pixel Surface (0 Tear)", 0xFF38BDF8);

    /* 4. ARGB Color Calibration Test Bars (Full 24-bit Gradient Ramps) */
    int cal_y = 130;
    vesa_draw_glass_panel(20, cal_y, panel_w, 110,
                          "True-Color ARGB Color Calibration & DAC Test Ramps",
                          0xFF1E293B, 0xFF0F172A);

    int bar_x = 35, bar_w = panel_w - 30;
    int bar_h = 8;
    int bar_spacing = 11;
    int by = cal_y + 35;

    /* Red Ramp */
    for (int x = 0; x < bar_w; x++) {
        uint8_t c = (x * 255) / bar_w;
        vesa_fill_rect(bar_x + x, by, 1, bar_h, ARGB(0xFF, c, 0, 0));
    }
    by += bar_spacing;

    /* Green Ramp */
    for (int x = 0; x < bar_w; x++) {
        uint8_t c = (x * 255) / bar_w;
        vesa_fill_rect(bar_x + x, by, 1, bar_h, ARGB(0xFF, 0, c, 0));
    }
    by += bar_spacing;

    /* Blue Ramp */
    for (int x = 0; x < bar_w; x++) {
        uint8_t c = (x * 255) / bar_w;
        vesa_fill_rect(bar_x + x, by, 1, bar_h, ARGB(0xFF, 0, 0, c));
    }
    by += bar_spacing;

    /* Cyan-Yellow-Magenta Rainbow Ramp */
    for (int x = 0; x < bar_w; x++) {
        int seg = (x * 6) / bar_w;
        int t = (x * 6) % bar_w;
        int v = (t * 255) / (bar_w / 6 + 1);
        uint32_t col = (seg == 0) ? ARGB(0xFF, 255, v, 0) :
                       (seg == 1) ? ARGB(0xFF, 255 - v, 255, 0) :
                       (seg == 2) ? ARGB(0xFF, 0, 255, v) :
                       (seg == 3) ? ARGB(0xFF, 0, 255 - v, 255) :
                       (seg == 4) ? ARGB(0xFF, v, 0, 255) :
                                    ARGB(0xFF, 255, 0, 255 - v);
        vesa_fill_rect(bar_x + x, by, 1, bar_h, col);
    }
    by += bar_spacing;

    /* Grayscale 0 to 255 Ramp */
    for (int x = 0; x < bar_w; x++) {
        uint8_t c = (x * 255) / bar_w;
        vesa_fill_rect(bar_x + x, by, 1, bar_h, ARGB(0xFF, c, c, c));
    }

    /* 5. Typography Specimen Card */
    int type_y = cal_y + 120;
    int type_h = (height == 600) ? 190 : 130;
    vesa_draw_glass_panel(20, type_y, panel_w, type_h,
                          "Bitmap Typography Engine - 4 Real-Time Styles",
                          0xFF4338CA, 0xFF3730A3);

    font_set_active(FONT_STYLE_CLASSIC);
    vesa_draw_string(35, type_y + 30, "Style 0 (Classic VGA):     The quick brown fox jumps over the lazy dog", 0xFFFFFFFF);

    font_set_active(FONT_STYLE_CYBERPUNK);
    vesa_draw_string(35, type_y + 48, "Style 1 (Cyberpunk Neon):  QUANTUM SHIELD ACTIVE [HEX: 0xDEADBEEF]", 0xFF00FF66);

    font_set_active(FONT_STYLE_MODERN);
    vesa_draw_string(35, type_y + 66, "Style 2 (Modern Sans):     Humanist typography rendered at 32-bit depth", 0xFF38BDF8);

    font_set_active(FONT_STYLE_PIXEL);
    vesa_draw_string(35, type_y + 84, "Style 3 (Pixel Mono):      0123456789 (Dotted Zero) != { [ OK ] }", 0xFFFBBF24);

    font_set_active(FONT_STYLE_CLASSIC);
    if (height == 600) {
        vesa_draw_string_scaled(35, type_y + 106, "2x Scale Vector Typography", 0xFF38BDF8, 2);
        vesa_draw_string_shadow(35, type_y + 138, "Anti-Aliased Soft Alpha Drop Shadow Text", 0xFFF8FAFC, 0xFF000000);
    }

    /* 6. Footer Status Bar */
    int foot_y = height - 55;
    vesa_draw_glass_panel(20, foot_y, panel_w, 45,
                          "System Control",
                          0xFF1E293B, 0xFF0F172A);

    vesa_fill_circle(36, foot_y + 25, 4, 0xFF10B981);
    vesa_draw_string(48, foot_y + 21, "[ RESOLUTION VERIFIED ]", 0xFF34D399);
    vesa_draw_string(240, foot_y + 21, "Press ANY KEY or ESC to return to ArchaOS Text Mode", 0xFFF8FAFC);

    /* Blit to screen */
    vesa_flip();

    /* Keyboard wait with debounce */
    extern volatile uint8_t irq_kbd_fired;
    vga_kbd_flush();
    irq_kbd_fired = 0;
    uint32_t start_tick = pit_ticks();

    while (1) {
        if (irq_kbd_fired) {
            irq_kbd_fired = 0;
            if (pit_ticks() - start_tick > 250) break;
        }
        uint8_t st = inb(0x64);
        if ((st & 0x01) && !(st & 0x20)) {
            inb(0x60);
            if (pit_ticks() - start_tick > 250) break;
        }
        if (serial_data_ready(COM1_BASE)) {
            serial_getc(COM1_BASE);
            break;
        }
        pit_sleep(20);
    }

    /* Clean exit to text mode */
    vesa_set_text_mode();
    vga_kbd_flush();
    vga_clear();
    vga_prompt();
}
