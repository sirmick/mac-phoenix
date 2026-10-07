#include <OSUtil.h>
#include "emustubs.h"

#include <SegmentLdr.h>
#include <error/error.h>
#include <error/system_error.h>
#include <base/debugger.h>
#include <sound/soundopts.h>
#include <rsys/executor.h>

using namespace Executor;

static uint16_t bad_traps[10];
static int n_bad_traps = 0;

void
Executor::ROMlib_reset_bad_trap_addresses(void)
{
    n_bad_traps = 0;
}

static void
add_to_bad_trap_addresses(bool tool_p, unsigned short index)
{
    int i;
    uint16_t aline_trap;

    aline_trap = 0xA000 + index;
    if(tool_p)
        aline_trap += 0x800;

    for(i = 0; i < n_bad_traps && i < std::size(bad_traps) && bad_traps[i] != aline_trap; ++i)
        ;
    if(i >= n_bad_traps)
    {
        bad_traps[n_bad_traps % std::size(bad_traps)] = aline_trap;
        ++n_bad_traps;
    }
}

RAW_68K_IMPLEMENTATION(bad_trap_unimplemented)
{
    char buf[1024];

    sprintf(buf,
            "Fatal error.\r"
            "Jumped to unimplemented trap handler, "
            "probably by getting the address of one of these traps: [");
    {
        int i;
        bool need_comma_p;

        need_comma_p = false;
        for(i = 0; i < (int)std::size(bad_traps) && i < n_bad_traps; ++i)
        {
            if(need_comma_p)
                strcat(buf, ",");
            {
                char trap_buf[7];
                sprintf(trap_buf, "0x%04x", bad_traps[i]);
                gui_assert(trap_buf[6] == 0);
                strcat(buf, trap_buf);
            }
            need_comma_p = true;
        }
    }
    strcat(buf, "].");
    system_error(buf, 0,
                    "Restart", nullptr, nullptr,
                    nullptr, nullptr, nullptr);

    ExitToShell();
    return /* dummy */ -1;
}

RAW_68K_IMPLEMENTATION(Unimplemented)
{
    char buf[1024];

    sprintf(buf,
            "Fatal error.\r"
            "encountered unknown, unimplemented trap `%X'.",
            mostrecenttrap);
    int button = system_error(buf, 0,
                    "Quit", base::Debugger::instance ? "Debugger" : nullptr, nullptr,
                    nullptr, nullptr, nullptr);

    if(button == 0 || !base::Debugger::instance)
    {
        ExitToShell();
    }

    if(base::Debugger::instance)
        if(auto ret = base::Debugger::instance->trapBreak68K(trap_address, "Unimplemented"); ~ret)
            return ret;

    return POPADDR();
}


static int getTrapIndex(uint16_t trapnum, bool newTraps, bool &tool)
{
    if(!newTraps)
    {
        trapnum &= 0x1FF;
        tool = trapnum > 0x4F && trapnum  != 0x54 && trapnum != 0x57;
    }
    return trapnum & (tool ? 0x3FF : 0xFF);
}


static bool shouldHideTrap(bool tool, int index)
{
    if(tool)
    {
        switch(index)
        {
            case 0x00: /* SoundDispatch -- if sound is off, soundispatch is unimpl */
                return ROMlib_PretendSound == soundoff;
            case 0x8F: /* OSDispatch (Word uses old, undocumented selectors) */
                return system_version < 0x700;
            case 0x30: /* Pack14 */
                return true;
            case 0xB5: /* ScriptUtil */
                return ROMlib_pretend_script ? 0 : 1;
            default:
                return false;
        }   
    }
    else
    {
        switch(index)
        {
            case 0x77: /* CountADBs */
            case 0x78: /* GetIndADB */
            case 0x79: /* GetADBInfo */
            case 0x7A: /* SetADBInfo */
            case 0x7B: /* ADBReInit */
            case 0x7C: /* ADBOp */
            case 0x3D: /* DrvrInstall */
            case 0x3E: /* DrvrRemove */
            case 0x4F: /* RDrvrInstall */
                return true;
            case 0x8B: /* Communications Toolbox */
                return ROMlib_creator == "KR09"_4; /* kermit */
                break;
            default:
                return false;
        }
    }
}

ProcPtr Executor::_GetTrapAddress_flags(uint16_t n, bool newTraps, bool tool)
{
    int index = getTrapIndex(n, newTraps, tool);
    
    auto addr = (tool ? tooltraptable : ostraptable)[index];

    if(addr == tooltraptable[_Unimplemented & 0x3FF] || shouldHideTrap(tool, index))
    {
        add_to_bad_trap_addresses(tool, index);
        return (ProcPtr)&stub_bad_trap_unimplemented;
    }
    return ptr_from_longint<ProcPtr>(addr);
}

void Executor::_SetTrapAddress_flags(ProcPtr addr, uint16_t n, bool newTraps, bool tool)
{
    int index = getTrapIndex(n, newTraps, tool);
    
    (tool ? tooltraptable : ostraptable)[index] = ptr_to_longint(addr);
}

void Executor::C_SetToolboxTrapAddress(ProcPtr addr, uint16_t n)
{
    _SetTrapAddress_flags(addr, n, true, true);
}

ProcPtr Executor::C_GetToolboxTrapAddress(uint16_t n)
{
    return _GetTrapAddress_flags(n, true, true);
}

ProcPtr Executor::C_NGetTrapAddress(uint16_t n, TrapType ttype) /* IMII-384 */
{
    return _GetTrapAddress_flags(n, true, ttype != kOSTrapType);
}

void Executor::C_NSetTrapAddress(ProcPtr addr, uint16_t n, TrapType ttype)
{
    _SetTrapAddress_flags(addr, n, true, ttype != kOSTrapType);
}
