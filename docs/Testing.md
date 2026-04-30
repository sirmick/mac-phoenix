# Testing

MacPhoenix has two test suites: **CTest integration tests** (shell scripts that exercise the emulator binary via curl) and **Playwright E2E tests** (browser-based tests that verify the full UI and WebRTC pipeline).

Both require a built binary (`cmake --build build -j$(nproc)`) and a Quadra 650 ROM.

## Quick Reference

```bash
# Fast suite (unit + api, ~15s)
ctest --test-dir build -L "unit|api"

# Boot + bridge (excludes guest_suite, ~3 min)
ctest --test-dir build -L "boot|bridge"

# Full ctest (including guest_suite — needs BridgeAgent + MacPerl, ~5 min)
ctest --test-dir build

# Playwright E2E (~2 min)
npx playwright test

# Everything
ctest --test-dir build && npx playwright test
```

## CTest Integration Tests

These are shell scripts in `tests/` that start the emulator, hit HTTP API endpoints with curl, and check responses. They run without a browser.

```bash
# Run all
ctest --test-dir build

# Run specific test
ctest --test-dir build -R boot_uae

# Verbose output
ctest --test-dir build -V

# Run by label
ctest --test-dir build -L unit     # mac_roman, browser_shm — host-only
ctest --test-dir build -L api      # api_endpoints, config_api, extfs
ctest --test-dir build -L boot     # boot_*, mouse_position*, command_bridge*
ctest --test-dir build -L bridge   # wne_patch_sanity
ctest --test-dir build -L guest    # guest_suite{,_761,_ppc}
```

### Test List

