#if !defined(_RSYS_DISPLAYMODE_H_)
#define _RSYS_DISPLAYMODE_H_

/* MacPhoenix: the screen's resolution changed live, as the Display Manager
 * does (quickdraw/displays.cpp, docs/executor/DisplayModes.md). */

#include <ExMacTypes.h>
#include <vector>

namespace Executor
{
/* Change the screen to width x height now, from the current process (a
   DMSetDisplayMode, or the host's request at the front process's event
   call): framebuffer, GDevice, ports, GrayRgn, windows moved back on
   screen, everything repainted; each process's QuickDraw globals, menu bar
   and notify procs follow at its next event call. False if the size is
   refused. */
extern bool ROMlib_dm_set_screen_size(int width, int height);

/* A size the screen can have (512x342 up to the host's largest). */
extern bool ROMlib_dm_size_acceptable(int width, int height);

/* At every event call (toolevent.cpp doevent): the host's pending request
   if this is the front process, then this process's part of changes it
   has not seen. */
extern void ROMlib_dm_poll();

/* How far the Display Manager moves a window when the screen changes from
   old_screen to new_screen: (0, 0) while its title bar (struct's top to
   content_top) is on the new screen, else up to the bottom edge and left
   by as much as its part on the old screen ran past the new one's right
   edge (measured: tests/guest/dmprobe). */
extern Point ROMlib_dm_window_offset(const Rect &struct_bbox, INTEGER content_top,
                                     const Rect &old_screen, const Rect &new_screen);

/* The resolutions the main display offers (DMNewDisplayModeList, the
   Monitors control panel): the classic sizes, and the current one. */
struct DisplayResolution
{
    uint32_t id;
    int width, height;
};
extern std::vector<DisplayResolution> ROMlib_dm_resolutions();

/* Every window of every process (and floating layer) moved by
   ROMlib_dm_window_offset, the desktop layer's region the new GrayRgn,
   visible regions recomputed and the whole screen repainted (process.cpp). */
extern void ROMlib_layers_screen_changed(const Rect &old_screen, const Rect &new_screen);
}

#endif
