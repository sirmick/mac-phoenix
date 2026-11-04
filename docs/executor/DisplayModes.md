# Display modes: the screen's resolution changed live

Executor's screen changes size while the Mac runs, as a Mac's Display
Manager changes it: from Settings (the tab), the browser window
(fit-to-window), `/api/resolution` (native), or inside the Mac with the
Monitors control panel or any program's `DMSetDisplayMode`. One path does
it: `ROMlib_dm_set_screen_size` (`romlib/quickdraw/displays.cpp`).

## What a Mac does (measured)

`tests/guest/dmprobe` (Retro68, 68k; `ref/` has its output) on the Quadra's
7.6.1 (Display Manager 2.2.2) and the G3's 8.6 (2.4.4), switching from
1024x768 (8.6: 3840x2160) to 640x480 and back, plus SimpleText's and a
Finder window dragged about and `/api/resolution` (BridgeAgent's
`DMSetDisplayMode`):

- The mode list has every resolution the driver offers; each resolution's
  depths are depth modes from 0x80.
- `DMSetDisplayMode` changes the GDevice (gdRect, the PixMap's bounds,
  rowBytes, base address), the application's `screenBits`, `GrayRgn` and
  the windows before it returns. Between `DMBeginConfigureDisplays` and
  `DMEndConfigureDisplays` the GDevice changes at once, `screenBits` and
  `GrayRgn` at the end.
- Extended notify procs get `kDMNotifyInstalled` (1) when registered,
  `kDMNotifyRemoved` (3) when removed, and for a change `kDMNotifyPrep`
  (4, no data), `kDMNotifyDependents` (6) and `kDMNotifyEvent` (2, the
  event); then classic notify procs get the same event: `'aevt'/'cnfg'`
  with a `'dspl'` record holding one record per display, keyed by its
  display ID, each with `'dold'` and `'dnew'` (display ID, flags, mode,
  device flags and rect, pixmap rect, resolution, pixel type and size,
  component count and size, colour table seed).
- No Apple Event is sent, to Display Manager aware applications or others.
- Windows of every application, Display Manager aware or not, whose title
  bar the new screen doesn't show are moved: up until the title bar's
  bottom is on the screen's bottom edge, and left by as much as the part of
  the window on the old screen ran past the new right edge. A window whose
  title bar is still on screen (partly, horizontally) stays.
- The Finder (it registers notify procs) lays its icons out again and keeps
  its windows on screen; the Control Strip moves itself to the new edge.

## Executor

- `VideoDriver::requestSize(w, h)` (any thread) is the host's request; the
  Display Manager takes it at the front process's next event call
  (`ROMlib_dm_poll` in `doevent`). The wasm front end's `resize` request
  (`web_main.cpp`) and the native child's `resize W H` IPC command
  (`/api/resolution` on the executor backend) call it.
- `gd_set_screen_size` (`qGDevice.cpp`): the framebuffer (one block kept as
  large as the largest screen allowed: the low allocator reuses only blocks
  of the same size, and the wasm build has 64 MB for it and the coroutine
  stacks; 3840x2400 native, 1920x1200 wasm), the main GDevice, the screen's
  ports (a GWorld's port keeps its own bounds), ScrnBase, ScreenRow.
- Then `GrayRgn`, every layer's windows moved (`ROMlib_dm_window_offset`,
  `ROMlib_layers_screen_changed` in `process.cpp`; a window with no title
  bar, the Control Strip's, comes on screen whole), the screen repainted.
- Each process catches up at its own event call: its `screenBits`, its menu
  bar's layout (and drawing, in front), its notify procs, in its own world.
  The process that made the change does so before `DMSetDisplayMode`
  returns. Procs registered for `kSystemProcess` (extensions) run in the
  process that made the change.
- Notify procs called at an event call from PowerPC code (8.6's Finder):
  PowerPC procs through `callPPC`, and 68k ones with A7 below the PowerPC
  frames, which sit just below the 68k stack there.

The page (`web/executor/main.js` `fitCanvas`) shows a Mac pixel as a whole
number of device pixels that fits the window, but never less than a CSS
pixel when that fits, and as large as the room when the screen is bigger
than it; the pointer is at least 16 CSS pixels (`client/cursor.js`). (A
1280x1024 screen at dpr 2 in a 1440x900 window was drawn a device pixel
each, a quarter of the window, the pointer 8 CSS pixels: the owner saw
"1280x1024 doesn't take effect, the pointer is small".)

Tests: `executor_tests --gtest_filter='DisplayMode.*'`
(`src/executor/tests/displaymode.cpp`); the tab cells `executor.web.dynres`, `executor.web.input_86_laptop`.

## Open

- After growing back, 7.6.1's Finder leaves its icons where they were on
  the smaller screen; on the Quadra they went back to the right edge.
  What is known: the Finder's desktop window has an empty structure
  region and a content region that follows the desktop; its portRect keeps
  the size it had (1024x768 at the shrink's notification, 640x480 at the
  grow's). After the change the Finder calls the private
  `DMKeepWindowOnScreen` (a no-op here) on it. Setting the portRect to the
  content's bounds there did not change the layout, so the Finder takes it
  from elsewhere: next would be the Finder's notify proc on the Quadra
  against Executor (the desktop icon region at ExpandMem+$1F4, which the
  Finder edits before `DMDesktopIconRgnChanged`, is a candidate).

## Not done: the Monitors control panels

Not chased (disproportionate for now); what each would need:

- **Monitors 7.5.6** (7.6.1) lists only the current resolution: it builds
  its list from the video card's sResources through the Slot Manager, not
  from the Display Manager. It would need a pseudo slot for the screen with
  a declaration ROM (a board sResource and one video sResource per
  resolution, IDs matching the mode list's) answered by Executor's Slot
  Manager calls. The driver's `cscSwitchMode` already goes through the
  Display Manager, so a choice there would work once listed.
- **Monitors & Sound** (8.6) shows no Monitor panel without the Display
  Enabler extension, which Executor's extension policy leaves off. Either
  let the Enabler load over Executor's Display Manager (its patches would
  replace ours; unknown cost) or answer what the panel asks of the Enabler
  in its place (a trap trace of the panel with the Enabler on, on the G3,
  would give the list).
