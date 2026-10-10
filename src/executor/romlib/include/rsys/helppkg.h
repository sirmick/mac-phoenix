#pragma once

#include <MenuMgr.h>

/* MacPhoenix (helppkg.cpp): Apple's Help Manager, the System file's PACK
   14, in place of Executor's stubs (balloon.cpp). */

namespace Executor
{
/* Load the package once and route the Pack14 trap to it; sets the
   per-process low-memory handle, so every launch calls it again. Nothing
   without Apple's System file. */
void ROMlib_install_help_package();

/* Is Apple's package in charge of the trap? */
bool ROMlib_help_package_p();

/* Are balloons on (the globals' flag, which the System's patches test)? */
bool ROMlib_help_balloons_p();

/* From SystemTask: the package's idle call, which shows the balloons for
   windows and dialog items, as 7.5.5's patch makes it. */
void ROMlib_help_idle();

/* From the menu definition (menu/stdmdef.cpp), as Apple's MDEF 0 does:
   the balloon for the item the mouse is on (r: the item's rectangle,
   global), and its removal when the item or the menu changes. Nothing
   while balloons are off. */
void ROMlib_help_menu_balloon(MenuHandle mh, INTEGER item, const Rect *r);
void ROMlib_help_remove_balloon();

/* The Help menu's own items (menu/sysmenu.cpp): "About Balloon Help..."
   and "Show/Hide Balloons", and the latter's text for the state. */
void ROMlib_help_about();
void ROMlib_help_toggle_balloons();
void ROMlib_help_fix_menu_text();
}
