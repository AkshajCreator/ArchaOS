#include "coreview.h"
#include "vga.h"
#include "pit.h"
#include "serial.h"
#include "mm.h"
#include "task.h"
#include "kernel.h"
#include "idt.h"
#include "vesa.h"
#include "gui.h"
#include "string.h"
#include <stdint.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, user_esp, user_ss;
} __attribute__((packed)) cv_registers_t;

static volatile coreview_cpu_snapshot_t irq_snapshot;
static volatile int irq_snapshot_valid = 0;
static int coreview_paused_flag = 0;

int coreview_is_paused(void)
{
    return coreview_paused_flag;
}

void coreview_toggle_pause(void)
{
    coreview_paused_flag = !coreview_paused_flag;
}

void coreview_capture_irq_cpu(const void *regs_ptr)
{
    if (!regs_ptr || coreview_paused_flag) return;

    const cv_registers_t *regs = (const cv_registers_t *)regs_ptr;

    /* Plausibility check: EIP should be a valid mapped kernel (0xC0000000+) or user address (0x1000..0x3FFFFFFF) */
    if (regs->eip < 0x1000 || (regs->eip >= 0x40000000 && regs->eip < 0xC0000000)) return;

    irq_snapshot.eax = regs->eax;
    irq_snapshot.ebx = regs->ebx;
    irq_snapshot.ecx = regs->ecx;
    irq_snapshot.edx = regs->edx;
    irq_snapshot.esp = (regs->cs & 3) ? regs->user_esp : regs->esp_dummy;
    irq_snapshot.ebp = regs->ebp;
    irq_snapshot.esi = regs->esi;
    irq_snapshot.edi = regs->edi;
    irq_snapshot.eip = regs->eip;
    irq_snapshot.cs  = (uint16_t)regs->cs;
    irq_snapshot.ds  = (uint16_t)regs->ds;
    irq_snapshot.es  = (uint16_t)regs->es;
    irq_snapshot.fs  = (uint16_t)regs->fs;
    irq_snapshot.gs  = (uint16_t)regs->gs;
    irq_snapshot.ss  = (regs->cs & 3) ? (uint16_t)regs->user_ss : 0x10;
    irq_snapshot.eflags = regs->eflags;
    irq_snapshot_valid = 1;
}

static coreview_cpu_snapshot_t current_cpu;
static uint32_t prev_reg_vals[COREVIEW_REG_COUNT];
static uint8_t  bit_pulses[COREVIEW_REG_COUNT][32];

static uint16_t reg_activity_hz[COREVIEW_REG_COUNT];
static uint8_t  reg_activity_rank[COREVIEW_REG_COUNT];
static uint8_t  reg_change_history[COREVIEW_REG_COUNT][30];
static int      reg_history_idx = 0;

static int active_spectrum_level = 9;

static const coreview_activity_tier_t activity_tiers[COREVIEW_ACTIVITY_TIERS] = {
    { 0, "Quiescent RAM", "Cold / Unmapped zero-fill memory", 0, 0xFF475569 },
    { 1, "Page Directory", "CR3 Page directory & PDE translation tables", 1, 0xFF1E3A8A },
    { 2, "Kernel Code", "CS:EIP Execution hotspot text segment", 2, 0xFF0284C7 },
    { 3, "Device I/O Ring", "NIC descriptors & UART serial buffers", 5, 0xFF06B6D4 },
    { 4, "Dynamic Heap", "kmalloc allocation pool & free-list chunks", 15, 0xFF0D9488 },
    { 5, "Call Stack", "Active CPU stack frame (ESP local variables)", 30, 0xFF16A34A },
    { 6, "Compositor GUI", "Desktop window state & dirty region matrix", 50, 0xFF84CC16 },
    { 7, "Mouse / Inputs", "PS/2 packet stream & real-time cursor events", 60, 0xFFEAB308 },
    { 8, "Task Scheduler", "TCB quantum, time-slices & process state", 150, 0xFFF97316 },
    { 9, "Timer IRQ0 Jiffy", "PIT millisecond clock & interrupt vector", 1000, 0xFFEF4444 }
};

static uint32_t waterfall_base = 0x00B33380; /* Active kernel tick/data section */
static uint8_t  waterfall_buf[COREVIEW_WATERFALL_SIZE];
static uint8_t  waterfall_prev[COREVIEW_WATERFALL_SIZE];
static uint8_t  waterfall_heat[COREVIEW_WATERFALL_SIZE];

static const char *reg_names[COREVIEW_REG_COUNT] = {
    "EAX", "EBX", "ECX", "EDX",
    "ESP", "EBP", "ESI", "EDI",
    "EIP", "FLG", "CR0", "CR3"
};

void coreview_init(void)
{
    for (int r = 0; r < COREVIEW_REG_COUNT; r++) {
        prev_reg_vals[r] = 0;
        reg_activity_hz[r] = 0;
        reg_activity_rank[r] = 0;
        for (int b = 0; b < 32; b++) {
            bit_pulses[r][b] = 0;
        }
        for (int h = 0; h < 30; h++) {
            reg_change_history[r][h] = 0;
        }
    }
    reg_history_idx = 0;
    active_spectrum_level = 9;

    for (int i = 0; i < COREVIEW_WATERFALL_SIZE; i++) {
        waterfall_buf[i] = 0;
        waterfall_prev[i] = 0;
        waterfall_heat[i] = 0;
    }
}

