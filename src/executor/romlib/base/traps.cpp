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

void traps::Entrypoint::init()
{
    entrypoints[name] = this;
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
    tooltraptable[0xAA5A & 0x3FF] = US_TO_SYN68K((void*)&stub_Unimplemented);

    trap_install_handler(0xA, &alinehandler, (void *)0);
}

/* ---- MacPhoenix: selectors no routine answers --------------------------- */

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
   that have one. ResourceDispatch's older selectors carry no size, so it
   isn't here. */
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
    /* the Sound Manager's: $ssccgggg, ss the parameter words, cc the
       selector, gggg the part (GetSysBeepVolume $02240018) */
    { "SoundDispatch", 2, 24 },
};
}

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
    snprintf(what, sizeof what, "%s selector $%x: no size in the selector, session stopped (called from $%x)",
             trap_name, (unsigned)sel, (unsigned)READUL(EM_A7));
    log_unimplemented(what);
    return 0;
}
