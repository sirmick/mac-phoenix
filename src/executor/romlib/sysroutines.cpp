/* MacPhoenix: the System's low-memory routines $7B0 (selector-table
 * dispatch) and $668 (detached-package call).
 *
 * Written from docs/executor/cleanroom/system-routines.md (the clean-room
 * spec), as Executor callbacks into C++ rather than 68k code: guest code
 * reaches them with `move.l $7B0,-(sp); rts` (or $668), which lands on a
 * callback stub with the stack exactly as its caller left it. A callback
 * returns the address the 68k carries on at, so neither routine nests on
 * the host stack: the package routine runs as ordinary 68k code and comes
 * back through a third callback, the detached call's epilogue. That keeps
 * re-entrant calls (a package calling itself through $668) free.
 *
 * Both serve Pascal routines selected by D0: the low byte is the selector
 * (signed), the high byte the size of the parameters in words. On entry
 * the stack holds the return address into the original caller, then the
 * parameters, then the word above them (a function's result).
 */
#include <base/common.h>
#include <base/trapglue.h>
#include <rsys/sysroutines.h>
#include <MemoryMgr.h>
#include <Package.h>
#include <ResourceMgr.h>

#include <cstring>

using namespace Executor;

namespace
{
syn68k_addr_t dispatch_stub, detached_stub, epilogue_stub;

inline int8_t selector_of(uint32_t d0)
{
    return (int8_t)(d0 & 0xFF);
}

inline uint32_t param_bytes_of(uint32_t d0)
{
    return ((d0 >> 8) & 0xFF) * 2;
}

/* Fail a call: drop the return address and the parameters, leave `err`
   as the word result and go back to the caller. */
syn68k_addr_t fail(OSErr err)
{
    syn68k_addr_t ret = POPADDR();
    EM_A7 += param_bytes_of(EM_D0);
    WRITEUW(EM_A7, (uint16_t)err);
    return ret;
}

/* $7B0: A0 a selector table (kind.w, lowest.b, highest.b, entries). */
syn68k_addr_t selector_table_dispatch(syn68k_addr_t, void *)
{
    syn68k_addr_t table = EM_A0;
    int kind = READUW(table);
    int lowest = (int8_t)READUB(table + 2);
    int highest = (int8_t)READUB(table + 3);
    int selector = selector_of(EM_D0);
    if(selector < lowest || selector > highest)
        return fail(paramErr);

    int n = selector - lowest;
    syn68k_addr_t entries = table + 4;
    syn68k_addr_t routine;
    switch(kind)
    {
        case 0: /* a word offset from the table per selector */
            routine = table + (int16_t)READUW(entries + 2 * n);
            break;
        case 1: /* word offsets, the selector values being byte offsets */
            routine = table + (int16_t)READUW(entries + n);
            break;
        case 2: /* absolute addresses */
            routine = READUL(entries + 4 * n);
            break;
        default:
            /* Not a kind the System makes; refuse rather than jump into
               the weeds. */
            return fail(paramErr);
    }
    EM_A0 = routine;
    return routine; /* the return address is still on top */
}

/* The frame the detached call keeps below the caller's stack, from the
   package routine's SP upward:

     +0              the epilogue's address (the routine's return address)
     +4              the parameters, moved down
     +4+P            the word that was above them (a function's result)
     +6+P            A0: the package's handle
     +10+P           A1: the lock count's address
     +14+P           the original return address

   It ends where the original result word ended, so the caller's stack
   above is untouched, and it starts 12 bytes below the original SP. */
enum
{
    frame_extra = 12,
};

/* $668: A0 the package's handle, A1 its lock count, D0 the selector. */
syn68k_addr_t call_detached_package(syn68k_addr_t, void *)
{
    uint32_t d0 = EM_D0;
    syn68k_addr_t handle_addr = EM_A0;
    syn68k_addr_t count_addr = EM_A1;

    if(!handle_addr)
        return fail(memFullErr);

    Handle h = (Handle)SYN68K_TO_US(handle_addr);
    if(!*h)
    {
        /* Reload it as LoadResource does for the System's own calls: the
           resource may live in ROM's map, and isn't to stay loaded beyond
           need. (Executor's Resource Manager doesn't read these; we clear
           them after the call as the Resource Manager does.) */
        LM(RomMapInsert) = 0xFF;
        LM(TmpResLoad) = 0xFF;
        LoadResource(h);
        LM(RomMapInsert) = 0;
        LM(TmpResLoad) = 0;
        EM_D0 = d0;
        if(!*h)
            return fail(LM(ResErr));
    }

    GUEST<int16_t> *count = (GUEST<int16_t> *)SYN68K_TO_US(count_addr);
    if(*count == 0)
        HLock(h);
    *count = *count + 1;

    /* Re-stack: the parameters and the word above them move down 12
       bytes, under the epilogue's address and over the saved state. */
    uint32_t p = param_bytes_of(d0);
    syn68k_addr_t sp = EM_A7;
    syn68k_addr_t ret = READUL(sp);
    syn68k_addr_t new_sp = sp - frame_extra;
    memmove(SYN68K_TO_US(new_sp + 4), SYN68K_TO_US(sp + 4), p + 2);
    WRITEUL(new_sp, epilogue_stub);
    WRITEUL(new_sp + 6 + p, handle_addr);
    WRITEUL(new_sp + 10 + p, count_addr);
    WRITEUL(new_sp + 14 + p, ret);
    EM_A7 = new_sp;

    /* Into the package's selector table, after the package header. */
    EM_D0 = d0;
    EM_A0 = ptr_to_longint(*h) + 10;
    EM_A1 = count_addr;
    return US_TO_SYN68K(LM(SelectorTableDispatch));
}

/* The routine has returned, its parameters popped: SP is at the result
   word, the saved state above it. */
syn68k_addr_t detached_epilogue(syn68k_addr_t, void *)
{
    syn68k_addr_t sp = EM_A7;
    uint16_t result = READUW(sp);
    syn68k_addr_t handle_addr = READUL(sp + 2);
    syn68k_addr_t count_addr = READUL(sp + 6);
    syn68k_addr_t ret = READUL(sp + 10);

    GUEST<int16_t> *count = (GUEST<int16_t> *)SYN68K_TO_US(count_addr);
    *count = *count - 1;
    if(*count == 0)
        HUnlock((Handle)SYN68K_TO_US(handle_addr));

    /* Where a direct call would leave SP: on the original result slot. */
    EM_A7 = sp + frame_extra;
    WRITEUW(EM_A7, result);
    return ret;
}
}

void Executor::ROMlib_install_system_routines()
{
    if(!dispatch_stub)
    {
        dispatch_stub = callback_install(selector_table_dispatch, nullptr);
        detached_stub = callback_install(call_detached_package, nullptr);
        epilogue_stub = callback_install(detached_epilogue, nullptr);
    }
    LM(SelectorTableDispatch) = (ProcPtr)SYN68K_TO_US(dispatch_stub);
    LM(CallDetachedPackage) = (ProcPtr)SYN68K_TO_US(detached_stub);
}
