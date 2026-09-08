#include "tcc_core.h"
#include "../vga.h"
#include "../mm.h"
#include "../pit.h"
#include "../kernel.h"
#include "../shellext.h"

#define TCC_JIT_BUF_SIZE (64 * 1024)
static uint8_t *tcc_jit_buf = NULL;
static int      tcc_initialized = 0;

void tcc_core_init(void) {
    if (tcc_initialized) return;
    tcc_jit_buf = (uint8_t*)kmalloc(TCC_JIT_BUF_SIZE);
    if (tcc_jit_buf) {
        tcc_initialized = 1;
    }
}

void tcc_core_deinit(void) {
    if (tcc_jit_buf) {
        kfree(tcc_jit_buf);
        tcc_jit_buf = NULL;
    }
    tcc_initialized = 0;
}

/* Native helper functions called directly by JIT machine code */
static void tcc_native_printf(const char *fmt, int arg1, int arg2, int arg3) {
    if (!fmt) return;
    char buf[128]; int bi = 0;
    int args[3] = { arg1, arg2, arg3 };
    int ai = 0;

    for (int i = 0; fmt[i] && bi < 126; i++) {
        if (fmt[i] == '%' && fmt[i+1] == 'd' && ai < 3) {
            char num[16];
            int n = args[ai++];
            int ni = 0;
            if (n < 0 && bi < 126) { buf[bi++] = '-'; n = -n; }
            if (n == 0) { num[ni++] = '0'; }
            else {
                char tmp[16]; int ti = 0;
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                while (ti > 0) num[ni++] = tmp[--ti];
            }
            num[ni] = '\0';
            for (int k = 0; num[k] && bi < 126; k++) buf[bi++] = num[k];
            i++;
        } else if (fmt[i] == '\\' && fmt[i+1] == 'n') {
            buf[bi++] = '\n'; i++;
        } else {
            buf[bi++] = fmt[i];
        }
    }
    buf[bi] = '\0';
    for (int i = 0; i < bi; i++) {
        vga_print_char(buf[i]);
    }
}

static void tcc_native_puts(const char *s) {
    if (!s) return;
    while (*s) {
        vga_print_char(*s++);
    }
    vga_print_char('\n');
}

static void tcc_native_beep(int freq, int ms) {
    if (freq <= 0 || ms <= 0) return;
    char cmd[32];
    int bi = 0;
    const char *b = "beep "; while (*b && bi < 31) cmd[bi++] = *b++;
    char fb[12]; int fi = 0;
    int f = freq;
    if (f == 0) fb[fi++] = '0';
    else { char tmp[12]; int ti = 0; while (f > 0) { tmp[ti++] = '0' + (f % 10); f /= 10; } while (ti > 0) fb[fi++] = tmp[--ti]; }
    fb[fi] = '\0'; for (int k = 0; fb[k] && bi < 31; k++) cmd[bi++] = fb[k];
    if (bi < 31) cmd[bi++] = ' ';
    int d = ms; fi = 0;
    if (d == 0) fb[fi++] = '0';
    else { char tmp[12]; int ti = 0; while (d > 0) { tmp[ti++] = '0' + (d % 10); d /= 10; } while (ti > 0) fb[fi++] = tmp[--ti]; }
    fb[fi] = '\0'; for (int k = 0; fb[k] && bi < 31; k++) cmd[bi++] = fb[k];
    cmd[bi] = '\0';
    shell_exec(cmd);
}

static void tcc_native_sleep(int ms) {
    if (ms > 0) pit_sleep((uint32_t)ms);
}

static void tcc_native_system(const char *cmd) {
    if (cmd && cmd[0]) shell_exec(cmd);
}

/* JIT Code Emitter for x86 32-bit Protected Mode */
typedef struct {
    uint8_t *code;
    size_t   size;
    int      overflow;
} jit_emitter_t;

