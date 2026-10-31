/* Copyright 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <FileMgr.h>
#include <MemoryMgr.h>

#include <MixedMode.h>
#include <OSUtil.h>
#include <rsys/cfm.h>
#include <base/builtinlibs.h>

#include <file/file.h>
#include <base/cpu.h>
#include <base/traps.h>
#include <PowerCore.h>
#include <cassert>
#include <algorithm>
#include <unordered_map>

using namespace Executor;

UniversalProcPtr Executor::C_NewRoutineDescriptor(
    ProcPtr proc, ProcInfoType info, ISAType isa)
{
    UniversalProcPtr retval;

    if(!proc)
        retval = nullptr;
    else
    {
        RoutineDescriptor *p;

        p = (RoutineDescriptor *)NewPtr(sizeof *p);
        p->goMixedModeTrap = MIXED_MODE_TRAP;
        ROMlib_destroy_blocks((syn68k_addr_t)
                                  US_TO_SYN68K(&p->goMixedModeTrap),
                              2, true);
        p->version = kRoutineDescriptorVersion;
        p->routineDescriptorFlags = 0;
        p->reserved1 = 0;
        p->reserved2 = 0;
        p->selectorInfo = 0;
        p->routineCount = 0;
        p->routineRecords[0].procInfo = info;
        p->routineRecords[0].reserved1 = 0;
        p->routineRecords[0].ISA = isa;
        p->routineRecords[0].routineFlags = kSelectorsAreNotIndexable;
        p->routineRecords[0].procDescriptor = proc;
        p->routineRecords[0].reserved2 = 0;
        p->routineRecords[0].selector = 0;
        retval = (UniversalProcPtr)p;
    }
    return retval;
}

void Executor::C_DisposeRoutineDescriptor(UniversalProcPtr ptr)
{
    DisposePtr((Ptr)ptr);
    warning_trace_info("");
}

/* MacPhoenix: two records, the 68k routine's and the PowerPC one's (Mixed
   Mode calls the PowerPC one; mode_switch). A system that says it runs on
   PowerPC gets asked for them (8.x's libraries). */
UniversalProcPtr Executor::C_NewFatRoutineDescriptor(
    ProcPtr m68k, ProcPtr ppc, ProcInfoType info)
{
    UniversalProcPtr single = NewRoutineDescriptor(m68k, info, kM68kISA);
    if(!single)
        return nullptr;
    RoutineDescriptor *p = (RoutineDescriptor *)NewPtr(sizeof(RoutineDescriptor) + sizeof(RoutineRecord));
    if(!p)
    {
        DisposePtr((Ptr)single);
        return nullptr;
    }
    BlockMoveData((Ptr)single, (Ptr)p, sizeof(RoutineDescriptor));
    DisposePtr((Ptr)single);
    ROMlib_destroy_blocks((syn68k_addr_t)US_TO_SYN68K(&p->goMixedModeTrap), 2, true);
    p->routineCount = 1;
    RoutineRecord *records = p->routineRecords;
    RoutineRecord &r = records[1];
    r = records[0];
    r.ISA = kPowerPCISA;
    r.procDescriptor = ppc;
    return (UniversalProcPtr)p;
}

OSErr Executor::C_SaveMixedModeState(void *statep, uint32_t vers)
{
    warning_unimplemented("");
    return paramErr;
}

OSErr Executor::C_RestoreMixedModeState(void *statep, uint32_t vers)
{
    warning_unimplemented("");
    return paramErr;
}

// Local helpers for ModeSwitch
namespace
{
    struct PowerPCFrame
    {
        GUEST<uint32_t> backChain;
        GUEST<uint32_t> saveCR;
        GUEST<uint32_t> saveLR;
        GUEST<uint32_t> reserved1;
        GUEST<uint32_t> reserved2;
        GUEST<uint32_t> saveRTOC;
        GUEST<uint32_t> parameters[8];
    };

    // --- utility functions for loading and storing arguments

    uint32_t sizeOnStack(int argsize) { return argsize == 3 ? 4 : 2; };
    uint32_t readsized(void *p, int argsize)
    {
        switch(argsize)
        {
            case 1:
                return *((uint8_t*)p);
            case 2:
                return *(GUEST<uint16_t>*)p;
            case 3:
                return *(GUEST<uint32_t>*)p;
            default:
                std::abort();
        }
    }
    void writesized(void *p, int argsize, uint32 val)
    {
        switch(argsize)
        {
            case 1:
                *((uint8_t*)p) = val;
                break;
            case 2:
                *(GUEST<uint16_t>*)p = val;
                break;
            case 3:
                *(GUEST<uint32_t>*)p = val;
                break;
        }
    }

    void pusharg(int argsize, uint32 val)
    {
        EM_A7 -= sizeOnStack(argsize);
        writesized(ptr_from_longint<uint8_t*>(EM_A7), argsize, val);
    }

    static constexpr int regmap[] = {
            0, /* kRegisterD0,  0 */
            1, /* kRegisterD1,  1 */
            2, /* kRegisterD2,  2 */
            3, /* kRegisterD3,  3 */
            8, /* kRegisterA0,  4 */
            9, /* kRegisterA1,  5 */
            10, /* kRegisterA2,  6 */
            11, /* kRegisterA3,  7 */
            4, /* kRegisterD4,  8 */
            5, /* kRegisterD5,  9 */
            6, /* kRegisterD6, 10 */
            7, /* kREgisterD7, 11 */
            12, /* kRegisterA4, 12 */
            13, /* kRegisterA5, 13 */
            14, /* kRegisterA6, 14 */
        };
    static constexpr uint32_t regmask[] = {0U, 0xFFU, 0xFFFFU, 0xFFFFFFFFU};
}

/* MacPhoenix: a CFM-68K routine (ISA kM68kISA | kCFM68kRTA; docs/executor/
   CFM68K.md): the procedure is a transition vector, and the routine takes
   its arguments as CFM-68K code passes them, whatever the convention its
   procInfo names: pushed right to left, 4 bytes each, the result in D0
   (a Pascal routine pops its arguments; the stack is reset either way). */
static constexpr ISAType kCFM68kISA = kM68kISA | 0x10;

static uint32_t call_cfm68k_routine(ProcPtr tvector, int nParameters,
                                    const uint32_t paramValues[])
{
    uint32_t save_a7 = EM_A7;
    for(int i = nParameters - 1; i >= 0; i--)
    {
        EM_A7 -= 4;
        *ptr_from_longint<GUEST<uint32_t>*>(EM_A7) = paramValues[i];
    }
    uint32_t tv = guest_cast<uint32_t>(tvector);
    uint32_t retval = ROMlib_call_cfm68k(tv);
    EM_A7 = save_a7;
    return retval;
}

/* An accelerated resource's routine record as it was before preparing,
   and the descriptor's address then, by the main transition vector. */
struct PreparedRecord
{
    uint32_t descriptor;
    uint32_t procDescriptor;
    uint16_t routineFlags;
};
static std::unordered_map<uint32_t, PreparedRecord> prepared_at;

