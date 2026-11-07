# Executor

Classic Mac applications on a Toolbox reimplemented in C++, without a Mac
ROM: [Executor 2000](https://github.com/autc04/executor), imported into
MacPhoenix and grown since to run Apple's System 7.5–8.6 and Finder
(docs/executor/README.md: its origin, what changed, the licence). This
directory is Executor as its own product: the core, its front ends, the
browser tab and the server that hands the tab out. The ROM-based machines
are MacPhoenix's, in `src/`.

| Directory | What |
|---|---|
| `CMakeLists.txt` | the build: `add_subdirectory(executor)` from the top level (native) or `cmake/ExecutorWasm.cmake` (wasm) |
| `core/romlib/` | the Toolbox (upstream's `src/`, lightly patched); `generated/` holds the committed generator output |
| `core/multiversal/` | the API definitions (YAML) and their Ruby generators: headers, trap glue, struct dumps (`-DEXECUTOR_REGENERATE=ON`); also Retro68's Multiversal Interfaces |
| `core/cpu/` | the syn68k API on a `cpu::Core` (mame-68k), PowerCore on mame-ppc, the guest memory windows (`host/posix`, `host/wasm`) |
| `core/res/` | built-in resources (System, Browser, policies), compiled in through `cmake/CMakeRC.cmake` |
| `core/third_party/` | googletest, minicoro (their own licences) |
| `frontends/phoenix/` | the `executor_host` contract (`executor_host.h`: run, input, hooks), `PhoenixVideoDriver` and the sound driver, the System sideload (`hfs_sideload.h`); every front end runs Executor through it. `executor_child.cpp`: mac-phoenix's `--backend executor` (the IPC child), compiled into mac-phoenix only |
| `frontends/wasm/` | the tab's module, `executor-web.js`/`.wasm`: `web_main.cpp` is the emscripten/JS glue; the tab's ctest cells (`executor.web.*`) are registered here |
| `frontends/desktop/` | the desktop application, `executor-desktop` (Qt 6 Widgets, Executor in-process through `host/`; its README.md); its ctest cells `executor.desktop.*` |
| `frontends/headless/` | upstream's headless front end |
| `host/` | front-end logic above `executor_host` that the tab and a native desktop front end share (`session.h`): the command line, the System Folder a session runs on, disks' System Folder additions, disk images mounted and ejected live, automation requests (`app`, `windows`, `menus`, `launch`, `mount`, `eject`, `resize`, ...), frames as RGBA |
| `tests/` | the gtest suite (`executor_tests`, ctest label `executor`), its fixtures, `smoke_web_backend.sh`; `tests/web/`: the tab's node/Playwright cells |
| `web/` | the tab: page, Worker, settings, disks, overlay, sync (copied next to the module by the wasm build) |
| `server/` | the Go server with the tab embedded, `executor-standalone` (docs/executor/Standalone.md; `make WASM_BUILD=...`, `go test ./...`) |
| `COPYING`, `LICENSE-CONTRIBUTIONS` | Executor's MIT licence; MacPhoenix's changes and additions, MIT too |

Not here: the Executor cells that drive mac-phoenix (`tests/test_*executor*.sh`
and others, on `tests/lib` shared with the ROM machines), Executor's developer
tools (`tools/executor`, `tools/difftest`, `tools/multiversal`, `tools/web`,
`tools/mpw`) and its documentation (`docs/executor`).

Building:

```bash
cmake -B build && cmake --build build -j4        # mac-phoenix with --backend executor
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm && cmake --build build-wasm -j4   # the tab: build-wasm/executor/frontends/wasm
make -C executor/server WASM_BUILD=$PWD/build-wasm            # the server with the tab in it
```

The upstream checkout used for comparisons is `executor-upstream/` at the
repository root (gitignored; `tools/executor-upstream-diff.sh`).
