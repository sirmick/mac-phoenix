/*
 *  thunks.h - Thunks for sharing data/code with MacOS (PPC)
 *
 *  Adapted from SheepShaver for mac-phoenix PPC backend.
 *  Original: SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Licensed under GPL v2+
 */

#ifndef THUNKS_H
#define THUNKS_H

#include "kpx_cpu_emulation.h"

/*
 *  Native function selectors
 */

enum {
    NATIVE_PATCH_NAME_REGISTRY,
    NATIVE_VIDEO_INSTALL_ACCEL,
    NATIVE_VIDEO_VBL,
    NATIVE_VIDEO_DO_DRIVER_IO,
    NATIVE_ETHER_AO_GET_HWADDR,
    NATIVE_ETHER_AO_ADD_MULTI,
    NATIVE_ETHER_AO_DEL_MULTI,
    NATIVE_ETHER_AO_SEND_PACKET,
    NATIVE_ETHER_IRQ,
    NATIVE_ETHER_INIT,
    NATIVE_ETHER_TERM,
    NATIVE_ETHER_OPEN,
    NATIVE_ETHER_CLOSE,
    NATIVE_ETHER_WPUT,
    NATIVE_ETHER_RSRV,
    NATIVE_SERIAL_NOTHING,
    NATIVE_SERIAL_OPEN,
    NATIVE_SERIAL_PRIME_IN,
    NATIVE_SERIAL_PRIME_OUT,
    NATIVE_SERIAL_CONTROL,
    NATIVE_SERIAL_STATUS,
    NATIVE_SERIAL_CLOSE,
    NATIVE_GET_RESOURCE,
    NATIVE_GET_1_RESOURCE,
    NATIVE_GET_IND_RESOURCE,
    NATIVE_GET_1_IND_RESOURCE,
    NATIVE_R_GET_RESOURCE,
    NATIVE_MAKE_EXECUTABLE,
    NATIVE_CHECK_LOAD_INVOC,
    NATIVE_NQD_SYNC_HOOK,
    NATIVE_NQD_BITBLT_HOOK,
    NATIVE_NQD_FILLRECT_HOOK,
    NATIVE_NQD_UNKNOWN_HOOK,
    NATIVE_NQD_BITBLT,
    NATIVE_NQD_INVRECT,
    NATIVE_NQD_FILLRECT,
    NATIVE_NAMED_CHECK_LOAD_INVOC,
    NATIVE_GET_NAMED_RESOURCE,
    NATIVE_GET_1_NAMED_RESOURCE,
    NATIVE_OP_MAX
};

// Thunks system (stubbed for now - initialized during PPC boot)
extern bool ThunksInit(void);
extern void ThunksExit(void);

#if EMULATED_PPC
extern uint32 NativeOpcode(int selector);
#endif

extern uint32 NativeTVECT(int selector);
extern uint32 NativeFunction(int selector);
extern uint32 NativeRoutineDescriptor(int selector);

/*
 *  SheepMem - shared 32-bit addressable memory
 */

class SheepMem {
    static uint32 align(uint32 size);
protected:
    static uint32  page_size;
    static uintptr zero_page;
    static uintptr base;
    static uintptr data;
    static uintptr proc;
    static const uint32 size = 0x80000; // 512 KB
public:
    static bool Init(void);
    static void Exit(void);
    static uint32 PageSize();
    static uint32 ZeroPage();
    static uint32 Reserve(uint32 size);
    static void Release(uint32 size);
    static uint32 ReserveProc(uint32 size);
    friend class SheepVar;
};

inline uint32 SheepMem::align(uint32 size)
{
    return (size + 3) & -4;
}

inline uint32 SheepMem::PageSize()
{
    return page_size;
}

inline uint32 SheepMem::ZeroPage()
{
    return zero_page;
}

inline uint32 SheepMem::Reserve(uint32 size)
{
    data -= align(size);
    assert(data >= proc);
    return data;
}

inline void SheepMem::Release(uint32 size)
{
    data += align(size);
}

inline uint32 SheepMem::ReserveProc(uint32 size)
{
    uint32 mproc = proc;
    proc += align(size);
    assert(proc < data);
    return mproc;
}

static inline uint32 SheepProc(const uint8 *proc, uint32 proc_size)
{
    uint32 mac_proc = SheepMem::ReserveProc(proc_size);
    Host2Mac_memcpy(mac_proc, proc, proc_size);
    return mac_proc;
}

#define BUILD_SHEEPSHAVER_PROCEDURE(PROC) \
    static uint32 PROC = 0; \
    if (PROC == 0) \
        PROC = SheepProc(PROC##_template, sizeof(PROC##_template))

class SheepVar
{
    uint32 m_base;
    uint32 m_size;
public:
    SheepVar(uint32 requested_size);
    ~SheepVar() { SheepMem::Release(m_size); }
    uint32 addr() const { return m_base; }
};

inline SheepVar::SheepVar(uint32 requested_size)
{
    m_size = SheepMem::align(requested_size);
    m_base = SheepMem::Reserve(m_size);
}

template< int requested_size >
struct SheepArray : public SheepVar
{
    SheepArray() : SheepVar(requested_size) { }
};

struct SheepVar32 : public SheepVar
{
    SheepVar32() : SheepVar(4) { }
    SheepVar32(uint32 value) : SheepVar(4) { set_value(value); }
    uint32 value() const { return ReadMacInt32(addr()); }
    void set_value(uint32 v) { WriteMacInt32(addr(), v); }
};

struct SheepString : public SheepVar
{
    SheepString(const char *str) : SheepVar(strlen(str) + 1)
        { if (str) strcpy(value(), str); else WriteMacInt8(addr(), 0); }
    char *value() const
        { return (char *)Mac2HostAddr(addr()); }
};

// SheepRoutineDescriptor — creates Mac Mixed Mode routine descriptor in SheepMem
// Required for Resource Manager thunks to work
#include <cstddef>       // for offsetof

// Forward types (full structs in macos_util.h)
#ifndef _ROUTINE_DESCRIPTOR_DEFINED
#define _ROUTINE_DESCRIPTOR_DEFINED
typedef uint32 ProcInfoType;
struct RoutineRecord {
    ProcInfoType procInfo; int8 reserved1; int8 ISA;
    uint16 routineFlags; uint32 procDescriptor;
    uint32 reserved2; uint32 selector;
};
struct RoutineDescriptor {
    uint16 goMixedModeTrap; int8 version; uint8 routineDescriptorFlags;
    uint32 reserved1; uint8 reserved2; uint8 selectorInfo;
    int16 routineCount; RoutineRecord routineRecords[1];
};
#endif

struct SheepRoutineDescriptor : public SheepVar
{
    SheepRoutineDescriptor(ProcInfoType procInfo, uint32 procedure)
        : SheepVar(sizeof(RoutineDescriptor))
    {
        const uintptr desc = addr();
        Mac_memset(desc, 0, sizeof(RoutineDescriptor));
        WriteMacInt16(desc + offsetof(RoutineDescriptor, goMixedModeTrap), 0xAAFE);
        WriteMacInt8 (desc + offsetof(RoutineDescriptor, version), 7);
        WriteMacInt32(desc + offsetof(RoutineDescriptor, routineRecords) + offsetof(RoutineRecord, procInfo), procInfo);
        WriteMacInt8 (desc + offsetof(RoutineDescriptor, routineRecords) + offsetof(RoutineRecord, ISA), 1);
        WriteMacInt16(desc + offsetof(RoutineDescriptor, routineRecords) + offsetof(RoutineRecord, routineFlags), 0 | 0 | 4);
        WriteMacInt32(desc + offsetof(RoutineDescriptor, routineRecords) + offsetof(RoutineRecord, procDescriptor), procedure);
    }
};

#endif /* THUNKS_H */
