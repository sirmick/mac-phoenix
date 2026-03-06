/*
 * UAE - The Un*x Amiga Emulator
 *
 * memory management
 *
 * Copyright 1995 Bernd Schmidt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ============================================================================
 * ENDIANNESS WARNING:
 * ============================================================================
 * UAE stores memory in LITTLE-ENDIAN format on x86 hosts:
 *   - RAMBaseHost: Contains RAM in little-endian byte order
 *   - get_long/put_long: Automatically byte-swap between LE storage and BE M68K view
 *   - ROM is an exception: Stored in big-endian as loaded from file
 *
 * When interfacing with other M68K emulators (e.g., Unicorn):
 *   - They expect BIG-ENDIAN memory (M68K native byte order)
 *   - DO NOT copy RAMBaseHost directly - must byte-swap first!
 *   - See unicorn_wrapper.c:unicorn_map_ram() for proper byte-swapping
 * ============================================================================
 */

#ifndef UAE_MEMORY_H
#define UAE_MEMORY_H

#if !DIRECT_ADDRESSING && !REAL_ADDRESSING

/* Enabling this adds one additional native memory reference per 68k memory
 * access, but saves one shift (on the x86). Enabling this is probably
 * better for the cache. My favourite benchmark (PP2) doesn't show a
 * difference, so I leave this enabled. */

#if 1 || defined SAVE_MEMORY
#define SAVE_MEMORY_BANKS
#endif

typedef uae_u32 (REGPARAM2 *mem_get_func)(uaecptr) REGPARAM;
typedef void (REGPARAM2 *mem_put_func)(uaecptr, uae_u32) REGPARAM;
typedef uae_u8 *(REGPARAM2 *xlate_func)(uaecptr) REGPARAM;

#undef DIRECT_MEMFUNCS_SUCCESSFUL

#ifndef CAN_MAP_MEMORY
#undef USE_COMPILER
#endif

#if defined(USE_COMPILER) && !defined(USE_MAPPED_MEMORY)
#define USE_MAPPED_MEMORY
#endif

typedef struct {
    /* These ones should be self-explanatory... */
    mem_get_func lget, wget, bget;
    mem_put_func lput, wput, bput;
    /* Use xlateaddr to translate an Amiga address to a uae_u8 * that can
     * be used to address memory without calling the wget/wput functions.
     * This doesn't work for all memory banks, so this function may call
     * abort(). */
    xlate_func xlateaddr;
} addrbank;

extern uae_u8 filesysory[65536];

extern addrbank ram_bank;	// Mac RAM
extern addrbank rom_bank;	// Mac ROM
extern addrbank frame_bank;	// Frame buffer

/* Default memory access functions */

extern uae_u8 *REGPARAM2 default_xlate(uaecptr addr) REGPARAM;

#define bankindex(addr) (((uaecptr)(addr)) >> 16)

#ifdef SAVE_MEMORY_BANKS
extern addrbank *mem_banks[65536];
#define get_mem_bank(addr) (*mem_banks[bankindex(addr)])
#define put_mem_bank(addr, b) (mem_banks[bankindex(addr)] = (b))
#else
extern addrbank mem_banks[65536];
#define get_mem_bank(addr) (mem_banks[bankindex(addr)])
#define put_mem_bank(addr, b) (mem_banks[bankindex(addr)] = *(b))
#endif

extern void memory_init(void);
extern void map_banks(addrbank *bank, int first, int count);

#ifndef NO_INLINE_MEMORY_ACCESS

#define longget(addr) (call_mem_get_func(get_mem_bank(addr).lget, addr))
#define wordget(addr) (call_mem_get_func(get_mem_bank(addr).wget, addr))
#define byteget(addr) (call_mem_get_func(get_mem_bank(addr).bget, addr))
#define longput(addr,l) (call_mem_put_func(get_mem_bank(addr).lput, addr, l))
#define wordput(addr,w) (call_mem_put_func(get_mem_bank(addr).wput, addr, w))
#define byteput(addr,b) (call_mem_put_func(get_mem_bank(addr).bput, addr, b))