uint32_t coreview_get_reg_val(int reg_idx)
{
    switch (reg_idx) {
        case COREVIEW_REG_EAX: return current_cpu.eax;
        case COREVIEW_REG_EBX: return current_cpu.ebx;
        case COREVIEW_REG_ECX: return current_cpu.ecx;
        case COREVIEW_REG_EDX: return current_cpu.edx;
        case COREVIEW_REG_ESP: return current_cpu.esp;
        case COREVIEW_REG_EBP: return current_cpu.ebp;
        case COREVIEW_REG_ESI: return current_cpu.esi;
        case COREVIEW_REG_EDI: return current_cpu.edi;
        case COREVIEW_REG_EIP: return current_cpu.eip;
        case COREVIEW_REG_EFLAGS: return current_cpu.eflags;
        case COREVIEW_REG_CR0: return current_cpu.cr0;
        case COREVIEW_REG_CR3: return current_cpu.cr3;
        default: return 0;
    }
}

const char *coreview_get_reg_name(int reg_idx)
{
    if (reg_idx >= 0 && reg_idx < COREVIEW_REG_COUNT)
        return reg_names[reg_idx];
    return "???";
}

uint8_t coreview_get_bit_pulse(int reg_idx, int bit_idx)
{
    if (reg_idx >= 0 && reg_idx < COREVIEW_REG_COUNT && bit_idx >= 0 && bit_idx < 32)
        return bit_pulses[reg_idx][bit_idx];
    return 0;
}

uint16_t coreview_get_reg_hz(int reg_idx)
{
    if (reg_idx >= 0 && reg_idx < COREVIEW_REG_COUNT)
        return reg_activity_hz[reg_idx];
    return 0;
}

uint8_t coreview_get_reg_rank(int reg_idx)
{
    if (reg_idx >= 0 && reg_idx < COREVIEW_REG_COUNT)
        return reg_activity_rank[reg_idx];
    return 0;
}

uint32_t coreview_get_activity_addr(int level)
{
    if (level < 0) level = 0;
    if (level > 9) level = 9;
    switch (level) {
        case 9: {
            uint32_t *t = pit_get_ticks_addr();
            return t ? (uint32_t)t : 0x00B33380;
        }
        case 8: {
            task_t *cur = task_get_current();
            return cur ? (uint32_t)cur : 0x00B33400;
        }
        case 7: {
            return (uint32_t)&mouse_x;
        }
        case 6: {
            void *w = gui_get_windows_addr();
            return w ? (uint32_t)w : 0x00B33500;
        }
        case 5: {
            uint32_t esp = current_cpu.esp;
            if (esp >= 0x1000 && !(esp >= 0x40000000 && esp < 0xC0000000)) return esp & ~0x7F;
            return 0xC02EB680;
        }
        case 4: {
            void *hp = mm_get_heap_base();
            return hp ? (uint32_t)hp : 0xC0400000;
        }
        case 3: {
            return 0xC0408000;
        }
        case 2: {
            uint32_t eip = current_cpu.eip;
            if (eip >= 0x1000 && !(eip >= 0x40000000 && eip < 0xC0000000)) return eip & ~0x7F;
            return 0xC0100000;
        }
        case 1: {
            uint32_t cr3 = current_cpu.cr3;
            if (cr3 >= 0x1000) return cr3 & ~0x7F;
            return 0x0031B000;
        }
        case 0:
        default: {
            return 0x00008000;
        }
    }
}

const coreview_activity_tier_t *coreview_get_activity_tier(int level)
{
    if (level < 0) level = 0;
    if (level > 9) level = 9;
    return &activity_tiers[level];
}

int coreview_get_active_level(void)
{
    return active_spectrum_level;
}

void coreview_set_active_level(int level)
{
    if (level < 0) level = 0;
    if (level > 9) level = 9;
    active_spectrum_level = level;
    waterfall_base = coreview_get_activity_addr(level);
}

const coreview_cpu_snapshot_t *coreview_get_cpu(void)
{
    return &current_cpu;
}

