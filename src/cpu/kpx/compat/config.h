/* config.h - KPX compat, matches legacy SheepShaver Unix/config.h */
#ifndef KPX_CONFIG_H
#define KPX_CONFIG_H

/* Must come before any system headers */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <sys/mman.h>

#define HAVE_MMAP_VM 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SIGINFO_T 1
#define REAL_ADDRESSING 1
#define HAVE_CONFIG_H 1

/* Legacy SheepShaver uses linker script to place binary below 2GB */
#define HAVE_LINKER_SCRIPT 1

/* MAP_BASE: let vm_alloc.cpp use its default (0x10000000 with HAVE_LINKER_SCRIPT).
   Previously hardcoded to 0x10C00000 which pushed JIT to wrong address. */
/* MAP_EXTRA_FLAGS: let vm_alloc.cpp use its default (MAP_32BIT on x86-64). */
#define FORCE_MAP_32BIT 0

/* Not on this platform */
/* #undef HAVE_MACH_VM */
/* #undef HAVE_WIN32_VM */
/* #undef PAGEZERO_HACK */

#endif
