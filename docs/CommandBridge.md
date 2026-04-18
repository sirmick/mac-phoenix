# Command Bridge

Host-side automation channel for controlling the running Mac OS. Two layers:

1. **Read commands** — host peeks Mac memory directly from the 60Hz IRQ; no guest cooperation required.
2. **Action commands** — host writes a request file; a guest-side helper app (BridgeAgent) reads it, executes via Process Manager / Shutdown Manager / AppleEvents, and writes back a result.

There is no EmulOp injection, no `jGNEFilter` patch, no SHM command queue. The transport is plain files in a directory shared between host and guest via ExtFS.

## Enabling the Bridge

```bash
./build/mac-phoenix --bridge ...           # auto: creates /tmp/macemu-bridge-XXXXXX, mounts it as ExtFS
./build/mac-phoenix --bridge --extfs /my/dir ...   # uses /my/dir as the bridge dir
```

Or via JSON config / UI: `bridge_enabled: true` (Settings → "Enable automation bridge"). The first `--extfs` path is reused as the bridge dir if one is provided; otherwise a temp dir is created.

`--no-webserver` mode in tests sets `bridge_enabled = true` automatically.

## Architecture

### 1. Read Commands (IRQ context)

Implemented in `command_bridge_read()` (`src/core/command_bridge.cpp`). Safe to call from any thread because they only read low memory.

| `CmdType` | What it reads | Notes |
|-----------|--------------|-------|
| `GET_APP_NAME` | `CurApName` at `0x0910` | Used by `/api/app` |
| `GET_WINDOW_LIST` | Walks `WindowList` at `0x09D6` | Up to 50 windows, returns JSON |
| `GET_TICKS` | `Ticks` at `0x016A` | |
| `READ_MEMORY` | Arbitrary address | Capped at 1024 bytes, returns hex |

### 2. Action Commands (BridgeAgent)

A small Retro68 m68k application (`tests/guest/bridge/bridge_agent.c`) installed in `:System Folder:Startup Items:` of every test disk image. Finder launches it at desktop time. The same 68k binary runs natively on System 7.x and under Mac OS 9's built-in 68k emulator on PPC.

BridgeAgent runs a `WaitNextEvent` loop. Each tick it:

1. Writes a heartbeat (see below).
2. Looks for `Host:_bridge_cmd`. If present, reads it, deletes it, dispatches it, then writes the OSErr to `Host:_bridge_result`.

```
Host (parent process)              BridgeAgent (guest app)
─────────────────────              ───────────────────────
POST /api/shutdown
  rm   <bridge_dir>/_bridge_result
  echo SHUTDOWN > _bridge_cmd
                                   WNE tick:
                                     read Host:_bridge_cmd
                                     delete it
                                     quit other apps via AESend
                                     ShutDwnPower()
  poll <bridge_dir>/_bridge_result
  ← {"success": true, "error_code": 0}
```

### Supported commands

| Command | Argument | Guest action |
|---------|----------|--------------|
| `LAUNCH <path>` | HFS path | `LaunchApplication` on the FSSpec |
| `OPEN <path>` | HFS path to a doc | Find owning app via Desktop DB, launch it, then `'misc'`/`'dosc'` AppleEvent (used to dispatch Perl scripts to MacPerl) |
| `QUIT` | — | `kAEQuitApplication` to the front process |
| `SHUTDOWN` | — | Best-effort Quit AE to every other app, then `ShutDwnPower()` |
| `RESTART` | — | Same, but `ShutDwnStart()` |

**Why `ShutDwnPower`/`ShutDwnStart` instead of an AppleEvent to Finder?** Inside Macintosh Vol VI shows that Finder only handles the four Required Apple Events (Open App, Open Docs, Print Docs, Quit App). `kAEShutDown`/`kAERestart` are events Finder *sends*, not ones it receives — so an AE-to-Finder shutdown silently no-ops. The Shutdown Manager is the entry point Finder itself uses after quitting apps.

## Transport

Plain files in `cfg.bridge_dir`, which is mounted into the guest as an ExtFS volume named `Host:`. Both the host parent process and the IPC child see the same directory; `_bridge_cmd` written by the parent is visible to the guest's Filesystem Manager calls in the child.

| File | Direction | Format |
|------|-----------|--------|
| `_bridge_cmd` | host → guest | ASCII line: `LAUNCH path` / `OPEN path` / `QUIT` / `SHUTDOWN` / `RESTART` |
| `_bridge_result` | guest → host | Decimal OSErr, CR-terminated |
| `bridge_heartbeat` | guest → host | JSON: `{"heartbeat":N,"commands":N,"last_result":N,"last_cmd":"..."}` |
| `bridge_loaded` | guest → host | Empty marker, written once at first poll |
| `bridge_step` | guest → host | Single-step debug breadcrumb (`do_open_document` only) |

The `_` prefix on `_bridge_cmd` / `_bridge_result` is cosmetic — there's no ExtFS interception anymore. Heartbeat / loaded / step are unprefixed by historical accident.

## Lifecycle

### Init (`command_bridge_init()`)

