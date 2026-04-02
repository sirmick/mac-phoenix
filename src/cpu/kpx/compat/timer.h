/*
 *  timer.h - KPX compatibility: forward to common timer.h
 */

#ifndef KPX_TIMER_H
#define KPX_TIMER_H

#include "ppc_memory.h"

// tm_time_t is defined in common/include/sysdeps.h based on HAVE_CLOCK_GETTIME
// For KPX compat, define it here if not already available
#ifndef _COMMON_SYSDEPS_H  // Not using common sysdeps.h
#include <time.h>
typedef struct timespec tm_time_t;
#endif

// Real timer API
extern void TimerInit(void);
extern void TimerExit(void);
extern void TimerReset(void);
extern void TimerInterrupt(void);

extern int16 InsTime(uint32 tm, uint16 trap);
extern int16 RmvTime(uint32 tm);
extern int16 PrimeTime(uint32 tm, int32 time);
extern void Microseconds(uint32 &hi, uint32 &lo);
extern uint32 TimerDateTime(void);

// System specific functions (from timer_unix.cpp)
extern void timer_current_time(tm_time_t &t);
extern void timer_add_time(tm_time_t &res, tm_time_t a, tm_time_t b);
extern void timer_sub_time(tm_time_t &res, tm_time_t a, tm_time_t b);
extern int timer_cmp_time(tm_time_t a, tm_time_t b);
extern void timer_mac2host_time(tm_time_t &res, int32 mactime);
extern int32 timer_host2mac_time(tm_time_t hosttime);

extern void idle_wait(void);
extern void idle_resume(void);

#endif