#else

extern uae_u32 longget(uaecptr addr);
extern uae_u32 wordget(uaecptr addr);
extern uae_u32 byteget(uaecptr addr);
extern void longput(uaecptr addr, uae_u32 l);
extern void wordput(uaecptr addr, uae_u32 w);
extern void byteput(uaecptr addr, uae_u32 b);

#endif

#ifndef MD_HAVE_MEM_1_FUNCS

#define longget_1 longget
#define wordget_1 wordget
#define byteget_1 byteget
#define longput_1 longput
#define wordput_1 wordput
#define byteput_1 byteput

#endif

#endif /* !DIRECT_ADDRESSING && !REAL_ADDRESSING */

#if REAL_ADDRESSING
const uintptr MEMBaseDiff = 0;
#elif DIRECT_ADDRESSING
extern uintptr MEMBaseDiff;
#endif

// Memory layout variables (defined in basilisk_glue.cpp)
extern uint8_t *RAMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMBaseMac;
extern uint32_t ROMSize;

#if REAL_ADDRESSING || DIRECT_ADDRESSING
static __inline__ uae_u8 *do_get_real_address(uaecptr addr)
{
	if (addr < RAMSize) {
		return (uae_u8 *)MEMBaseDiff + addr;
	}
	// ROM + ScratchMem + FrameBuffer region (contiguous after RAM)
	// Layout: ROM(1MB) + ScratchMem(64KB) + FrameBuffer(4MB) = 5.06MB
	if (addr >= ROMBaseMac && addr < ROMBaseMac + ROMSize + 0x10000 + 0x400000) {
		return (uae_u8 *)MEMBaseDiff + addr;
	}
	// OOB: return pointer into zero-filled page (reads=0, writes=drop)
	static uae_u8 dummy_page[4096] = {0};
	return dummy_page + (addr & 0xFFF);
}
static __inline__ uae_u32 do_get_virtual_address(uae_u8 *addr)
{
	return (uintptr)addr - MEMBaseDiff;
}

/* Forward declarations for CPU tracing */
#ifdef __cplusplus
extern "C" {
#endif
extern void cpu_trace_log_mem_read(uint32_t addr, uint32_t value, int size);
extern bool cpu_trace_memory_enabled(void);
#ifdef __cplusplus
}
#endif

/* Platform API for backend-independent memory access
 *
 * IMPORTANT: REAL_ADDRESSING mode is NOT compatible with Unicorn or other
 * non-UAE backends. REAL_ADDRESSING was designed for running on actual M68K
 * hardware where the CPU natively executes M68K instructions. Unicorn requires
 * its own managed memory space and cannot share host memory directly.
 *
 * Only DIRECT_ADDRESSING and banking modes support multi-backend operation.
 */
#ifdef __cplusplus
extern "C" {
#endif
#include "../../common/include/platform.h"
extern Platform g_platform;
#ifdef __cplusplus
}
#endif