Called once from `main.cpp` after `EmulatorConfig` is finalized. No-op when `bridge_enabled` is false; otherwise logs the bridge dir to stderr and runs `provisioning/install_bridge_agent.sh` against every entry in `cfg.disk_paths`. The script skips disks without a `:System Folder:` (data disks, CDROM-style images), and (re)installs `BridgeAgent.bin` into `:System Folder:Startup Items:` on the rest. Runs synchronously before the CPU subprocess is spawned, so disk-image writes don't race the emulator. There's no in-process bridge state to allocate — the directory itself is the state.

### Watchdog (`command_bridge_start_watchdog()`)

Spawned from `main.cpp` once for the parent process. Waits up to 5 minutes for `boot_phase >= Finder`, then `grace_seconds` (default 15) for the agent to drop `bridge_heartbeat`. Logs one of:

- `[Bridge] BridgeAgent heartbeat detected (Ns old)` — happy path.
- `[Bridge] WARNING: bridge enabled but no heartbeat at <path>` — agent missing from `Startup Items`.
- `[Bridge] WARNING: heartbeat at <path> is stale (Ns old)` — agent crashed or quit.

In webserver mode the parent reads `boot_phase` out of the IPC SHM (the actual CPU runs in the child). In headless mode it reads `boot_progress_phase_reached("Finder")` directly.

### Per-command flow (`bridge_command()` in `api_handlers.cpp`)

1. Delete any stale `_bridge_result`.
2. Write `_bridge_cmd`.
3. Poll up to `poll_iterations × 100ms` (default 30 = 3s) for `_bridge_result` or for `_bridge_cmd` to disappear (= agent consumed it but didn't reply yet).
4. Return `{success: true, error_code: 0}` on OSErr `0`, otherwise the Mac error code.
5. On timeout, delete `_bridge_cmd` and return `{success: false, error: "timeout waiting for bridge agent"}`.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/app` | GET | Current app name (read command, no guest cooperation) |
| `/api/windows` | GET | Window list as JSON (read command) |
| `/api/wait` | POST | Block on `boot=Finder` or `app=Name` (host-side polling of read commands) |
| `/api/launch` | POST | `{"path":"Host:foo","open":true?}` — `LAUNCH` or `OPEN` via the agent |
| `/api/quit` | POST | `QUIT` front app via the agent |
| `/api/shutdown` | POST | `SHUTDOWN` via the agent (`ShutDwnPower`) |
| `/api/restart` | POST | `RESTART` via the agent (`ShutDwnStart`) |

`/api/launch`, `/api/quit`, `/api/shutdown`, `/api/restart` all return:

```json
{ "success": true, "error_code": 0 }
{ "success": false, "error_code": -43, "message": "bridge command failed (Mac OS error -43)" }
{ "success": false, "error": "bridge not enabled" }
{ "success": false, "error": "timeout waiting for bridge agent" }
```

## UI

The toolbar exposes split buttons:

- **Power Off ▾** — hard kill (immediate `SIGTERM` to the emulator) / **Shut Down…** (graceful via `/api/shutdown`).
- **Reset ▾** — hard reset / **Restart…** (graceful via `/api/restart`).

A checkbox in Settings → "Enable automation bridge (BridgeAgent + ExtFS)" toggles `bridge_enabled`. **The toggle takes effect on the next emulator start** because the subprocess `--bridge` flag is set at spawn time.

## Files

| File | Role |
|------|------|
| `src/core/command_bridge.{cpp,h}` | Read commands; init + watchdog |
| `src/webserver/api_handlers.cpp` | HTTP endpoints; disk-backed bridge file helpers |
| `src/config/emulator_config.cpp` | `bridge_enabled`, `bridge_dir`, `--bridge` CLI flag, auto extfs mount |
| `src/main.cpp` | Calls `command_bridge_init()` and `command_bridge_start_watchdog()` |
| `tests/guest/bridge/bridge_agent.c` | BridgeAgent source (Retro68, m68k) |
| `tests/guest/bridge/BridgeAgent.bin` | Pre-built MacBinary (committed) |
| `tests/guest/bridge/Makefile` | `make` against Retro68 toolchain |
| `provisioning/install_bridge_agent.sh` | hfsutils install of `BridgeAgent.bin` into `:System Folder:Startup Items:` |
| `tests/guest/MacTestSuite.pl` | MacPerl test script (runs on m68k & PPC) |
| `tests/guest/install_perl_test.py` | Lay out `.pl` + `.finf` for ExtFS |
| `tests/test_command_bridge.sh` | Integration test for `/api/app`, `/api/windows`, `/api/wait`, `/api/launch` |
| `tests/test_guest_suite.sh` | Boot → dispatch script → collect results |

## Process Topology

In webserver mode the host runs **two processes**: a parent that owns the HTTP server and a forked child that runs the CPU. They share the IPC SHM region (used for boot phase, mouse position, framebuffer, the passive `cur_app_name` field) and the bridge directory on disk. They do **not** share heap, which is why an in-memory bridge map didn't work and was replaced with files.

In headless mode (`--no-webserver`) there's only one process; the bridge still uses files because the agent inside the guest is a separate Mac OS process from the host emulator.
