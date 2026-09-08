#include "interpreter.h"
#include "micropython/mp_core.h"
#include "tcc/tcc_core.h"
#include "vga.h"
#include "fs.h"
#include "kernel.h"
#include "pit.h"

/* Symbol & Variable Table */
typedef struct {
    char name[24];
    int val;
    char sval[48];
    int is_str;
} py_var_t;

#define MAX_PY_VARS 64
static py_var_t py_vars[MAX_PY_VARS];
static int      py_var_count = 0;

/* Function Table */
typedef struct {
    char name[24];
    char body[128];
} py_func_t;

#define MAX_PY_FUNCS 16
static py_func_t py_funcs[MAX_PY_FUNCS];
static int       py_func_count = 0;

static int local_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static int local_strncmp(const char *a, const char *b, size_t n) {
    size_t i = 0;
    while (i < n && a[i] && (a[i] == b[i])) { i++; }
    if (i == n) return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static void local_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

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

    if (local_strncmp(code, "def ", 4) == 0 && py_func_count < MAX_PY_FUNCS) {
        const char *p = code + 4;
        while (*p == ' ') p++;
        char fname[24]; int fi = 0;
        while (*p && *p != '(' && *p != ':' && *p != ' ' && fi < 23) fname[fi++] = *p++;
        fname[fi] = '\0';
        if (fi > 0) {
            local_strcpy(py_funcs[py_func_count].name, fname, 24);
            py_func_count++;
            vga_print("Function registered.\n");
        }
    } else if (local_strcmp(code, "vars") == 0) {
        for (int i = 0; i < py_var_count; i++) {
            vga_print(py_vars[i].name);
            vga_print("\n");
        }
    }
}

void interpreter_run_c(const char *code) {
    if (!code || !code[0]) return;
    tcc_core_compile_and_run(code);

    if (local_strncmp(code, "int ", 4) == 0 && py_var_count < MAX_PY_VARS) {
        const char *p = code + 4;
        while (*p == ' ') p++;
        char vname[24]; int vi = 0;
        while (*p && *p != '=' && *p != ';' && *p != ' ' && vi < 23) vname[vi++] = *p++;
        vname[vi] = '\0';
        if (vi > 0) {
            local_strcpy(py_vars[py_var_count].name, vname, 24);
            py_var_count++;
            vga_print("C Variable defined.\n");
        }
    }
}
