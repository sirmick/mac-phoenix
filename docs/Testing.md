# Testing

MacPhoenix has three kinds of tests: **CTest tests** (host unit tests, shell
and Python scripts that drive the emulator binary over its HTTP API, and
Executor's Toolbox gtests), **Playwright E2E tests** (browser tests of the
web UI and the video pipeline) and a few **node unit tests** for the web
client. Executor's front ends have their own layers
([Executor front ends](#executor-front-ends)): the desktop application's
cells, the WebAssembly tab's cells in `build-wasm`, the Go server's
`go test` and a Windows smoke test.

All of them need a built tree (`cmake --build build -j4`). The ROM-machine
tests need the ROMs and the provisioned test images described under
[Configuration](#configuration); a test whose ROM, image or tool is missing
exits 77, which ctest reports as skipped.

## Quick Reference

```bash
# Host-only and API tests
ctest --test-dir build -L "unit|api"

# Boot tests (every backend and core)
ctest --test-dir build -L boot

# Guest tests (apps running in the guest), in parallel
ctest --test-dir build -L guest -j4

# Executor: guest cells plus the native Toolbox gtests
ctest --test-dir build -L executor -j4

# Everything
ctest --test-dir build -j4

# Playwright E2E
npx playwright test

# Web client unit tests (node:test)
npm run test:unit
```

## CTest

```bash
ctest --test-dir build -N              # list the tests
ctest --test-dir build -N -L guest     # list one label
ctest --test-dir build -R boot_uae     # by name (regex)
ctest --test-dir build -L "boot|bridge"
ctest --test-dir build -V              # verbose
```

`tests/CMakeLists.txt` registers 206 named tests; `executor/` adds 413
more named `executor.*` (619 in all, in a native build with the HFS+
tools; a few cells depend on what configure finds).

### Labels

A test can carry several labels.

| Label | Tests |
|-------|-------|
| `unit` | `mac_roman`, `browser_shm`, `cpu_cores`, `multiversal_retro68`: no emulator |
| `api` | `api_endpoints`, `config_api`, `extfs` |
| `boot` | The `boot_*` tests, `mouse_position*` and the `command_bridge*` cells but the two `command_bridge_quit_*` (24) |
| `bridge` | `bridge_trap`, `bridge_trap_ppc`, `bridge_trap_executor` |
| `guest` | Tests that run an application in the guest: the suites, the probes and the real-application cells below, and the `executor.hfsplus_*` cells that boot (156) |
| `mame` | Most tests on the MAME cores (`--core mame-68k`, `--core mame-ppc`), plus `cpu_cores` |
| `executor` | Most `--backend executor` cells and every `executor.*` test (522) |
| `761` | The cells on Mac OS 7.6.1 (`*_761`, `guest_suite_761`, `ot_retro68_68k_rom`, `otcfg_*`, `slm_*`; 44) |
| `provisioning` | `bake_serials`, `bake_exclude`, `custom_icons`, `system_additions`, `build_printer`, `hfs_glob`: the provisioning scripts, no emulator |
| `tools` | `disk_audit`, `disk_contents`: host-side disk tools on fixture images |
| `qt` | `qt_rom_68k_761`, `qt_rom_ppc_86`, `qt_executor_761`: QTTest, QuickTime from an application |
| `flaky` | `ot_retro68_ppc_rom_761` (STRAGGLERS 66); `ctest -LE flaky` leaves it out |
| `xfail` | Three Executor gtests expected to fail (`executor/tests/test-properties.cmake`); `executor.FileTest.SetFInfo_CrDat` is also disabled |
| `web`, `site` | The tab's cells, in `build-wasm` only ([below](#executor-front-ends)) |

### Test List

Timeouts are the ctest `TIMEOUT` values in `tests/CMakeLists.txt`.

**Host-only and API**

| Name | Script / source | Labels | Timeout | What it tests |
|------|-----------------|--------|---------|---------------|
| `mac_roman` | `test_mac_roman.cpp` | unit | 5 s | MacRoman to UTF-8 conversion |
| `browser_shm` | `test_browser_shm.cpp` | unit | 10 s | The BrowserShm SPSC ring (`src/drivers/browser/ring.cpp`) |
| `cpu_cores` | `test_cpu_cores.cpp` | unit, mame | 10 s | Two mame-68k cores in one process |
| `multiversal_retro68` | `test_multiversal_retro68.sh` | unit, executor | 120 s | Executor's API definitions as Retro68's Multiversal Interfaces: the C headers and glue generated, a program built against them; skips without Retro68 |
| `bake_serials`, `bake_exclude`, `custom_icons`, `system_additions`, `build_printer`, `hfs_glob` | `test_*.py` | provisioning | 60-120 s | `provisioning/bake.py` and `build.py` steps, library disks' System additions, hfsutils names with glob characters |
| `disk_audit`, `disk_contents` | `test_disk_audit.py`, `test_disk_contents.py` | tools | 120 s | `tools/diskaudit/disk_audit.py` and `tools/web/disk_contents.py` on fixture images |
| `api_endpoints` | `test_api_endpoints.sh` | api | 30 s | The `/api/*` endpoints return the expected status and JSON |
| `config_api` | `test_config_api.sh` | api | 30 s | Config round trip through `/api/config`, CD-ROM paths, boot driver |
| `extfs` | `test_extfs.sh` | api | 60 s | ExtFS config, CLI and backward compatibility (8 checks) |

**Boot** (label `boot`; the `mame` ones also `mame`)

| Name | Script | What it tests |
|------|--------|---------------|
| `boot_uae_interp`, `boot_uae_jit` | `test_boot_to_finder.sh` | Quadra 650, UAE core without and with JIT, boots 7.5.5 to the Finder |
| `boot_core_mame68k`, `boot_mame68k` | `test_boot_to_finder.sh` | The same on `--core mame-68k` |
| `boot_se`, `boot_se_mame68k` | `test_boot_se.sh` | Mac SE boots System 6.0.8 (UAE, mame-68k) |
| `boot_iici`, `boot_iici_mame68k` | `test_boot_iici.sh` | Mac IIci boots System 6.0.8 and the Finder draws its desktop (checked with `/api/text`) |
| `boot_ppc_interp`, `boot_ppc_jit` | `test_boot_ppc.sh` | Power Mac G3, Kheperix core without and with JIT, boots 7.5.5 (headless, log milestones) |
| `boot_ppc_api` | `test_boot_ppc.sh --webserver` | The same with the web server, boot phases through `/api/status` |
| `boot_ppc_mame` | `test_boot_ppc.sh --core mame-ppc` | 7.5.5 on mame-ppc, UML interpreter (headless) |
| `boot_ppc_mame_jit` | `test_boot_ppc.sh --core mame-ppc --jit --webserver` | 7.5.5 on mame-ppc's native x86-64 back-end |
| `boot_ppc_mame_api` | `test_boot_ppc.sh --core mame-ppc --webserver` | mame-ppc interpreter with the web server |
| `mouse_position`, `mouse_position_ppc` | `test_mouse_position.sh` | Absolute and relative `POST /api/mouse`, checked against the Mac's low-memory globals (Quadra, G3) |
| `command_bridge` | `test_command_bridge.sh` | `/api/app`, `/api/windows`, `/api/wait`, `/api/launch`, `/api/quit` on the Quadra |
| `command_bridge_mame68k` | `test_command_bridge.sh --core mame-68k` | The same on mame-68k |
| `command_bridge_ppc` | `test_command_bridge.sh --backend kpx` | The same on the G3 (Kheperix) |
| `command_bridge_ppc_mame_m68k` | `test_command_bridge.sh --backend kpx --core mame-ppc --jit --m68k-host 1` | The same on mame-ppc with host-initiated 68k routines on a mame-68k core (C3a) |
| `command_bridge_executor` | `test_command_bridge.sh --backend executor` | The same on Executor |
| `command_bridge_quit_executor_761`, `command_bridge_quit_ppc_executor_761` | `test_command_bridge.sh --backend executor --os-version 7.6.1 [--executor-ppc] --quit-app ... SimpleSound` | The same, then SimpleSound launched and quit with `/api/quit` (the Quit Apple event, AppleScript's PACK 8 on 7.6.1), its 68k code and its PowerPC code. Labels `executor`, `761`, not `boot` |

**Guest** (label `guest`; plus `executor`, `mame`, `bridge` as noted in
`ctest -N -L`)

| Name | Script | What it tests |
|------|--------|---------------|
| `guest_suite` | `test_guest_suite.sh` | MacPhoenixSuite (`tests/guest/suite`) on the Quadra, 7.5.5: disk, ExtFS, audio, AppleScript, network and serial sections |
| `guest_suite_761` | `test_guest_suite.sh --os-version 7.6.1` | The same on 7.6.1 |
| `guest_suite_mame68k` | `test_guest_suite.sh --core mame-68k` | The same on mame-68k |
| `guest_suite_ppc` | `test_guest_suite.sh --backend kpx` | The same on the G3 (Kheperix), Mac OS 9.0.4 |
| `guest_suite_ppc_mame` | `test_guest_suite.sh --backend kpx --core mame-ppc --jit` | The same on mame-ppc's native back-end |
| `guest_suite_executor` | `test_guest_suite.sh --backend executor` | The same on Executor (its network tests use a host echo server) |
| `finder_suite`, `finder_suite_mame68k`, `finder_suite_executor` | `test_guest_suite.sh --sections finder` | The suite's Finder section: AppleScript drives the Finder (make, rename, duplicate, move, copy to another volume, windows, Trash) and the File Manager checks the disk |
| `macperl_smoke`, `macperl_smoke_executor` | `test_guest_suite.sh --script MacPerlSmoke.pl` | MacPerl launched by a do-script Apple Event (`/api/script`), runs Perl, reaches the Host volume and AppleScript |
| `bridge_trap`, `bridge_trap_ppc`, `bridge_trap_executor` | `test_guest_suite.sh --app BridgeDemo.bin` | The bridge trap `$ABE5` answers and a sleeping app is woken by the host (Time Manager task, `WakeUpProcess`): `tests/guest/bridgedemo`. 68k, PowerPC (9.0.4) and Executor. Also label `bridge` |
| `ppc_hello_executor` | `test_guest_suite.sh --backend executor --app PPCHello.bin` | A PowerPC-only application (`tests/guest/ppc-hello`) runs on Executor on the mame-ppc core |
| `notify_guest`, `notify_executor` | `test_guest_suite.sh --app NotifyTest.bin` | The Notification Manager from a background application (`tests/guest/notify`); the ROM machine is the reference |
| `keyboard_guest`, `keyboard_guest_mame68k`, `keyboard_guest_executor` | `test_keyboard_guest.sh` | Keystrokes sent with `/api/keypress` reach the front application |
| `drag_executor` | `test_drag_executor.sh` | Drag and drop between SimpleText and the Finder on Executor (a clipping file on the desktop and back) |
| `aslm_executor` | `test_aslm_executor.sh` | Apple's Shared Library Manager on Executor through Apple's ASLM test tools in MPW Shell; skips without `~/storage/aslm-kit` |
| `toolbox_suite_uae`, `toolbox_suite_executor` | `test_toolbox_suite.sh` | Executor's Toolbox gtests built with Retro68 into a 68k application and run in the guest: on Apple's ROM and System (the ground truth) and on Executor. Expected failures in `tests/guest/toolbox_expected_<backend>.txt` (`..._<backend>_<version>.txt` with `--os-version`, where there is one). Skips without Retro68; the two share a build directory (`RESOURCE_LOCK toolbox_app`) |
| `mpw_toolchain_executor` | `test_mpw_toolchain.sh` | Apple's MPW (SC, Link, ToolServer) on Executor through `tools/mpw/mpw`; skips without the MPW disk (`tools/mpw/mpw setup`) |
| `guest_builds_current` | `test_guest_builds.sh` | The committed guest programs (MacPhoenixSuite, OTTest, BridgeDemo, NotifyTest) are their sources' builds: rebuilt with `tools/mpw/mpw` and compared |
| `ot_executor` | `test_ot_executor.sh` | Executor's native Open Transport through Apple's 68k OT glue (`tests/guest/ot`, an MPW tool run in ToolServer) against a host echo server |
| `otcfg_rom`, `otcfg_rom_ppc`, `otcfg_executor`, `otcfg_executor_ppc` | `test_guest_suite.sh --os-version 7.6.1 --app CfgTest68K.bin` (`CfgTestPPC.bin`) | Open Transport's configuration database (what the TCP/IP control panel reads and writes; `tests/guest/otcfg`) called directly, from 68k code and PowerPC code: Apple's OT 1.1.1 on the Quadra and the G3 (the reference) and Executor's |
| `slm_rom_ppc`, `slm_executor_ppc` | `test_guest_suite.sh --os-version 7.6.1 --app SLMTest.bin` | The Shared Library Manager's PowerPC side through the ASLM SDK's glue (`tests/guest/slm`, MPW MrCpp: InitLibraryManager, operator new and delete, TRY/CATCH/Fail, the current client and global world, a death watcher), what the TCP/IP control panel's PowerPC code uses: Apple's "Shared Library Manager PPC" on the G3 (the reference) and Executor's |
| `drag_ppc_executor_761`, `drag_rom_ppc_761`, `drag_rom_ppc_904` | `test_guest_suite.sh --app DragTestPPC.bin` | The Drag Manager from PowerPC code (DragLib: `tests/guest/drag`): on Executor glue to Apple's 68k Drag Manager, Apple's own on the G3 (7.6.1, 9.0.4) |
| `threads_executor` | `test_threads_executor.sh` | The Thread Manager on Executor through UserLand Frontier's UserTalk threads (`/api/script`) |
| `guest_suite_ppc_executor` | `test_guest_suite.sh --backend executor --executor-ppc` | The fat suite's PowerPC code on Executor |
| `guest_suite_retro68`, `guest_suite_retro68_executor`, `guest_suite_retro68_ppc`, `guest_suite_retro68_ppc_executor` | `test_guest_suite.sh --app .../retro68/MacPhoenixSuite*.bin` | The suite built with Retro68 on the Multiversal Interfaces alone, 68k and PowerPC, on Apple's systems and on Executor |
| `guest_suite_cfm68k`, `guest_suite_cfm68k_executor` | `test_guest_suite.sh --app MacPhoenixSuiteCFM68K.bin` | The suite as a CFM-68K application: Apple's CFM-68K Runtime Enabler on the Quadra (`dev-7.6.1`), Executor's own Code Fragment Manager |
| `retro68_executor`, `retro68_ppc_executor`, `retro68_rom`, `retro68_rom_ppc`, `retro68_ppc_executor_asl` | `test_guest_suite.sh --app RetroTest*.bin` | RetroTest (`tests/guest/retro68`): traps, Component Manager and OSA inlines, InterfaceLib and AppleScriptLib imports. `_asl`: Executor's own AppleScriptLib, Apple's deleted |
| `retro68_automated_executor` | `tools/retro68/automated_tests.py` | Retro68's own AutomatedTests on Executor through LaunchAPPL; skips without `~/opt/retro68/build-ppc` |
| `qt_rom_68k_761`, `qt_rom_ppc_86`, `qt_executor_761` | `tests/guest/qt/run_qt.sh NAME -- SUITE-ARGS` | QTTest (`tests/guest/qt`, MPW, fat) on movies `tests/lib/qtmovie.py` writes from the QuickTime File Format: Gestalt, the component lists, movie headers, every frame decoded (CRCs, also checked against the files' own pixels), playback time, the movie controller, the ICM's codecs, a JPEG PICT, movies it makes in Cinepak, Video, Animation, Photo-JPEG and Graphics (kept in `~/storage/qt/NAME`), the tones in `--audio-dump`. References `expected_rom_68k_761.txt` (Quadra, QuickTime 2.5) and `expected_rom_ppc_86.txt` (G3 on Kheperix, QuickTime 3.0.2; no sound there, playback "done too soon"); `--record` makes one again. On Executor it skips while there is no QuickTime |
| `clib_executor`, `clib_rom_ppc`, `clib_cfm68k_executor`, `clib_cfm68k` | `test_guest_suite.sh --app CLibTest*.bin` | StdCLib and MathLib (`tests/guest/clib`): Executor's against Apple's on 9.0.4 (PowerPC) and NuStdCLib on 7.6.1 (CFM-68K) |
| `devtest_executor`, `devtest_rom_ppc` | `test_guest_suite.sh --app DevTest.bin` | StdCLib's device table and descriptor layer (`tests/guest/devtest`) |
| `ppc_break_executor` | `test_executor_ppc_break.sh` | PowerPC breakpoints from the command line and over `/api/executor/ppc-break` |
| `mactcp_executor`, `mactcp_rom`, `mactcp_rom_ppc` | `test_guest_suite.sh --sections "network mactcp"` | MacTCP in depth: async calls, ASRs, passive open, UDP, the resolver. Executor against a host echo server, Open Transport's MacTCP as the reference |
| `ot_executor_noaslm` | `test_ot_executor.sh --no-aslm` | `ot_executor` without Apple's Shared Library Manager |
| `ot_ppc_executor`, `ot_retro68_ppc_executor`, `ot_retro68_ppc_rom`, `ot_retro68_68k_executor`, `ot_retro68_68k_rom` | `test_guest_suite.sh --app OTTest*.bin` | OTTest from PowerPC code (Apple's glue, MPW) and built with Retro68 (68k and PowerPC); Apple's OT on the G3 (9.0.4) and the Quadra (7.6.1) against net-bridge's echo service |
| `ressize_*`, `itemstyle_*`, `listcell_*` (`_guest`, `_executor`) | `test_guest_suite.sh --os-version 7.6.1 --app ...` | Small probes for single calls (SizeResource, GetItemStyle, LNextCell); the ROM machine is the reference |
| `threads_ppc_executor_761`, `threads_rom_ppc_761`, `collections_ppc_executor_761`, `collections_rom_ppc_904`, `icons_*` | `test_guest_suite.sh --app ...PPC.bin` | The Thread Manager, Collection Manager and Icon Services from PowerPC code (Retro68 probes), Executor against Apple's on the G3 |
| `partition_*`, `cglue_*`, `strprobe_*`, `mman68k_executor_761` | `test_guest_suite.sh`, `tests/guest/difftest/mman68k.sh` | Probes against the Macs: an application's partition, InterfaceLib's lowercase C routines, GetIndString out of range, Memory Manager alignment |
| `difftest_*_executor_86`, `difftest_vminfo_executor_904` | `tools/difftest/run.py PROBE --no-refresh` | Differential probes on Executor against the committed G3 references ([DiffTest.md](executor/DiffTest.md)); one at a time (`RESOURCE_LOCK difftest_executor`) |
| `applemenu_submenus_*_86`, `scroll_bars_*_86` | `test_apple_menu_submenus.sh`, `test_scroll_bars_86.sh` | Apple Menu Options' submenus and live scrolling on 8.6, the G3 the reference |
| `appleworks_executor_761_68k`, `_ppc` | `test_appleworks.py` | AppleWorks 5 on Executor's 7.6.1: a document made, saved, opened and saved again, 68k and PowerPC |
| `codewarrior_executor_86`, `mpw_executor_86`, `thinkc_run_executor` | `test_codewarrior_86.sh`, `test_mpw_86.sh`, `test_thinkc_run_executor.sh` | CodeWarrior IDE 3.2 and MPW Shell (PowerPC) on 8.6, THINK C 6's Project > Run on 7.6.1 |
| `utilities_executor_7.6.1`, `_8.6`, `gestalt_executor_*` | `test_utilities_executor.sh`, `test_gestalt_executor.sh` | The Utilities disk (Gestalt.Appl, TattleTech, MacBench) on Executor; Executor's Gestalt against answers measured on the Quadra and the G3 |
| `pcexchange_executor` | `test_pcexchange_executor.sh` | A FAT16 image through PC Exchange on Executor's File System Manager (7.6.1) |
| `executor.hfsplus_volume`, `_write`, `_fsref`, `_app` | `tests/test_hfsplus_*.py` | The File Manager on HFS+ volumes on Executor against Mac OS 9.0.4's (label `executor` too) |

**Executor sessions** (label `executor`, not `guest`): scripts that drive an
Executor session and check its screen or log. Most skip without their
System image.

| Name | Script | What it tests |
|------|--------|---------------|
| `cursor_executor_png`, `cursor_executor_h264` | `test_cursor_executor.sh` | The page draws Executor's cursor (`client/cursor.js`) on both transports; headless Chromium through playwright (`PLAYWRIGHT_NODE_PATH`) |
| `hidpi_chrome_executor`, `hidpi_gworld_executor`, `hidpi_windows_executor`, `hidpi_labels_executor` | `test_hidpi_*.sh` | `--executor-scale 2` on 8.6 against the 1x frame scaled up ([HiDPI.md](executor/HiDPI.md)); the chrome and GWorld cells skip without Skia |
| `desktop_labels_executor`, `platinum_86_executor`, `text_hilite_executor`, `modal_idle_executor` | `test_*_executor.sh` | 8.6 drawing where the G3 was measured: desktop labels, platinum chrome, the highlight colour; an alert left up idles the host |
| `finder_executor_904`, `office98_executor`, `relaunch_ppc_executor` | `test_finder_904_executor.sh`, `test_office98_executor.sh`, `test_relaunch_ppc_executor.sh` | 9.0.4's own System to the Finder; Word 98 to Office First Run; a PowerPC application launched twice |
| `cdev_shell_executor` | `test_cdev_shell.py` | The control panel shell against the G3's 9.0.4 (`tests/guest/cdev/expected.txt`) |
| `system_sessions_executor`, `system_locked_executor` | `test_executor_system_*.sh` | Two sessions on one `--executor-system`; that image mounted locked |
| `executor.smoke.web_backend` | `executor/tests/smoke_web_backend.sh` | The web parent starts `--backend executor` as its IPC child; input through the API changes the screen |
| `executor.hfsplus_reader`, `executor.hfsplus_writer`, `executor.hfsplus_sideload` | `tests/test_hfsplus_*.py` | The HFS+ reader and writer against hfsfuse's `hfsdump` (and `fsck.hfsplus` in docker); Executor's System from an HFS+ image |
| `executor.desktop.*` | see [Executor front ends](#executor-front-ends) | |
| `*_761` | the cell's script with `--os-version 7.6.1` | The same cells on Mac OS 7.6.1 (`tests-7.6.1`; label `761`): on the Quadra the guest suite (Retro68 build), RetroTest, the Finder and MacTCP sections, the command bridge and the Toolbox suite (`toolbox_expected_uae_7.6.1.txt`: 7.6.1's List Manager makes larger lists); on the G3 under mame-ppc the guest suite (MPW and Retro68 builds), RetroTest, OTTest and the command bridge; on Executor (Apple's 7.6.1 System) the guest suite in all four builds, RetroTest, OTTest (MPW, PowerPC, Retro68 68k and PowerPC), the command bridge, the Toolbox suite, Finder, MacTCP and the Notification Manager. `ot_retro68_ppc_rom_761` is also labelled `flaky`: Apple's OT 1.1.1 on the G3 crashes in `tcp_async` about half the runs (STRAGGLERS 66) |

**Executor gtests** (label `executor`)

`executor.<Suite>.<Test>`: Executor's Toolbox gtests (`executor/tests`,
`executor_tests`) built natively and run on mame-68k, and the overlay
tests (`executor_overlay_tests`: `executor/host`'s disk overlays against
`executor/server`'s format); `executor.compilefail.*`: checks that misuse
of the guest value types fails to compile.

### Executor front ends

```bash
# The desktop application (Qt 6 Widgets), in the native build
ctest --test-dir build -R executor.desktop

# The tab: the gtests under node and the executor.web.* cells
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-wasm && cmake --build build-wasm -j4
ctest --test-dir build-wasm -L executor
ctest --test-dir build-wasm -L site        # the long executor.web.site.* cells

# The Go server
(cd executor/server && go test ./...)

# The Windows zip under Wine (docker)
tools/windows/build.sh && tools/windows/package.sh
tools/windows/smoke.sh ZIP ~/storage/tmp/win-smoke --system IMAGE [--ppc]
```

- **`executor.desktop.*`** (label `executor`): `finder` and `apple761`
  run `executor-desktop` offscreen to the System's shell application
  (`executor/tests/desktop_finder.sh`), on Executor's own System and on
  Apple's 7.6.1 from `~/storage/web-library` with a disk written through
  its overlay (skipped without the library); `settings` checks the
  desktop's `settings.json` against the server's (a file the server wrote
  loads; what the desktop writes is what the schema allows).
- **`executor.web.*`** (labels `executor`, `web`; `build-wasm` only): the
  tab under headless Chromium through playwright (`executor/tests/web/*.mjs`)
  on `executor/server`, which `harness.mjs` builds with go. Finder, writes,
  persistence, PowerPC, HiDPI, input at each screen size and
  devicePixelRatio, the Disks and Settings panels, the claim, the relay.
  `executor.web.standalone_server` runs `make -C executor/server test`
  (the binary and `go test`). They need `-DEXECUTOR_WEB_NODE_PATH=` (a
  `node_modules` with playwright) and go, and skip without them.
  `executor.web.site.*` (labels `web`, `site`, not `executor`) open each
  application of the web library on each of the site's systems; long, and
  skipped without `EXECUTOR_WEB_LIBRARY`.
- **`go test ./...`** in `executor/server`: the API over a real
  connection, the claim, uploads, settings, overlays (against the C++
  ones), shutdown, the standalone binary.
- **`tools/windows/smoke.sh`**: the Windows zip in a Debian container
  under Wine: `executor-desktop.exe` offscreen to a screenshot and its
  log, a System's Finder with `--system`, a crash report
  (`executor-desktop.exe --crash-test`), and `executor-standalone.exe` serving the tab. Not in
  ctest.

### Configuration

Paths come from the CMake cache (passed to the scripts as environment
variables) or from the environment directly:

| Setting | Default | Override |
|---------|---------|----------|
| Quadra 650 ROM | `~/roms/quadra.rom` | `cmake -B build -DTEST_ROM=...` or `MACEMU_ROM` |
| G3 ROM | `~/storage/roms/g3.rom` | `cmake -B build -DTEST_PPC_ROM=...` or `MACEMU_ROM` |
| Mac SE ROM | `~/storage/roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM` | `cmake -B build -DTEST_SE_ROM=...` or `MACEMU_SE_ROM` |
| Mac IIci ROM | `~/storage/roms/iici.rom` | `MACEMU_IICI_ROM` |
| Disk image | a copy of `tests-7.5.5` (see below) | `MACEMU_DISK`; `MACEMU_SE_DISK`, `MACEMU_IICI_DISK` for the System 6 tests |
| Binary | `build/mac-phoenix` | `MACEMU_BINARY` |
| Storage root | `~/storage` | `MACEMU_STORAGE` (scripts that look for images and kits) |
| Scratch | `~/storage/tmp` | `cmake -B build -DTEST_TMPDIR=...` (every test's `TMPDIR`; `/tmp` is RAM-backed here) |

The disk images are the ones `provisioning/build.py` makes:
`tests-7.5.5` (most tests), `tests-7.6.1` (the `*_761` cells),
`tests-9.0.4` (the PowerPC guest tests) and `6.0.8` (the SE and the IIci).
`tests/lib/refresh_test_disk.sh NAME $$` builds the image if it is missing
or out of date, then gives the test its own copy,
`~/storage/images/test-NAME-PID.img` (with its own extracted Executor
System); the copy is removed when the test exits, and copies left by killed
tests are swept on the next call. Without a PID it makes the fixed
`test-NAME.img`, kept, for runs by hand.

Some guest tests also need software that is not on every machine: the MPW
disk (`tools/mpw/mpw setup`) for `mpw_toolchain_executor`,
`guest_builds_current` and `ot_executor*`; the Retro68 toolchain
(`toolchain/retro68`) for `toolbox_suite_*` and `multiversal_retro68`;
Retro68's PowerPC build (`~/opt/retro68/build-ppc`) for
`retro68_automated_executor`; the ASLM kit for `aslm_executor`; Skia
(`-DEXECUTOR_SKIA=ON`) for `hidpi_chrome_executor` and
`hidpi_gworld_executor`; the web library (`~/storage/web-library`) for
`executor.desktop.apple761`. Without them they skip. The
`executor.hfsplus_*` cells are registered only when the build has
hfsfuse's `hfsdump` (the `hfsplus_dump` target).

### Running in parallel

Tests run in parallel (`ctest -j4`) with their own disk copies (above) and,
with `--network socket`, their own net-bridge
(`socket:$TMPDIR/mac-ether-test-PID.sock`; plain `socket` would join the
shared `/tmp/mac-ether.sock` every running MacPhoenix uses). Each test
has its own fixed port (below), so `ctest -j` in one checkout never binds a
port twice, and two checkouts never share one.

### Ports

Every port a test binds is named in one nominal range, **18000-18999**
(`tests/CMakeLists.txt`'s `--port N`, the scripts' defaults,
`tools/difftest/run.py`'s 18600/18610/18620, Playwright's 18094;
`guest_builds_current` also uses the next one), away from a running
emulator on :11000. A new test needs a nominal port no other uses:
`ctest --show-only=json-v1` lists the commands.

Each checkout moves the range to its own **port base**
(`tests/lib/ports.py`, sourced by the scripts as `tests/lib/ports.sh`):

    actual = MACPHOENIX_PORT_BASE + (nominal - 18000)

`MACPHOENIX_PORT_BASE` (a multiple of 1000) sets it. By default each
checkout gets one from a registry every checkout on the box shares
(`~/.cache/macphoenix/port-bases`, under `flock`): the main tree takes
18000 (the historical ports), each git worktree (or a copy of a tree that
isn't a git checkout) the lowest free base with nothing listening in its
range at the time (20000, 21000, ... 31000, then 12000-17000, then
61000-64000: below the ephemeral range, away from 10000-11999), kept for as
long as its directory exists. `python3 tests/lib/ports.py slots` lists
them, `ports.py base` the checkout's. A port outside 18000-18999 is taken
as already mapped (or asked for on purpose) and left alone. An emulator
started by hand: :11000, or 19000-19999, which no checkout is given. A
script whose port something already listens on fails at once (`FAIL: port N
is in use by pid ...`): a session left over from a run ctest killed (an MPW
service outlives its script) would otherwise hold the test up until its
timeout.

### Guest tests and the bridge

The guest tests run with `--bridge` (or `--headless-http`, which implies it)
and `--extfs <tmp>`. The emulator launches **BridgeAgent** from its bridge
directory on the Host volume once the desktop is up; nothing is installed on
the disk image. The script waits for `/api/status` `bridge_agent_connected`,
copies the application it tests into the shared folder (for
MacPhoenixSuite, with a `suite.cfg` naming the results file and sections),
and starts it with `/api/launch` (or `/api/script` for MacPerl and Frontier
scripts). Requests reach the agent through the bridge trap `$ABE5`
([CommandBridge.md](CommandBridge.md)). The application writes PASS/FAIL/SKIP
lines to a results file on the Host volume, which the script reads.

MacPhoenixSuite is built with `tools/mpw/mpw build tests/guest/suite -o
tests/guest/suite` ([MPW.md](MPW.md)); the built `.bin` is committed.
`BridgeAgent/BridgeAgent.bin` is committed too and rebuilt by `cmake --build`
when Retro68 is found (`toolchain/retro68/bin`, else `PATH`); opt out with
`-DBUILD_BRIDGE_AGENT=OFF`.

### Scripts not in ctest

| Script | What it does |
|--------|--------------|
| `tests/test_net_suite.sh` | MacPhoenixSuite's deeper networking section (`netdeep`) with `--network socket` |
| `tests/test_stop_restart.sh` | Stop, start and restart cycles through the API |
| `tests/test_resource_cleanup.sh` | No SHM files, sockets or processes left after exit (graceful and killed) |
| `tests/test_boot_matrix.sh`, `tests/run_boot_matrix.sh` | Boot capacity matrix ([Commands.md](Commands.md#boot-capacity-matrix)) |
| `tests/boot_screenshot.sh` | Boot a machine on a given core and save a screenshot |
| `tests/launch_probe.sh` | Boot the Power Mac headless with the bridge and `POST /api/launch`, keeping the log |

## Playwright E2E Tests

Browser tests in `tests/e2e/` covering the UI controls, video streaming,
mouse input and config persistence. The fixture (`tests/e2e/fixtures.ts`)
spawns the emulator itself (`--config /dev/null --backend uae`, a copy of
`tests-7.5.5` from `refresh_test_disk.sh`).

```bash
npx playwright test                                  # all
npx playwright test tests/e2e/mouse-input.spec.ts    # one spec
npx playwright test --headed                         # visible browser
npx playwright test --ui                             # interactive
```

### Test Specs

| File | What it covers |
|------|----------------|
| `ui-basic.spec.ts` | Page loads, no JS errors, controls present, dropdowns populated |
| `emulator.spec.ts` | Start/stop buttons, status JSON shape |
| `stop-reset.spec.ts` | Start/stop/restart state machine, CPU halt |
| `screenshot.spec.ts` | `/api/screenshot` 503 when stopped, valid PNG when running |
| `mouse-input.spec.ts` | Absolute/relative mouse via HTTP API + readback |
| `config-modal.spec.ts` | Config modal open/close, persistence |
| `settings-dialog.spec.ts` | Boot priority, disk/CD-ROM checkboxes, ROM dropdown, round trip |
| `codec.spec.ts` | Mouse-mode dropdown has options |
| `codec-fullstack.spec.ts` | Codec switching (h264/vp9/png/webp/httpstream) end to end |
| `codec-fallback.spec.ts` | The client's codec auto-fallback controller against a running emulator |
| `stall-detection.spec.ts` | WebRTC mouse latency, pixel verification, 60 s soak (stall rate < 2%) |

### Architecture

- **Fixtures** (`fixtures.ts`): one emulator per worker on port 18094
  (`MACEMU_HTTP_PORT` overrides; ROM from `MACEMU_ROM` or
  `~/roms/quadra.rom`). Provides `emulatorPort` and `hasRom`; tests skip
  without the ROM. No ctest test uses 18094, so Playwright and ctest can
  run at the same time.
- **Single worker**: `workers: 1` in `playwright.config.ts`.
- **No retries**: `retries: 0`.
- **60 s timeout** per test. The soak test in `stall-detection.spec.ts` can run
  longer (`SOAK_DURATION_S`, default 60).

### Stall Detection & Soak Testing

`stall-detection.spec.ts` checks the input-to-output pipeline:

1. **Mouse round trip**: sends absolute/relative mouse positions over the
   `/ws` WebSocket, polls `/api/mouse` until Mac OS reflects the change, and
   measures the latency.
2. **Pixel verification**: takes screenshots with `/api/screenshot`, decodes
   the PNG in the browser and checks for non-black pixels in the menu bar and
   centre.
3. **Soak test**: moves the mouse between 8 positions for 60 s, tracks
   latency percentiles (avg, p95, p99, max) and stalls. Passes if the stall
   rate is under 2%.

## Web client unit tests

`tests/unit/codec-fallback.test.js` tests the codec fallback controller
(`client/codec-fallback.js`) with `node:test`, no emulator:

```bash
npm run test:unit
```

## Screenshot Utility

A standalone script (not a test) that boots the emulator and saves a
screenshot:

```bash
# Default: quadra.rom, wait 15 s, save to /tmp/screenshot.png
npx tsx tests/e2e/take-screenshot.ts

# Custom ROM and disk image
npx tsx tests/e2e/take-screenshot.ts --rom /path/to/rom --disk /path/to/hd.img

# Shorter wait, custom output path
npx tsx tests/e2e/take-screenshot.ts --wait 5 --output ~/desktop.png

# Dismiss the "improper shutdown" dialog before capture
npx tsx tests/e2e/take-screenshot.ts --dismiss-dialog

# Or via npm
npm run screenshot -- --disk /path/to/hd.img --output ~/screenshot.png
```

## Shell Boot Tests

Two scripts in `tests/e2e/` test the dirty-shutdown dialog (not part of the
regular suite):

| File | What it does |
|------|--------------|
| `boot-to-dialog.sh` | Two passes: boot, hard kill, boot again; expects the "not shut down cleanly" dialog |
| `boot-to-dialog-headless.sh` | The same headless with `--no-webserver`, PPM to PNG screenshot conversion |

```bash
tests/e2e/boot-to-dialog.sh uae
tests/e2e/boot-to-dialog-headless.sh uae
```
