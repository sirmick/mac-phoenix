#ifndef _sys_time_h
#define _sys_time_h

#include <time.h>

struct timezone
{
    int tz_minuteswest; /* minutes W of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};

struct timeval
{
    long tv_sec;  /* seconds */
    long tv_usec; /* microseconds */
};

#ifdef __cplusplus
extern "C"
#endif
int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif // _sys_time_h
