/*
 *  memory_access.h - Backend-independent memory access functions
 *
 *  These functions provide direct memory access for ROM patching and
 *  system initialization, independent of any CPU backend (UAE, Unicorn, etc.)
 *
 *  Unlike UAE's get_long/put_long which go through memory banks, these
 *  functions access ROMBaseHost and RAMBaseHost directly.
 */

#ifndef MEMORY_ACCESS_H
#define MEMORY_ACCESS_H

#include "sysdeps.h"

// External memory pointers (defined in main.cpp or basilisk_glue.cpp)
extern uint8 *RAMBaseHost;   // RAM base (host address space)
extern uint32 RAMBaseMac;    // RAM base (Mac address space)
extern uint8 *ROMBaseHost;   // ROM base (host address space)
extern uint32 ROMBaseMac;    // ROM base (Mac address space)
extern uint32 RAMSize;       // Size of RAM
extern uint32 ROMSize;       // Size of ROM

/*
 * Backend-independent memory access functions
 * These convert Mac addresses to host pointers and access memory directly
 */

static inline uint8 *Mac2HostAddr(uint32 addr) {
	// Determine if address is in RAM or ROM range
	if (addr >= RAMBaseMac && addr < RAMBaseMac + RAMSize) {
		return RAMBaseHost + (addr - RAMBaseMac);
	} else if (addr >= ROMBaseMac && addr < ROMBaseMac + ROMSize) {
		return ROMBaseHost + (addr - ROMBaseMac);
	}
	// Invalid address - return nullptr (caller should check)
	return nullptr;
}

static inline uint32 Host2MacAddr(uint8 *addr) {
	// Check if address is in RAM range
	if (addr >= RAMBaseHost && addr < RAMBaseHost + RAMSize) {
		return RAMBaseMac + (addr - RAMBaseHost);
	}
	// Check if address is in ROM range
	if (addr >= ROMBaseHost && addr < ROMBaseHost + ROMSize) {
		return ROMBaseMac + (addr - ROMBaseHost);
	}
	// Invalid address
	return 0;
}

/*
 * Direct memory access (big-endian, M68K native format)
 * ROM is stored in big-endian format (as loaded from file)
 * RAM needs byte-swapping on little-endian hosts
 */

static inline uint32 DirectReadMacInt32(uint32 addr) {
	uint8 *ptr = Mac2HostAddr(addr);
	if (!ptr) return 0;
	// Read as big-endian (M68K native)
	return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
}

static inline uint16 DirectReadMacInt16(uint32 addr) {
	uint8 *ptr = Mac2HostAddr(addr);
	if (!ptr) return 0;
	// Read as big-endian (M68K native)
	return (ptr[0] << 8) | ptr[1];
}

static inline uint8 DirectReadMacInt8(uint32 addr) {
	uint8 *ptr = Mac2HostAddr(addr);
	if (!ptr) return 0;
	return *ptr;
}

static inline void DirectWriteMacInt32(uint32 addr, uint32 val) {
	uint8 *ptr = Mac2HostAddr(addr);
	if (!ptr) return;
	// Write as big-endian (M68K native)
	ptr[0] = (val >> 24) & 0xFF;
	ptr[1] = (val >> 16) & 0xFF;
	ptr[2] = (val >> 8) & 0xFF;
	ptr[3] = val & 0xFF;
}

static inline void DirectWriteMacInt16(uint32 addr, uint16 val) {
	uint8 *ptr = Mac2HostAddr(addr);
	if (!ptr) return;
	// Write as big-endian (M68K native)
	ptr[0] = (val >> 8) & 0xFF;
	ptr[1] = val & 0xFF;
}

static inline void DirectWriteMacInt8(uint32 addr, uint8 val) {
	uint8 *ptr = Mac2HostAddr(addr);
	if (!ptr) return;
	*ptr = val;
}

#endif /* MEMORY_ACCESS_H */
