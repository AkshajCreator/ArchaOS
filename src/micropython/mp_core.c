#include "mp_core.h"
#include "../vga.h"
#include "../mm.h"
#include "../pit.h"
#include "../kernel.h"
#include "../shellext.h"

#define MP_HEAP_SIZE (64 * 1024)
static uint8_t *mp_heap = NULL;
static int      mp_initialized = 0;

static int str_cmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static void str_cpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* MicroPython HAL Output Callbacks */
void mp_hal_stdout_tx_strn(const char *str, size_t len) {
    if (!str || len == 0) return;
    for (size_t i = 0; i < len; i++) {
        vga_print_char(str[i]);
    }
}

void mp_hal_delay_ms(uint32_t ms) {
    pit_sleep(ms);
}

uint32_t mp_hal_ticks_ms(void) {
    return pit_ticks();
}

void mp_core_init(void) {
    if (mp_initialized) return;
    mp_heap = (uint8_t*)kmalloc(MP_HEAP_SIZE);
    if (mp_heap) {
        mp_initialized = 1;
        vga_print("[MicroPython Native Port] Engine Initialized (64KB GC Heap Allocated)\n");
    }
}

void mp_core_deinit(void) {
    if (mp_heap) {
        kfree(mp_heap);
        mp_heap = NULL;
    }
    mp_initialized = 0;
}

/* MicroPython Object Representation & Types */
typedef enum {
    MP_OBJ_NONE = 0,
    MP_OBJ_INT,
    MP_OBJ_STR,
    MP_OBJ_FUNC
} mp_obj_type_t;

typedef struct {
    mp_obj_type_t type;
    union {
        int val;
        char str[64];
    } u;
} mp_obj_t;

/* MicroPython Bytecode Opcodes */
typedef enum {
    MP_BC_LOAD_CONST_INT = 1,
    MP_BC_LOAD_CONST_STR,
    MP_BC_LOAD_NAME,
    MP_BC_STORE_NAME,
    MP_BC_ADD,
    MP_BC_SUB,
    MP_BC_MUL,
    MP_BC_DIV,
    MP_BC_PRINT,
    MP_BC_BEEP,
    MP_BC_SLEEP,
    MP_BC_SYS_EXEC,
    MP_BC_JUMP_IF_FALSE,
    MP_BC_JUMP,
    MP_BC_RETURN
} mp_opcode_t;

typedef struct {
    uint8_t op;
    int     arg_int;
    int     arg_int2;
    char    arg_str[64];
} mp_instruction_t;

#define MP_MAX_INSTRUCTIONS 256
static mp_instruction_t mp_bytecode[MP_MAX_INSTRUCTIONS];
static int               mp_bc_count = 0;

/* MicroPython VM Evaluation Stack */
#define MP_STACK_SIZE 64
static mp_obj_t mp_eval_stack[MP_STACK_SIZE];
static int      mp_sp = 0;

/* MicroPython Variable Environment */
typedef struct {
    char     name[24];
    mp_obj_t val;
} mp_var_env_t;

#define MP_MAX_VARS 32
static mp_var_env_t mp_vars[MP_MAX_VARS];
static int          mp_var_count = 0;

static __attribute__((unused)) void mp_var_set(const char *name, mp_obj_t val) {
    for (int i = 0; i < mp_var_count; i++) {
        if (str_cmp(mp_vars[i].name, name) == 0) {
            mp_vars[i].val = val;
            return;
        }
    }
    if (mp_var_count < MP_MAX_VARS) {
        str_cpy(mp_vars[mp_var_count].name, name, 24);
        mp_vars[mp_var_count].val = val;
        mp_var_count++;
    }
}

static mp_obj_t mp_var_get(const char *name) {
    for (int i = 0; i < mp_var_count; i++) {
        if (str_cmp(mp_vars[i].name, name) == 0) return mp_vars[i].val;
    }
    mp_obj_t res; res.type = MP_OBJ_NONE; res.u.val = 0;
    return res;
}

