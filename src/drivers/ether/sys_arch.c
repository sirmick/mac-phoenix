/*
 *  sys_arch.c - Minimal lwIP system architecture for NO_SYS=1
 *
 *  Only sys_now() is required - returns milliseconds since boot.
 */

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include "lwip/sys.h"

u32_t sys_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (u32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
