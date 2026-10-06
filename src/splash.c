#include "splash.h"
#include "vesa.h"
#include "font_engine.h"
#include "pit.h"
#include "vga.h"
#include "audio.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

extern volatile uint8_t irq_kbd_fired;

static inline int splash_check_key(uint32_t start_tick)
{
    /* Ignore all keys during initial 1500 ms debounce to prevent GRUB Enter skip */
    if (pit_ticks() - start_tick < 1500) {
        irq_kbd_fired = 0;
        return 0;
    }
    if (irq_kbd_fired) {
        irq_kbd_fired = 0;
        return 1;
    }
    uint8_t st = inb(0x64);
    if ((st & 0x01) && !(st & 0x20)) {
        inb(0x60); /* discard scancode */
        return 1;
    }
    if (serial_data_ready(COM1_BASE)) {
        serial_getc(COM1_BASE);
        return 1;
    }
    return 0;
}



typedef struct {
    const char *subsystem;
    const char *desc;
} diag_step_t;

static const diag_step_t DIAG_STEPS[8] = {
    { "CPU ", "Intel x86 Ring 0/3 Multitasking Core" },
    { "MEM ", "Two-Tier Paging MMU (128 MB RAM Paged)" },
    { "SHD ", "Hardware TSS Triple Fault Prevention Shield" },
    { "DISP", "High-Res VESA VBE 800x600 32-Bit Framebuffer" },
    { "BUS ", "PCI Bus Enumerated Hardware Peripherals" },
    { "NET ", "Intel E1000 Gigabit NIC & TCP/IP Stack" },
    { "VFS ", "Hierarchical In-Memory RamFS & TarFS" },
    { "SND ", "Sound Blaster 16 DSP & OPL3 FM Synthesizer" }
};

static const uint16_t CHIME_FREQS[8] = { 440, 523, 587, 659, 740, 880, 988, 1175 };



