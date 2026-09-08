#ifndef TCC_CORE_H
#define TCC_CORE_H

#include <stdint.h>
#include <stddef.h>

void tcc_core_init(void);
void tcc_core_deinit(void);
int  tcc_core_compile_and_run(const char *c_code);

#endif
