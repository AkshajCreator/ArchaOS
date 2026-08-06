#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include "multiboot.h"

void  kernel_main(uint32_t mb_magic, multiboot_info_t *mb_info);
void  kernel_execute_command(const char *cmd);
void  reboot(void);
void  beep(void);
void  boot_chime(void);
char *itoa(int value, char *str, int base);

#endif
