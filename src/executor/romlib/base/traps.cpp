#include <vector>
#include <cstring>
#include <syn68k_public.h>

#include <base/functions.h>
#include <base/mactype.h>
#include <base/functions.impl.h>
#include <base/logging.h>
#include <base/dispatcher.h>
#include <base/debugger.h>

#include <assert.h>

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

    trap_install_handler(0xA, &alinehandler, (void *)0);
}
