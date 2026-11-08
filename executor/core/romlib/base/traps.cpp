#include <rsys/quicktime.h>
#include <vector>
#include <cstring>
#include <syn68k_public.h>
#include <base/common.h>
#include <SegmentLdr.h>

#include <base/functions.h>
#include <base/mactype.h>
#include <base/functions.impl.h>
#include <base/logging.h>
#include <base/dispatcher.h>
#include <base/builtinlibs.h>
#include <base/debugger.h>

#include <assert.h>
#include <set>
#include <filesystem>
#include <ctime>
#include <string>

using namespace Executor;

TrapTableEntry *Executor::tooltraptable; /* at $E00 in guest memory; filled in at run time */
TrapTableEntry *Executor::ostraptable; /* at $400 */

namespace Executor
{
RAW_68K_TRAP(Unimplemented, 0xA89F);
void ReferenceAllTraps();
}

Executor::traps::internal::DeferredInit *Executor::traps::internal::DeferredInit::first = nullptr;
Executor::traps::internal::DeferredInit *Executor::traps::internal::DeferredInit::last = nullptr;

std::unordered_map<std::string, traps::Entrypoint*> Executor::traps::entrypoints;

traps::internal::DeferredInit::DeferredInit()
    : next(nullptr)
{
    if(!first)
        first = last = this;
    else
    {
        last->next = this;
        last = this;
    }
}

void traps::internal::DeferredInit::initAll()
{
    for(DeferredInit *p = first; p; p = p->next)
        p->init();
}

const traps::Entrypoint *Executor::traps::served_entry = nullptr;

/* MacPhoenix: the guest's own allocation calls, whose answer on a full
   heap is nil (or an error) on the Mac: measured on the Quadra 650 with
   7.6.1 (tests/guest/fullheap) for NewHandle, NewPtr, NewRgn, NewPalette,
   NewGWorld and OpenPicture; the rest of the Memory Manager's and the
   Resource Manager's loaders by Inside Macintosh. While one of these is
   served, a failure stays a failure; under any other routine Executor's
   own allocation falls back to the System heap (mman/fullheap.cpp). */
const char *const traps::guest_allocators[] = {
    "_NewHandle_flags", "NewHandle", "NewHandleClear", "NewHandleSys", "NewHandleSysClear",
    "_NewPtr_flags", "NewPtr", "NewPtrClear", "NewPtrSys", "NewPtrSysClear",
    "_NewEmptyHandle_flags", "NewEmptyHandle", "NewEmptyHandleSys",
    "ReallocateHandle", "SetHandleSize", "SetPtrSize", "MoreMasters", "TempNewHandle",
    "HandToHand", "PtrToHand", "PtrToXHand", "HandAndHand", "PtrAndHand",
    "NewRgn", "NewPalette", "NewGWorld", "UpdateGWorld", "OpenPicture", "OpenCPicture",
    "GetResource", "Get1Resource", "GetNamedResource", "Get1NamedResource",
    "GetIndResource", "Get1IndResource", "LoadResource",
    nullptr
};

void traps::Entrypoint::init()
{
    entrypoints[name] = this;
    for(const char *const *p = guest_allocators; *p; p++)
        if(!strcmp(*p, name))
            guestAllocator = true;
}

void traps::LibraryRawExport::init()
{
    builtinlibs::expectRawExport(library, name);
}

void traps::DataExport::init()
{
    builtinlibs::addDataExport(library, name, getter);
}

void traps::LibraryAlsoExport::init()
{
    builtinlibs::alsoExport(library, name, from);
}

void traps::ASLMRawExport::init()
{
    builtinlibs::expectASLMRawEntrypoint(set, index, name);
}

uint32_t traps::Entrypoint::break68K(uint32_t addr)
{
    if(base::Debugger::instance)
        return base::Debugger::instance->trapBreak68K(addr, name);
    else
        return (uint32_t)~0;
}

uint32_t traps::Entrypoint::breakPPC(PowerCore& cpu)
{
    if(base::Debugger::instance)
        return base::Debugger::instance->trapBreakPPC(cpu, name);
    else
        return (uint32_t)~0;
}