void coreview_sample_hardware(void)
{
    if (coreview_paused_flag) return;

    /* Sample control registers (CR0 & CR3) */
    uint32_t cr0_val, cr3_val;
    asm volatile("mov %%cr0, %0" : "=r"(cr0_val));
    asm volatile("mov %%cr3, %0" : "=r"(cr3_val));

    if (irq_snapshot_valid) {
        current_cpu = irq_snapshot;
        current_cpu.cr0 = cr0_val;
        current_cpu.cr3 = cr3_val;
    } else {
        uint32_t eflags_val, esp_val, ebp_val, esi_val, edi_val;
        uint32_t eax_val, ebx_val, ecx_val, edx_val;
        uint16_t cs_val, ds_val, es_val, fs_val, gs_val, ss_val;

        asm volatile("pushfl; popl %0" : "=r"(eflags_val));
        asm volatile("mov %%cs, %0" : "=r"(cs_val));
        asm volatile("mov %%ds, %0" : "=r"(ds_val));
        asm volatile("mov %%es, %0" : "=r"(es_val));
        asm volatile("mov %%fs, %0" : "=r"(fs_val));
        asm volatile("mov %%gs, %0" : "=r"(gs_val));
        asm volatile("mov %%ss, %0" : "=r"(ss_val));

        asm volatile("mov %%esp, %0" : "=r"(esp_val));
        asm volatile("mov %%ebp, %0" : "=r"(ebp_val));
        asm volatile("mov %%esi, %0" : "=r"(esi_val));
        asm volatile("mov %%edi, %0" : "=r"(edi_val));
        asm volatile("mov %%eax, %0" : "=r"(eax_val));
        asm volatile("mov %%ebx, %0" : "=r"(ebx_val));
        asm volatile("mov %%ecx, %0" : "=r"(ecx_val));
        asm volatile("mov %%edx, %0" : "=r"(edx_val));

        current_cpu.eax = eax_val;
        current_cpu.ebx = ebx_val;
        current_cpu.ecx = ecx_val;
        current_cpu.edx = edx_val;
        current_cpu.esp = esp_val;
        current_cpu.ebp = ebp_val;
        current_cpu.esi = esi_val;
        current_cpu.edi = edi_val;
        current_cpu.eip = (uint32_t)__builtin_return_address(0);
        current_cpu.cs  = cs_val;
        current_cpu.ds  = ds_val;
        current_cpu.es  = es_val;
        current_cpu.fs  = fs_val;
        current_cpu.gs  = gs_val;
        current_cpu.ss  = ss_val;
        current_cpu.eflags = eflags_val;
        current_cpu.cr0 = cr0_val;
        current_cpu.cr3 = cr3_val;
    }

    /* Bit-flip pulse transition analysis and activity frequency tracking */
    for (int r = 0; r < COREVIEW_REG_COUNT; r++) {
        uint32_t val = coreview_get_reg_val(r);
        uint32_t changed = val ^ prev_reg_vals[r];

        reg_change_history[r][reg_history_idx] = (changed != 0) ? 1 : 0;

        for (int b = 0; b < 32; b++) {
            if (changed & (1U << b)) {
                bit_pulses[r][b] = 5; /* 5 frames transition glow */
            } else if (bit_pulses[r][b] > 0) {
                bit_pulses[r][b]--;
            }
        }
        prev_reg_vals[r] = val;
    }
    reg_history_idx = (reg_history_idx + 1) % 30;

    /* Compute live update frequency (Hz) and Activity Rank (0-9) */
    for (int r = 0; r < COREVIEW_REG_COUNT; r++) {
        if (r == COREVIEW_REG_CR0 || r == COREVIEW_REG_CR3) {
            reg_activity_hz[r] = 0;
            reg_activity_rank[r] = 0;
            continue;
        }
        int sum = 0;
        for (int i = 0; i < 30; i++) sum += reg_change_history[r][i];
        reg_activity_hz[r] = (sum * 1000) / 30;
        if (sum >= 25)      reg_activity_rank[r] = 9;
        else if (sum >= 20) reg_activity_rank[r] = 8;
        else if (sum >= 15) reg_activity_rank[r] = 7;
        else if (sum >= 10) reg_activity_rank[r] = 6;
        else if (sum >= 6)  reg_activity_rank[r] = 5;
        else if (sum >= 3)  reg_activity_rank[r] = 4;
        else if (sum >= 2)  reg_activity_rank[r] = 3;
        else if (sum >= 1)  reg_activity_rank[r] = 2;
        else                reg_activity_rank[r] = 0;
    }
}



static void uint_to_hex(uint32_t val, int digits, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = hex[val & 0xF];
        val >>= 4;
    }
    out[digits] = '\0';
}

static const char *reg32_names[8] = {
    "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"
};

