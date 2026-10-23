/*
 * platform_glue.cpp - the Platform table for binaries with Executor but
 * without the rest of mac-phoenix (executor_tests). mac-phoenix defines
 * it in src/core; the facade fills its memory and CPU entries
 * (cpu::platform_install) on every binary.
 */
#include "platform.h"

Platform g_platform;
