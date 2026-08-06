#ifndef KERNEL_H
#define KERNEL_H

void kernel_main(void);

void kernel_execute_command(const char *cmd);

void reboot(void);

void beep(void);

char *itoa(int value, char *str, int base);

#endif
