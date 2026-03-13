/*
 *  cpu_emulation.h - KPX shim for cpu_emulation interface
 *
 *  Provides ReadMacInt/WriteMacInt using KPX's vm.hpp memory accessors
 *  instead of UAE's get_long/put_long.
 */

#ifndef CPU_EMULATION_H
#define CPU_EMULATION_H

#include "sysdeps.h"
#include "cpu/vm.hpp"

// Memory layout constants (from mac-phoenix)
extern uint32_t RAMBase;
extern uint32_t RAMSize;
extern uint32_t ROMBase;

// ROM area constants
#ifndef ROM_SIZE
#define ROM_SIZE 0x100000
#endif
#ifndef ROM_AREA_SIZE
#define ROM_AREA_SIZE 0x500000
#endif

// Memory accessors using KPX vm.hpp
static inline uint32 ReadMacInt32(uint32 addr) { return vm_read_memory_4(addr); }
static inline uint32 ReadMacInt16(uint32 addr) { return vm_read_memory_2(addr); }
static inline uint32 ReadMacInt8(uint32 addr)  { return vm_read_memory_1(addr); }
static inline void WriteMacInt32(uint32 addr, uint32 l) { vm_write_memory_4(addr, l); }
static inline void WriteMacInt16(uint32 addr, uint32 w) { vm_write_memory_2(addr, w); }
static inline void WriteMacInt8(uint32 addr, uint32 b)  { vm_write_memory_1(addr, b); }
static inline uint8 *Mac2HostAddr(uint32 addr) { return vm_do_get_real_address(addr); }
static inline uint32 Host2MacAddr(uint8 *addr)  { return vm_do_get_virtual_address(addr); }
static inline void Mac2Host_memcpy(void *dest, uint32 src, size_t n) { vm_memcpy(dest, src, n); }
static inline void Host2Mac_memcpy(uint32 dest, const void *src, size_t n) { vm_memcpy(dest, src, n); }
static inline void Mac2Mac_memcpy(uint32 dest, uint32 src, size_t n) { vm_memcpy(dest, src, n); }

#endif /* CPU_EMULATION_H */