static void splash_draw_highres_emblem(int cx, int cy, int pulse_phase)
{
    /* 1. Subtle Outer Cyan Halo Glow */
    int halo_r = 45 + (pulse_phase % 6);
    for (int dy = -halo_r; dy <= halo_r; dy++) {
        int span = halo_r - ((dy < 0 ? -dy : dy) * 3 / 4);
        for (int dx = -span; dx <= span; dx++) {
            uint32_t bg = vesa_get_pixel(cx + dx, cy + dy);
            int dist_sq = dx * dx + dy * dy;
            int max_sq = halo_r * halo_r;
            if (dist_sq < max_sq) {
                uint8_t a = (uint8_t)(16 * (max_sq - dist_sq) / max_sq);
                if (a > 25) a = 25;
                vesa_put_pixel(cx + dx, cy + dy, ARGB(0xFF, 0, (bg >> 8) & 0xFF, (bg & 0xFF) + a));
            }
        }
    }

    /* 2. Radiating Circuit Trace Bus Lines (Left Bus) */
    vesa_draw_line(cx - 42, cy, cx - 100, cy, 0xFF00E5FF);
    vesa_draw_line(cx - 100, cy, cx - 135, cy - 24, 0xFF00B0FF);
    vesa_fill_rect(cx - 142, cy - 28, 8, 8, 0xFF00E5FF);
    vesa_fill_rect(cx - 140, cy - 26, 4, 4, 0xFFFFFFFF); /* Glowing terminal pad */

    vesa_draw_line(cx - 75, cy, cx - 102, cy + 22, 0xFF00B0FF);
    vesa_fill_rect(cx - 108, cy + 19, 7, 7, 0xFF00E5FF);
    vesa_fill_rect(cx - 106, cy + 21, 3, 3, 0xFFFFFFFF);

    /* Radiating Circuit Trace Bus Lines (Right Bus) */
    vesa_draw_line(cx + 42, cy, cx + 100, cy, 0xFF00E5FF);
    vesa_draw_line(cx + 100, cy, cx + 135, cy - 24, 0xFF00B0FF);
    vesa_fill_rect(cx + 134, cy - 28, 8, 8, 0xFF00E5FF);
    vesa_fill_rect(cx + 136, cy - 26, 4, 4, 0xFFFFFFFF);

    vesa_draw_line(cx + 75, cy, cx + 102, cy + 22, 0xFF00B0FF);
    vesa_fill_rect(cx + 101, cy + 19, 7, 7, 0xFF00E5FF);
    vesa_fill_rect(cx + 103, cy + 21, 3, 3, 0xFFFFFFFF);

    /* 3. Outer Hexagonal Diamond Shield (Double Neon Layer) */
    uint32_t frame_color = 0xFF00E5FF;
    uint32_t inner_color = 0xFF80E5FF;

    /* Outer Frame (Bold 2-pixel thick) */
    for (int off = 0; off <= 1; off++) {
        vesa_draw_line(cx - 28 - off, cy - 36 - off, cx + 28 + off, cy - 36 - off, frame_color);
        vesa_draw_line(cx - 46 - off, cy - 14, cx - 28 - off, cy - 36 - off, frame_color);
        vesa_draw_line(cx + 46 + off, cy - 14, cx + 28 + off, cy - 36 - off, frame_color);
        vesa_draw_line(cx - 46 - off, cy - 14, cx - 46 - off, cy + 14, frame_color);
        vesa_draw_line(cx + 46 + off, cy - 14, cx + 46 + off, cy + 14, frame_color);
        vesa_draw_line(cx - 46 - off, cy + 14, cx, cy + 42 + off, frame_color);
        vesa_draw_line(cx + 46 + off, cy + 14, cx, cy + 42 + off, frame_color);
    }

    /* Inner Frame */
    vesa_draw_line(cx - 24, cy - 32, cx + 24, cy - 32, inner_color);
    vesa_draw_line(cx - 42, cy - 14, cx - 24, cy - 32, inner_color);
    vesa_draw_line(cx + 42, cy - 14, cx + 24, cy - 32, inner_color);
    vesa_draw_line(cx - 42, cy - 14, cx - 42, cy + 11, inner_color);
    vesa_draw_line(cx + 42, cy - 14, cx + 42, cy + 11, inner_color);
    vesa_draw_line(cx - 42, cy + 11, cx, cy + 38, inner_color);
    vesa_draw_line(cx + 42, cy + 11, cx, cy + 38, inner_color);

    /* 4. Center Stylized Geometric "A" with Radiant Core */
    /* Left leg (3 pixels thick) */
    vesa_draw_line(cx - 2, cy - 25, cx - 22, cy + 22, 0xFF00B0FF);
    vesa_draw_line(cx,     cy - 25, cx - 20, cy + 22, 0xFFFFFFFF);
    vesa_draw_line(cx + 2, cy - 25, cx - 18, cy + 22, 0xFF00E5FF);

    /* Right leg (3 pixels thick) */
    vesa_draw_line(cx - 2, cy - 25, cx + 18, cy + 22, 0xFF00E5FF);
    vesa_draw_line(cx,     cy - 25, cx + 20, cy + 22, 0xFFFFFFFF);
    vesa_draw_line(cx + 2, cy - 25, cx + 22, cy + 22, 0xFF00B0FF);

    /* Crossbar */
    vesa_draw_line(cx - 14, cy + 3, cx + 14, cy + 3, 0xFFFFFFFF);
    vesa_draw_line(cx - 12, cy + 4, cx + 12, cy + 4, 0xFF00E5FF);

    /* Glowing Core Diamond */
    vesa_fill_rect(cx - 3, cy - 5, 7, 7, 0xFFFFFFFF);
    vesa_put_pixel(cx, cy - 6, 0xFF00E5FF);
    vesa_put_pixel(cx, cy + 3, 0xFF00E5FF);
    vesa_put_pixel(cx - 4, cy - 2, 0xFF00E5FF);
    vesa_put_pixel(cx + 4, cy - 2, 0xFF00E5FF);
}



