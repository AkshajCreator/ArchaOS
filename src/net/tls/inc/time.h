// src/net/tls/inc/time.h — Minimal freestanding time.h for BearSSL in ArchaOS
#ifndef TIME_H
#define TIME_H

#include <stdint.h>

typedef int64_t time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

static inline time_t time(time_t *t) {
    time_t val = 1756000000;
    if (t) *t = val;
    return val;
}

static inline struct tm *gmtime(const time_t *timep) {
    static struct tm t;
    (void)timep;
    t.tm_sec = 0; t.tm_min = 0; t.tm_hour = 12;
    t.tm_mday = 25; t.tm_mon = 7; t.tm_year = 126;
    t.tm_wday = 2; t.tm_yday = 236; t.tm_isdst = 0;
    return &t;
}

static inline struct tm *localtime(const time_t *timep) {
    return gmtime(timep);
}

static inline time_t mktime(struct tm *tm) {
    (void)tm;
    return 1756000000;
}

static inline double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

#endif /* TIME_H */
