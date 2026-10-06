#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifdef __cplusplus
extern "C" {
#endif

int      read(int fd, void *buf, size_t count);
int      write(int fd, const void *buf, size_t count);
int      close(int fd);
int      lseek(int fd, long offset, int whence);
void     sleep(uint32_t ms);
void     yield(void);
int      getpid(void);
void    *sbrk(intptr_t incr);
char    *getcwd(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */
