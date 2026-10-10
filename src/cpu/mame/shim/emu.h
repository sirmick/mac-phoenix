/*
 * emu.h - MacPhoenix's stand-in for MAME's emu.h.
 *
 * Just enough of the MAME framework for the CPU cores lifted into
 * src/cpu/mame to compile unchanged: the device class hierarchy and its
 * interfaces, address spaces bound to a cpu::GuestMemory, timers kept in
 * CPU cycles, delegates on std::function, no-op save-state and
 * debugger-state registration, logging, and the options the recompiler
 * reads. MAME's own self-contained utility headers (strformat,
 * endianness, corefloat, mfpresolve) are lifted rather than imitated.
 * Nothing here is a general emulation framework; each stand-in does what
 * the lifted code needs and no more. docs/cpu/PLAN.md, "The shim".
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iosfwd>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "osdcomm.h"
#include "cpu_core.h"

#include "corefloat.h"
#include "coretmpl.h"
#include "endianness.h"
#include "strformat.h"

/* ---------------------------------------------------------------------- */
/* Types and constants (emucore.h, diexec.h, emumem.h, distate.h)         */
/* ---------------------------------------------------------------------- */

using offs_t = u32;

#define NAME(x) x, #x
#define STRUCT_MEMBER(s, m) s, #s "." #m
#define FUNC(x) &x, #x

#define WORD_ALIGNED(a) (((a) & 1) == 0)
#define DWORD_ALIGNED(a) (((a) & 3) == 0)
#define QWORD_ALIGNED(a) (((a) & 7) == 0)

enum line_state { CLEAR_LINE = 0, ASSERT_LINE, HOLD_LINE };
constexpr int MAX_INPUT_LINES = 64 + 3;
constexpr int INPUT_LINE_IRQ0 = 0;
constexpr int INPUT_LINE_NMI = MAX_INPUT_LINES - 3;
constexpr int INPUT_LINE_RESET = MAX_INPUT_LINES - 2;
constexpr int INPUT_LINE_HALT = MAX_INPUT_LINES - 1;

using endianness_t = std::endian;
constexpr endianness_t ENDIANNESS_LITTLE = std::endian::little;
constexpr endianness_t ENDIANNESS_BIG = std::endian::big;
constexpr endianness_t ENDIANNESS_NATIVE = std::endian::native;

constexpr int AS_PROGRAM = 0;
constexpr int AS_DATA = 1;
constexpr int AS_IO = 2;
constexpr int AS_OPCODES = 3;

enum { STATE_GENPC = -1, STATE_GENPCBASE = -2, STATE_GENFLAGS = -3 };

constexpr u32 DEBUG_FLAG_ENABLED = 0x01;
constexpr u32 DEBUG_FLAG_CALL_HOOK = 0x02;

#define EXPECTED(x) __builtin_expect(!!(x), 1)
#define UNEXPECTED(x) __builtin_expect(!!(x), 0)

/* eminline.h */
inline s64 mul_32x32(s32 a, s32 b) { return (s64)a * (s64)b; }
inline u64 mulu_32x32(u32 a, u32 b) { return (u64)a * (u64)b; }
inline s32 mul_32x32_hi(s32 a, s32 b) { return (s32)(((s64)a * (s64)b) >> 32); }
inline u32 mulu_32x32_hi(u32 a, u32 b) { return (u32)(((u64)a * (u64)b) >> 32); }

/* profiler.h: nothing is profiled here. */
enum profile_type { PROFILER_DRC_COMPILE = 0 };
struct profiler_state
{
    struct scope { };
    [[nodiscard]] scope start(profile_type) noexcept { return {}; }
};
extern profiler_state g_profiler;

inline void osd_break_into_debugger(const char *message) { fprintf(stderr, "%s\n", message); }
inline std::pair<std::error_condition, unsigned> osd_get_cache_line_size() noexcept
{
    return std::make_pair(std::error_condition(), 64u);
}

/* ---------------------------------------------------------------------- */
/* Utilities (lib/util/coretmpl.h); formatting is MAME's strformat.h      */
/* ---------------------------------------------------------------------- */

namespace util {

/* Disassembler interface (disasmintf.h). */
class disasm_interface
{
public:
    virtual ~disasm_interface() = default;

    using u8 = ::u8;
    using u16 = ::u16;
    using u32 = ::u32;
    using u64 = ::u64;
    using s8 = ::s8;
    using s16 = ::s16;
    using s32 = ::s32;
    using s64 = ::s64;
    using offs_t = u32;

    static constexpr u32 SUPPORTED = 0x80000000;
    static constexpr u32 STEP_OUT = 0x40000000;
    static constexpr u32 STEP_OVER = 0x20000000;
    static constexpr u32 STEP_COND = 0x10000000;
    static constexpr u32 OVERINSTMASK = 0x0c000000;
    static constexpr u32 OVERINSTSHIFT = 26;
    static constexpr u32 LENGTHMASK = 0x0000ffff;
    static inline u32 step_over_extra(u32 x) { return x << OVERINSTSHIFT; }

    class data_buffer
    {
    public:
        virtual ~data_buffer() = default;
        virtual u8 r8(offs_t pc) const = 0;
        virtual u16 r16(offs_t pc) const = 0;
        virtual u32 r32(offs_t pc) const = 0;
        virtual u64 r64(offs_t pc) const = 0;
    };

    enum {
        NONLINEAR_PC = 0x00000001,
        PAGED = 0x00000002,
        PAGED2LEVEL = 0x00000006,
        INTERNAL_DECRYPTION = 0x00000008,
        SPLIT_DECRYPTION = 0x00000018
    };

