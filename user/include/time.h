#ifndef _TIME_H
#define _TIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __time_t_defined
#define __time_t_defined
typedef long time_t;
#endif

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

time_t time(time_t *tloc);
struct tm *localtime(const time_t *timep);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */
