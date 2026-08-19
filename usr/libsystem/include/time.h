#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
#include <sys/time.h>

typedef i64 time_t;
time_t time(time_t *tloc);

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

struct tm *localtime(const time_t *timer);
struct tm *gmtime(const time_t *timer);

#endif