static uint32_t mode_switch(UniversalProcPtr theProcPtr, ProcInfoType procinfo,
                            ISAType fromISA, uint32_t cfm68kArgs)
{
    // Step 1/5: Figure out what routine we are calling

    RoutineRecord m68kroutine = {};
    const RoutineRecord* routine = nullptr;

    if(theProcPtr->goMixedModeTrap == MIXED_MODE_TRAP)
    {
        const RoutineRecord *beginRoutines = &theProcPtr->routineRecords[0],
                            *endRoutines = &theProcPtr->routineRecords[theProcPtr->routineCount + 1];
       
       routine = std::find_if(beginRoutines, endRoutines,
                    [](const RoutineRecord& rr) { return rr.ISA == kPowerPCISA; });
        if(routine == endRoutines)
            routine = beginRoutines;
    
        if(routine == endRoutines)
        {
            fprintf(stderr, "*** bad CallUniversalProc ***\n");
            std::abort();
        }
    }
    else
    {
        /* A plain procedure pointer is classic 68k code; from CFM-68K code
           (CallUniversalProc) too. */
        assert(fromISA != kM68kISA);
        m68kroutine.procDescriptor = (ProcPtr)theProcPtr;
        m68kroutine.procInfo = procinfo;
        m68kroutine.ISA = kM68kISA;
        routine = &m68kroutine;
    }

    /* MacPhoenix: an accelerated code resource ('MDEF', 'WDEF', ... with a
       routine descriptor before a PEF container; Mac OS 8's Finder's): its
       record says the fragment is to be prepared, at an offset from the
       descriptor. Prepared at the first call, as Apple's Mixed Mode does,
       and the record then names the main routine's transition vector.
       The prepared code runs in place, and a purgeable resource moves
       between calls (8.6's MDEF 63; the Menu Manager locks it only for the
       call): a record found at another address than it was prepared at
       gets its own values back and is prepared again there. */
    if(routine->ISA == kPowerPCISA)
    {
        RoutineRecord *rr = const_cast<RoutineRecord *>(routine);
        uint32_t descriptor = ptr_to_longint(theProcPtr);
        if(!(rr->routineFlags & kFragmentNeedsPreparing))
        {
            auto it = prepared_at.find(ptr_to_longint(rr->procDescriptor.get()));
            if(it != prepared_at.end() && it->second.descriptor != descriptor)
            {
                rr->procDescriptor = ptr_from_longint<ProcPtr>(it->second.procDescriptor);
                rr->routineFlags = it->second.routineFlags;
            }
        }
        if(rr->routineFlags & kFragmentNeedsPreparing)
        {
            PreparedRecord was = { descriptor, ptr_to_longint(rr->procDescriptor.get()), rr->routineFlags };
            uint32_t container = was.procDescriptor;
            if(rr->routineFlags & kProcDescriptorIsRelative)
                container += descriptor;
            GUEST<ConnectionID> conn;
            GUEST<Ptr> main;
            Str255 err;
            OSErr e = GetMemFragment(ptr_from_longint<void *>(container), 0, (StringPtr) "\0",
                                     kReferenceCFrag, &conn, &main, err);
            if(e != noErr || !main)
            {
                fprintf(stderr, "[Executor] Mixed Mode: preparing the fragment at $%08x failed (%d)\n",
                        container, e);
                return 0;
            }
            rr->procDescriptor = (ProcPtr)main.get();
            rr->routineFlags = rr->routineFlags & ~(kProcDescriptorIsRelative | kFragmentNeedsPreparing);
            if(was.routineFlags & kProcDescriptorIsRelative)
                prepared_at[ptr_to_longint(main.get())] = was;
        }
    }

    procinfo = routine->procInfo;
    ISAType toISA = routine->ISA;
    // Now, routine points to target routine, and procinfo is the corresponding procinfo.

    // Step 2/5: Parse the procinfo

    int convention = procinfo & 0xf;

    int nParameters = 0;
    constexpr int maxParameters = 13;
    int paramSizes[maxParameters];
    int paramRegs[maxParameters];
    int resultSize = (procinfo >> 4) & 0x3;
    int resultReg = -1;

    uint32_t stackArgsSize = 0;

    /* MacPhoenix: a dispatched routine (a trap dispatcher's selector in D0,
       D1 or on top of the stack): the selector is the routine's first
       parameter for native code, and the rest are Pascal (or C) stack
       parameters from bit 8 on. */
    enum { kNoSelector, kSelectorD0, kSelectorD1, kSelectorStack };
    int selectorWhere = kNoSelector;
    switch(convention)
    {
        case kD0DispatchedPascalStackBased:
        case kD0DispatchedCStackBased:
            selectorWhere = kSelectorD0;
            break;
        case kD1DispatchedPascalStackBased:
            selectorWhere = kSelectorD1;
            break;
        case kStackDispatchedPascalStackBased:
            selectorWhere = kSelectorStack;
            break;
    }
    if(selectorWhere != kNoSelector)
    {
        int selectorSize = (procinfo >> 6) & 0x3;
        paramSizes[nParameters++] = selectorSize ? selectorSize : 2;
        if(selectorWhere == kSelectorStack)
            stackArgsSize += sizeOnStack(paramSizes[0]);
        for(uint32_t field = 8; field <= 30; field += 2)
        {
            uint32_t argsize = (procinfo >> field) & 0x3;
            if(!argsize)
                break;
            paramSizes[nParameters++] = argsize;
            stackArgsSize += argsize == 3 ? 4 : 2;
        }
        convention = convention == kD0DispatchedCStackBased ? kCStackBased : kPascalStackBased;
    }
    int firstStackParam = selectorWhere == kNoSelector ? 0 : 1;

    switch(selectorWhere != kNoSelector ? -1 : convention)
    {
        case -1:
            break;
        case kRegisterBased:
            for(uint32_t field = 11; field <= 27; field += 5)
            {
                uint32_t argsize = (procinfo >> field) & 0x3;
                if(argsize)
                {
                    paramRegs[nParameters] = (procinfo >> (field + 2)) & 0x7;
                    paramSizes[nParameters++] = argsize;
                }
                else
                    break;
            }
            resultReg = (procinfo >> 6) & 0x1F;
            assert(resultReg < 16);
            break;
        case kPascalStackBased:
        case kCStackBased:
        case kThinkCStackBased: /* MacPhoenix: as C's (AppleWorks 5's MDEF from PowerPC GetMenu) */
            for(uint32_t field = 6; field <= 30; field += 2)
            {
                uint32_t argsize = (procinfo >> field) & 0x3;
                if(argsize)
                {
                    paramSizes[nParameters++] = argsize;
                    stackArgsSize += argsize == 3 ? 4 : 2;
                }
                else
                    break;
            }
            break;
        default:
            fprintf(stderr,"modeswitch: unimplemented/invalid convention %d (procinfo = %08x)\n", convention, procinfo);
            std::abort();
    }

    // Step 3/5: Collect the arguments into paramValues[]

    PowerCore& cpu = getPowerCore();
    uint32_t stackPointer = fromISA == kM68kISA || fromISA == kCFM68kISA ? EM_A7: cpu.r[1];

    uint32_t paramValues[maxParameters];
    uint32_t retval = 0;

    if(fromISA == kCFM68kISA)
    {
        /* CallUniversalProc's arguments after the procInfo, 4 bytes each,
           the value at the low end. */
        for(int i = 0; i < nParameters; i++)
            paramValues[i] = *ptr_from_longint<GUEST<uint32_t>*>(cfm68kArgs + 4 * i)
                & regmask[paramSizes[i]];
    }
    else if(fromISA == kM68kISA)
    {
        uint8_t *stackArgPtr = ptr_from_longint<uint8_t*>(EM_A7 + 4);

        switch(convention)
        {
            case kRegisterBased:
                for(int i = 0; i < nParameters; i++)
                {
                    paramValues[i] = cpu_state.regs[regmap[paramRegs[i]]].ul.n
                        & regmask[paramSizes[i]];
                }
                break;
            case kPascalStackBased:
                if(selectorWhere == kSelectorStack)
                {
                    paramValues[0] = readsized(stackArgPtr, paramSizes[0]);
                    stackArgPtr += sizeOnStack(paramSizes[0]);
                }
                for(int i = nParameters-1; i >= firstStackParam; i--)
                {
                    paramValues[i] = readsized(stackArgPtr, paramSizes[i]);
                    stackArgPtr += sizeOnStack(paramSizes[i]);
                }
                break;
            case kCStackBased:
            case kThinkCStackBased:
                for(int i = firstStackParam; i < nParameters; i++)
                {
                    paramValues[i] = readsized(stackArgPtr, paramSizes[i]);
                    stackArgPtr += sizeOnStack(paramSizes[i]);
                }
                break;
            default:
                fprintf(stderr,"modeswitch: unimplemented/invalid convention %d (procinfo = %08x)\n", convention, procinfo);
                std::exit(1);
        }
        if(selectorWhere == kSelectorD0 || selectorWhere == kSelectorD1)
            paramValues[0] = (selectorWhere == kSelectorD0 ? EM_D0 : EM_D1) & regmask[paramSizes[0]];
    }
    else if(fromISA == kPowerPCISA)
    {
        PowerPCFrame& parentFrame = *ptr_from_longint<PowerPCFrame*>(cpu.r[1]);

        for(int i = 0; i < nParameters; i++)
            paramValues[i] = (i+2) < 8 ? cpu.r[i+2+3] : parentFrame.parameters[i+2].get();
    }
    else
        std::abort();


    // Step 4/5: Invoke function

    if(toISA == kPowerPCISA)
    {
        /* MacPhoenix: through callPPC, which keeps the registers: 68k code
           can call a PowerPC routine while PowerPC code is in a system
           call (a Time Manager task during a blocking OTRcv), whose return
           (cpu.lr) the routine would otherwise overwrite (STRAGGLERS 64). */
        PPCProcDescriptor& proc = *(PPCProcDescriptor*) routine->procDescriptor;
        retval = callPPC(proc.code, proc.rtoc, paramValues, nParameters);
    }
    else if(toISA == kCFM68kISA)
    {
        EM_A7 = stackPointer;
        retval = call_cfm68k_routine(routine->procDescriptor, nParameters, paramValues)
            & regmask[resultSize];
    }
    else if(fromISA == kPowerPCISA
            && ptr_to_longint(routine->procDescriptor) == tooltraptable[_Unimplemented & 0x3FF])
    {
        /* MacPhoenix: PowerPC code calling a trap Executor lacks by its
           address (the System's libraries read the OS trap table in low
           memory: _HeapDispatch at $0690). _Unimplemented would stop the
           session without saying which; logged with what there is, and
           answered paramErr, as a dispatcher answers a selector it lacks. */
        char what[160];
        snprintf(what, sizeof what,
                 "trap through its address from PowerPC (procInfo $%08x, first argument $%x, caller $%08x): answered paramErr",
                 (unsigned)procinfo, nParameters ? (unsigned)paramValues[0] : 0u, (unsigned)cpu.lr);
        traps::log_unimplemented(what);
        retval = (uint32_t)(int32_t)paramErr & regmask[resultSize];
    }
    else
    {
        EM_A7 = stackPointer;

        switch(convention)
        {
            case kRegisterBased:
                for(int i = 0; i < nParameters; i++)
                {
                    cpu_state.regs[regmap[paramRegs[i]]].ul.n = paramValues[i];
                }
                break;

            case kPascalStackBased:
                if(resultSize)
                    pusharg(resultSize, 0);
                for(int i = firstStackParam; i < nParameters; i++)
                    pusharg(paramSizes[i], paramValues[i]);
                if(selectorWhere == kSelectorStack)
                    pusharg(paramSizes[0], paramValues[0]);
                break;

            case kCStackBased:
            case kThinkCStackBased:
                for(int i = nParameters-1; i >= firstStackParam; i--)
                    pusharg(paramSizes[i], paramValues[i]);
                break;
        }
        if(selectorWhere == kSelectorD0)
            EM_D0 = paramValues[0];
        else if(selectorWhere == kSelectorD1)
            EM_D1 = paramValues[0];

        /* MacPhoenix: what this call pushed, for a dispatcher that is
           the routine itself and gets a selector Executor lacks: it can
           answer the call as a Mac's does (traps.cpp,
           unknown_selector_68k). Only for that entry: its stack pointer. */
        traps::MixedModeCall saved_call = traps::mixed_mode_call;
        if(convention == kPascalStackBased)
            traps::mixed_mode_call = { EM_A7 - 4, stackArgsSize, resultSize ? (uint32_t)sizeOnStack(resultSize) : 0 };
        else
            traps::mixed_mode_call = {};
        execute68K(guest_cast<uint32_t>(routine->procDescriptor));
        traps::mixed_mode_call = saved_call;

        switch(convention)
        {
            case kRegisterBased:
                retval = cpu_state.regs[regmap[resultReg]].ul.n
                    & regmask[resultSize];
                break;

            case kPascalStackBased:
                /* MacPhoenix: the result was read and dropped. */
                if(resultSize)
                    retval = readsized(ptr_from_longint<uint8_t*>(EM_A7), resultSize);
                EM_A7 += resultSize ? sizeOnStack(resultSize) : 0;
                break;
            
            case kCStackBased:
            case kThinkCStackBased:
                retval = EM_D0 & regmask[resultSize];
                EM_A7 += stackArgsSize;
                break;
        }
    }

    // Step 5/5: deallocate stack frame,
    //           maybe pop arguments (pascal calls from 68K),
    //           and store return value in the right plae

    /* MacPhoenix: r1 only for a PowerPC caller; a 68k caller's stack
       pointer is not a suspended PowerPC caller's. */
    EM_A7 = stackPointer;
    if(fromISA == kPowerPCISA)
        cpu.r[1] = stackPointer;

    if(fromISA == kM68kISA)
    {
        uint32_t retaddr = POPADDR();

        switch(convention)
        {
            case kRegisterBased:
                if(resultSize)
                    cpu_state.regs[regmap[resultReg]].ul.n = retval;
                break;
            case kPascalStackBased:
                EM_A7 += stackArgsSize;
                writesized(ptr_from_longint<uint8_t*>(EM_A7), resultSize, retval);
                break;
            case kCStackBased:
            case kThinkCStackBased:
                EM_D0 = retval;
                break;
        }

        return retaddr;
    }
    else
    {
        return retval;
    }
}