static __inline__ uae_u32 get_long(uaecptr addr)
{
    uae_u32 value;

    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_read_long) {
        value = g_platform.mem_read_long(addr);
    } else {
        // Fallback to direct UAE memory banking (during early init)
        uae_u32 * const m = (uae_u32 *)do_get_real_address(addr);
        value = do_get_mem_long(m);
    }

    if (cpu_trace_memory_enabled()) {
        cpu_trace_log_mem_read(addr, value, 4);
    }
    return value;
}
static __inline__ uae_u32 get_word(uaecptr addr)
{
    uae_u32 value;

    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_read_word) {
        value = g_platform.mem_read_word(addr);
    } else {
        // Fallback to direct UAE memory banking (during early init)
        uae_u16 * const m = (uae_u16 *)do_get_real_address(addr);
        value = do_get_mem_word(m);
    }

    if (cpu_trace_memory_enabled()) {
        cpu_trace_log_mem_read(addr, value, 2);
    }
    return value;
}
static __inline__ uae_u32 get_byte(uaecptr addr)
{
    uae_u32 value;

    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_read_byte) {
        value = g_platform.mem_read_byte(addr);
    } else {
        // Fallback to direct UAE memory banking (during early init)
        uae_u8 * const m = (uae_u8 *)do_get_real_address(addr);
        value = do_get_mem_byte(m);
    }

    if (cpu_trace_memory_enabled()) {
        cpu_trace_log_mem_read(addr, value, 1);
    }
    return value;
}
static __inline__ void put_long(uaecptr addr, uae_u32 l)
{
    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_write_long) {
        g_platform.mem_write_long(addr, l);
    } else {
        // Fallback to direct UAE memory banking (during early init)
        uae_u32 * const m = (uae_u32 *)do_get_real_address(addr);
        do_put_mem_long(m, l);
    }
}
static __inline__ void put_word(uaecptr addr, uae_u32 w)
{
    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_write_word) {
        g_platform.mem_write_word(addr, w);
    } else {
        // Fallback to direct UAE memory banking (during early init)
        uae_u16 * const m = (uae_u16 *)do_get_real_address(addr);
        do_put_mem_word(m, w);
    }
}
static __inline__ void put_byte(uaecptr addr, uae_u32 b)
{
    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_write_byte) {
        g_platform.mem_write_byte(addr, b);
    } else {
        // Fallback to direct UAE memory banking (during early init)
        uae_u8 * const m = (uae_u8 *)do_get_real_address(addr);
        do_put_mem_byte(m, b);
    }
}
static __inline__ uae_u8 *get_real_address(uaecptr addr)
{
	// Use platform API if available (backend-independent memory access)
	if (g_platform.mem_mac_to_host) {
		return g_platform.mem_mac_to_host(addr);
	} else {
		// Fallback to direct UAE address translation (during early init)
		return do_get_real_address(addr);
	}
}
static __inline__ uae_u32 get_virtual_address(uae_u8 *addr)
{
	// Use platform API if available (backend-independent memory access)
	if (g_platform.mem_host_to_mac) {
		return g_platform.mem_host_to_mac(addr);
	} else {
		// Fallback to direct UAE address translation (during early init)
		return do_get_virtual_address(addr);
	}
}
#else
static __inline__ uae_u32 get_long(uaecptr addr)
{
    // Use platform API if available (backend-independent memory access)
    if (g_platform.mem_read_long) {
        return g_platform.mem_read_long(addr);
    } else {
        return longget_1(addr);
    }
}
static __inline__ uae_u32 get_word(uaecptr addr)
{
    if (g_platform.mem_read_word) {
        return g_platform.mem_read_word(addr);
    } else {
        return wordget_1(addr);
    }
}
static __inline__ uae_u32 get_byte(uaecptr addr)
{
    if (g_platform.mem_read_byte) {
        return g_platform.mem_read_byte(addr);
    } else {
        return byteget_1(addr);
    }
}
static __inline__ void put_long(uaecptr addr, uae_u32 l)
{
    if (g_platform.mem_write_long) {
        g_platform.mem_write_long(addr, l);
    } else {
        longput_1(addr, l);
    }
}
static __inline__ void put_word(uaecptr addr, uae_u32 w)
{
    if (g_platform.mem_write_word) {
        g_platform.mem_write_word(addr, w);
    } else {
        wordput_1(addr, w);
    }
}
static __inline__ void put_byte(uaecptr addr, uae_u32 b)
{
    if (g_platform.mem_write_byte) {
        g_platform.mem_write_byte(addr, b);
    } else {
        byteput_1(addr, b);
    }
}
static __inline__ uae_u8 *get_real_address(uaecptr addr)
{
    if (g_platform.mem_mac_to_host) {
        return g_platform.mem_mac_to_host(addr);
    } else {
        return get_mem_bank(addr).xlateaddr(addr);
    }
}
/* gb-- deliberately not implemented since it shall not be used... */
extern uae_u32 get_virtual_address(uae_u8 *addr);
#endif /* DIRECT_ADDRESSING || REAL_ADDRESSING */

#endif /* MEMORY_H */

