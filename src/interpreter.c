#include "interpreter.h"
#include "micropython/mp_core.h"
#include "tcc/tcc_core.h"
#include "vga.h"
#include "fs.h"
#include "kernel.h"
#include "pit.h"
#include "string.h"

typedef struct {
    char name[24];
    int val;
    char sval[48];
    int is_str;
} py_var_t;

#define MAX_PY_VARS 64
static py_var_t py_vars[MAX_PY_VARS];
static int      py_var_count = 0;

typedef struct {
    char name[24];
    char body[128];
} py_func_t;

#define MAX_PY_FUNCS 16
static py_func_t py_funcs[MAX_PY_FUNCS];
static int       py_func_count = 0;

void interpreter_init(void) {
    py_var_count = 0;
    py_func_count = 0;
    for (int i = 0; i < MAX_PY_VARS; i++) {
        py_vars[i].name[0] = '\0';
        py_vars[i].val = 0;
        py_vars[i].sval[0] = '\0';
        py_vars[i].is_str = 0;
    }
    for (int i = 0; i < MAX_PY_FUNCS; i++) {
        py_funcs[i].name[0] = '\0';
        py_funcs[i].body[0] = '\0';
    }
    mp_core_init();
    tcc_core_init();
}

void interpreter_run_python(const char *code) {
    if (!code || !code[0]) return;
    mp_core_exec(code);

    if (strncmp(code, "def ", 4) == 0 && py_func_count < MAX_PY_FUNCS) {
        const char *p = code + 4;
        while (*p == ' ') p++;
        char fname[24]; int fi = 0;
        while (*p && *p != '(' && *p != ':' && *p != ' ' && fi < 23) fname[fi++] = *p++;
        fname[fi] = '\0';
        if (fi > 0) {
            strncpy(py_funcs[py_func_count].name, fname, 24);
            py_funcs[py_func_count].name[23] = '\0';
            py_func_count++;
            vga_print("Function registered.\n");
        }
    } else if (strcmp(code, "vars") == 0) {
        for (int i = 0; i < py_var_count; i++) {
            vga_print(py_vars[i].name);
            vga_print("\n");
        }
    }
}

void interpreter_run_c(const char *code) {
    if (!code || !code[0]) return;
    tcc_core_compile_and_run(code);

    if (strncmp(code, "int ", 4) == 0 && py_var_count < MAX_PY_VARS) {
        const char *p = code + 4;
        while (*p == ' ') p++;
        char vname[24]; int vi = 0;
        while (*p && *p != '=' && *p != ';' && *p != ' ' && vi < 23) vname[vi++] = *p++;
        vname[vi] = '\0';
        if (vi > 0) {
            strncpy(py_vars[py_var_count].name, vname, 24);
            py_vars[py_var_count].name[23] = '\0';
            py_var_count++;
            vga_print("C Variable defined.\n");
        }
    }
}

void cmd_python(const char *args) {
    if (args && args[0] != '\0') {
        while (*args == ' ') args++;
        if (strncmp(args, "-c ", 3) == 0) {
            const char *code = args + 3;
            while (*code == ' ' || *code == '"' || *code == '\'') code++;
            char code_buf[256];
            int ci = 0;
            while (*code && *code != '"' && *code != '\'' && ci < (int)sizeof(code_buf) - 1) {
                code_buf[ci++] = *code++;
            }
            code_buf[ci] = '\0';
            interpreter_run_python(code_buf);
            return;
        }

        /* Try executing file */
        fs_node_t *node = fs_resolve(args);
        if (node && node->type == FS_FILE && node->data) {
            char fbuf[1024];
            size_t sz = node->size < sizeof(fbuf) - 1 ? node->size : sizeof(fbuf) - 1;
            memcpy(fbuf, node->data, sz);
            fbuf[sz] = '\0';
            interpreter_run_python(fbuf);
            return;
        }

        /* Direct string execution */
        interpreter_run_python(args);
        return;
    }

    /* Interactive REPL */
    vga_print_color("ArchaOS MicroPython v1.20 (i386-archaos-kernel)\n", 0x0B);
    vga_print_color("Type \"help()\", \"credits\", or \"exit()\" for more information.\n", 0x07);

    char line[128];
    while (1) {
        vga_get_input(">>> ", line, sizeof(line));
        char *p = line;
        while (*p == ' ') p++;
        if (strcmp(p, "exit()") == 0 || strcmp(p, "quit()") == 0 || strcmp(p, "exit") == 0) {
            break;
        }
        if (strcmp(p, "help()") == 0 || strcmp(p, "help") == 0) {
            vga_print("MicroPython Native Commands:\n");
            vga_print("  print(...)       - Print text or expressions\n");
            vga_print("  sleep(ms)        - Delay execution in milliseconds\n");
            vga_print("  vars             - List defined environment variables\n");
            vga_print("  exit()           - Exit interactive REPL\n");
            continue;
        }
        if (strcmp(p, "credits") == 0 || strcmp(p, "copyright") == 0) {
            vga_print("ArchaOS MicroPython Native Interpreter Port (C) 2026 ArchaOS Team\n");
            continue;
        }
        if (p[0] == '\0') {
            continue;
        }
        interpreter_run_python(p);
    }
}