extern uint32_t Executor::ModeSwitch(UniversalProcPtr theProcPtr, ProcInfoType procinfo, ISAType fromISA)
{
    return mode_switch(theProcPtr, procinfo, fromISA, 0);
}

uint32_t Executor::ROMlib_cfm68k_call_universal_proc(UniversalProcPtr theProcPtr,
                                                     ProcInfoType procinfo, uint32_t args)
{
    return mode_switch(theProcPtr, procinfo, kCFM68kISA, args);
}

LONGINT Executor::C_CallUniversalProc(UniversalProcPtr theProcPtr, ProcInfoType procInfo)
{
    return ModeSwitch(theProcPtr, procInfo, kPowerPCISA);
}

LONGINT Executor::C_CallOSTrapUniversalProc(UniversalProcPtr theProcPtr, ProcInfoType procInfo)
{
    return ModeSwitch(theProcPtr, procInfo, kPowerPCISA);
}

/* MacPhoenix: CallUniversalProc and CallOSTrapUniversalProc from CFM-68K code
   (docs/executor/CFM68K.md): C routines, the routine's arguments after the
   procInfo (and the trap word's), 4 bytes each; the caller pops. */
namespace
{
struct CFM68KMixedMode
{
    CFM68KMixedMode()
    {
        builtinlibs::overrideCFM68KEntrypoint("CallUniversalProc", [](syn68k_addr_t) {
            uint32_t sp = EM_A7;
            auto upp = ptr_from_longint<UniversalProcPtr>(*ptr_from_longint<GUEST<uint32_t>*>(sp + 4));
            ProcInfoType info = *ptr_from_longint<GUEST<uint32_t>*>(sp + 8);
            EM_D0 = ROMlib_cfm68k_call_universal_proc(upp, info, sp + 12);
            return POPADDR();
        });
        builtinlibs::overrideCFM68KEntrypoint("CallOSTrapUniversalProc", [](syn68k_addr_t) {
            uint32_t sp = EM_A7;
            auto upp = ptr_from_longint<UniversalProcPtr>(*ptr_from_longint<GUEST<uint32_t>*>(sp + 4));
            ProcInfoType info = *ptr_from_longint<GUEST<uint32_t>*>(sp + 8);
            EM_D0 = ROMlib_cfm68k_call_universal_proc(upp, info, sp + 12);
            return POPADDR();
        });
    }
} cfm68k_mixed_mode;
}
