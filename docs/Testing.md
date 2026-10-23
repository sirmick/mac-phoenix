# Testing

MacPhoenix has three kinds of tests: **CTest tests** (host unit tests, shell
scripts that drive the emulator binary over its HTTP API with curl, and
Executor's Toolbox gtests), **Playwright E2E tests** (browser tests of the
web UI and the video pipeline) and a few **node unit tests** for the web
client.

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

`tests/CMakeLists.txt` registers 65 named tests;
`src/executor/tests/CMakeLists.txt` adds 343 more named `executor.*`
(408 in all).

### Labels

A test can carry several labels.

| Label | Tests |
|-------|-------|
| `unit` | `mac_roman`, `browser_shm`, `cpu_cores`: host-only, no emulator |
| `api` | `api_endpoints`, `config_api`, `extfs` |
| `boot` | The `boot_*` tests, `command_bridge*` and `mouse_position*` (22) |
| `bridge` | `bridge_trap`, `bridge_trap_ppc`, `bridge_trap_executor` |
| `guest` | Tests that run an application in the guest: the suites, `bridge_trap*`, `notify_*`, `keyboard_guest*`, `drag_executor*`, `aslm_executor*`, `toolbox_suite_*`, `mpw_toolchain_executor`, `guest_builds_current`, `ot_executor*`, `threads_executor*`, `ppc_hello_executor*` (37) |
| `mame` | Tests on the MAME cores (`--core mame-68k`, `--core mame-ppc`), plus `cpu_cores` |
| `executor` | The `--backend executor` guest cells and all `executor.*` gtests |
| `xfail` | Executor gtests that are expected to fail (`src/executor/tests/test-properties.cmake`). Two of them on each core are also disabled |

### Test List

Timeouts are the ctest `TIMEOUT` values in `tests/CMakeLists.txt`.

**Host-only and API**

| Name | Script / source | Labels | Timeout | What it tests |
|------|-----------------|--------|---------|---------------|
| `mac_roman` | `test_mac_roman.cpp` | unit | 5 s | MacRoman to UTF-8 conversion |
| `browser_shm` | `test_browser_shm.cpp` | unit | 10 s | The BrowserShm SPSC ring (`src/drivers/browser/ring.cpp`) |
| `cpu_cores` | `test_cpu_cores.cpp` | unit, mame | 10 s | Two mame-68k cores in one process |
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
| `toolbox_suite_uae`, `toolbox_suite_executor` | `test_toolbox_suite.sh` | Executor's Toolbox gtests built with Retro68 into a 68k application and run in the guest: on Apple's ROM and System (the ground truth) and on Executor. Expected failures in `tests/guest/toolbox_expected_<backend>.txt`. Skips without Retro68; the two share a build directory (`RESOURCE_LOCK toolbox_app`) |
| `mpw_toolchain_executor` | `test_mpw_toolchain.sh` | Apple's MPW (SC, Link, ToolServer) on Executor through `tools/mpw/mpw`; skips without the MPW disk (`tools/mpw/mpw setup`) |
| `guest_builds_current` | `test_guest_builds.sh` | The committed guest programs (MacPhoenixSuite, OTTest) are their sources' builds: rebuilt with `tools/mpw/mpw` and compared |
| `ot_executor` | `test_ot_executor.sh` | Executor's native Open Transport through Apple's 68k OT glue (`tests/guest/ot`, an MPW tool run in ToolServer) against a host echo server |
| `threads_executor` | `test_threads_executor.sh` | The Thread Manager on Executor through UserLand Frontier's UserTalk threads (`/api/script`) |

**Executor gtests** (label `executor`)

`executor.<Suite>.<Test>`: Executor's Toolbox gtests (`src/executor/tests`)
built natively and run on mame-68k; `executor.compilefail.*`: checks
that misuse of the guest value types fails to compile.

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
`tests-7.5.5` (most tests), `tests-7.6.1` (`guest_suite_761`),
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
(`toolchain/retro68`) for `toolbox_suite_*`; the ASLM kit for
`aslm_executor*`. Without them they skip.

### Running in parallel

Tests run in parallel (`ctest -j4`) with their own disk copies (above) and,
with `--network socket`, their own net-bridge
(`socket:$TMPDIR/mac-ether-test-PID.sock`; plain `socket` would join the
shared `/tmp/mac-ether.sock` every running MacPhoenix uses). Each test
has its own fixed port in the 18080-18442 range (`guest_builds_current`
also uses the next one), away from a running emulator on :11000 and from
Playwright's 18094. A new test needs a port no other uses:
`ctest --show-only=json-v1` lists the commands.

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