static void emit_byte(jit_emitter_t *e, uint8_t b) {
    if (e->size < TCC_JIT_BUF_SIZE) {
        e->code[e->size++] = b;
    } else {
        e->overflow = 1;
    }
}

static void emit_int32(jit_emitter_t *e, uint32_t val) {
    emit_byte(e, val & 0xFF);
    emit_byte(e, (val >> 8) & 0xFF);
    emit_byte(e, (val >> 16) & 0xFF);
    emit_byte(e, (val >> 24) & 0xFF);
}

static void emit_call(jit_emitter_t *e, void *target_func) {
    emit_byte(e, 0xE8); /* CALL rel32 */
    uint32_t current_pc = (uint32_t)(e->code + e->size + 4);
    uint32_t offset = (uint32_t)target_func - current_pc;
    emit_int32(e, offset);
}

/* Embed string literal in-line in JIT code stream and push its address */
static void emit_push_string_literal(jit_emitter_t *e, const char *str) {
    size_t slen = 0;
    while (str[slen]) slen++;
    slen++; /* include '\0' */

    /* JMP rel32 over string literal: 0xE9, (slen) */
    emit_byte(e, 0xE9);
    emit_int32(e, (uint32_t)slen);

    uint32_t str_addr = (uint32_t)(e->code + e->size);
    for (size_t k = 0; k < slen; k++) {
        emit_byte(e, (uint8_t)str[k]);
    }

    /* PUSH str_addr: 0x68 imm32 */
    emit_byte(e, 0x68);
    emit_int32(e, str_addr);
}