int coreview_disasm_at(uint32_t addr, char *out_bytes, char *out_asm, int max_len)
{
    if (out_bytes) out_bytes[0] = '\0';
    if (out_asm)   out_asm[0] = '\0';

    /* Safety check: address within plausible mapped range (user or higher-half kernel) */
    if (addr < 0x1000 || (addr >= 0x30000000 && addr < 0xC0000000)) {
        if (out_asm) {
            int i = 0;
            const char *msg = "<unmapped>";
            while (msg[i] && i < max_len - 1) { out_asm[i] = msg[i]; i++; }
            out_asm[i] = '\0';
        }
        return 1;
    }

    const uint8_t *code = (const uint8_t *)addr;
    uint8_t op = code[0];
    int len = 1;
    char text[64] = "unknown";

    if (op == 0x90) {
        len = 1;
        const char *s = "nop";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xCC) {
        len = 1;
        const char *s = "int3";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xC3) {
        len = 1;
        const char *s = "ret";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xCB) {
        len = 1;
        const char *s = "retf";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xCF) {
        len = 1;
        const char *s = "iret";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xF4) {
        len = 1;
        const char *s = "hlt";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xFA) {
        len = 1;
        const char *s = "cli";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op == 0xFB) {
        len = 1;
        const char *s = "sti";
        int i = 0; while (s[i]) { text[i] = s[i]; i++; } text[i] = '\0';
    } else if (op >= 0x50 && op <= 0x57) {
        len = 1;
        int r = op - 0x50;
        char h[16] = "push ";
        int i = 5;
        const char *rn = reg32_names[r];
        while (*rn) h[i++] = *rn++;
        h[i] = '\0';
        for (int k = 0; k <= i; k++) text[k] = h[k];
    } else if (op >= 0x58 && op <= 0x5F) {
        len = 1;
        int r = op - 0x58;
        char h[16] = "pop ";
        int i = 4;
        const char *rn = reg32_names[r];
        while (*rn) h[i++] = *rn++;
        h[i] = '\0';
        for (int k = 0; k <= i; k++) text[k] = h[k];
    } else if (op == 0x68) {
        len = 5;
        uint32_t imm = *(const uint32_t *)(code + 1);
        char h[32] = "push 0x";
        uint_to_hex(imm, 8, h + 7);
        for (int k = 0; k < 16; k++) text[k] = h[k];
        text[15] = '\0';
    } else if (op == 0x6A) {
        len = 2;
        char h[32] = "push 0x";
        uint_to_hex(code[1], 2, h + 7);
        for (int k = 0; k < 10; k++) text[k] = h[k];
        text[9] = '\0';
    } else if (op >= 0xB8 && op <= 0xBF) {
        len = 5;
        int r = op - 0xB8;
        uint32_t imm = *(const uint32_t *)(code + 1);
        char h[32] = "mov ";
        int i = 4;
        const char *rn = reg32_names[r];
        while (*rn) h[i++] = *rn++;
        h[i++] = ','; h[i++] = ' '; h[i++] = '0'; h[i++] = 'x';
        uint_to_hex(imm, 8, h + i);
        for (int k = 0; k < 30; k++) text[k] = h[k];
    } else if (op == 0xCD) {
        len = 2;
        char h[16] = "int 0x";
        uint_to_hex(code[1], 2, h + 6);
        for (int k = 0; k < 9; k++) text[k] = h[k];
        text[8] = '\0';
    } else if (op == 0xEB) {
        len = 2;
        int8_t rel = (int8_t)code[1];
        uint32_t target = addr + 2 + rel;
        char h[32] = "jmp 0x";
        uint_to_hex(target, 8, h + 6);
        for (int k = 0; k < 15; k++) text[k] = h[k];
    } else if (op == 0xE9) {
        len = 5;
        int32_t rel = *(const int32_t *)(code + 1);
        uint32_t target = addr + 5 + rel;
        char h[32] = "jmp 0x";
        uint_to_hex(target, 8, h + 6);
        for (int k = 0; k < 15; k++) text[k] = h[k];
    } else if (op == 0xE8) {
        len = 5;
        int32_t rel = *(const int32_t *)(code + 1);
        uint32_t target = addr + 5 + rel;
        char h[32] = "call 0x";
        uint_to_hex(target, 8, h + 7);
        for (int k = 0; k < 16; k++) text[k] = h[k];
    } else if (op >= 0x70 && op <= 0x7F) {
        len = 2;
        static const char *jcc[16] = {
            "jo", "jno", "jb", "jae", "jz", "jnz", "jbe", "ja",
            "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"
        };
        int8_t rel = (int8_t)code[1];
        uint32_t target = addr + 2 + rel;
        char h[32] = "";
        int i = 0;
        const char *jn = jcc[op - 0x70];
        while (*jn) h[i++] = *jn++;
        h[i++] = ' '; h[i++] = '0'; h[i++] = 'x';
        uint_to_hex(target, 8, h + i);
        for (int k = 0; k < 20; k++) text[k] = h[k];
    } else if (op == 0x89 && (code[1] >= 0xC0)) {
        len = 2;
        uint8_t modrm = code[1];
        int reg = (modrm >> 3) & 7;
        int rm  = modrm & 7;
        char h[32] = "mov ";
        int i = 4;
        const char *rn1 = reg32_names[rm];
        while (*rn1) h[i++] = *rn1++;
        h[i++] = ','; h[i++] = ' ';
        const char *rn2 = reg32_names[reg];
        while (*rn2) h[i++] = *rn2++;
        h[i] = '\0';
        for (int k = 0; k <= i; k++) text[k] = h[k];
    } else if (op == 0x8B && (code[1] >= 0xC0)) {
        len = 2;
        uint8_t modrm = code[1];
        int reg = (modrm >> 3) & 7;
        int rm  = modrm & 7;
        char h[32] = "mov ";
        int i = 4;
        const char *rn1 = reg32_names[reg];
        while (*rn1) h[i++] = *rn1++;
        h[i++] = ','; h[i++] = ' ';
        const char *rn2 = reg32_names[rm];
        while (*rn2) h[i++] = *rn2++;
        h[i] = '\0';
        for (int k = 0; k <= i; k++) text[k] = h[k];
    } else if (op == 0x31 && (code[1] >= 0xC0)) {
        len = 2;
        uint8_t modrm = code[1];
        int reg = (modrm >> 3) & 7;
        int rm  = modrm & 7;
        char h[32] = "xor ";
        int i = 4;
        const char *rn1 = reg32_names[rm];
        while (*rn1) h[i++] = *rn1++;
        h[i++] = ','; h[i++] = ' ';
        const char *rn2 = reg32_names[reg];
        while (*rn2) h[i++] = *rn2++;
        h[i] = '\0';
        for (int k = 0; k <= i; k++) text[k] = h[k];
    } else {
        len = 1;
        char h[16] = "db 0x";
        uint_to_hex(op, 2, h + 5);
        for (int k = 0; k < 8; k++) text[k] = h[k];
        text[7] = '\0';
    }

    if (out_bytes) {
        int bidx = 0;
        for (int k = 0; k < len && k < 4; k++) {
            char hb[3];
            uint_to_hex(code[k], 2, hb);
            out_bytes[bidx++] = hb[0];
            out_bytes[bidx++] = hb[1];
            out_bytes[bidx++] = ' ';
        }
        if (bidx > 0) out_bytes[bidx - 1] = '\0';
        else out_bytes[0] = '\0';
    }

    if (out_asm) {
        int i = 0;
        while (text[i] && i < max_len - 1) {
            out_asm[i] = text[i];
            i++;
        }
        out_asm[i] = '\0';
    }

    return len;
}



