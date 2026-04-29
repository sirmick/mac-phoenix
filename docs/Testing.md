# Testing

MacPhoenix has two test suites: **CTest integration tests** (shell scripts that exercise the emulator binary via curl) and **Playwright E2E tests** (browser-based tests that verify the full UI and WebRTC pipeline).

Both require a built binary (`cmake --build build -j$(nproc)`) and a Quadra 650 ROM.

## Quick Reference

```bash
# CTest integration tests — fast suite (~15s)
ctest --test-dir build -R "api_endpoints|boot_uae|mouse_position|command_bridge"

# CTest integration tests — full suite including Unicorn (~60s)
ctest --test-dir build

# Playwright E2E tests (~2 min)
npx playwright test

# Both suites
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
ctest --test-dir build -L api    # API-only tests
ctest --test-dir build -L boot   # Tests that require booting
ctest --test-dir build -L guest  # Tests that require the BridgeAgent installed
```

### Test List

| Name | File | Suite | Time | What it tests |
|------|------|-------|------|---------------|
| `api_endpoints` | `test_api_endpoints.sh` | api | ~5s | All API endpoints return expected HTTP status codes and JSON fields (10 checks) |
| `config_api` | `test_config_api.sh` | api | ~5s | Config round-trip, CDROM config, boot driver settings |
| `boot_uae` | `test_boot_to_finder.sh` | boot | ~8s | UAE backend boots Mac OS to Finder, tracking boot phases |
| `boot_unicorn` | `test_boot_to_finder.sh` | boot | ~50s | Unicorn backend boots to Finder (slow — JIT compilation) |
| `mouse_position` | `test_mouse_position.sh` | boot | ~10s | Absolute and relative mouse movement via POST /api/mouse, verifies Mac OS reflects changes (8 checks) |
| `command_bridge` | `test_command_bridge.sh` | boot | ~10s | `/api/app`, `/api/windows`, `/api/wait` (read commands), `/api/launch` (BridgeAgent dispatch) — 7 checks |
| `guest_suite` | `test_guest_suite.sh` | guest | ~30s | Boots, dispatches `MacTestSuite.pl` to MacPerl via `/api/launch`, reads results back from ExtFS (m68k) |
| `guest_suite_ppc` | `test_guest_suite.sh` | guest | ~60s | Same, on Mac OS 9 / PPC via the 68k emulator |

### Configuration

Tests use these defaults (override with environment variables or CMake options):

| Setting | Default | Override |
|---------|---------|----------|
| ROM path | `/home/mick/quadra.rom` | `MACEMU_ROM` env var or `cmake -B build -DTEST_ROM=/path` |
| Disk image | `/home/mick/storage/images/7.6.img` | `MACEMU_DISK` env var |

Tests use dedicated ports (18090-18093) to avoid conflicts with a running emulator.

### Guest Tests (BridgeAgent + MacPerl)

`guest_suite` and `guest_suite_ppc` (label: `guest`) require the test disk image to have **BridgeAgent** installed in `:System Folder:Startup Items:` so Finder auto-launches it at desktop time. The agent is what receives `/api/launch` requests, locates MacPerl by creator code, and dispatches the Perl script via a `'misc'`/`'dosc'` AppleEvent.

```bash
# (Re)install the BridgeAgent into the test disk images
provisioning/install_bridge_agent.sh
```

The script writes `BridgeAgent/BridgeAgent.bin` (which is committed to the repo) into the disk images via `hcopy`. Rebuild the agent with `make -C BridgeAgent` if you change `bridge_agent.c`; the Makefile expects the Retro68 toolchain at `~/Retro68`.

The test harness boots the emulator with `--bridge --extfs <tmp>` (the temp dir doubles as the bridge transport and the `Host:` volume the script reads/writes), waits for the agent to drop `bridge_heartbeat`, posts to `/api/launch`, and reads `test_results.txt` back from the same dir.

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

| File | Tests | What it covers |
|------|-------|----------------|
| `ui-basic.spec.ts` | 5 | Page loads without JS errors, all UI controls present, dropdowns populated |
| `emulator.spec.ts` | 4 | Start/stop buttons hit correct API endpoints, status JSON structure |
| `stop-reset.spec.ts` | 7 | Start/stop/restart state machine, button label toggling, CPU halt verification |
| `screenshot.spec.ts` | 2 | /api/screenshot returns 503 when stopped, valid PNG when running |
| `mouse-input.spec.ts` | 3 | Absolute/relative mouse via HTTP API, position readback after boot |
| `config-modal.spec.ts` | 6 | Config modal open/close, dropdown population, save persistence |
| `settings-dialog.spec.ts` | 7 | Boot priority, disk/CDROM checkboxes, ROM dropdown, config round-trip |
| `codec.spec.ts` | 1 | Mouse mode dropdown has options |
| `stall-detection.spec.ts` | 7 | WebRTC mouse latency (<500ms), framebuffer pixel verification, 60s soak test (stall rate <2%) |

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
