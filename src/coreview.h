#ifndef COREVIEW_H
#define COREVIEW_H

#include <stdint.h>
#include <stddef.h>

#define COREVIEW_REG_COUNT 12

/* Register index identifiers for bit-flip tracking */
enum {
    COREVIEW_REG_EAX = 0,
    COREVIEW_REG_EBX,
    COREVIEW_REG_ECX,
    COREVIEW_REG_EDX,
    COREVIEW_REG_ESP,
    COREVIEW_REG_EBP,
    COREVIEW_REG_ESI,
    COREVIEW_REG_EDI,
    COREVIEW_REG_EIP,
    COREVIEW_REG_EFLAGS,
    COREVIEW_REG_CR0,
    COREVIEW_REG_CR3
};

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esp, ebp, esi, edi;
    uint32_t eip, cs, ds, es, fs, gs, ss;
    uint32_t eflags;
    uint32_t cr0, cr3;
} coreview_cpu_snapshot_t;

typedef struct {
    uint8_t  crtc[25];        /* CRTC Index 0x00..0x18 */
    uint8_t  seq[5];          /* Sequencer SR00..SR04 */
    uint8_t  gc[9];           /* Graphics Controller GR00..GR08 */
    uint16_t scanline;        /* Estimated live raster scanline */
    uint16_t beam_x;          /* Estimated live raster beam X (0..799) */
    uint8_t  vblank;          /* 1 if VBLANK active (0x3DA bit 3) */
    uint8_t  hblank;          /* 1 if HBLANK active */
    uint8_t  disp_en;         /* 1 if Display Enable active (0x3DA bit 0) */
    uint16_t cursor_pos;      /* CRTC cursor offset */
    uint16_t bga_id;          /* Bochs/QEMU BGA ID (e.g. 0xB0C5) */
    uint16_t bga_xres;        /* Horizontal resolution */
    uint16_t bga_yres;        /* Vertical resolution */
    uint16_t bga_bpp;         /* Bits per pixel */
    uint16_t bga_enable;      /* BGA enable flags */
    uint32_t vram_bytes;      /* Total VRAM */
    uint32_t lfb_phys;        /* Physical LFB base address */
    uint32_t fps;             /* Real-time compositor FPS */
    uint32_t frame_time_ms;   /* Frame interval in ms */
    uint32_t vram_bandwidth_mb;/* Bandwidth in MB/s */
} coreview_gpu_t;

/* Activity Spectrum (0 = Quiescent Cold, 9 = Hyper-Active Hotspot) */
#define COREVIEW_ACTIVITY_TIERS 10

typedef struct {
    uint8_t     level;          /* 0 to 9 */
    const char *name;           /* Short title */
    const char *desc;           /* Full hardware description */
    uint16_t    est_hz;         /* Estimated update frequency */
    uint32_t    color;          /* Spectrum ARGB color */
} coreview_activity_tier_t;

/* Public Telemetry Core API */
void     coreview_init(void);
void     coreview_capture_irq_cpu(const void *regs_ptr);
void     coreview_sample_hardware(void);
const coreview_cpu_snapshot_t *coreview_get_cpu(void);
uint8_t  coreview_get_bit_pulse(int reg_idx, int bit_idx);
uint32_t coreview_get_reg_val(int reg_idx);
const char *coreview_get_reg_name(int reg_idx);
int      coreview_is_paused(void);
void     coreview_toggle_pause(void);

/* Activity Spectrum API (0-9) */
uint32_t coreview_get_activity_addr(int level);
const coreview_activity_tier_t *coreview_get_activity_tier(int level);
int      coreview_get_active_level(void);
void     coreview_set_active_level(int level);
uint16_t coreview_get_reg_hz(int reg_idx);
uint8_t  coreview_get_reg_rank(int reg_idx);

/* Disassembler */
int      coreview_disasm_at(uint32_t addr, char *out_bytes, char *out_asm, int max_len);

/* RAM Waterfall Scanner */
#define COREVIEW_WATERFALL_ROWS 8
#define COREVIEW_WATERFALL_COLS 16
#define COREVIEW_WATERFALL_SIZE (COREVIEW_WATERFALL_ROWS * COREVIEW_WATERFALL_COLS)

void     coreview_waterfall_step(uint32_t base_addr);
uint32_t coreview_waterfall_get_base(void);
const uint8_t *coreview_waterfall_get_data(void);
const uint8_t *coreview_waterfall_get_heat(void);

/* GPU Inspector */
void     coreview_sample_gpu(coreview_gpu_t *out_gpu);

/* Full-Screen CLI Application */
void     coreview_run_cli(void);

#endif
