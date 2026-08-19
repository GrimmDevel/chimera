#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <sys/types.h>

struct timeval {
    i64 tv_sec;
    i64 tv_usec;
};

struct timespec {
    i64 tv_sec;
    i64 tv_nsec;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif
