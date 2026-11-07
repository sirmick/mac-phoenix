# Executor desktop application

`executor-desktop`: Executor as a native desktop application, Qt 6 Widgets
(Core, Gui, Widgets), Executor in-process. The same session as the tab's
(`executor/host`, `session.h`): Executor's own System or Apple's from a disk
image, disks put in and taken out live, the screen resized live, HiDPI.

```bash
cmake -B build && cmake --build build -j4 --target executor-desktop
./build/executor-desktop                                   # Executor's own System
./build/executor-desktop --system ~/storage/web-library/boot-8.6.img --ppc
ctest --test-dir build -R executor.desktop                 # offscreen, to the Finder
```

HiDPI (Executor's own sharp drawing at 2x, docs/executor/HiDPI.md) needs
`-DEXECUTOR_SKIA=ON` (`tools/skia/fetch.sh` first); without it the HiDPI box
stays off and Qt scales the 1x screen. `-DEXECUTOR_DESKTOP=OFF` leaves the
application out; it is also left out when Qt 6 Widgets isn't there.

## Using it

- **Settings…**: the dialog is made from `executor/web/settings.schema.json`
  (compiled in; the tab and executor/server use the same schema), the
  page-scoped settings: System, memory, colours, PowerPC, screen size, input,
  display, sound, logging. Those marked "Executor restarts" restart it (the
  application starts again: Executor's state is the process's).
- **Disks**: images added (Add Image…, or dropped on the window) and library
  folders (Add Folder…, e.g. `~/storage/web-library`; each image's record,
  `NAME.img.json`, gives its title, note and System). Tick to put a disk in,
  untick to eject it (as the Finder's Put Away). An image is never written:
  the Mac's changes go to an overlay in the data folder's `overlays/`, its
  changed 4 KB blocks in the format executor/server keeps the tab's in
  (`host/overlay.h`; either reads the other's), through the File Manager's
  image opener (`core/romlib/hfs/image_io.h`); Revert throws it away. CDs
  (`.iso`, `.toast`, `.cdr`) are locked, no overlay.
  A disk whose record is for the System running (`systems`) has its System
  Folder additions copied in, as in the tab.
- **Screen size**: the schema's sizes, or Auto (the Mac's screen follows the
  window), live through the Display Manager.
- **HiDPI**: Executor draws the screen at 2x (live).
- **Screenshot**: the 1x screen (and the 2x one with HiDPI) to
  `screenshots/` in the data folder, and the clipboard.
- **Fullscreen** (F11), **Log** (Executor's log; also `logs/` in the data
  folder), **Restart**. The status bar: the front application and the CPU
  meter.

Keys go to the Mac by the physical key (the Mac's keyboard layout applies);
Ctrl, Alt and Meta are Command, Option and Control as Settings > Input says
(on macOS, the Mac's own). The right button is a Control-click unless
Settings says otherwise. The wheel scrolls the window under the pointer.

## The data folder

`QStandardPaths::AppDataLocation` (Linux `~/.local/share/MacPhoenix/Executor`,
macOS `~/Library/Application Support/MacPhoenix/Executor`, Windows
`%APPDATA%/MacPhoenix/Executor`), or `--data DIR`. It is the store (the tab's
is the server):

| | |
|---|---|
| `settings.json` | the settings, in the server's format (the schema's) |
| `desktop.json` | the window, the library's images and folders |
| `volumes/NAME/executor/` | Executor's volume for a System (its own: `Executor`; Apple's: one per System image): its System Folder, preferences and what the Mac saves there |
| `overlays/` | the disks' overlays (`NAME-HASH.overlay`: the image's path, size and time; a changed image starts afresh) |
| `screenshots/`, `logs/`, `unimplemented.log` | |

## Command line

```
executor-desktop [--data DIR] [--system IMAGE] [--disk IMAGE]... [--ppc]
                 [--size WxH] [--scale N] [--ram MB] [--depth BPP]
                 [--screenshot PNG [--screenshot-delay S] [--quit]] [--timeout S]
                 [--request "OP ARG"]... [APP]
```

For this run only (settings.json keeps the dialog's). `--screenshot`: once
the application (the Finder) is in its event loop, after the delay, the 1x
screen to PNG and the window to PNG-window.png, then `--quit`.
`--request`: an automation request at that point (host/session.h:
`resize WxH`, `scale N`, `volumes`, `windows`, ...; `mount`/`eject IMAGE` as
the Disks panel). Executor's own options (`--logtraps`, `--debug`, ...) pass
through to `session::parse_args`. `QT_QPA_PLATFORM=offscreen` runs it with
no display (the ctest cells `executor.desktop.*`).

## How it works

| File | |
|---|---|
| `main.cpp` | the data folder, the command line, the settings as the tab's arguments (`session::parse_args`, `prepare`) |
| `emulator.*` | the session: `executor_host::run()` on a thread of its own (PhoenixVideoDriver's render loop; the Toolbox on the thread run() makes); the changed rectangles (`rectsHook`) into a `QImage` (`Format_RGB32` is Executor's 0xAARRGGBB, no conversion); requests queued to the next event call (`set_event_poll_hook`), replies on the GUI thread |
| `screen_widget.*` | paints what changed, scaled to the window (whole times when it fits); mouse, wheel, keys to `executor_host`; the Mac's cursor as the host pointer (`on_cursor`) |
| `keys.*` | Mac key codes by the physical key (evdev on Linux, virtual keys on macOS, set 1 on Windows; Qt::Key offscreen) |
| `audio.*` | miniaudio (`third_party/miniaudio`, public domain / MIT-0): Executor's 48 kHz frames through a ring; full, `on_audio` says no and Executor offers the frame again |
| `settings.*`, `settings_dialog.*` | settings.json and the dialog from the schema |
| `disks.*` | the library and overlays' names (the opener is in `main.cpp`, the overlay in `host/overlay.*`) |
| `log_capture.*` | stderr to the Log view and `logs/` |
| `main_window.*` | the window, tool bar, Disks and Log panels, automation |

Sound is miniaudio rather than Qt Multimedia: one header with ALSA,
PulseAudio, PipeWire (through Pulse), Core Audio and WASAPI back ends,
against Qt Multimedia's FFmpeg-based plugin and its dependencies; Qt
Multimedia also isn't in Qt's base packages.

Quitting asks the Mac to write its volumes out (`flush`, at its next event
call, three seconds at most) and ends the process: Executor's threads can't
be stopped and started again. Known limit: there is no guest shutdown, so
an application's unsaved documents are lost and the Mac isn't told it is
going (as when a Mac's power is cut after a disk flush).

## Packaging (to do)

- Linux AppImage: linuxdeploy with its Qt plugin (xcb, wayland, platform
  themes, image formats); miniaudio dlopens libpulse/libasound, which stay
  the system's.
- macOS `.app`: `MACOSX_BUNDLE`, Info.plist (name, icon, `NSHighResolutionCapable`),
  macdeployqt, codesign and notarisation; the guest memory windows
  (core/cpu/host/posix) on arm64 macOS (MAP_FIXED, 16 KB pages) and
  mame-ppc's arm64 back end under the hardened runtime (`MAP_JIT`,
  `com.apple.security.cs.allow-jit`) need checking.
- Windows `.exe`: an MSVC or MinGW build of Executor (core/cpu/host has no
  Windows side yet: VirtualAlloc for the guest windows; pread/pwrite, fsync,
  `ioctl` uses in core), windeployqt; keys' set 1 table is untested.
- All: an application icon, a desktop entry / file associations for `.img`
  and `.dsk`.
