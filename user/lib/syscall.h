#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* POSIX-compatible ArchaOS System Call Numbers */
#define SYS_EXIT    1
#define SYS_SPAWN   2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_SLEEP   7
#define SYS_YIELD   8
#define SYS_GETPID  9
#define SYS_MMAP    10
#define SYS_SBRK    11
#define SYS_TIME    12
#define SYS_LSEEK   13

/* ArchaOS GUI System Call Numbers (20 - 30) */
#define SYS_GUI_CREATE_WINDOW  20
#define SYS_GUI_CLOSE_WINDOW   21
#define SYS_GUI_CLEAR          22
#define SYS_GUI_DRAW_RECT      23
#define SYS_GUI_DRAW_TEXT      24
#define SYS_GUI_DRAW_PIXEL     25
#define SYS_GUI_UPDATE         26
#define SYS_GUI_GET_EVENT      27
#define SYS_GUI_SET_ICON       28
#define SYS_GUI_SET_AUTHOR     29
#define SYS_GUI_DRAW_BUFFER    30
#define SYS_GUI_SET_TITLE      31
#define SYS_BEEP               32
#define SYS_READDIR            33
#define SYS_GUI_SET_CURSOR_MODE 34
#define SYS_AUDIO_WRITE        35

/* Inlined x86 int 0x80 system call invocations */
static inline int sys_call0(int num)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int sys_call1(int num, uint32_t a1)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory");
    return ret;
}

static inline int sys_call2(int num, uint32_t a1, uint32_t a2)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline int sys_call3(int num, uint32_t a1, uint32_t a2, uint32_t a3)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static inline int sys_call4(int num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4) : "memory");
    return ret;
}

static inline int sys_call5(int num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5) : "memory");
    return ret;
}

#endif /* USER_SYSCALL_H */
