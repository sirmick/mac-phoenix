
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

/* The DA Handler's menus in the menu bar (the control panel shell's too). */
void ROMlib_install_da_handler_menus();

/* MacPhoenix: the control panel shell, run by a control panel's process
   (LaunchControlPanel, cdevshell.cpp). */
void ROMlib_run_control_panel(const FSSpec *file);
}
