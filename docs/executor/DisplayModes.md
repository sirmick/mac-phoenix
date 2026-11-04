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

Tests: `executor_tests --gtest_filter='DisplayMode.*'`
(`src/executor/tests/displaymode.cpp`); the tab cell `executor.web.dynres`.

## Open

- After growing back, 7.6.1's Finder leaves its icons where they were on
  the smaller screen; on the Quadra they went back to the right edge.
