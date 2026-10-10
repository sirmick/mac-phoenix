/*
 * osdcomm.h - MacPhoenix's stand-in for MAME's osdcomm.h: the basic
 * integer types and attributes. Included by emu.h and by the lifted
 * utility sources (mfpresolve.cpp).
 */
#pragma once

#include <cstddef>
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

namespace osd {
using u8 = ::u8;
using u16 = ::u16;
using u32 = ::u32;
using u64 = ::u64;
using s8 = ::s8;
using s16 = ::s16;
using s32 = ::s32;
using s64 = ::s64;
}  // namespace osd

/* Byte-order helpers (osdcomm.h). */
constexpr uint16_t swapendian_int16(uint16_t val) { return (val << 8) | (val >> 8); }
constexpr uint32_t swapendian_int32_partial16(uint32_t val) { return ((val << 8) & 0xFF00FF00U) | ((val >> 8) & 0x00FF00FFU); }
constexpr uint32_t swapendian_int32(uint32_t val) { return (swapendian_int32_partial16(val) << 16) | (swapendian_int32_partial16(val) >> 16); }
constexpr uint64_t swapendian_int64_partial16(uint64_t val) { return ((val << 8) & 0xFF00FF00FF00FF00U) | ((val >> 8) & 0x00FF00FF00FF00FFU); }
constexpr uint64_t swapendian_int64_partial32(uint64_t val) { return ((swapendian_int64_partial16(val) << 16) & 0xFFFF0000FFFF0000U) | ((swapendian_int64_partial16(val) >> 16) & 0x0000FFFF0000FFFFU); }
constexpr uint64_t swapendian_int64(uint64_t val) { return (swapendian_int64_partial32(val) << 32) | (swapendian_int64_partial32(val) >> 32); }
constexpr uint16_t big_endianize_int16(uint16_t x) { return swapendian_int16(x); }
constexpr uint32_t big_endianize_int32(uint32_t x) { return swapendian_int32(x); }
constexpr uint64_t big_endianize_int64(uint64_t x) { return swapendian_int64(x); }
constexpr uint16_t little_endianize_int16(uint16_t x) { return x; }
constexpr uint32_t little_endianize_int32(uint32_t x) { return x; }
constexpr uint64_t little_endianize_int64(uint64_t x) { return x; }

#define ATTR_COLD __attribute__((cold))
#define ATTR_HOT __attribute__((hot))
#define ATTR_FORCE_INLINE __attribute__((always_inline))
#define ATTR_PRINTF(x, y) __attribute__((format(printf, x, y)))
#define ATTR_UNUSED __attribute__((__unused__))
#define ATTR_NORETURN __attribute__((noreturn))