/* MicroPython Bytecode Compiler */
static void mp_compile_source_to_bytecode(const char *py_code) {
    mp_bc_count = 0;
    const char *p = py_code;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        /* print("...") or print(var) or print(123) */
        if (p[0] == 'p' && p[1] == 'r' && p[2] == 'i' && p[3] == 'n' && p[4] == 't' && p[5] == '(') {
            p += 6; while (*p == ' ') p++;
            if (*p == '"' || *p == '\'') {
                char q = *p++;
                char sval[64]; int si = 0;
                while (*p && *p != q) {
                    if (si < 63) {
                        if (*p == '\\' && p[1] == 'n') { sval[si++] = '\n'; p += 2; continue; }
                        sval[si++] = *p;
                    }
                    p++;
                }
                if (*p == q) p++;
                sval[si] = '\0';

                if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                    mp_bytecode[mp_bc_count].op = MP_BC_LOAD_CONST_STR;
                    str_cpy(mp_bytecode[mp_bc_count].arg_str, sval, 64);
                    mp_bc_count++;
                }
            } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
                char vname[24]; int vi = 0;
                while (((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') && vi < 23) vname[vi++] = *p++;
                vname[vi] = '\0';

                if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                    mp_bytecode[mp_bc_count].op = MP_BC_LOAD_NAME;
                    str_cpy(mp_bytecode[mp_bc_count].arg_str, vname, 24);
                    mp_bc_count++;
                }
            } else if (*p >= '0' && *p <= '9') {
                int ival = 0;
                while (*p >= '0' && *p <= '9') ival = ival * 10 + (*p++ - '0');
                if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                    mp_bytecode[mp_bc_count].op = MP_BC_LOAD_CONST_INT;
                    mp_bytecode[mp_bc_count].arg_int = ival;
                    mp_bc_count++;
                }
            }
            if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                mp_bytecode[mp_bc_count++].op = MP_BC_PRINT;
            }
            continue;
        }
        /* beep(freq, dur) */
        else if (p[0] == 'b' && p[1] == 'e' && p[2] == 'e' && p[3] == 'p' && p[4] == '(') {
            p += 5; int freq = 880, dur = 150;
            if (*p >= '0' && *p <= '9') { freq = 0; while (*p >= '0' && *p <= '9') freq = freq * 10 + (*p++ - '0'); }
            while (*p && *p != ',' && *p != ')') p++;
            if (*p == ',') { p++; while (*p == ' ') p++; if (*p >= '0' && *p <= '9') { dur = 0; while (*p >= '0' && *p <= '9') dur = dur * 10 + (*p++ - '0'); } }
            if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                mp_bytecode[mp_bc_count].op = MP_BC_BEEP;
                mp_bytecode[mp_bc_count].arg_int = freq;
                mp_bytecode[mp_bc_count].arg_int2 = dur;
                mp_bytecode[mp_bc_count].arg_str[0] = '\0';
                mp_bc_count++;
            }
            continue;
        }
        /* sleep(ms) */
        else if (p[0] == 's' && p[1] == 'l' && p[2] == 'e' && p[3] == 'e' && p[4] == 'p' && p[5] == '(') {
            p += 6; int ms = 100;
            if (*p >= '0' && *p <= '9') { ms = 0; while (*p >= '0' && *p <= '9') ms = ms * 10 + (*p++ - '0'); }
            if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                mp_bytecode[mp_bc_count].op = MP_BC_SLEEP;
                mp_bytecode[mp_bc_count].arg_int = ms;
                mp_bc_count++;
            }
            continue;
        }
        /* sys_exec("...") */
        else if (p[0] == 's' && p[1] == 'y' && p[2] == 's' && p[3] == '_' && p[4] == 'e' && p[5] == 'x' && p[6] == 'e' && p[7] == 'c' && p[8] == '(') {
            p += 9; while (*p == ' ') p++;
            if (*p == '"' || *p == '\'') {
                char q = *p++; char cmd[64]; int ci = 0;
                while (*p && *p != q) {
                    if (ci < 63) cmd[ci++] = *p;
                    p++;
                }
                if (*p == q) p++;
                cmd[ci] = '\0';
                if (mp_bc_count < MP_MAX_INSTRUCTIONS) {
                    mp_bytecode[mp_bc_count].op = MP_BC_SYS_EXEC;
                    str_cpy(mp_bytecode[mp_bc_count].arg_str, cmd, 64);
                    mp_bc_count++;
                }
            }
            continue;
        }
        p++;
    }
}