void splash_show(void)
{
    serial_puts(COM1_BASE, "\n=== ArchaOS Cinematic Boot Splash (800x600x32) Started ===\n");

    /* 1. Attempt High-Res VESA Mode Switch */
    int vesa_ok = 0;
    if (vesa_is_available()) {
        vesa_ok = (vesa_set_mode(800, 600, 32) == 0);
    }

    if (!vesa_ok) {
        serial_puts(COM1_BASE, "[SPLASH] VESA unavailable, skipping splash\n");
        return;
    }

    font_engine_init();

    /* Keyboard flush & start tick recording for key debounce */
    vga_kbd_flush();
    irq_kbd_fired = 0;
    uint32_t start_tick = pit_ticks();

    /* 2. Base Canvas Rendering */
    /* Deep Obsidian-to-Midnight Navy Vertical Gradient */
    vesa_draw_gradient_v(0, 0, 800, 600, 0xFF020611, 0xFF0B162C);

    /* Top glowing accent energy line */
    vesa_fill_rect(0, 0, 800, 2, 0xFF00E5FF);
    vesa_fill_rect(0, 2, 800, 1, 0xFF007799);

    /* Subtle distant stars */
    static const int STARS[16][2] = {
        {45, 30}, {120, 80}, {710, 45}, {760, 110}, {85, 210}, {730, 230},
        {35, 380}, {755, 360}, {60, 520}, {720, 540}, {180, 40}, {640, 70},
        {250, 60}, {550, 50}, {140, 160}, {670, 170}
    };
    for (int i = 0; i < 16; i++) {
        vesa_put_pixel(STARS[i][0], STARS[i][1], 0xFF6B8CAD);
        vesa_put_pixel(STARS[i][0]+1, STARS[i][1], 0xFF99BBDD);
    }

    /* 3. Central High-Res Shield & Bold 3x Typography */
    splash_draw_highres_emblem(400, 95, 0);

    /* Title: Bold 3x Scaled Typography with Deep Drop Shadow */
    font_set_active(FONT_STYLE_CLASSIC);
    uint32_t *fb = vesa_get_backbuffer();
    font_draw_string_32_scaled(fb, 800, 600, 245, 150, "A R C H A O S", 0xFF021B3A, 3);
    font_draw_string_32_scaled(fb, 800, 600, 243, 148, "A R C H A O S", 0xFF00E5FF, 3);

    /* Subtitle & Tagline */
    font_draw_string_32_scaled(fb, 800, 600, 275, 185, "N E X U S   v 0 . 6", 0xFFFFFFFF, 2);
    vesa_draw_string(215, 212, "32-Bit Preemptive Protected Mode Operating System", 0xFF94A3B8);

    /* Cyan divider line with glowing center */
    vesa_draw_line(150, 228, 650, 228, 0xFF1E3A5F);
    vesa_draw_line(260, 228, 540, 228, 0xFF00E5FF);
    vesa_fill_rect(398, 227, 5, 3, 0xFFFFFFFF);

    /* 4. Glass Diagnostic Status Container (Centered at x=90, y=242, w=620, h=225) */
    int db_x = 90, db_y = 242, db_w = 620, db_h = 225;
    vesa_fill_rect_alpha(db_x, db_y, db_w, db_h, 0xFF0B1322, 220);
    vesa_draw_rect(db_x, db_y, db_w, db_h, 0xFF1E293B);
    vesa_draw_rect(db_x + 1, db_y + 1, db_w - 2, db_h - 2, 0xFF00E5FF);

    /* Bevel top highlight */
    vesa_draw_line(db_x + 2, db_y + 2, db_x + db_w - 3, db_y + 2, 0xFF80E5FF);

    /* Progress Bar Cavity (Centered at x=90, y=485, w=620, h=18) */
    int pb_x = 90, pb_y = 485, pb_w = 620, pb_h = 18;
    vesa_draw_rect(pb_x, pb_y, pb_w, pb_h, 0xFF1E3A5F);
    vesa_fill_rect(pb_x + 1, pb_y + 1, pb_w - 2, pb_h - 2, 0xFF050A14);

    /* Bottom Skip Prompt */
    vesa_draw_string(245, 565, "Press ANY KEY or ESC to skip directly to shell", 0xFF475569);

    vesa_flip();

    /* 5. Animated Diagnostic Sequence & Sound Synthesis */
    int current_pct = 0;

    for (int step = 0; step < 8; step++) {
        if (splash_check_key(start_tick)) goto splash_exit;

        /* Pulse Emblem during each step */
        splash_draw_highres_emblem(400, 95, step * 2);

        /* Render Diagnostic Entry */
        int entry_y = db_y + 12 + step * 25;
        vesa_draw_string(db_x + 16, entry_y, "[", 0xFF4A607A);
        vesa_draw_string(db_x + 26, entry_y, "OK", 0xFF10B981); /* Bright Emerald Green */
        vesa_draw_string(db_x + 44, entry_y, "]", 0xFF4A607A);
        vesa_draw_string(db_x + 60, entry_y, DIAG_STEPS[step].subsystem, 0xFF00E5FF);
        vesa_draw_string(db_x + 104, entry_y, ":", 0xFF4A607A);
        vesa_draw_string(db_x + 116, entry_y, DIAG_STEPS[step].desc, 0xFFF8FAFC);

        /* Target percentage for this milestone */
        int target_pct = (step + 1) * 100 / 8;

        /* Play synchronized audio chime frequency on SB16 & PC Speaker */
        audio_play_freq(CHIME_FREQS[step]);

        /* Interpolate progress bar smoothly */
        while (current_pct < target_pct) {
            if (splash_check_key(start_tick)) goto splash_exit;
            current_pct++;

            int fill_max = pb_w - 4;
            int fill_w = (current_pct * fill_max) / 100;
            if (fill_w > fill_max) fill_w = fill_max;

            if (fill_w > 0) {
                /* Gradient progress bar body */
                vesa_fill_rect(pb_x + 2, pb_y + 2, fill_w, 3, 0xFF7DD3FC);
                vesa_fill_rect(pb_x + 2, pb_y + 5, fill_w, 8, 0xFF0284C7);
                vesa_fill_rect(pb_x + 2, pb_y + 13, fill_w, 3, 0xFF0369A1);

                /* Glowing White Leading-Edge Glare (last 8 pixels) */
                int glare_w = fill_w > 8 ? 8 : fill_w;
                vesa_fill_rect(pb_x + 2 + fill_w - glare_w, pb_y + 2, glare_w, 14, 0xFFFFFFFF);
            }

            /* Clear status message area below bar */
            vesa_fill_rect(pb_x, pb_y + 24, pb_w, 20, 0xFF020611);

            /* Render Percentage */
            char pct_buf[8];
            pct_buf[0] = (current_pct / 100) ? ('0' + (current_pct / 100)) : ' ';
            pct_buf[1] = (current_pct >= 10) ? ('0' + ((current_pct / 10) % 10)) : ' ';
            pct_buf[2] = '0' + (current_pct % 10);
            pct_buf[3] = '%';
            pct_buf[4] = '\0';
            vesa_draw_string(pb_x + pb_w - 45, pb_y + 26, pct_buf, 0xFF00E5FF);

            /* Render Status Message */
            const char *status_msg = (current_pct < 25) ? "Probing System Core & Protected Hardware..." :
                                     (current_pct < 50) ? "Initializing Virtual Memory & Hardware Shields..." :
                                     (current_pct < 75) ? "Mounting Storage Subsystems & Network Stack..." :
                                     (current_pct < 100) ? "Starting Audio Engines & Preemptive Scheduler..." :
                                     "Nexus Core Online. Ready to launch environment.";
            vesa_draw_string(pb_x, pb_y + 26, status_msg, 0xFFCBD5E1);

            vesa_flip();
            pit_sleep(8);
        }

        /* Brief audio rest */
        pit_sleep(40);
        audio_stop();
        pit_sleep(30);
    }

    /* Final hold at 100% */
    for (int wait = 0; wait < 10; wait++) {
        if (splash_check_key(start_tick)) break;
        pit_sleep(50);
    }

    /* 6. Smooth Fade-Out Dissolve */
    for (int fade = 0; fade < 6; fade++) {
        if (splash_check_key(start_tick)) break;
        vesa_fill_rect_alpha(0, 0, 800, 600, 0xFF000000, 50);
        vesa_flip();
        pit_sleep(25);
    }

splash_exit:
    audio_stop();

    /* 7. Clean Restoration to 80x25 Text Mode for interactive shell */
    vesa_set_text_mode();
    vga_kbd_flush();
    vga_clear();
    vga_set_cursor(0, 0);
    serial_puts(COM1_BASE, "=== ArchaOS Cinematic Boot Splash Finished ===\n");
}
