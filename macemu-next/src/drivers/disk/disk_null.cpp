/*
 *  disk_null.cpp - Disk null driver
 *
 *  Note: This file is kept for consistency but is currently empty.
 *  Disk driver init/exit is handled by disk.cpp in core, which calls
 *  Sys_* functions from platform_adapter.cpp for file I/O.
 *
 *  If disk-specific platform functions are needed in the future
 *  (beyond file I/O), they can be added here.
 */

#include "sysdeps.h"
#include "platform.h"

// No disk-specific platform functions needed currently
// disk.cpp uses Sys_* functions directly for all file operations
