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
                 [--request "OP ARG"]... [--console] [APP]
```

For this run only (settings.json keeps the dialog's). `--screenshot`: once
the application (the Finder) is in its event loop, after the delay, the 1x
screen to PNG and the window to PNG-window.png, then `--quit`.
`--request`: an automation request at that point (host/session.h:
`resize WxH`, `scale N`, `volumes`, `windows`, ...; `mount`/`eject IMAGE` as
the Disks panel). Executor's own options (`--logtraps`, `--debug`, ...) pass
through to `session::parse_args`. `--console`: on Windows, a console
showing the log. `QT_QPA_PLATFORM=offscreen` runs it with
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

## Packaging

**Linux AppImage** (done): `cmake --build build --target executor-desktop-appimage`
or `tools/desktop/appimage.sh [BUILD [OUT]]` makes `Executor-x86_64.AppImage`
(53 MB with `-DEXECUTOR_SKIA=ON`: the binary is 39 MB stripped, Skia and
Executor's resources in it; Qt and the rest 70 MB before compression) with
linuxdeploy and its Qt plugin, fetched once into `~/storage/deps/linuxdeploy`.
Platform plugins: xcb, Wayland and offscreen. It also carries what
linuxdeploy's excludelist leaves to a desktop system (libglvnd's dispatch
libraries, X11/xcb/Wayland client libraries, fontconfig, FreeType,
HarfBuzz) and DejaVu Sans, loaded only when the system has no fonts, so it
starts on a bare system: `tools/desktop/appimage.sh --test APPIMAGE` runs it
in a plain `debian:stable` container, offscreen, to Executor's own System's
Browser. It needs the glibc it was built against or newer (built on
Debian 13: Debian 13, Ubuntu 25.04 and later; build on an older system for
older targets). Sound uses the system's libpulse or libasound if present
(miniaudio loads them), none otherwise.

**macOS `.app`** (not built): `MACOSX_BUNDLE` on the target, an Info.plist
(name, `executor.svg` as an `.icns`, `NSHighResolutionCapable`, the `.img`
and `.dsk` document types), `macdeployqt`, codesign and notarisation. To
check first: the guest memory windows (core/cpu/host/posix) on arm64 macOS
(fixed mappings, 16 KB pages) and mame-ppc's arm64 back end under the
hardened runtime (`MAP_JIT`, the `com.apple.security.cs.allow-jit`
entitlement); Skia's macOS prebuilt; keys use `nativeVirtualKey`
(untested). The data folder is `~/Library/Application Support/MacPhoenix/Executor`.

**Windows zip** (cross-built here: `cmake/ExecutorWindows.cmake`):
`tools/windows/build.sh` fetches llvm-mingw 20231128 (LLVM 17, UCRT) and
Qt's own llvm-mingw build of Qt 6.8.2 (aqtinstall) into
`~/storage/deps/windows` and builds `build-windows/executor-desktop.exe`
(toolchain `tools/windows/llvm-mingw.cmake`; moc/rcc/uic are the build
machine's Qt 6.8). `tools/windows/package.sh` makes
`executor-windows-VERSION.zip`: the stripped exe, Qt's DLLs and plugins
(qwindows, qoffscreen, the modern Windows style, ico/jpeg/gif), libc++,
libunwind and winpthreads, `executor-standalone.exe` (the Go server, as
`make dist` builds it), `README.txt` (`tools/windows/README.txt`), the
licences and SHA256SUMS; the unstripped exe beside the zip, for
`tools/windows/symbolize.sh`. `tools/windows/smoke.sh ZIP DIR [--system
IMAGE [--ppc]]` runs the zip under Wine in a Debian container
(`tools/windows/wine.Dockerfile`): Executor's own System, an Apple System,
`--crash-test`, and the server.
On Windows: a GUI executable with a manifest (UTF-8 code page: Executor's
paths are UTF-8 `char` strings; per-monitor DPI), `--console` for a
console; the log's first line names the version (also in the window's
title); crashes (unhandled exceptions, `std::terminate`, `abort()`) write
`crash-STAMP.dmp` (a minidump) and `crash-STAMP.txt` (the stack as
module+offset, the log's tail) to `logs/` and say so in a dialog
(`windows_support.h`). The data folder is `%AppData%\MacPhoenix\Executor`.
Mac names that Windows forbids in a file name map to the Services for
Macintosh private-use characters (`util/macstrings.cpp`). Not yet: the
network (`EXECUTOR_NET=none`: Winsock types only), Skia (HiDPI), the
gtests on Windows. Keys' set 1 table and WASAPI sound are untested on real
Windows (Wine has neither keyboard input offscreen nor a sound device).

Disk images given on the command line without `--disk` (opened from a
file manager: the desktop entry's `%F`, `application/x-apple-diskimage`)
are mounted as `--disk` ones; on macOS and Windows that needs the
platform's open-file event (`QFileOpenEvent`) too.
