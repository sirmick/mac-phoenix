/*
 * backtrace.h - print_backtrace(PREFIX): the calling thread's stack to
 * stderr, where the host can (crash_handler.h: execinfo); on Windows a
 * line saying there is none (the desktop app's crash report has the stack:
 * executor/frontends/desktop/windows_support.h). What Executor uses of
 * crash_handler.h, without it on Windows.
 */
#pragma once

#ifdef _WIN32
#include <stdio.h>
static inline void print_backtrace(const char *prefix)
{
    fprintf(stderr, "\n=== %s (no backtrace on this host) ===\n", prefix);
}
#else
#include "crash_handler.h"
#endif