/* MacPhoenix: Toolbox traps a System has from 8.0 on, _Unimplemented
   before (68k code tests for a manager by its trap): the Code Fragment
   Manager's, and the Appearance Manager's and its Control Manager's (on 7.x,
   a Mac without the Appearance extension). */
static const uint16_t system8_traps[] = {
    0xAA5A, /* _CodeFragmentDispatch */
    0xAA73, /* _ControlDispatch */
    0xAA74, /* _AppearanceDispatch */
};
static syn68k_addr_t system8_entries[std::size(system8_traps)];

void traps::installSystem8Traps()
{
    for(size_t i = 0; i < std::size(system8_traps); i++)
        if(system8_entries[i]
           && tooltraptable[system8_traps[i] & 0x3FF] == US_TO_SYN68K((void*)&stub_Unimplemented))
            tooltraptable[system8_traps[i] & 0x3FF] = system8_entries[i];
}

/* ... and, with PowerPC code on a 7.x System (--executor-ppc: a Power
   Mac's CFM), _CodeFragmentDispatch alone. */
void traps::installCodeFragmentDispatch()
{
    if(system8_entries[0]
       && tooltraptable[0xAA5A & 0x3FF] == US_TO_SYN68K((void*)&stub_Unimplemented))
        tooltraptable[0xAA5A & 0x3FF] = system8_entries[0];
}

void traps::init(bool log, const std::string& trapFilter)
{
    logging::setEnabled(log);
    logging::setTrapFilter(trapFilter);
    /* MacPhoenix: the tables are in guest memory (trapglue.h). */
    tooltraptable = (TrapTableEntry *)SYN68K_TO_US(TOOLTRAPTABLE_ADDR);
    ostraptable = (TrapTableEntry *)SYN68K_TO_US(OSTRAPTABLE_ADDR);
    memset(tooltraptable, 0, NTOOLENTRIES * sizeof *tooltraptable);
    memset(ostraptable, 0, NOSENTRIES * sizeof *ostraptable);
    ReferenceAllTraps();
    internal::DeferredInit::initAll();
    for(int i = 0; i < NTOOLENTRIES; i++)
    {
        if(tooltraptable[i] == 0)
            tooltraptable[i] = US_TO_SYN68K((void*)&stub_Unimplemented);
    }
    for(int i = 0; i < NOSENTRIES; i++)
    {
        if(ostraptable[i] == 0)
            ostraptable[i] = US_TO_SYN68K((void*)&stub_Unimplemented);
    }
    /* MacPhoenix: _CodeFragmentDispatch ($AA5A) is unimplemented to 68k
       code, as on a 68k Mac without the CFM-68K Runtime Enabler. Executor
       launches CFM-68K applications through its own Code Fragment Manager
       (docs/executor/CFM68K.md), so their CFMLaunch stub never runs; one
       that does (it takes a $AA5A that differs from _Unimplemented for the
       Enabler and calls its private selectors -1, -3, $44) shows its own
       "requires the CFM-68K Runtime Enabler" alert. */
    for(size_t i = 0; i < std::size(system8_traps); i++)
    {
        system8_entries[i] = tooltraptable[system8_traps[i] & 0x3FF];
        tooltraptable[system8_traps[i] & 0x3FF] = US_TO_SYN68K((void*)&stub_Unimplemented);
    }

    trap_install_handler(0xA, &alinehandler, (void *)0);
}

/* ---- MacPhoenix: selectors no routine answers --------------------------- */

void Executor::traps::putOSTrapWord(uint16_t trapno)
{
    if(!(trapno & TOOLBIT))
        EM_D1 = (EM_D1 & 0xFFFF0000) | trapno;
}

void Executor::traps::log_unimplemented(const std::string &what)
{
    static std::set<std::string> logged;
    if(!logged.insert(what).second)
        return;
    fprintf(stderr, "[unimplemented] %s\n", what.c_str());
    if(unimplemented_log_path.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(unimplemented_log_path).parent_path(), ec);
    if(FILE *f = fopen(unimplemented_log_path.c_str(), "a"))
    {
        char date[32];
        time_t now = time(nullptr);
        strftime(date, sizeof date, "%Y-%m-%d %H:%M", localtime(&now));
        fprintf(f, "%s\t%s\t%s\n", date, LM(CurApName)[0] ? std::string((char *)LM(CurApName) + 1, LM(CurApName)[0]).c_str() : "-", what.c_str());
        fclose(f);
    }
}

std::string Executor::traps::unimplemented_log_path;

namespace
{
/* Apple's common dispatcher (the routine at $7B0 on 7.5.5): a byte of D0
   is the size of the parameters (in words for most managers, in bytes for
   the Palette Manager's), and a selector outside its table gets them
   popped and paramErr in the word on top: the result, for the functions
   that have one. */
struct D0SizedDispatcher
{
    const char *name;
    int unit;  /* bytes per count */
    int shift; /* where in D0 the count byte is */
};
const D0SizedDispatcher d0_sized[] = {
    { "PaletteDispatch", 1, 8 }, { "MenuDispatch", 2, 8 },   { "ThreadDispatch", 2, 8 },
    { "LayerDispatch", 2, 8 },   { "DisplayDispatch", 2, 8 }, { "GestaltValueDispatch", 2, 8 },
    { "DialogDispatch", 2, 8 },
    /* ResourceMgr.yaml's ResourceDispatch dispatcher comment: the signed
       low byte of D0 is the selector, the high byte the parameter size in
       words (documented selectors bypass this, registered with their own
       C++ signatures; only an unregistered one reaches this table). */
    { "ResourceDispatch", 2, 8 },
    /* the Sound Manager's: $ssccgggg, ss the parameter words, cc the
       selector, gggg the part (GetSysBeepVolume $02240018) */
    { "SoundDispatch", 2, 24 },
};
}

Executor::traps::MixedModeCall Executor::traps::mixed_mode_call;

uint32_t Executor::traps::unknown_selector_68k(const char *trap_name, uint32_t sel)
{
    char what[160];
    for(const D0SizedDispatcher &d : d0_sized)
        if(strcmp(d.name, trap_name) == 0)
        {
            uint32_t d0 = EM_D0;
            unsigned bytes = ((d0 >> d.shift) & 0xFF) * d.unit;
            snprintf(what, sizeof what,
                     "%s selector $%x (D0 $%08x, %u parameter bytes): answered paramErr",
                     trap_name, (unsigned)sel, (unsigned)d0, bytes);
            log_unimplemented(what);
            syn68k_addr_t ret = POPADDR();
            EM_A7 += bytes;
            WRITEUW(EM_A7, (uint16_t)-50); /* paramErr */
            EM_D0 = (uint32_t)-50;
            return ret;
        }
    if(strcmp(trap_name, "FSDispatch") == 0)
    {
        /* The File Manager's: a register call, the parameter block in A0;
           a selector with no routine (the table's holes, $3, $4, $C-$F)
           answers paramErr in D0 and the block's ioResult. MPW Shell's
           File > New calls $C, and carries on. */
        uint32_t pb = EM_A0;
        snprintf(what, sizeof what, "FSDispatch selector $%x (pb $%x): answered paramErr",
                 (unsigned)sel, (unsigned)pb);
        log_unimplemented(what);
        if(pb)
            WRITEUW(pb + 16, (uint16_t)-50); /* ioResult */
        EM_D0 = (uint32_t)-50;
        return POPADDR();
    }
    if(strcmp(trap_name, "ScriptUtil") == 0)
    {
        /* The selector long: argument bytes in its second byte, the
           result's size in the low six bits of the first (Script.h). What
           7.5.5 answers a selector it lacks isn't known; the result is 0. */
        unsigned arg_bytes = (sel >> 16) & 0xFF, result_bytes = (sel >> 24) & 0x3F;
        snprintf(what, sizeof what,
                 "ScriptUtil selector $%08x (%u argument bytes, %u-byte result): answered 0",
                 (unsigned)sel, arg_bytes, result_bytes);
        log_unimplemented(what);
        syn68k_addr_t ret = POPADDR();
        EM_A7 += 4 + arg_bytes;
        if(result_bytes == 2)
            WRITEUW(EM_A7, 0);
        else if(result_bytes == 4)
            WRITEUL(EM_A7, 0);
        return ret;
    }
    if(mixed_mode_call.entry_sp && mixed_mode_call.entry_sp == EM_A7)
    {
        snprintf(what, sizeof what,
                 "%s selector $%x (through Mixed Mode, %u argument bytes): answered paramErr",
                 trap_name, (unsigned)sel, (unsigned)mixed_mode_call.arg_bytes);
        log_unimplemented(what);
        syn68k_addr_t ret = POPADDR();
        EM_A7 += mixed_mode_call.arg_bytes;
        if(mixed_mode_call.result_bytes == 2)
            WRITEUW(EM_A7, (uint16_t)-50);
        else if(mixed_mode_call.result_bytes == 4)
            WRITEUL(EM_A7, (uint32_t)-50);
        EM_D0 = (uint32_t)-50;
        return ret;
    }
    if(strcmp(trap_name, "TranslationDispatch") == 0 && sel == 0x24)
    {
        /* Selector $24's own call site (disassembled at the resident System
           code THINK C's, THINK Pascal's and MacPerl's launches all go
           through, its address varying run to run): subq.w #6,sp;
           pea.l 2(sp); moveq #$24,d0; _TranslationDispatch -- one 4-byte
           pointer argument, with 2 bytes reserved below it on the stack
           for a word result. What the routine is for isn't known (no
           public name covers it); GetFlavorTranslation's own stand-in
           for a System without Mac OS Easy Open (drag.cpp) answers the
           same way. */
        snprintf(what, sizeof what,
                 "TranslationDispatch selector $24 (4 argument bytes): answered noTranslationPathErr");
        log_unimplemented(what);
        syn68k_addr_t ret = POPADDR();
        EM_A7 += 4;
        WRITEUW(EM_A7, (uint16_t)-3025); /* noTranslationPathErr */
        EM_D0 = (uint32_t)-3025;
        return ret;
    }
    if(strcmp(trap_name, "TranslationDispatch") == 0 && sel == 0x20)
    {
        /* Selector $20, from the same resident System code as $24 (a
           ResEdit launch from the Finder on 8.6 in the tab): subq.w #2,sp;
           clr.w -(sp); pea $A92(a6); pea $A8E(a6); moveq #$20,d0;
           _TranslationDispatch; addq.w #2,sp -- two pointers and a word,
           10 argument bytes, a word result. No translation, as $24. */
        snprintf(what, sizeof what,
                 "TranslationDispatch selector $20 (10 argument bytes): answered noTranslationPathErr");
        log_unimplemented(what);
        syn68k_addr_t ret = POPADDR();
        EM_A7 += 10;
        WRITEUW(EM_A7, (uint16_t)-3025); /* noTranslationPathErr */
        EM_D0 = (uint32_t)-3025;
        return ret;
    }
    uint32_t icm_args, icm_result;
    const char *icm_name;
    if(strcmp(trap_name, "CodecDispatch") == 0
       && ROMlib_icm_unknown_selector(sel, &icm_args, &icm_result, &icm_name))
    {
        /* MacPhoenix: the ICM's selectors by their declarations (icm.cpp);
           its private ones that Executor answers */
        int32_t icm_value;
        if(ROMlib_icm_private_selector(sel, EM_A7 + 4, &icm_value))
        {
            syn68k_addr_t ret = POPADDR();
            EM_A7 += icm_args;
            if(icm_result == 2)
                WRITEUW(EM_A7, (uint16_t)icm_value);
            else if(icm_result == 4)
                WRITEUL(EM_A7, icm_value);
            EM_D0 = icm_value;
            return ret;
        }
        snprintf(what, sizeof what, "CodecDispatch selector $%x (%s, %u argument bytes): answered %s",
                 (unsigned)sel, icm_name, (unsigned)icm_args, icm_result == 2 ? "paramErr" : "0");
        log_unimplemented(what);
        syn68k_addr_t ret = POPADDR();
        EM_A7 += icm_args;
        if(icm_result == 2)
            WRITEUW(EM_A7, (uint16_t)-50);
        else if(icm_result == 4)
            WRITEUL(EM_A7, 0);
        EM_D0 = icm_result == 2 ? (uint32_t)-50 : 0;
        return ret;
    }
    snprintf(what, sizeof what, "%s selector $%x: no size in the selector, session stopped (called from $%x)",
             trap_name, (unsigned)sel, (unsigned)READUL(EM_A7));
    log_unimplemented(what);
    return 0;
}