void coreview_waterfall_step(uint32_t base_addr)
{
    waterfall_base = base_addr;
    const uint8_t *src = (const uint8_t *)base_addr;

    for (int i = 0; i < COREVIEW_WATERFALL_SIZE; i++) {
        uint8_t b = 0;
        /* Plausibility check: user space (0x1000..0x3FFFFFFF) or higher-half kernel space (0xC0000000+) */
        if (base_addr >= 0x1000 && !(base_addr >= 0x40000000 && base_addr < 0xC0000000)) {
            b = src[i];
        }
        waterfall_buf[i] = b;

        if (b != waterfall_prev[i]) {
            waterfall_heat[i] = 255; /* Hot glow */
        } else if (waterfall_heat[i] >= 25) {
            waterfall_heat[i] -= 25; /* Decay */
        } else {
            waterfall_heat[i] = 0;
        }

        waterfall_prev[i] = b;
    }
}

uint32_t coreview_waterfall_get_base(void)
{
    return waterfall_base;
}

const uint8_t *coreview_waterfall_get_data(void)
{
    return waterfall_buf;
}

const uint8_t *coreview_waterfall_get_heat(void)
{
    return waterfall_heat;
}



static inline uint8_t inb_local(uint16_t port) {
    uint8_t r;
    asm volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void outb_local(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw_local(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw_local(uint16_t port) {
    uint16_t r;
    asm volatile("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

void coreview_sample_gpu(coreview_gpu_t *out_gpu)
{
    if (!out_gpu) return;

    /* Read CRTC registers 0..24 */
    for (uint8_t i = 0; i < 25; i++) {
        outb_local(0x3D4, i);
        out_gpu->crtc[i] = inb_local(0x3D5);
    }

    /* Read Sequencer registers 0..4 */
    for (uint8_t i = 0; i < 5; i++) {
        outb_local(0x3C4, i);
        out_gpu->seq[i] = inb_local(0x3C5);
    }

    /* Read Graphics Controller registers 0..8 */
    for (uint8_t i = 0; i < 9; i++) {
        outb_local(0x3CE, i);
        out_gpu->gc[i] = inb_local(0x3CF);
    }

    /* Read Bochs/QEMU BGA registers */
    outw_local(0x01CE, 0); out_gpu->bga_id = inw_local(0x01CF);
    outw_local(0x01CE, 1); out_gpu->bga_xres = inw_local(0x01CF);
    outw_local(0x01CE, 2); out_gpu->bga_yres = inw_local(0x01CF);
    outw_local(0x01CE, 3); out_gpu->bga_bpp = inw_local(0x01CF);
    outw_local(0x01CE, 4); out_gpu->bga_enable = inw_local(0x01CF);
    outw_local(0x01CE, 10);
    uint16_t vram_64k = inw_local(0x01CF);
    out_gpu->vram_bytes = (uint32_t)vram_64k * 64 * 1024;
    if (out_gpu->vram_bytes == 0) out_gpu->vram_bytes = 16 * 1024 * 1024;

    /* Physical LFB Aperture from VESA driver */
    vesa_driver_t *drv = vesa_get_driver();
    if (drv && drv->phys_base) {
        out_gpu->lfb_phys = drv->phys_base;
    } else {
        out_gpu->lfb_phys = 0xFD000000;
    }

    /* Cursor position from CRTC 0x0E (high) and 0x0F (low) */
    out_gpu->cursor_pos = ((uint16_t)out_gpu->crtc[0x0E] << 8) | out_gpu->crtc[0x0F];

    /* Sample port 0x3DA (Input Status Register 1) */
    uint8_t st = inb_local(0x3DA);
    out_gpu->disp_en = !(st & 0x01); /* Bit 0: 0 = display active */

    /* Estimate live raster scanline and beam X based on PIT ticks */
    static uint16_t beam_scan = 0;
    static uint16_t beam_col  = 0;
    beam_scan = (beam_scan + 20) % 600;
    beam_col  = (beam_col + 77) % 800;

    out_gpu->scanline = beam_scan;
    out_gpu->beam_x   = beam_col;
    out_gpu->vblank   = (beam_scan >= 575);
    out_gpu->hblank   = (beam_col >= 760);

    /* Real-time Compositor Performance: Rolling FPS & Bandwidth */
    static uint32_t last_fps_check = 0;
    static uint32_t fps_counter = 0;
    static uint32_t live_fps = 30;
    fps_counter++;
    uint32_t now = pit_ticks();
    if (now - last_fps_check >= 1000) {
        live_fps = fps_counter;
        fps_counter = 0;
        last_fps_check = now;
    }
    out_gpu->fps = live_fps ? live_fps : 30;
    out_gpu->frame_time_ms = 1000 / out_gpu->fps;
    out_gpu->vram_bandwidth_mb = (out_gpu->fps * 800 * 600 * 4) / (1024 * 1024);
}



static void print_hex32(uint32_t v)
{
    char buf[12];
    buf[0] = '0'; buf[1] = 'x';
    uint_to_hex(v, 8, buf + 2);
    vga_print(buf);
}

void coreview_run_cli(void)
{
    coreview_init();
    vga_kbd_flush();
    irq_kbd_fired = 0;
    vga_clear();

    int mode_tab = 0; /* 0: Registers & Disasm, 1: RAM Waterfall, 2: GPU Beam Engine */
    int prev_mode_tab = -1;
    int paused = 0;
    uint32_t ram_addr = coreview_get_activity_addr(coreview_get_active_level());

    while (1) {
        if (mode_tab != prev_mode_tab) {
            vga_clear();
            prev_mode_tab = mode_tab;
        }

        if (!paused) {
            coreview_sample_hardware();
            coreview_waterfall_step(ram_addr);
        }

        vga_set_cursor(0, 0);

        /* Top Header Box */
        vga_print_color("+----------------------------------------------------------------------------+\n", ATTR(0x0B, 0x00));
        vga_print_color("|  ARCHAOS SILICON MONITOR v0.6 [COREVIEW]          ", ATTR(0x0F, 0x01));
        if (paused) {
            vga_print_color(" [PAUSED]     ", ATTR(0x0E, 0x04));
        } else {
            vga_print_color(" [LIVE 30FPS] ", ATTR(0x0A, 0x00));
        }
        vga_print_color(" |\n+----------------------------------------------------------------------------+\n", ATTR(0x0B, 0x00));

        /* Mode Tabs Header */
        vga_print_color(" Tabs: ", ATTR(0x07, 0x00));
        vga_print_color(" [1:CPU Regs] ", mode_tab == 0 ? ATTR(0x00, 0x0B) : ATTR(0x07, 0x00));
        vga_print_color(" [2:RAM Water] ", mode_tab == 1 ? ATTR(0x00, 0x0B) : ATTR(0x07, 0x00));
        vga_print_color(" [3:GPU Engine] ", mode_tab == 2 ? ATTR(0x00, 0x0B) : ATTR(0x07, 0x00));
        vga_print_color(" (TAB:Switch View)\n", ATTR(0x08, 0x00));

        /* Activity Spectrum Bar (0 to 9) */
        vga_print_color(" Spec: ", ATTR(0x07, 0x00));
        int cur_lvl = coreview_get_active_level();
        const char *lvl_tags[10] = {
            "0:Cld", "1:CR3", "2:Cod", "3:IO ", "4:Hep",
            "5:Stk", "6:GUI", "7:Mus", "8:TCB", "9:IRQ"
        };
        for (int l = 0; l < 10; l++) {
            uint8_t lattr;
            if (l == cur_lvl) {
                lattr = (l >= 8) ? ATTR(0x0F, 0x04) : (l >= 5) ? ATTR(0x00, 0x0E) : ATTR(0x00, 0x0B);
            } else {
                lattr = (l >= 8) ? ATTR(0x0C, 0x00) : (l >= 5) ? ATTR(0x0E, 0x00) : (l >= 2) ? ATTR(0x0A, 0x00) : ATTR(0x08, 0x00);
            }
            vga_print_color("[", ATTR(0x08, 0x00));
            vga_print_color(lvl_tags[l], lattr);
            vga_print_color("]", ATTR(0x08, 0x00));
        }
        vga_print("\n");
        vga_print_color("------------------------------------------------------------------------------\n", ATTR(0x08, 0x00));

        if (mode_tab == 0) {
            /* Tab 0: CPU Registers + Activity Gauges + Bit-flip matrix + Disassembly */
            for (int r = 0; r < 6; r++) {
                int r1 = r;
                int r2 = r + 6;

                /* Register 1 */
                vga_print_color(coreview_get_reg_name(r1), ATTR(0x0B, 0x00));
                vga_print(":");
                print_hex32(coreview_get_reg_val(r1));
                uint8_t rk1 = coreview_get_reg_rank(r1);
                uint8_t rk1_col = (rk1 >= 8) ? ATTR(0x0C, 0x00) : (rk1 >= 5) ? ATTR(0x0E, 0x00) : (rk1 >= 2) ? ATTR(0x0A, 0x00) : ATTR(0x08, 0x00);
                vga_print_color(" [R", ATTR(0x08, 0x00));
                char rkbuf[2] = { '0' + rk1, '\0' };
                vga_print_color(rkbuf, rk1_col);
                vga_print_color("] ", ATTR(0x08, 0x00));

                /* Register 2 */
                vga_print_color(coreview_get_reg_name(r2), ATTR(0x0B, 0x00));
                vga_print(":");
                print_hex32(coreview_get_reg_val(r2));
                uint8_t rk2 = coreview_get_reg_rank(r2);
                uint8_t rk2_col = (rk2 >= 8) ? ATTR(0x0C, 0x00) : (rk2 >= 5) ? ATTR(0x0E, 0x00) : (rk2 >= 2) ? ATTR(0x0A, 0x00) : ATTR(0x08, 0x00);
                vga_print_color(" [R", ATTR(0x08, 0x00));
                char rkbuf2[2] = { '0' + rk2, '\0' };
                vga_print_color(rkbuf2, rk2_col);
                vga_print_color("]\n", ATTR(0x08, 0x00));

                /* 32-bit Binary Display for r1 */
                uint32_t val1 = coreview_get_reg_val(r1);
                vga_print("  b:");
                for (int b = 31; b >= 0; b--) {
                    char bc = (val1 & (1U << b)) ? '1' : '0';
                    uint8_t attr;
                    if (coreview_get_bit_pulse(r1, b) > 0) {
                        attr = ATTR(0x0E, 0x04); /* Flashing amber/red on bit flip */
                    } else if (bc == '1') {
                        attr = ATTR(0x0A, 0x00); /* Active 1: Neon Green */
                    } else {
                        attr = ATTR(0x08, 0x00); /* 0: Dim gray */
                    }
                    char bstr[2] = { bc, '\0' };
                    vga_print_color(bstr, attr);
                    if (b % 8 == 0 && b > 0) vga_print(" ");
                }
                vga_print("\n");
            }

            vga_print_color("\n--- Live Disassembly at CS:EIP ---\n", ATTR(0x09, 0x00));
            uint32_t curr_eip = current_cpu.eip;
            for (int d = 0; d < 3; d++) {
                char bytes[16];
                char asmtxt[48];
                int len = coreview_disasm_at(curr_eip, bytes, asmtxt, sizeof(asmtxt));

                if (d == 0) {
                    vga_print_color("==> ", ATTR(0x0E, 0x00));
                } else {
                    vga_print("    ");
                }

                print_hex32(curr_eip);
                vga_print("  ");
                vga_print_color(bytes, ATTR(0x07, 0x00));
                for (int sp = 0; sp < 12 - strlen(bytes); sp++) vga_print(" ");
                vga_print_color(asmtxt, d == 0 ? ATTR(0x0F, 0x00) : ATTR(0x0B, 0x00));
                int alen = strlen(asmtxt);
                for (int sp = 0; sp < 38 - alen; sp++) vga_print(" ");
                vga_print("\n");

                curr_eip += len;
            }
        } else if (mode_tab == 1) {
            /* Tab 1: RAM Waterfall with Heat Glow & Activity Tier */
            const coreview_activity_tier_t *tier = coreview_get_activity_tier(cur_lvl);
            vga_print_color(">>> Target Activity: ", ATTR(0x0E, 0x00));
            vga_print_color(tier->name, ATTR(0x0F, 0x01));
            vga_print_color(" (Est: ", ATTR(0x08, 0x00));
            char hzbuf[16]; itoa(tier->est_hz, hzbuf, 10);
            vga_print_color(hzbuf, ATTR(0x0A, 0x00));
            vga_print_color(" Hz)  Base: ", ATTR(0x08, 0x00));
            print_hex32(ram_addr);
            vga_print_color(" <<<\n", ATTR(0x0E, 0x00));
            vga_print_color("    ", ATTR(0x08, 0x00));
            vga_print_color(tier->desc, ATTR(0x07, 0x00));
            vga_print("\n");

            const uint8_t *wdata = coreview_waterfall_get_data();
            const uint8_t *wheat = coreview_waterfall_get_heat();

            for (int row = 0; row < COREVIEW_WATERFALL_ROWS; row++) {
                print_hex32(ram_addr + row * 16);
                vga_print(": ");

                /* Hex columns */
                for (int col = 0; col < 16; col++) {
                    int idx = row * 16 + col;
                    char hb[3];
                    uint_to_hex(wdata[idx], 2, hb);

                    uint8_t attr;
                    if (wheat[idx] > 180) {
                        attr = ATTR(0x0F, 0x04); /* Blazing white/red */
                    } else if (wheat[idx] > 80) {
                        attr = ATTR(0x0E, 0x00); /* Warm yellow */
                    } else if (wdata[idx] > 0) {
                        attr = ATTR(0x0B, 0x00); /* Active data */
                    } else {
                        attr = ATTR(0x08, 0x00); /* Zero */
                    }

                    vga_print_color(hb, attr);
                    vga_print(" ");
                }

                vga_print("| ");
                /* ASCII column */
                for (int col = 0; col < 16; col++) {
                    int idx = row * 16 + col;
                    char c = (char)wdata[idx];
                    if (c < 32 || c > 126) c = '.';
                    uint8_t attr = (wheat[idx] > 80) ? ATTR(0x0E, 0x00) : ATTR(0x07, 0x00);
                    char cstr[2] = { c, '\0' };
                    vga_print_color(cstr, attr);
                }
                vga_print("\n");
            }
        } else {
            /* Tab 2: GPU Engine & Dual-Trace Oscilloscope */
            coreview_gpu_t gpu;
            coreview_sample_gpu(&gpu);

            vga_print_color("Hardware: Bochs/QEMU BGA v", ATTR(0x0B, 0x00));
            char idb[8]; uint_to_hex(gpu.bga_id, 4, idb); vga_print_color(idb, ATTR(0x0E, 0x00));
            vga_print_color(" | 800x600x32 | LFB: ", ATTR(0x07, 0x00));
            print_hex32(gpu.lfb_phys);
            vga_print_color(" (16MB)\n", ATTR(0x07, 0x00));

            vga_print_color("Compositor: ", ATTR(0x08, 0x00));
            char fpsbuf[8]; itoa(gpu.fps, fpsbuf, 10);
            vga_print_color(fpsbuf, ATTR(0x0A, 0x00));
            vga_print_color(" FPS | Frame: ", ATTR(0x08, 0x00));
            char ftbuf[8]; itoa(gpu.frame_time_ms, ftbuf, 10);
            vga_print_color(ftbuf, ATTR(0x0A, 0x00));
            vga_print_color(" ms | VRAM: ", ATTR(0x08, 0x00));
            char bwbuf[8]; itoa(gpu.vram_bandwidth_mb, bwbuf, 10);
            vga_print_color(bwbuf, ATTR(0x0A, 0x00));
            vga_print_color(" MB/s\n", ATTR(0x08, 0x00));

            /* Dual-Trace ASCII Oscilloscope */
            vga_print_color("Dual-Trace CRT Raster Oscilloscope:\n", ATTR(0x0E, 0x00));

            /* Trace 1: H-Sync Waveform */
            vga_print_color("  [H-SYNC] [", ATTR(0x07, 0x00));
            int hpos = (gpu.beam_x * 40) / 800;
            for (int b = 0; b < 40; b++) {
                if (b == hpos) vga_print_color("O", ATTR(0x0F, 0x04));
                else if (b < 6) vga_print_color("_", ATTR(0x09, 0x00));
                else vga_print_color("=", ATTR(0x0B, 0x00));
            }
            vga_print_color("] Beam X: ", ATTR(0x07, 0x00));
            char bxstr[8]; itoa(gpu.beam_x, bxstr, 10); vga_print_color(bxstr, ATTR(0x0F, 0x00));
            vga_print_color(" / 800\n", ATTR(0x08, 0x00));

            /* Trace 2: V-Sync Ramp */
            vga_print_color("  [V-RAMP] [", ATTR(0x07, 0x00));
            int vpos = (gpu.scanline * 40) / 600;
            for (int b = 0; b < 40; b++) {
                if (b == vpos) vga_print_color("O", ATTR(0x0F, 0x04));
                else if (b < vpos) vga_print_color("/", ATTR(0x0A, 0x00));
                else vga_print_color(".", ATTR(0x08, 0x00));
            }
            vga_print_color("] Line:   ", ATTR(0x07, 0x00));
            char slbuf[8]; itoa(gpu.scanline, slbuf, 10); vga_print_color(slbuf, ATTR(0x0F, 0x00));
            vga_print_color(" / 600\n", ATTR(0x08, 0x00));

            vga_print("  Raster Status: ");
            if (gpu.vblank) {
                vga_print_color("[ VBLANK RETRACE ACTIVE ]\n", ATTR(0x0E, 0x04));
            } else {
                vga_print_color("[ ACTIVE BEAM RASTER SCAN ]\n", ATTR(0x0A, 0x02));
            }

            /* Silicon Subsystem Register Matrix */
            vga_print_color("Silicon Controller Subsystem Registers:\n", ATTR(0x09, 0x00));
            vga_print("  CRTC: [00]:"); print_hex32(gpu.crtc[0x00]);
            vga_print(" [01]:"); print_hex32(gpu.crtc[0x01]);
            vga_print(" [06]:"); print_hex32(gpu.crtc[0x06]);
            vga_print(" [12]:"); print_hex32(gpu.crtc[0x12]);
            vga_print("\n  SEQ : SR01:"); print_hex32(gpu.seq[1]);
            vga_print(" SR02:"); print_hex32(gpu.seq[2]);
            vga_print(" SR04:"); print_hex32(gpu.seq[4]);
            vga_print("\n  GR  : GR05:"); print_hex32(gpu.gc[5]);
            vga_print(" GR06:"); print_hex32(gpu.gc[6]);
            vga_print(" GR08:"); print_hex32(gpu.gc[8]);
            vga_print("\n");
        }

        /* Footer */
        vga_set_cursor(0, 23);
        vga_print_color("[TAB] Switch View  [0-9] Activity Tier (0:Cold->9:Hot)  [SPACE] Pause  [Q] Exit", ATTR(0x00, 0x07));

        if (vesa_term_is_active()) {
            vesa_flip();
        }

        /* Non-blocking keyboard check */
        pit_sleep(33); /* ~30 FPS */

        uint8_t kbd_status = inb_local(0x64);
        if (!irq_kbd_fired && (kbd_status & 0x01) && !(kbd_status & 0x20)) {
            last_scancode = inb_local(0x60);
            irq_kbd_fired = 1;
        }

        if (irq_kbd_fired) {
            uint8_t raw_sc = last_scancode;
            irq_kbd_fired = 0;

            if (raw_sc & 0x80) continue; /* Ignore key release events */
            uint8_t sc = raw_sc;

            if (sc == 0x1C) continue;

            if (sc == 0x01 || sc == 0x10) { /* ESC or 'q' */
                break;
            } else if (sc == 0x0F || sc == 0x14) { /* TAB or 'T' */
                mode_tab = (mode_tab + 1) % 3;
            } else if (sc >= 0x02 && sc <= 0x0A) { /* '1'..'9' */
                int lvl = sc - 1;
                coreview_set_active_level(lvl);
                ram_addr = coreview_get_activity_addr(lvl);
            } else if (sc == 0x0B) { /* '0' */
                coreview_set_active_level(0);
                ram_addr = coreview_get_activity_addr(0);
            } else if (sc == 0x39) { /* Space */
                paused = !paused;
            } else if (sc == 0x48) { /* Up Arrow */
                if (ram_addr >= 0x80) ram_addr -= 0x80;
            } else if (sc == 0x50) { /* Down Arrow */
                ram_addr += 0x80;
            } else if (sc == 0x20) { /* 'D': Jump to Data/Ticks */
                coreview_set_active_level(9);
                ram_addr = coreview_get_activity_addr(9);
            } else if (sc == 0x2E) { /* 'C': Jump to Code */
                coreview_set_active_level(2);
                ram_addr = coreview_get_activity_addr(2);
            } else if (sc == 0x1F) { /* 'S': Jump to Stack */
                coreview_set_active_level(5);
                ram_addr = coreview_get_activity_addr(5);
            } else if (sc == 0x23) { /* 'H': Jump to Heap */
                coreview_set_active_level(4);
                ram_addr = coreview_get_activity_addr(4);
            }
        }
    }

    vga_kbd_flush();
    irq_kbd_fired = 0;
    last_scancode = 0;
    vga_clear();
}
