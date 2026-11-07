# Executor

Classic Mac applications, 68k and PowerPC, on a Toolbox reimplemented in
C++, without a Mac ROM. It started as [Executor
2000](https://github.com/autc04/executor) and has grown in MacPhoenix to
run Apple's System 7.5–8.6 Folders and Finder as well as its own small
System (origin, what changed, licence: docs/executor/README.md). This
directory is Executor as its own product: the core, its front ends, the
browser tab and the server that hands the tab out. The ROM-based machines
are MacPhoenix's, in `src/`.

**[SHOWCASE.md](SHOWCASE.md)**: a classic Mac at 2x in the desktop
application or a browser, on Apple's free System 7.5.3 or Executor's own,
with 18 freeware titles (where to download them, step by step).

## Three ways to run it

| | What | Where it keeps things |
|---|---|---|
| `executor-desktop` | the desktop application: Qt 6 Widgets, Executor in-process ([frontends/desktop/README.md](frontends/desktop/README.md)) | the data folder: `~/.local/share/MacPhoenix/Executor` (Linux), `%AppData%\MacPhoenix\Executor` (Windows), or `--data DIR`; logs in its `logs/` |
| `executor-standalone` | the tab (WebAssembly) and its server in one Go program: run it, open the page ([docs/executor/Standalone.md](../docs/executor/Standalone.md)) | the data directory: `~/.config/executor-web` (Linux), `~/Library/Application Support/executor-web` (macOS), `%AppData%\executor-web` (Windows), or `--data DIR`; disk images in its `images/`; the tab's logs in its `logs/` |
| `mac-phoenix --backend executor` | Executor behind MacPhoenix's web UI and HTTP API, as an IPC child (`frontends/phoenix/executor_child.cpp`) | `~/storage/executor` (Executor's own System; `--executor-data`), `~/storage/executor-systems/IMAGE` (Apple's, `--executor-system`); calls Executor lacks in `~/storage/executor/unimplemented.log`; the log on stderr |

The desktop application and the tab run the same session code (`host/`)
and offer the same choices: Executor's own System or an Apple System
Folder from a disk image, disks put in and taken out live, PowerPC on or
off, the screen size changed live, 2x drawing. Neither writes a disk
image: the Mac's changes go to an overlay, in the same format in both.

### Linux

```bash
cmake -B build && cmake --build build -j4                      # mac-phoenix and executor-desktop
./build/executor-desktop                                       # Executor's own System
./build/executor-desktop --system ~/storage/web-library/boot-8.6.img --ppc
cmake --build build --target executor-desktop-appimage        # Executor-x86_64.AppImage (tools/desktop/appimage.sh)
./build/mac-phoenix --backend executor --executor-system 7.5.5.img   # web UI on :11000

source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm && cmake --build build-wasm -j4   # the tab: build-wasm/executor/frontends/wasm
make -C executor/server WASM_BUILD=$PWD/build-wasm            # executor/server/executor-standalone
executor/server/executor-standalone --open                    # http://127.0.0.1:8090/
```

`-DEXECUTOR_SKIA=ON` (`tools/skia/fetch.sh` first; for the tab
`tools/skia/build-wasm.sh`) adds the 2x drawing (docs/executor/HiDPI.md);
without it 2x is the 1x screen scaled up. `-DEXECUTOR_DESKTOP=OFF` leaves
the desktop application out (it is also left out without Qt 6 Widgets);
`-DBUILD_EXECUTOR=OFF` leaves Executor out of mac-phoenix. Executor needs
only a C++17 compiler: its generated sources are committed
(`-DEXECUTOR_REGENERATE=ON` makes them again, with Ruby, Bison and Perl).
A release of the server for six targets: `make -C executor/server dist`
(Standalone.md, "A release"); the site with its library: docs/executor/WebSite.md.

### Windows

Cross-built on Linux (`cmake/ExecutorWindows.cmake`: llvm-mingw and Qt 6.8's
llvm-mingw build, fetched into `~/storage/deps/windows`):

```bash
tools/windows/build.sh                  # build-windows/executor-desktop.exe
tools/windows/package.sh                # executor-windows-VERSION.zip: the exe, Qt, executor-standalone.exe, README.txt
tools/windows/smoke.sh ZIP ~/storage/tmp/win-smoke --system ~/storage/web-library/boot-8.6.img --ppc   # under Wine (docker)
tools/windows/symbolize.sh crash-STAMP.txt   # a crash report's stack, named
```

The zip's `README.txt` (`tools/windows/README.txt`) is the user's guide.
Not yet on Windows: networking inside the Mac (`EXECUTOR_NET=none`), 2x
drawing (no Skia), the gtests.

### macOS

`make -C executor/server dist` cross-compiles `executor-standalone` for
macOS (arm64 and amd64); the tab then runs in the Mac's browser. The
desktop application is not built for macOS yet: what it would take is in
frontends/desktop/README.md, "Packaging".

## Tests

```bash
ctest --test-dir build -L executor              # the gtests, the cells driving mac-phoenix, executor.desktop.*
ctest --test-dir build-wasm -L executor         # the gtests under node, the tab's cells (executor.web.*)
ctest --test-dir build-wasm -L site             # every library application in the tab (WebSite.md)
(cd executor/server && go test ./...)
```

The tab's cells need `-DEXECUTOR_WEB_NODE_PATH=` a `node_modules` with
Playwright (and `-DEXECUTOR_WEB_CHROMIUM=` a browser if not Playwright's).

## What is where

| Directory | What |
|---|---|
| `CMakeLists.txt` | the build: `add_subdirectory(executor)` from the top level (native) or `cmake/ExecutorWasm.cmake` (wasm) |
| `core/romlib/` | the Toolbox (upstream's `src/`, lightly patched); `generated/` holds the committed generator output |
| `core/multiversal/` | the API definitions (YAML) and their Ruby generators: headers, trap glue, struct dumps; also Retro68's Multiversal Interfaces |
| `core/cpu/` | the syn68k API on a `cpu::Core` (mame-68k), PowerCore on mame-ppc, the guest memory windows (`host/posix`, `host/wasm`) |
| `core/res/` | built-in resources (System, Browser, policies), compiled in through `cmake/CMakeRC.cmake` |
| `core/third_party/` | googletest, minicoro (their own licences) |
| `frontends/phoenix/` | the `executor_host` contract (`executor_host.h`: run, input, hooks), `PhoenixVideoDriver` and the sound driver, the System sideload (`hfs_sideload.h`); every front end runs Executor through it. `executor_child.cpp`: mac-phoenix's `--backend executor`, compiled into mac-phoenix only |
| `frontends/wasm/` | the tab's module, `executor-web.js`/`.wasm`: `web_main.cpp` is the emscripten/JS glue; the tab's ctest cells are registered here |
| `frontends/desktop/` | `executor-desktop` (its README.md); ctest cells `executor.desktop.*` |
| `frontends/headless/` | upstream's headless front end (a library under `frontends/phoenix`) |
| `host/` | front-end logic above `executor_host` that the tab and the desktop application share (`session.h`): the command line, the System Folder a session runs on, disks' System Folder additions, disk images mounted and ejected live, overlays (`overlay.h`), automation requests (`app`, `windows`, `menus`, `launch`, `mount`, `eject`, `resize`, ...) |
| `tests/` | the gtest suite (`executor_tests`, ctest label `executor`), its fixtures; `tests/web/`: the tab's node/Playwright cells |
| `web/` | the tab: page, Worker, settings (`settings.schema.json`, the desktop's too), disks, overlay, sync (copied next to the module by the wasm build) |
| `server/` | `executor-standalone`, the Go server with the tab embedded (`make`, `make dist`, `go test ./...`) |
| `COPYING`, `LICENSE-CONTRIBUTIONS` | Executor's MIT licence; MacPhoenix's changes and additions, MIT too |

Not here: the Executor cells that drive mac-phoenix (`tests/test_*executor*.sh`
and others, on `tests/lib` shared with the ROM machines), Executor's
developer tools (`tools/executor`, `tools/difftest`, `tools/multiversal`,
`tools/web`, `tools/mpw`, `tools/desktop`, `tools/windows`) and its
documentation (`docs/executor`).

## Documentation

[docs/executor/README.md](../docs/executor/README.md) is the index: every
document there with a line on what it covers. To start:
[Standalone.md](../docs/executor/Standalone.md) (the server and its API),
[WebSite.md](../docs/executor/WebSite.md) (putting the site up, the
library), [Settings.md](../docs/executor/Settings.md) and
[Disks.md](../docs/executor/Disks.md) (the tab's dialogs),
[SCOREBOARD.md](../docs/executor/SCOREBOARD.md) (which applications run),
[PLAN.md](../docs/executor/PLAN.md) (design and milestones).

The upstream checkout used for comparisons is `executor-upstream/` at the
repository root (gitignored; `tools/executor-upstream-diff.sh`).