| Name | File | Label | Time | What it tests |
|------|------|-------|------|---------------|
| `mac_roman` | `test_mac_roman.cpp` | unit | ~1s | MacRoman ↔ UTF-8 codec round-trip |
| `browser_shm` | `test_browser_shm.cpp` | unit | ~1s | BrowserShm SPSC ring (10 cases / 47 assertions) |
| `api_endpoints` | `test_api_endpoints.sh` | api | ~5s | All `/api/*` endpoints return expected status + JSON |
| `config_api` | `test_config_api.sh` | api | ~5s | Config round-trip, CD-ROM config, boot driver |
| `extfs` | `test_extfs.sh` | api | ~30s | ExtFS config, CLI, backward compat (8 checks) |
| `boot_se` | `test_boot_se.sh` | boot | ~30s | Mac SE boot to Finder (System 6) — needs SE ROM |
| `boot_uae_interp` | `test_boot_to_finder.sh` | boot | ~10s | UAE backend, no JIT, boots to Finder |
| `boot_uae_jit` | `test_boot_to_finder.sh` | boot | ~10s | UAE backend, JIT, boots to Finder |
| `boot_unicorn` | `test_boot_to_finder.sh` | boot | ~120s | Unicorn-m68k boots to Finder |
| `boot_ppc_interp` | `test_boot_ppc.sh` | boot | ~45s | KPX interpreter boots Mac OS 7.5.5 to Finder |
| `boot_ppc_jit` | `test_boot_ppc.sh` | boot | ~45s | KPX dyngen JIT boot attempt (currently blocked by GCC codegen) |
| `boot_ppc_api` | `test_boot_ppc.sh` | boot | ~45s | PPC + webserver, status API + boot phase tracking |
| `mouse_position` | `test_mouse_position.sh` | boot | ~15s | Absolute + relative mouse via POST /api/mouse (m68k) |
| `mouse_position_ppc` | `test_mouse_position.sh` | boot | ~20s | Same, KPX backend |
| `command_bridge` | `test_command_bridge.sh` | boot | ~20s | `/api/app`, `/api/windows`, `/api/wait`, `/api/launch` (m68k) |
| `command_bridge_ppc` | `test_command_bridge.sh` | boot | ~20s | Same, KPX backend |
| `wne_patch_sanity` | `test_wne_patch_sanity.sh` | bridge | ~60s | Verifies the WaitNextEvent jGNEFilter shim doesn't crash long-running idle |
| `guest_suite` | `test_guest_suite.sh` | guest | ~60s | Boots 7.5.5, dispatches `MacTestSuite.pl` → MacPerl via `/api/launch`, reads results back from ExtFS |
| `guest_suite_761` | `test_guest_suite.sh` | guest | ~60s | Same, 7.6.1 disk |
| `guest_suite_ppc` | `test_guest_suite.sh` | guest | ~60s | Same, KPX backend (PPC's 68k emulator running MacPerl) |

### Configuration

Tests pick up paths from CMake cache or environment variables:

| Setting | Default | Override |
|---------|---------|----------|
| m68k ROM | `~/roms/quadra.rom` | `cmake -B build -DTEST_ROM=...` or `MACEMU_ROM` |
| PPC ROM | `~/storage/roms/g3.rom` | `cmake -B build -DTEST_PPC_ROM=...` or `MACEMU_ROM` |
| Mac SE ROM | (unset; `boot_se` skips) | `cmake -B build -DTEST_SE_ROM=...` or `MACEMU_SE_ROM` |
| Disk image | `~/storage/images/macos-7.5.5.img` | `MACEMU_DISK`; `tests/lib/refresh_test_disk.sh` clones a fresh copy per run |

Tests pick non-overlapping ports in the 18080–18108 range so they don't
collide with a running emulator on :11000.

### Guest Tests (BridgeAgent + MacPerl)

`guest_suite{,_761,_ppc}` (label `guest`) need a disk image with
**BridgeAgent** installed in `:System Folder:Startup Items:` so Finder
auto-launches it at desktop time. The agent receives `/api/launch` calls,
finds MacPerl by creator code, and dispatches the script via a
`'misc'`/`'dosc'` AppleEvent. MacPerl writes `test_results.txt` back into
the same ExtFS share the test reads from.

```bash
# (Re)install BridgeAgent.bin into the test disk images
provisioning/install_bridge_agent.sh
```

`BridgeAgent/BridgeAgent.bin` is committed; the install script `hcopy`s it
into the image. `cmake --build build` rebuilds it from `BridgeAgent.c`
when Retro68 is detected (default at `~/Retro68`); opt out with
`-DBUILD_BRIDGE_AGENT=OFF`. The harness runs the emulator with
`--bridge --extfs <tmp>` and waits for `bridge_heartbeat` before posting
to `/api/launch`.

## Playwright E2E Tests

Browser-based tests in `tests/e2e/` that verify the full stack: UI controls, WebRTC video streaming, mouse input latency, and config persistence. Playwright auto-spawns the emulator for each test worker.

```bash
# Run all
npx playwright test

# Run specific spec
npx playwright test tests/e2e/mouse-input.spec.ts

# Headed mode (visible browser)
npx playwright test --headed

# UI mode (interactive)
npx playwright test --ui
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
| `settings-dialog.spec.ts` | Boot priority, disk/CD-ROM checkboxes, ROM dropdown, round-trip |
| `codec.spec.ts` | Mouse-mode dropdown has options |
| `codec-fullstack.spec.ts` | Codec switching (h264/vp9/png/webp/httpstream) end-to-end |
| `stall-detection.spec.ts` | WebRTC mouse latency, pixel verification, 60s soak (stall rate <2%) |

### Architecture

- **Fixtures** (`fixtures.ts`): Auto-spawns the emulator as a child process per test worker on port 18094. Provides `emulatorPort` and `hasRom` fixtures. Tests skip if ROM is missing.
- **Single worker**: Tests run serially (`workers: 1` in `playwright.config.ts`) — one emulator instance shared across all specs.
- **No retries**: `retries: 0` — failures are real.
- **60s timeout**: Per-test timeout. The soak test in `stall-detection.spec.ts` can run longer (configurable via `SOAK_DURATION_S` env var, default 60s).

### Stall Detection & Soak Testing

The `stall-detection.spec.ts` suite is the most comprehensive — it validates the full input-to-output pipeline:

1. **Mouse round-trip**: Sends absolute/relative mouse positions via the `/ws` WebSocket, polls `/api/mouse` to confirm Mac OS reflects the change, measures latency.
2. **Pixel verification**: Takes screenshots via `/api/screenshot`, decodes PNG in the browser, checks for non-black pixels in key regions (menu bar, center).
3. **Soak test**: Runs sustained mouse movement for 60s, alternating between 8 positions. Tracks latency percentiles (avg, p95, p99, max) and stall count. Passes if stall rate < 2%.

## Screenshot Utility

A standalone script (not a test) for capturing screenshots of the running emulator:

```bash
# Default: boot with quadra.rom, wait 15s, save to /tmp/screenshot.png
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

Two additional shell scripts in `tests/e2e/` test the dirty-shutdown dialog scenario (not part of the regular test suite):

| File | What it does |
|------|--------------|
| `boot-to-dialog.sh` | Two-pass test: boot → hard kill → reboot, expects "not shut down cleanly" dialog |
| `boot-to-dialog-headless.sh` | Same but headless with `--no-webserver`, uses PPM→PNG screenshot conversion |

Run manually:
```bash
tests/e2e/boot-to-dialog.sh [uae|unicorn]
tests/e2e/boot-to-dialog-headless.sh [uae|unicorn]
```
