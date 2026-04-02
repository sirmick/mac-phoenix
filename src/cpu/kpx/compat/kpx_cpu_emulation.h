/*
 *  kpx_cpu_emulation.h - PPC CPU emulation and Mac memory access (KPX backend)
 *
 *  KPX-specific header for PPC interpreter code. Uses vm.hpp for direct
 *  memory access in the PPC execution hot path. Core code should include
 *  cpu_emulation.h (common/include) which dispatches through g_platform.
 *
 *  Original: SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Licensed under GPL v2+
 */

#ifndef KPX_CPU_EMULATION_H
#define KPX_CPU_EMULATION_H

// Block the common cpu_emulation.h from being included in KPX translation units.
// KPX provides its own ReadMacInt32/Mac2HostAddr etc. via vm.hpp (direct access).
// Without this, transitive includes (e.g. macos_util.h → cpu_emulation.h) would
// pull in the g_platform-dispatch versions and cause redefinition errors.
#define CPU_EMULATION_H

#include "sysdeps.h"

/*
 *  PPC Memory system constants (SheepShaver nanokernel layout)
 */

const uint32  ROM_SIZE = 0x400000;              // 4MB ROM
const uint32  ROM_AREA_SIZE = 0x500000;         // Size of ROM area
const uintptr DR_EMULATOR_BASE = 0x68070000;    // DR emulator code
const uint32  DR_EMULATOR_SIZE = 0x10000;
const uintptr DR_CACHE_BASE = 0x69000000;       // DR cache
const uint32  DR_CACHE_SIZE = 0x80000;

const uintptr KERNEL_DATA_BASE = 0x68ffe000;    // Kernel Data
const uintptr KERNEL_DATA2_BASE = 0x5fffe000;   // Alternate Kernel Data
const uint32  KERNEL_AREA_SIZE = 0x2000;

// MacOS 68k Emulator Data
struct EmulatorData {
    uint32 v[0x400];
};

// MacOS Kernel Data
struct KernelData {
    uint32 v[0x400];
    EmulatorData ed;
};

// RAM and ROM pointers (SheepShaver naming) — ppc:: namespace
namespace ppc {
    extern uint32 RAMBase;
    extern uint32 RAMSize;
    extern uint8 *RAMBaseHost;
    extern uint32 ROMBase;
    extern uint8 *ROMBaseHost;
}
using ppc::RAMBase;
using ppc::RAMSize;
using ppc::RAMBaseHost;
using ppc::ROMBase;
using ppc::ROMBaseHost;

/*
 *  KPX direct memory access via vm.hpp
 *
 *  These are used by the PPC interpreter hot path for maximum performance.
 *  Core/shared code must NOT include this header — use cpu_emulation.h
 *  (common/include) which dispatches through g_platform instead.
 */
#if EMULATED_PPC
#include "cpu/vm.hpp"
static inline uint32 ReadMacInt8(uint32 addr)  { return vm_read_memory_1(addr); }
static inline void WriteMacInt8(uint32 addr, uint32 v) { vm_write_memory_1(addr, v); }
static inline uint32 ReadMacInt16(uint32 addr) { return vm_read_memory_2(addr); }
static inline void WriteMacInt16(uint32 addr, uint32 v) { vm_write_memory_2(addr, v); }
static inline uint32 ReadMacInt32(uint32 addr) { return vm_read_memory_4(addr); }
static inline void WriteMacInt32(uint32 addr, uint32 v) { vm_write_memory_4(addr, v); }
static inline uint64 ReadMacInt64(uint32 addr) { return vm_read_memory_8(addr); }
static inline void WriteMacInt64(uint32 addr, uint64 v) { vm_write_memory_8(addr, v); }
static inline uint32 Host2MacAddr(uint8 *addr) { return vm_do_get_virtual_address(addr); }
static inline uint8 *Mac2HostAddr(uint32 addr) { return vm_do_get_real_address(addr); }
static inline void *Mac_memset(uint32 addr, int c, size_t n) { return vm_memset(addr, c, n); }
static inline void *Mac2Host_memcpy(void *dest, uint32 src, size_t n) { return vm_memcpy(dest, src, n); }
static inline void *Host2Mac_memcpy(uint32 dest, const void *src, size_t n) { return vm_memcpy(dest, src, n); }
static inline void *Mac2Mac_memcpy(uint32 dest, uint32 src, size_t n) { return vm_memcpy(dest, src, n); }
#endif

/*
 *  68k/PPC procedure helpers
 */

// 68k big-endian 16-bit word
#ifdef WORDS_BIGENDIAN
#define PW(W) W
#else
#define PW(X) ((((X) >> 8) & 0xff) | (((X) & 0xff) << 8))
#endif

// PPC big-endian 32-bit word
#ifdef WORDS_BIGENDIAN
#define PL(X) X
#else
#define PL(X) \
     ((((X) & 0xff000000) >> 24) | (((X) & 0x00ff0000) >>  8) | \
      (((X) & 0x0000ff00) <<  8) | (((X) & 0x000000ff) << 24))
#endif

// M68kRegisters — must match the definition in common/include/cpu_emulation.h
struct M68kRegisters {
    uint32 d[8];
    uint32 a[8];
};

// Single canonical dispatch — C linkage, lives in basilisk_glue.cpp,
// dispatches through g_platform.cpu_execute_68k[_trap].
extern "C" void Execute68k(uint32, M68kRegisters *r);
extern "C" void Execute68kTrap(uint16 trap, M68kRegisters *r);

#if EMULATED_PPC
extern void FlushCodeCache(uintptr start, uintptr end);
#endif
extern void ExecuteNative(int selector);

#endif /* KPX_CPU_EMULATION_H */