    virtual u32 interface_flags() const { return 0; }
    virtual u32 page_address_bits() const { return 0; }
    virtual u32 page2_address_bits() const { return 0; }
    virtual offs_t pc_linear_to_real(offs_t pc) const { return pc; }
    virtual offs_t pc_real_to_linear(offs_t pc) const { return pc; }
    virtual u8 decrypt8(u8 value, offs_t pc, bool opcode) const { return value; }
    virtual u16 decrypt16(u16 value, offs_t pc, bool opcode) const { return value; }
    virtual u32 decrypt32(u32 value, offs_t pc, bool opcode) const { return value; }
    virtual u64 decrypt64(u64 value, offs_t pc, bool opcode) const { return value; }

    virtual u32 opcode_alignment() const = 0;
    virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes,
                               const data_buffer &params) = 0;
};

}  // namespace util

using util::BIT;
using util::make_bitmask;
using util::string_format;

/* ---------------------------------------------------------------------- */
/* Errors and logging                                                     */
/* ---------------------------------------------------------------------- */

class emu_exception : public std::exception
{
};

class emu_fatalerror : public emu_exception
{
public:
    template <typename... Args>
    explicit emu_fatalerror(const char *fmt, Args &&...args)
        : what_(util::string_format(fmt, std::forward<Args>(args)...))
    {
    }
    explicit emu_fatalerror(const std::string &what) : what_(what) {}
    const char *what() const noexcept override { return what_.c_str(); }

private:
    std::string what_;
};

