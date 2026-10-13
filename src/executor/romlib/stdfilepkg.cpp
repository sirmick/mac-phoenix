/* MacPhoenix: Apple's Standard File, the System file's PACK 3, in place
 * of Executor's dialogs (stdfile.cpp), which have no desktop level (the
 * Desktop button there moved to the next volume) and no folder pop-up.
 *
 * The package is a classic one: entered at its start with the selector
 * word on the stack under the trap's return address, as the inline glue
 * leaves it (move.w #sel,-(sp); _Pack3), and it pops the selector itself;
 * the ROM's Pack3 handler only loads AppPacks[3] and jumps. The entry here
 * is that handler in 68k (the trap's stack must reach the package as the
 * trap left it, so no C++ in between). Scanned against the definitions
 * (docs/executor/PLAN.md, "For a new piece"), 7.5.5's PACK 3 uses public
 * traps alone: its trap $AA6D sits behind a Gestalt selector Executor
 * does not answer, and ScriptUtil -82 is a stub (script.cpp).
 */
#include <base/common.h>
#include <base/trapglue.h>
#include <rsys/stdfilepkg.h>
#include <rsys/version.h>
#include <mman/mman.h>

#include <MemoryMgr.h>
#include <Package.h>
#include <ResourceMgr.h>

#include <cstdio>
#include <cstring>

using namespace Executor;

namespace
{
Ptr code; /* 68k: the trap's entry */
bool installed;

/* movea.l AppPacks+12,a0; tst.l (a0); bne.s loaded;
   move.l a0,-(sp); _LoadResource; movea.l AppPacks+12,a0;
   loaded: movea.l (a0),a0; jmp (a0) */
const uint8_t entry_code[] = { 0x20, 0x78, 0x0A, 0xC4, 0x4A, 0x90, 0x66, 0x08,
                               0x2F, 0x08, 0xA9, 0xA2, 0x20, 0x78, 0x0A, 0xC4,
                               0x20, 0x50, 0x4E, 0xD0 };

/* Apple's package, not a stand-in: 'PACK' 3 from the System file, with
   Apple's header (BRA.S; 'PACK'; 3). */
bool apples_package(Handle h)
{
    if(!h || !*h || GetHandleSize(h) < 16)
        return false;
    const uint8_t *p = (const uint8_t *)*h;
    return p[0] == 0x60 && memcmp(p + 4, "PACK", 4) == 0 && p[8] == 0 && p[9] == 3
        && GetHandleSize(h) > 1024;
}
}

void Executor::ROMlib_install_standard_file()
{
    if(!ROMlib_apple_system_file || !LM(SysMapHndl))
        return;
    Handle h = LM(AppPacks)[3];
    if(!apples_package(h))
        return;
    if(!code)
    {
        TheZoneGuard guard(LM(SysZone));
        code = NewPtrSys(sizeof entry_code);
        if(!code)
            return;
        memcpy(code, entry_code, sizeof entry_code);
        fprintf(stderr, "[Executor] Standard File: the System's PACK 3 (%d bytes)\n",
                (int)GetHandleSize(h));
    }
    /* The code runs from the handle: it must not move or go. */
    HNoPurge(h);
    HLock(h);
    tooltraptable[0xA9EA & 0x3FF] = US_TO_SYN68K(code);
    installed = true;
}

bool Executor::ROMlib_standard_file_package_p()
{
    return installed;
}
