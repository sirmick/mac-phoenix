
#pragma once

#include <api/ExMacTypes.h>
#include <FileMgr.h>

namespace Executor
{
enum
{
    DESK_ACC_MIN = 12,
    DESK_ACC_MAX = 31
};

struct AppleMenuEntry
{
    Str31 name;
    void (*function)();
    
    AppleMenuEntry(const char32_t* n, void (*f)());
};

const std::vector<AppleMenuEntry>& appleMenuEntries();

/* MacPhoenix: the DA Handler, run by a desk accessory's process
   (LaunchDeskAccessory).  file: the DA's file (name empty: the System's);
   name: the DRVR, or nullptr for the file's first one. */
void ROMlib_run_desk_accessory(const FSSpec *file, ConstStringPtr name);
}