template <typename... Args> [[noreturn]] void fatalerror(const char *fmt, Args &&...args)
{
    throw emu_fatalerror(util::string_format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void osd_printf_error(const char *fmt, Args &&...args)
{
    fputs(util::string_format(fmt, std::forward<Args>(args)...).c_str(), stderr);
}
template <typename... Args> void osd_printf_warning(const char *fmt, Args &&...args)
{
    fputs(util::string_format(fmt, std::forward<Args>(args)...).c_str(), stderr);
}
template <typename... Args> void osd_printf_info(const char *fmt, Args &&...args)
{
    fputs(util::string_format(fmt, std::forward<Args>(args)...).c_str(), stderr);
}
template <typename... Args> void osd_printf_verbose(const char *, Args &&...) {}
template <typename... Args> void osd_printf_debug(const char *, Args &&...) {}

/* ---------------------------------------------------------------------- */
/* Time (attotime.h, xtal.h)                                              */
/* ---------------------------------------------------------------------- */

using seconds_t = s32;
using attoseconds_t = s64;
constexpr attoseconds_t ATTOSECONDS_PER_SECOND = 1000000000000000000LL;
constexpr seconds_t ATTOTIME_MAX_SECONDS = 1000000000;

class XTAL
{
public:
    constexpr XTAL(double v) : v_(v) {}
    constexpr u32 value() const { return (u32)v_; }
    constexpr double dvalue() const { return v_; }

private:
    double v_;
};

/* MAME's attotime: seconds plus attoseconds. Only what the lifted cores
 * use: construction from Hz, microseconds and clock ticks, conversion to
 * ticks, arithmetic and comparison. */
class attotime
{
public:
    constexpr attotime() noexcept : m_seconds(0), m_attoseconds(0) {}
    constexpr attotime(seconds_t secs, attoseconds_t attos) noexcept : m_seconds(secs), m_attoseconds(attos) {}

    constexpr bool is_zero() const noexcept { return m_seconds == 0 && m_attoseconds == 0; }
    constexpr bool is_never() const noexcept { return m_seconds >= ATTOTIME_MAX_SECONDS; }
    constexpr seconds_t seconds() const noexcept { return m_seconds; }
    constexpr attoseconds_t attoseconds() const noexcept { return m_attoseconds; }
    double as_double() const noexcept { return (double)m_seconds + (double)m_attoseconds / (double)ATTOSECONDS_PER_SECOND; }
    double as_hz() const noexcept { return is_never() ? 0.0 : 1.0 / as_double(); }

    u64 as_ticks(u32 frequency) const
    {
        return (u64)m_seconds * frequency + (u64)((__int128)m_attoseconds * frequency / ATTOSECONDS_PER_SECOND);
    }
    u64 as_ticks(const XTAL &xtal) const { return as_ticks(xtal.value()); }

    static attotime from_ticks(u64 ticks, u32 frequency)
    {
        if(frequency == 0)
            return never;
        u64 secs = ticks / frequency;
        u64 rem = ticks - secs * frequency;
        if(secs >= (u64)ATTOTIME_MAX_SECONDS)
            return never;
        return attotime((seconds_t)secs, (attoseconds_t)((__int128)rem * ATTOSECONDS_PER_SECOND / frequency));
    }
    static attotime from_ticks(u64 ticks, const XTAL &xtal) { return from_ticks(ticks, xtal.value()); }
    static constexpr attotime from_seconds(s32 seconds) { return attotime(seconds, 0); }
    static constexpr attotime from_msec(s64 msec)
    {
        return attotime((seconds_t)(msec / 1000), (msec % 1000) * (ATTOSECONDS_PER_SECOND / 1000));
    }
    static constexpr attotime from_usec(s64 usec)
    {
        return attotime((seconds_t)(usec / 1000000), (usec % 1000000) * (ATTOSECONDS_PER_SECOND / 1000000));
    }
    static constexpr attotime from_nsec(s64 nsec)
    {
        return attotime((seconds_t)(nsec / 1000000000), (nsec % 1000000000) * (ATTOSECONDS_PER_SECOND / 1000000000));
    }
    static attotime from_hz(u32 frequency)
    {
        return frequency > 1 ? attotime(0, ATTOSECONDS_PER_SECOND / frequency)
                             : (frequency == 1 ? attotime(1, 0) : never);
    }
    static attotime from_hz(int frequency) { return frequency > 0 ? from_hz((u32)frequency) : never; }
    static attotime from_hz(double frequency)
    {
        if(frequency > 1.0)
            return attotime(0, (attoseconds_t)((double)ATTOSECONDS_PER_SECOND / frequency));
        if(frequency > 0.0)
        {
            double i;
            double f = std::modf(1.0 / frequency, &i);
            return attotime((seconds_t)i, (attoseconds_t)(f * (double)ATTOSECONDS_PER_SECOND));
        }
        return never;
    }
    static attotime from_hz(const XTAL &xtal) { return from_hz(xtal.dvalue()); }
    static attotime from_double(double seconds)
    {
        double i;
        double f = std::modf(seconds, &i);
        return attotime((seconds_t)i, (attoseconds_t)(f * (double)ATTOSECONDS_PER_SECOND));
    }

    attotime &operator+=(const attotime &r) noexcept
    {
        if(is_never() || r.is_never())
            return *this = never;
        m_attoseconds += r.m_attoseconds;
        m_seconds += r.m_seconds;
        if(m_attoseconds >= ATTOSECONDS_PER_SECOND)
        {
            m_attoseconds -= ATTOSECONDS_PER_SECOND;
            m_seconds++;
        }
        if(m_seconds >= ATTOTIME_MAX_SECONDS)
            *this = never;
        return *this;
    }
    attotime &operator-=(const attotime &r) noexcept
    {
        if(is_never())
            return *this;
        m_attoseconds -= r.m_attoseconds;
        m_seconds -= r.m_seconds;
        if(m_attoseconds < 0)
        {
            m_attoseconds += ATTOSECONDS_PER_SECOND;
            m_seconds--;
        }
        return *this;
    }
    attotime &operator*=(u32 factor) noexcept
    {
        if(is_never())
            return *this;
        __int128 total = ((__int128)m_seconds * ATTOSECONDS_PER_SECOND + m_attoseconds) * factor;
        m_seconds = (seconds_t)(total / ATTOSECONDS_PER_SECOND);
        m_attoseconds = (attoseconds_t)(total % ATTOSECONDS_PER_SECOND);
        if(m_seconds >= ATTOTIME_MAX_SECONDS)
            *this = never;
        return *this;
    }

    static const attotime never;
    static const attotime zero;

    seconds_t m_seconds;
    attoseconds_t m_attoseconds;
};

inline attotime operator+(const attotime &l, const attotime &r) noexcept { attotime t = l; t += r; return t; }
inline attotime operator-(const attotime &l, const attotime &r) noexcept { attotime t = l; t -= r; return t; }
inline attotime operator*(const attotime &l, u32 f) noexcept { attotime t = l; t *= f; return t; }
inline attotime operator*(u32 f, const attotime &r) noexcept { attotime t = r; t *= f; return t; }
inline bool operator==(const attotime &l, const attotime &r) noexcept
{
    return l.m_seconds == r.m_seconds && l.m_attoseconds == r.m_attoseconds;
}
inline bool operator!=(const attotime &l, const attotime &r) noexcept { return !(l == r); }
inline bool operator<(const attotime &l, const attotime &r) noexcept
{
    return l.m_seconds < r.m_seconds || (l.m_seconds == r.m_seconds && l.m_attoseconds < r.m_attoseconds);
}
inline bool operator<=(const attotime &l, const attotime &r) noexcept { return l < r || l == r; }
inline bool operator>(const attotime &l, const attotime &r) noexcept { return r < l; }
inline bool operator>=(const attotime &l, const attotime &r) noexcept { return r <= l; }

/* ---------------------------------------------------------------------- */
/* Machine, options                                                       */
/* ---------------------------------------------------------------------- */

/* The recompiler options MAME reads from the command line. Fixed here:
 * the native back-end when there is one, no logging, RWX cache pages
 * (the W^X toggle comes with the arm64 back-end). */
class emu_options
{
public:
    bool drc_rwx() const { return true; }
    bool drc_use_c() const { return use_c_; }
    bool drc_log_uml() const { return false; }
    bool drc_log_native() const { return false; }

    /* Per device (every device has its own running_machine here): the
     * UML interpreter instead of the native back-end. */
    void set_drc_use_c(bool c) { use_c_ = c; }

private:
    bool use_c_ = false;
};

class running_machine
{
public:
    bool side_effects_disabled() const { return false; }
    const emu_options &options() const { return options_; }
    emu_options &mutable_options() { return options_; }
    u32 rand() { return (u32)::rand(); }
    u32 debug_flags = 0;

private:
    emu_options options_;
};

class machine_config
{
public:
    const emu_options &options() const { return options_; }

private:
    emu_options options_;
};

struct device_type_impl
{
    const char *shortname;
    const char *fullname;
};
using device_type = const device_type_impl &;

#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type_impl Type;
#define DEFINE_DEVICE_TYPE(Type, Class, ShortName, FullName) \
    const device_type_impl Type{ShortName, FullName};

class validity_checker;

/* ---------------------------------------------------------------------- */
/* Delegates (delegate.h, devdelegate.h): std::function underneath         */
/* ---------------------------------------------------------------------- */

class device_t;

template <typename Signature> class delegate;

template <typename R, typename... Args> class delegate<R(Args...)>
{
public:
    delegate() = default;
    delegate(std::nullptr_t) {}
    template <typename C, typename F> delegate(F C::*mfp, C *obj) : fn_([mfp, obj](Args... a) { return (obj->*mfp)(a...); }) {}
    template <typename C, typename F> delegate(F C::*mfp, const char *, C *obj) : delegate(mfp, obj) {}
    delegate(std::function<R(Args...)> f) : fn_(std::move(f)) {}

    bool isnull() const { return !fn_; }
    bool has_object() const { return bool(fn_); }
    R operator()(Args... a) const { return fn_(a...); }
    void set(std::nullptr_t) { fn_ = nullptr; }
    template <typename C, typename F> void set(F C::*mfp, C *obj) { fn_ = [mfp, obj](Args... a) { return (obj->*mfp)(a...); }; }

private:
    std::function<R(Args...)> fn_;
};

/* device_delegate: a delegate owned by a device, resolved later. The
 * device is irrelevant here; the delegate just starts unset. */
template <typename Signature> class device_delegate : public delegate<Signature>
{
public:
    using delegate<Signature>::delegate;
    device_delegate() = default;
    explicit device_delegate(device_t &) {}
    device_delegate(device_t &, std::nullptr_t) {}
    device_delegate &operator=(const delegate<Signature> &o)
    {
        delegate<Signature>::operator=(o);
        return *this;
    }
    bool isnull() const { return delegate<Signature>::isnull(); }
    void resolve() {}
    template <typename... T> void resolve_safe(T &&...) {}

    template <unsigned N> class array : public std::array<device_delegate, N>
    {
    public:
        explicit array(device_t &) {}
    };
};

class address_space;
using read8sm_delegate = device_delegate<u8(offs_t)>;
using read16sm_delegate = device_delegate<u16(offs_t)>;
using read32sm_delegate = device_delegate<u32(offs_t)>;
using read8smo_delegate = device_delegate<u8()>;
using write8sm_delegate = device_delegate<void(offs_t, u8)>;
using write16sm_delegate = device_delegate<void(offs_t, u16)>;
using write32sm_delegate = device_delegate<void(offs_t, u32)>;
using write8smo_delegate = device_delegate<void(u8)>;
using read8_delegate = device_delegate<u8(address_space &, offs_t, u8)>;
using read16_delegate = device_delegate<u16(address_space &, offs_t, u16)>;
using read32_delegate = device_delegate<u32(address_space &, offs_t, u32)>;
using write8_delegate = device_delegate<void(address_space &, offs_t, u8, u8)>;
using write16_delegate = device_delegate<void(address_space &, offs_t, u16, u16)>;
using write32_delegate = device_delegate<void(address_space &, offs_t, u32, u32)>;

/* devcb_write_line: a line-output callback. Unbound unless the host
 * sets one through set(). */
class devcb_write_line
{
public:
    struct binder
    {
    };
    explicit devcb_write_line(device_t &) {}
    binder bind() { return {}; }
    void set(std::function<void(int)> f) { fn_ = std::move(f); }
    bool isunset() const { return !fn_; }
    bool isnull() const { return !fn_; }
    void operator()(int state)
    {
        if(fn_)
            fn_(state);
    }

private:
    std::function<void(int)> fn_;
};

/* ---------------------------------------------------------------------- */
/* Address spaces on a cpu::GuestMemory                                   */
/* ---------------------------------------------------------------------- */

class address_map_entry
{
public:
    template <typename... Args> address_map_entry &m(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &lr8(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &lr16(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &lr32(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &r(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &w(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &rw(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &ram(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &rom(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &noprw(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &mirror(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &nopr(Args &&...) { return *this; }
    template <typename... Args> address_map_entry &nopw(Args &&...) { return *this; }
};

/* A device's internal address map: nothing is mapped, the whole guest
 * space is the GuestMemory. The entry builder swallows every call. */
class address_map
{
public:
    address_map_entry &operator()(offs_t, offs_t) { return entry_; }
    template <typename... Args> address_map &global_mask(Args &&...) { return *this; }
    template <typename... Args> address_map &unmap_value_high(Args &&...) { return *this; }
    template <typename... Args> address_map &unmap_value_low(Args &&...) { return *this; }

private:
    address_map_entry entry_;
};

class address_map_constructor
{
public:
    address_map_constructor() = default;
    template <typename... Args> address_map_constructor(Args &&...) {}
    bool isnull() const { return true; }
};

class address_space_config
{
public:
    address_space_config(const char *name, endianness_t endian, u8 data_width, u8 addr_width,
                         s8 addr_shift = 0, address_map_constructor internal = address_map_constructor())
        : m_name(name), m_endianness(endian), m_data_width(data_width), m_addr_width(addr_width),
          m_addr_shift(addr_shift), m_logaddr_width(addr_width), m_page_shift(0)
    {
        (void)internal;
    }
    address_space_config(const char *name, endianness_t endian, u8 data_width, u8 addr_width,
                         s8 addr_shift, u8 logaddr_width, u8 page_shift,
                         address_map_constructor internal = address_map_constructor())
        : m_name(name), m_endianness(endian), m_data_width(data_width), m_addr_width(addr_width),
          m_addr_shift(addr_shift), m_logaddr_width(logaddr_width), m_page_shift(page_shift)
    {
        (void)internal;
    }

    const char *name() const { return m_name; }
    endianness_t endianness() const { return m_endianness; }
    int data_width() const { return m_data_width; }
    int addr_width() const { return m_addr_width; }
    int addr_shift() const { return m_addr_shift; }
    int logaddr_width() const { return m_logaddr_width; }
    int page_shift() const { return m_page_shift; }
    offs_t addrmask() const { return make_bitmask<offs_t>(m_addr_width); }
    offs_t logaddrmask() const { return make_bitmask<offs_t>(m_logaddr_width); }

    const char *m_name;
    endianness_t m_endianness;
    u8 m_data_width;
    u8 m_addr_width;
    s8 m_addr_shift;
    u8 m_logaddr_width;
    u8 m_page_shift;
};

/* One address space. Big-endian data accesses go to a GuestMemory; the
 * "CPU space" variant answers interrupt-acknowledge reads with the
 * autovector for the level in the address (as MAME's default map does).
 * Masked writes write only the lanes the mask selects. */
class address_space
{
public:
    enum class Kind { Memory, Autovectors };

    /* MAME's native back-ends can call a space's handler dispatch table
     * directly; ours has none (function == 0, not virtual), so they fall
     * back to calling the resolved member functions below. */
    struct specific_access_info
    {
        struct side
        {
            void const *const *dispatch;
            uintptr_t function;
            ptrdiff_t displacement;
            bool is_virtual;
        };
        unsigned native_bytes;
        unsigned native_mask_bits;
        unsigned address_width;
        unsigned low_bits;
        side read;
        side write;
    };

    address_space() = default;
    address_space(cpu::GuestMemory *mem, Kind kind) : mem_(mem), kind_(kind) {}

    cpu::GuestMemory *guest_memory() const { return mem_; }
    offs_t addrmask() const { return 0xffffffffu; }
    endianness_t endianness() const { return ENDIANNESS_BIG; }
    int addr_shift() const { return 0; }
    int data_width() const { return 32; }
    specific_access_info specific_accessors() const
    {
        specific_access_info i{};
        i.native_bytes = 4;
        i.native_mask_bits = 0;
        i.address_width = 32;
        i.low_bits = 0;
        return i;
    }

    /* Host pointer for a guest address, or null when unmapped (MAME:
     * null for handler-backed addresses). */
    void *get_read_ptr(offs_t a) const { return kind_ == Kind::Memory ? mem_->host(a) : nullptr; }
    void *get_write_ptr(offs_t a) const { return kind_ == Kind::Memory ? mem_->host(a) : nullptr; }

    u8 read_byte(offs_t a)
    {
        if(kind_ == Kind::Autovectors)
            return autovector(a);
        return mem_->read8(a);
    }
    u8 read_byte(offs_t a, u8 mask) { return read_byte(a) & mask; }
    u16 read_word(offs_t a)
    {
        if(kind_ == Kind::Autovectors)
            return autovector(a);
        return mem_->read16(a);
    }
    u16 read_word(offs_t a, u16 mask) { return read_word(a) & mask; }
    u16 read_word_unaligned(offs_t a) { return mem_->read16(a); }
    u32 read_dword(offs_t a) { return mem_->read32(a); }
    u32 read_dword(offs_t a, u32 mask) { return mem_->read32(a) & mask; }
    u32 read_dword_unaligned(offs_t a) { return mem_->read32(a); }
    u64 read_qword(offs_t a) { return (u64)mem_->read32(a) << 32 | mem_->read32(a + 4); }
    u64 read_qword(offs_t a, u64 mask) { return read_qword(a) & mask; }
    u64 read_qword_unaligned(offs_t a) { return read_qword(a); }

    void write_byte(offs_t a, u8 v) { mem_->write8(a, v); }
    void write_byte(offs_t a, u8 v, u8 mask)
    {
        if(mask)
            mem_->write8(a, (u8)((mem_->read8(a) & ~mask) | (v & mask)));
    }
    void write_word(offs_t a, u16 v) { mem_->write16(a, v); }
    void write_word(offs_t a, u16 v, u16 mask)
    {
        if(mask == 0xffff)
            mem_->write16(a, v);
        else
        {
            if(mask & 0xff00) mem_->write8(a, (u8)(v >> 8));
            if(mask & 0x00ff) mem_->write8(a + 1, (u8)v);
        }
    }
    void write_word_unaligned(offs_t a, u16 v) { mem_->write16(a, v); }
    void write_dword(offs_t a, u32 v) { mem_->write32(a, v); }
    void write_dword(offs_t a, u32 v, u32 mask)
    {
        if(mask == 0xffffffffu)
            mem_->write32(a, v);
        else
        {
            if(mask & 0xff000000u) mem_->write8(a, (u8)(v >> 24));
            if(mask & 0x00ff0000u) mem_->write8(a + 1, (u8)(v >> 16));
            if(mask & 0x0000ff00u) mem_->write8(a + 2, (u8)(v >> 8));
            if(mask & 0x000000ffu) mem_->write8(a + 3, (u8)v);
        }
    }
    void write_dword_unaligned(offs_t a, u32 v) { mem_->write32(a, v); }
    void write_qword(offs_t a, u64 v)
    {
        mem_->write32(a, (u32)(v >> 32));
        mem_->write32(a + 4, (u32)v);
    }
    void write_qword(offs_t a, u64 v, u64 mask)
    {
        write_dword(a, (u32)(v >> 32), (u32)(mask >> 32));
        write_dword(a + 4, (u32)v, (u32)mask);
    }
    void write_qword_unaligned(offs_t a, u64 v) { write_qword(a, v); }

    /* MAME's cores ask a space for a cache or specific accessor object;
     * ours just point back at the space. */
    template <typename T> void cache(T &t) { t.m_space = this; }
    template <typename T> void specific(T &t) { t.m_space = this; }

private:
    static u8 autovector(offs_t a) { return 0x18 + ((a >> 1) & 7); }

    cpu::GuestMemory *mem_ = nullptr;
    Kind kind_ = Kind::Memory;
};

/* memory_access<HighBits, Width, AddrShift, Endian>::cache / ::specific:
 * forwarders to the address_space they were bound to. */
template <int HighBits, int Width, int AddrShift, endianness_t Endian> struct memory_access
{
    struct cache
    {
        address_space *m_space = nullptr;

        void *read_ptr(offs_t a) const { return m_space->get_read_ptr(a); }
        u8 read_byte(offs_t a) const { return m_space->read_byte(a); }
        u8 read_byte(offs_t a, u8 mask) const { return m_space->read_byte(a, mask); }
        u16 read_word(offs_t a) const { return m_space->read_word(a); }
        u16 read_word(offs_t a, u16 mask) const { return m_space->read_word(a, mask); }
        u16 read_word_unaligned(offs_t a) const { return m_space->read_word_unaligned(a); }
        u32 read_dword(offs_t a) const { return m_space->read_dword(a); }
        u32 read_dword(offs_t a, u32 mask) const { return m_space->read_dword(a, mask); }
        u32 read_dword_unaligned(offs_t a) const { return m_space->read_dword_unaligned(a); }
        u64 read_qword(offs_t a) const { return m_space->read_qword(a); }
        u64 read_qword(offs_t a, u64 mask) const { return m_space->read_qword(a, mask); }
        u64 read_qword_unaligned(offs_t a) const { return m_space->read_qword_unaligned(a); }

        void write_byte(offs_t a, u8 v) { m_space->write_byte(a, v); }
        void write_byte(offs_t a, u8 v, u8 mask) { m_space->write_byte(a, v, mask); }
        void write_word(offs_t a, u16 v) { m_space->write_word(a, v); }
        void write_word(offs_t a, u16 v, u16 mask) { m_space->write_word(a, v, mask); }
        void write_word_unaligned(offs_t a, u16 v) { m_space->write_word_unaligned(a, v); }
        void write_dword(offs_t a, u32 v) { m_space->write_dword(a, v); }
        void write_dword(offs_t a, u32 v, u32 mask) { m_space->write_dword(a, v, mask); }
        void write_dword_unaligned(offs_t a, u32 v) { m_space->write_dword_unaligned(a, v); }
        void write_qword(offs_t a, u64 v) { m_space->write_qword(a, v); }
        void write_qword(offs_t a, u64 v, u64 mask) { m_space->write_qword(a, v, mask); }
    };
    using specific = cache;
};

/* ---------------------------------------------------------------------- */
/* Devices                                                                */
/* ---------------------------------------------------------------------- */

class device_memory_interface;
class device_interface;

/* The debugger's per-device object: there is no debugger here. */
class device_debug
{
public:
    void instruction_hook(offs_t) {}
    void exception_hook(int) {}
};

class device_t
{
public:
    device_t(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
        : mconfig_(mconfig), type_(type), tag_(tag ? tag : type.shortname), owner_(owner), clock_(clock)
    {
    }
    virtual ~device_t() = default;

    const char *tag() const { return tag_.c_str(); }
    const char *basetag() const { return tag_.c_str(); }
    const char *name() const { return type_.fullname; }
    const char *shortname() const { return type_.shortname; }
    device_type type() const { return type_; }
    device_t *owner() const { return owner_; }
    u32 clock() const { return clock_; }
    u32 unscaled_clock() const { return clock_; }
    running_machine &machine() const { return machine_; }
    const machine_config &mconfig() const { return mconfig_; }
    device_debug *debug() const { return &debug_; }

    template <typename... Args> void logerror(const char *fmt, Args &&...args) const
    {
        fprintf(stderr, "[%s] %s", tag(), util::string_format(fmt, std::forward<Args>(args)...).c_str());
    }
    template <typename... Args> void popmessage(const char *fmt, Args &&...args) const
    {
        logerror(fmt, std::forward<Args>(args)...);
    }

    /* Save states: nothing to register, a Core's context does this. */
    template <typename... Args> void save_item(Args &&...) {}
    template <typename... Args> void save_pointer(Args &&...) {}

    /* Interfaces: a cross-cast, since cpu_device derives from each of them. */
    template <typename T> bool interface(T *&intf) const
    {
        intf = dynamic_cast<T *>(const_cast<device_t *>(this));
        return intf != nullptr;
    }
    device_memory_interface &memory() const { return *memory_; }
    void shim_add_interface(device_interface *intf) { interfaces_.push_back(intf); }
    void shim_set_memory_interface(device_memory_interface *m) { memory_ = m; }

    /* Lifecycle the lifted cores override. The host drives them through
     * shim_start() / shim_reset(). */
    virtual void device_start() = 0;
    virtual void device_reset() {}
    virtual void device_stop() {}
    virtual void device_pre_save() {}
    virtual void device_post_load() {}

    void shim_start();
    void shim_reset();
    bool started() const { return started_; }

private:
    const machine_config &mconfig_;
    device_type_impl type_;
    std::string tag_;
    device_t *owner_;
    u32 clock_;
    mutable running_machine machine_;
    mutable device_debug debug_;
    device_memory_interface *memory_ = nullptr;
    std::vector<device_interface *> interfaces_;
    bool started_ = false;
};

/* device_interface: a mix-in with start and reset hooks. */
class device_interface
{
public:
    device_interface(device_t &device, const char *type) : device_(device) { device.shim_add_interface(this); }
    virtual ~device_interface() = default;

    device_t &device() { return device_; }
    const device_t &device() const { return device_; }

    virtual void interface_validity_check(validity_checker &) const {}
    virtual void interface_pre_start() {}
    virtual void interface_post_start() {}
    virtual void interface_pre_reset() {}
    virtual void interface_post_reset() {}

private:
    device_t &device_;
};

inline void device_t::shim_start()
{
    for(auto *i : interfaces_)
        i->interface_pre_start();
    device_start();
    for(auto *i : interfaces_)
        i->interface_post_start();
    started_ = true;
}

inline void device_t::shim_reset()
{
    for(auto *i : interfaces_)
        i->interface_pre_reset();
    device_reset();
    for(auto *i : interfaces_)
        i->interface_post_reset();
}

/* device_state_interface: register entries so the debugger can show them.
 * We keep only the index for state_import/export dispatch. */
class device_state_entry
{
public:
    explicit device_state_entry(int index) : index_(index) {}
    int index() const { return index_; }

private:
    int index_;
};

class device_state_entry_builder
{
public:
    template <typename... Args> device_state_entry_builder &mask(Args &&...) { return *this; }
    template <typename... Args> device_state_entry_builder &formatstr(Args &&...) { return *this; }
    device_state_entry_builder &callimport() { return *this; }
    device_state_entry_builder &callexport() { return *this; }
    device_state_entry_builder &noshow() { return *this; }
    device_state_entry_builder &readonly() { return *this; }
    device_state_entry_builder &signed_() { return *this; }
};

class device_state_interface
{
public:
    virtual ~device_state_interface() = default;

    template <typename... Args> device_state_entry_builder &state_add(Args &&...) { return builder_; }

protected:
    virtual void state_import(const device_state_entry &) {}
    virtual void state_export(const device_state_entry &) {}
    virtual void state_string_export(const device_state_entry &, std::string &) const {}

private:
    device_state_entry_builder builder_;
};

/* device_memory_interface: the cores declare their spaces through
 * memory_space_config(); the host binds every declared space to one
 * GuestMemory with shim_bind_memory(). AS_OPCODES is never separate. */
class device_memory_interface
{
public:
    using space_config_vector = std::vector<std::pair<int, const address_space_config *>>;

    enum { TR_READ = 0, TR_WRITE = 1, TR_FETCH = 2 };

    virtual ~device_memory_interface() = default;

    virtual space_config_vector memory_space_config() const = 0;
    virtual bool memory_translate(int spacenum, int intention, offs_t &address, address_space *&target_space)
    {
        (void)spacenum; (void)intention; (void)address;
        target_space = nullptr;
        return true;
    }

    const address_space_config *space_config(int spacenum = 0) const
    {
        for(auto &p : memory_space_config())
            if(p.first == spacenum)
                return p.second;
        return nullptr;
    }
    int max_space_count() const { return (int)spaces_.size(); }
    bool has_space(int index) const { return index != AS_OPCODES && index < (int)spaces_.size() && bound_[index]; }
    bool has_configured_map(int) const { return false; }
    address_space &space(int index = AS_PROGRAM) const
    {
        if(index < 0 || index >= (int)spaces_.size() || !bound_[index])
            throw emu_fatalerror("shim: address space %d is not bound", index);
        return const_cast<address_space &>(spaces_[index]);
    }

    /* Bind the configured spaces: all onto mem, except the id the caller
     * names as the CPU (interrupt-acknowledge) space, if any (-1: none). */
    void shim_bind_memory(cpu::GuestMemory &mem, int cpu_space_id = -1)
    {
        int n = 0;
        for(auto &p : memory_space_config())
            n = std::max(n, p.first + 1);
        n = std::max(n, cpu_space_id + 1);
        spaces_.assign(n, address_space());
        bound_.assign(n, false);
        for(auto &p : memory_space_config())
        {
            spaces_[p.first] = address_space(&mem, p.first == cpu_space_id ? address_space::Kind::Autovectors
                                                                           : address_space::Kind::Memory);
            bound_[p.first] = true;
        }
        if(cpu_space_id >= 0 && !bound_[cpu_space_id])
        {
            spaces_[cpu_space_id] = address_space(&mem, address_space::Kind::Autovectors);
            bound_[cpu_space_id] = true;
        }
    }

private:
    std::vector<address_space> spaces_;
    std::vector<bool> bound_;
};

/* Timers (schedule.h): kept in the device's own cycle count. The core
 * adapter runs them between slices (shim_run_timers) and bounds a slice
 * so a deadline is not overshot (shim_cycles_to_next_timer). */
class device_execute_interface;

#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)

class emu_timer
{
public:
    emu_timer(device_execute_interface &exec, std::function<void(s32)> cb) : exec_(exec), cb_(std::move(cb)) {}

    void adjust(attotime start_delay, s32 param = 0, const attotime &periodicity = attotime::never) noexcept;
    bool enable(bool enable = true) noexcept
    {
        bool old = enabled_;
        enabled_ = enable;
        return old;
    }
    bool enabled() const noexcept { return enabled_; }
    s32 param() const noexcept { return param_; }
    void set_param(s32 p) noexcept { param_ = p; }
    void reset(const attotime &duration = attotime::never) noexcept { adjust(duration, param_, period_); }
    attotime elapsed() const noexcept;
    attotime remaining() const noexcept;

    bool shim_due(u64 now) const { return enabled_ && !never_ && deadline_ <= now; }
    u64 shim_deadline() const { return deadline_; }
    bool shim_armed() const { return enabled_ && !never_; }
    void shim_fire();

private:
    device_execute_interface &exec_;
    std::function<void(s32)> cb_;
    bool enabled_ = false;
    bool never_ = true;
    s32 param_ = 0;
    u64 start_ = 0;
    u64 deadline_ = 0;
    attotime period_ = attotime::never;
};

class device_execute_interface
{
public:
    device_execute_interface(device_t &device) : device_(device) {}
    virtual ~device_execute_interface() = default;

    virtual u32 execute_min_cycles() const noexcept { return 1; }
    virtual u32 execute_max_cycles() const noexcept { return 1; }
    virtual bool execute_input_edge_triggered(int) const noexcept { return false; }
    virtual void execute_run() = 0;
    virtual void execute_set_input(int, int) {}

    void set_input_line(int line, int state) { execute_set_input(line, state); }
    void set_icountptr(int &icount) { icount_ = &icount; }
    int *shim_icount() const { return icount_; }
    void standard_irq_callback(int, offs_t) {}
    bool debugger_enabled() const { return false; }

    /* Cycles executed so far: the slices completed plus the part of the
     * running slice already spent. */
    u64 total_cycles() const noexcept
    {
        return cycles_base_ + (icount_ ? (u64)(slice_start_ - *icount_) : 0);
    }
    attotime cycles_to_attotime(u64 cycles) const { return attotime::from_ticks(cycles, device_.clock()); }
    u64 attotime_to_cycles(const attotime &t) const { return t.as_ticks(device_.clock()); }
    attotime local_time() const { return cycles_to_attotime(total_cycles()); }

    /* Debugger hooks: there is no MAME debugger here. */
    void debugger_instruction_hook(offs_t) {}
    void debugger_exception_hook(int) {}
    void debugger_privilege_hook() {}
    void debugger_wait_hook() {}

    template <typename C>
    emu_timer *timer_alloc(void (C::*mfp)(s32), const char *, C *obj)
    {
        timers_.push_back(std::make_unique<emu_timer>(*this, [mfp, obj](s32 p) { (obj->*mfp)(p); }));
        return timers_.back().get();
    }

    /* The adapter's slice protocol. Slices nest (a host op running guest
     * code): the outer slice's remaining count is pushed, its cycles so
     * far are banked, and the inner slice starts fresh; at the end the
     * outer count comes back. */
    void shim_begin_slice(int cycles)
    {
        if(icount_)
        {
            int spent = slice_start_ - *icount_;
            if(spent > 0)
                cycles_base_ += spent;
            outer_.push_back(*icount_ > 0 ? *icount_ : 0);
            *icount_ = cycles;
        }
        slice_start_ = cycles;
    }
    void shim_end_slice()
    {
        if(icount_)
        {
            int spent = slice_start_ - *icount_;
            if(spent > 0)
                cycles_base_ += spent;
            int outer = 0;
            if(!outer_.empty())
            {
                outer = outer_.back();
                outer_.pop_back();
            }
            *icount_ = outer;
            slice_start_ = outer;
        }
        else
            slice_start_ = 0;
    }
    /* Cycles until the earliest armed timer, or limit if none sooner. */
    u64 shim_cycles_to_next_timer(u64 limit) const
    {
        u64 now = total_cycles();
        for(auto &t : timers_)
            if(t->shim_armed())
                limit = std::min(limit, t->shim_deadline() > now ? t->shim_deadline() - now : 0);
        return limit;
    }
    void shim_run_timers()
    {
        u64 now = total_cycles();
        for(auto &t : timers_)
            if(t->shim_due(now))
                t->shim_fire();
    }

private:
    device_t &device_;
    int *icount_ = nullptr;
    int slice_start_ = 0;
    u64 cycles_base_ = 0;
    std::vector<int> outer_;
    std::vector<std::unique_ptr<emu_timer>> timers_;
};

inline void emu_timer::adjust(attotime start_delay, s32 param, const attotime &periodicity) noexcept
{
    param_ = param;
    period_ = periodicity;
    enabled_ = true;
    start_ = exec_.total_cycles();
    if(start_delay.is_never())
    {
        never_ = true;
        return;
    }
    never_ = false;
    deadline_ = start_ + exec_.attotime_to_cycles(start_delay);
}

inline attotime emu_timer::elapsed() const noexcept
{
    return exec_.cycles_to_attotime(exec_.total_cycles() - start_);
}

inline attotime emu_timer::remaining() const noexcept
{
    if(never_ || !enabled_)
        return attotime::never;
    u64 now = exec_.total_cycles();
    return exec_.cycles_to_attotime(deadline_ > now ? deadline_ - now : 0);
}

inline void emu_timer::shim_fire()
{
    if(period_.is_never())
        never_ = true;
    else
        deadline_ += exec_.attotime_to_cycles(period_);
    cb_(param_);
}

class device_disasm_interface
{
public:
    virtual ~device_disasm_interface() = default;
    virtual std::unique_ptr<util::disasm_interface> create_disassembler() = 0;
};

class cpu_device : public device_t,
                   public device_execute_interface,
                   public device_memory_interface,
                   public device_state_interface,
                   public device_disasm_interface
{
public:
    cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
        : device_t(mconfig, type, tag, owner, clock), device_execute_interface(static_cast<device_t &>(*this))
    {
        shim_set_memory_interface(this);
    }

    /* devcpu.h: a memory access that must be retried (bus retry). */
    bool access_to_be_redone() noexcept { return std::exchange(m_access_to_be_redone, false); }
    bool access_to_be_redone_noclear() noexcept { return m_access_to_be_redone; }
    bool *access_to_be_redone_ptr() noexcept { return &m_access_to_be_redone; }

protected:
    bool m_access_to_be_redone = false;
};
