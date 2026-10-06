#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0100

int open(const char *path, int flags);

#endif /* _FCNTL_H */
