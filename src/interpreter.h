#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stdint.h>
#include <stddef.h>

void interpreter_init(void);
void interpreter_run_python(const char *code);
void interpreter_run_c(const char *code);

#endif