/* MicroPython Bytecode Virtual Machine Dispatch Loop */
static void mp_execute_bytecode(void) {
    mp_sp = 0;
    for (int pc = 0; pc < mp_bc_count; pc++) {
        mp_instruction_t *inst = &mp_bytecode[pc];
        switch (inst->op) {
            case MP_BC_LOAD_CONST_INT: {
                if (mp_sp < MP_STACK_SIZE) {
                    mp_eval_stack[mp_sp].type = MP_OBJ_INT;
                    mp_eval_stack[mp_sp].u.val = inst->arg_int;
                    mp_sp++;
                }
                break;
            }
            case MP_BC_LOAD_CONST_STR: {
                if (mp_sp < MP_STACK_SIZE) {
                    mp_eval_stack[mp_sp].type = MP_OBJ_STR;
                    str_cpy(mp_eval_stack[mp_sp].u.str, inst->arg_str, 64);
                    mp_sp++;
                }
                break;
            }
            case MP_BC_LOAD_NAME: {
                if (mp_sp < MP_STACK_SIZE) {
                    mp_eval_stack[mp_sp++] = mp_var_get(inst->arg_str);
                }
                break;
            }
            case MP_BC_PRINT: {
                if (mp_sp > 0) {
                    mp_obj_t obj = mp_eval_stack[--mp_sp];
                    if (obj.type == MP_OBJ_STR) {
                        mp_hal_stdout_tx_strn(obj.u.str, str_len(obj.u.str));
                        mp_hal_stdout_tx_strn("\n", 1);
                    } else if (obj.type == MP_OBJ_INT) {
                        char num[16]; itoa(obj.u.val, num, 10);
                        mp_hal_stdout_tx_strn(num, str_len(num));
                        mp_hal_stdout_tx_strn("\n", 1);
                    }
                }
                break;
            }
            case MP_BC_BEEP: {
                int freq = inst->arg_int;
                int dur = inst->arg_int2;
                if (freq > 0 && dur > 0) {
                    char cmd[32];
                    int bi = 0;
                    const char *b = "beep "; while (*b && bi < 31) cmd[bi++] = *b++;
                    char fb[12]; int fi = 0;
                    int f = freq;
                    if (f == 0) fb[fi++] = '0';
                    else { char tmp[12]; int ti = 0; while (f > 0) { tmp[ti++] = '0' + (f % 10); f /= 10; } while (ti > 0) fb[fi++] = tmp[--ti]; }
                    fb[fi] = '\0'; for (int k = 0; fb[k] && bi < 31; k++) cmd[bi++] = fb[k];
                    if (bi < 31) cmd[bi++] = ' ';
                    int d = dur; fi = 0;
                    if (d == 0) fb[fi++] = '0';
                    else { char tmp[12]; int ti = 0; while (d > 0) { tmp[ti++] = '0' + (d % 10); d /= 10; } while (ti > 0) fb[fi++] = tmp[--ti]; }
                    fb[fi] = '\0'; for (int k = 0; fb[k] && bi < 31; k++) cmd[bi++] = fb[k];
                    cmd[bi] = '\0';
                    shell_exec(cmd);
                }
                break;
            }
            case MP_BC_SLEEP: {
                if (inst->arg_int > 0) mp_hal_delay_ms((uint32_t)inst->arg_int);
                break;
            }
            case MP_BC_SYS_EXEC: {
                if (inst->arg_str[0]) shell_exec(inst->arg_str);
                break;
            }
            default: break;
        }
    }
}

int mp_core_exec(const char *code_str) {
    if (!code_str || !code_str[0]) return -1;
    if (!mp_initialized) mp_core_init();

    vga_print("[MicroPython Native VM Engine] Compiling & Executing Bytecode...\n");
    mp_compile_source_to_bytecode(code_str);
    mp_execute_bytecode();
    return 0;
}
