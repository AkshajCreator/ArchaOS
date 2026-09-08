#ifndef MP_CORE_H
#define MP_CORE_H

#include <stdint.h>
#include <stddef.h>

void mp_core_init(void);
void mp_core_deinit(void);
int  mp_core_exec(const char *code_str);

#endif
