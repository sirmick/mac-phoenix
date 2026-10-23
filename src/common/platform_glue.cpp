/*
 * platform_glue.cpp - the Platform table for binaries without the rest of
 * mac-phoenix (executor_tests, test_cpu_cores). mac-phoenix defines it in
 * src/core; cpu::platform_install fills its memory and CPU entries.
 */
#include "platform.h"

Platform g_platform;