int tcc_core_compile_and_run(const char *c_code) {
    if (!c_code || !c_code[0]) return -1;
    if (!tcc_initialized) tcc_core_init();
    if (!tcc_jit_buf) return -1;

    vga_print("[Tiny C Compiler Native JIT] Compiling C Code to x86 Machine Instructions...\n");

    jit_emitter_t emitter;
    emitter.code = tcc_jit_buf;
    emitter.size = 0;
    emitter.overflow = 0;

    /* x86 Function Prologue: push %ebp; mov %esp, %ebp; sub $128, %esp */
    emit_byte(&emitter, 0x55);
    emit_byte(&emitter, 0x89); emit_byte(&emitter, 0xE5);
    emit_byte(&emitter, 0x83); emit_byte(&emitter, 0xEC); emit_byte(&emitter, 0x80);

    /* Direct JIT Translation of Native C Operations */
    const char *p = c_code;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        /* printf("..."); */
        if (p[0] == 'p' && p[1] == 'r' && p[2] == 'i' && p[3] == 'n' && p[4] == 't' && p[5] == 'f' && p[6] == '(') {
            p += 7;
            while (*p == ' ') p++;
            if (*p == '"') {
                p++;
                char fmtbuf[128];
                int fi = 0;
                while (*p && *p != '"' && fi < 127) {
                    if (*p == '\\' && p[1] == 'n') { fmtbuf[fi++] = '\n'; p += 2; continue; }
                    fmtbuf[fi++] = *p++;
                }
                if (*p == '"') p++;
                fmtbuf[fi] = '\0';

                /* Push 4 args for C ABI: arg3, arg2, arg1, fmt_ptr */
                emit_byte(&emitter, 0x68); emit_int32(&emitter, 0); /* arg3 */
                emit_byte(&emitter, 0x68); emit_int32(&emitter, 0); /* arg2 */
                emit_byte(&emitter, 0x68); emit_int32(&emitter, 0); /* arg1 */
                emit_push_string_literal(&emitter, fmtbuf);         /* fmt */
                emit_call(&emitter, (void*)tcc_native_printf);
                emit_byte(&emitter, 0x83); emit_byte(&emitter, 0xC4); emit_byte(&emitter, 0x10); /* add $16, %esp */
            }
            continue;
        }
        /* puts("..."); */
        else if (p[0] == 'p' && p[1] == 'u' && p[2] == 't' && p[3] == 's' && p[4] == '(') {
            p += 5; while (*p == ' ') p++;
            if (*p == '"') {
                p++;
                char sbuf[128]; int si = 0;
                while (*p && *p != '"' && si < 127) sbuf[si++] = *p++;
                if (*p == '"') p++;
                sbuf[si] = '\0';

                emit_push_string_literal(&emitter, sbuf);
                emit_call(&emitter, (void*)tcc_native_puts);
                emit_byte(&emitter, 0x83); emit_byte(&emitter, 0xC4); emit_byte(&emitter, 0x04);
            }
            continue;
        }
        /* beep(freq, ms); */
        else if (p[0] == 'b' && p[1] == 'e' && p[2] == 'e' && p[3] == 'p' && p[4] == '(') {
            p += 5;
            int freq = 880, dur = 150;
            if (*p >= '0' && *p <= '9') {
                freq = 0; while (*p >= '0' && *p <= '9') freq = freq * 10 + (*p++ - '0');
            }
            while (*p && *p != ',' && *p != ')') p++;
            if (*p == ',') {
                p++; while (*p == ' ') p++;
                if (*p >= '0' && *p <= '9') {
                    dur = 0; while (*p >= '0' && *p <= '9') dur = dur * 10 + (*p++ - '0');
                }
            }

            emit_byte(&emitter, 0x68); emit_int32(&emitter, dur);
            emit_byte(&emitter, 0x68); emit_int32(&emitter, freq);
            emit_call(&emitter, (void*)tcc_native_beep);
            emit_byte(&emitter, 0x83); emit_byte(&emitter, 0xC4); emit_byte(&emitter, 0x08);
            continue;
        }
        /* sleep(ms); */
        else if (p[0] == 's' && p[1] == 'l' && p[2] == 'e' && p[3] == 'e' && p[4] == 'p' && p[5] == '(') {
            p += 6; int ms = 100;
            if (*p >= '0' && *p <= '9') {
                ms = 0; while (*p >= '0' && *p <= '9') ms = ms * 10 + (*p++ - '0');
            }
            emit_byte(&emitter, 0x68); emit_int32(&emitter, ms);
            emit_call(&emitter, (void*)tcc_native_sleep);
            emit_byte(&emitter, 0x83); emit_byte(&emitter, 0xC4); emit_byte(&emitter, 0x04);
            continue;
        }
        /* system("..."); */
        else if (p[0] == 's' && p[1] == 'y' && p[2] == 's' && p[3] == 't' && p[4] == 'e' && p[5] == 'm' && p[6] == '(') {
            p += 7; while (*p == ' ') p++;
            if (*p == '"') {
                p++;
                char sysbuf[128]; int ssi = 0;
                while (*p && *p != '"' && ssi < 127) sysbuf[ssi++] = *p++;
                if (*p == '"') p++;
                sysbuf[ssi] = '\0';

                emit_push_string_literal(&emitter, sysbuf);
                emit_call(&emitter, (void*)tcc_native_system);
                emit_byte(&emitter, 0x83); emit_byte(&emitter, 0xC4); emit_byte(&emitter, 0x04);
            }
            continue;
        }
        p++;
    }

    /* x86 Function Epilogue: xor %eax, %eax; leave; ret */
    emit_byte(&emitter, 0x31); emit_byte(&emitter, 0xC0); /* xor %eax, %eax */
    emit_byte(&emitter, 0xC9);                             /* leave */
    emit_byte(&emitter, 0xC3);                             /* ret */

    if (emitter.overflow) {
        vga_print("[Tiny C Compiler Native JIT] Error: Code buffer overflow during JIT compilation!\n");
        return -1;
    }

    vga_print("[Tiny C Compiler Native JIT] Executing Native x86 Machine Code at JIT Buffer...\n");

    /* Invoke Compiled x86 Machine Code Function Directly on CPU */
    typedef int (*jit_func_t)(void);
    jit_func_t fn = (jit_func_t)tcc_jit_buf;
    return fn();
}
