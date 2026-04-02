/*
 *  sysdeps.h - KPX compatibility header for mac-phoenix
 *
 *  Provides type definitions and platform macros expected by the
 *  Kheperix PPC interpreter code (originally from SheepShaver).
 */

#ifndef KPX_SYSDEPS_H
#define KPX_SYSDEPS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

// Platform feature defines (from legacy SheepShaver config.h)
#define HAVE_UNISTD_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_MMAP_VM 1

// SheepShaver type aliases (used pervasively in KPX code)
typedef uint8_t   uint8;
typedef uint16_t  uint16;
typedef uint32_t  uint32;
typedef uint64_t  uint64;
typedef int8_t    int8;
typedef int16_t   int16;
typedef int32_t   int32;
typedef int64_t   int64;
typedef uintptr_t uintptr;
typedef intptr_t  intptr;

// 64-bit constant macro
#define UVAL64(X) X##ULL

// Byte order (x86_64 is little-endian)
#undef WORDS_BIGENDIAN

// Network byte order functions
#include <arpa/inet.h>

// Byte swap functions
#ifdef __GNUC__
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#else
#include <byteswap.h>
#endif

// Spinlock — copied verbatim from legacy SheepShaver Unix sysdeps.h
typedef volatile int spinlock_t;

static const spinlock_t SPIN_LOCK_UNLOCKED = 0;

// testandset — x86_64 version from legacy (xchgl, no lock prefix needed)
static inline int testandset(volatile int *p)
{
    long int ret;
    __asm__ __volatile__("xchgl %k0, %1"
                         : "=r" (ret), "=m" (*p)
                         : "0" (1), "m" (*p)
                         : "memory");
    return ret;
}

#define HAVE_TEST_AND_SET 1
#define HAVE_SPINLOCKS 1
static inline void spin_lock(spinlock_t *lock)
{
    while (testandset(lock));
}
static inline void spin_unlock(spinlock_t *lock)
{
    *lock = 0;
}

// SheepShaver build flags
#ifndef SHEEPSHAVER
#define SHEEPSHAVER 1
#endif

#ifndef EMULATED_PPC
#define EMULATED_PPC 1
#endif

// PPC ROM uses 8KB XPRAM (vs 256 bytes for m68k)
#define POWERPC_ROM 1

// Memory addressing mode - REAL_ADDRESSING (same as SheepShaver)
// Mac address == host address, VMBaseDiff = 0
#define REAL_ADDRESSING 1

// Configure PowerPC emulator — match legacy SheepShaver Unix/sysdeps.h
#define PPC_REENTRANT_JIT 1
#define PPC_CHECK_INTERRUPTS 1
#define PPC_DECODE_CACHE 1
// Must be 1 — legacy binary also has FLIGHT_RECORDER=1 (sysdeps.h override).
// The dyngen precompiled ops were compiled with this struct layout.
#define PPC_FLIGHT_RECORDER 1
#define PPC_PROFILE_COMPILE_TIME 0
#define PPC_PROFILE_GENERIC_CALLS 0
#define PPC_PROFILE_REGS_USE 0

// KPX CPU count
#ifndef KPX_MAX_CPUS
#define KPX_MAX_CPUS 1
#endif

// Disable features not used in mac-phoenix
#define ENABLE_MON 0
#ifndef ENABLE_DYNGEN
#define ENABLE_DYNGEN 0
#endif
#define ENABLE_VOSF 0
#define HAVE_SIGSEGV_SKIP_INSTRUCTION 1

// Precise timing — legacy SheepShaver uses a dedicated timer thread
// for microsecond-accurate Time Manager task dispatch. Without this,
// timer tasks only fire at 60Hz from OP_IRQ, which may be too slow
// for Process Manager boot sequencing.
#define PRECISE_TIMING 1
#define PRECISE_TIMING_POSIX 1

// Dyngen direct block chaining — must match legacy SheepShaver (=1)
// Without this, JIT-compiled blocks don't return to the interpreter
// for spcflag checks, causing MODE_NATIVE starvation.
#ifndef DYNGEN_DIRECT_BLOCK_CHAINING
#define DYNGEN_DIRECT_BLOCK_CHAINING 1
#endif

// sizeof(void*) for 64-bit addressing wrapping
#define SIZEOF_VOID_P __SIZEOF_POINTER__

// FP support
#define HAVE_FENV_H 1

// Mark unused parameters
#define UNUSED(x) (void)(x)

// Color type (from SheepShaver Unix/sysdeps.h)
typedef struct rgb_color {
    uint8 red;
    uint8 green;
    uint8 blue;
    uint8 alpha;
} rgb_color;

// CallMacOS macros — call_macos* functions defined in cpu_ppc_kpx.cpp
// (matches SheepShaver's Unix/sysdeps.h)
extern uint32 call_macos(uint32 tvect);
extern uint32 call_macos1(uint32 tvect, uint32 a1);
extern uint32 call_macos2(uint32 tvect, uint32 a1, uint32 a2);
extern uint32 call_macos3(uint32 tvect, uint32 a1, uint32 a2, uint32 a3);
extern uint32 call_macos4(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4);
extern uint32 call_macos5(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4, uint32 a5);
extern uint32 call_macos6(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4, uint32 a5, uint32 a6);
extern uint32 call_macos7(uint32 tvect, uint32 a1, uint32 a2, uint32 a3, uint32 a4, uint32 a5, uint32 a6, uint32 a7);

#define CallMacOS(type, tvect) call_macos((uint32)(uintptr)(tvect))
#define CallMacOS1(type, tvect, arg1) call_macos1((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1))
#define CallMacOS2(type, tvect, arg1, arg2) call_macos2((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1), (uint32)(uintptr)(arg2))
#define CallMacOS3(type, tvect, arg1, arg2, arg3) call_macos3((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1), (uint32)(uintptr)(arg2), (uint32)(uintptr)(arg3))
#define CallMacOS4(type, tvect, arg1, arg2, arg3, arg4) call_macos4((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1), (uint32)(uintptr)(arg2), (uint32)(uintptr)(arg3), (uint32)(uintptr)(arg4))
#define CallMacOS5(type, tvect, arg1, arg2, arg3, arg4, arg5) call_macos5((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1), (uint32)(uintptr)(arg2), (uint32)(uintptr)(arg3), (uint32)(uintptr)(arg4), (uint32)(uintptr)(arg5))
#define CallMacOS6(type, tvect, arg1, arg2, arg3, arg4, arg5, arg6) call_macos6((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1), (uint32)(uintptr)(arg2), (uint32)(uintptr)(arg3), (uint32)(uintptr)(arg4), (uint32)(uintptr)(arg5), (uint32)(uintptr)(arg6))
#define CallMacOS7(type, tvect, arg1, arg2, arg3, arg4, arg5, arg6, arg7) call_macos7((uint32)(uintptr)(tvect), (uint32)(uintptr)(arg1), (uint32)(uintptr)(arg2), (uint32)(uintptr)(arg3), (uint32)(uintptr)(arg4), (uint32)(uintptr)(arg5), (uint32)(uintptr)(arg6), (uint32)(uintptr)(arg7))

#endif /* KPX_SYSDEPS_H */
