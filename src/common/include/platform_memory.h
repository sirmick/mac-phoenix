/*
 *  platform_memory.h - Backend-independent memory access via Platform API
 *
 *  This header provides ReadMacInt*/WriteMacInt* functions that work with
 *  ANY CPU backend (UAE, Unicorn, DualCPU) by using the Platform API.
 *
 *  Use this header in code that needs to be backend-independent:
 *    - ROM patching (rom_patches.cpp)
 *    - System initialization
 *    - Emulator infrastructure
 *
 *  DO NOT use this header in UAE CPU code (uae_cpu/) - that code should
 *  continue using UAE's memory.h with get_long/put_long directly.
 */

#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

#include "sysdeps.h"
#include "platform.h"

// External platform instance
extern Platform g_platform;

// Backend-independent memory access functions
// These call the appropriate backend implementation via g_platform
static inline uint32 ReadMacInt32(uint32 addr) {
	return g_platform.mem_read_long(addr);
}

static inline uint32 ReadMacInt16(uint32 addr) {
	return g_platform.mem_read_word(addr);
}

static inline uint32 ReadMacInt8(uint32 addr) {
	return g_platform.mem_read_byte(addr);
}

static inline void WriteMacInt32(uint32 addr, uint32 val) {
	g_platform.mem_write_long(addr, val);
}

static inline void WriteMacInt16(uint32 addr, uint32 val) {
	g_platform.mem_write_word(addr, val);
}

static inline void WriteMacInt8(uint32 addr, uint32 val) {
	g_platform.mem_write_byte(addr, val);
}

// Address translation
static inline uint8 *Mac2HostAddr(uint32 addr) {
	return g_platform.mem_mac_to_host(addr);
}

static inline uint32 Host2MacAddr(uint8 *ptr) {
	return g_platform.mem_host_to_mac(ptr);
}

// Helper functions for memory operations
static inline void *Mac_memset(uint32 addr, int c, size_t n) {
	return memset(Mac2HostAddr(addr), c, n);
}

static inline void *Mac2Host_memcpy(void *dest, uint32 src, size_t n) {
	return memcpy(dest, Mac2HostAddr(src), n);
}

static inline void *Host2Mac_memcpy(uint32 dest, const void *src, size_t n) {
	return memcpy(Mac2HostAddr(dest), src, n);
}

static inline void *Mac2Mac_memcpy(uint32 dest, uint32 src, size_t n) {
	return memcpy(Mac2HostAddr(dest), Mac2HostAddr(src), n);
}

#endif /* PLATFORM_MEMORY_H */
