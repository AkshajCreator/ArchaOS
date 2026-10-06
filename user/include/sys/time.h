#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <stdint.h>
#include <unistd.h>

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    if (tv) {
        uint32_t ticks = time_ticks();
        tv->tv_sec = ticks / 1000;
        tv->tv_usec = (ticks % 1000) * 1000;
    }
    return 0;
}

#endif /* _SYS_TIME_H */
