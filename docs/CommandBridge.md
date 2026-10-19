# Command Bridge

Host-side automation channel for controlling the running Mac OS. Two layers:

1. **Read commands** — host peeks Mac memory directly from the 60Hz IRQ; no guest cooperation required.
2. **Action commands** — the host hands a request to a guest-side helper app (BridgeAgent) through the bridge trap `$ABE5`; the agent executes it via Process Manager / Shutdown Manager / AppleEvents and answers through the trap.

There is no `jGNEFilter` patch and no polling of files: the agent sleeps in `WaitNextEvent` until a request waits.

## Enabling the Bridge

```bash
./build/mac-phoenix --bridge ...           # auto: creates <storage>/tmp/macemu-bridge-XXXXXX (removed at exit), mounts it as ExtFS
./build/mac-phoenix --bridge --extfs /my/dir ...   # uses /my/dir as the bridge dir
```

Or via JSON config / UI: `bridge_enabled: true` (Settings → "Enable automation bridge"). The first `--extfs` path is reused as the bridge dir if one is provided; otherwise a temp dir is created. The bridge dir (`Host:MacPhoenix:<pid>` in the guest) carries what the guest reads at startup (`netcfg.txt`, `NetworkInfo.txt`, MacBrowser's files), not commands.

`--no-webserver` mode in tests sets `bridge_enabled = true` automatically.

## Architecture

### 1. Read Commands (IRQ context)

Implemented in `command_bridge_read()` (`src/core/command_bridge.cpp`). Safe to call from any thread because they only read low memory.

| `CmdType` | What it reads | Notes |
|-----------|--------------|-------|
| `GET_APP_NAME` | `CurApName` at `0x0910` | `/api/app` when the agent's `STATE` isn't fresh |
| `GET_WINDOW_LIST` | Walks `WindowList` at `0x09D6` | Up to 50 windows, returns JSON |
| `GET_TICKS` | `Ticks` at `0x016A` | |
| `READ_MEMORY` | Arbitrary address | Capped at 1024 bytes, returns hex |

### 2. Action Commands (BridgeAgent)

A small Retro68 m68k application (`BridgeAgent/BridgeAgent.c`). The emulator launches it from the bridge dir on the Host volume at desktop time (see Launching the agent); nothing is installed on the boot disk. The same 68k binary runs natively on System 7.x, under Mac OS 9's built-in 68k emulator on PPC, and in Executor.

```
Host (HTTP worker)                     BridgeAgent (guest app)
──────────────────                     ───────────────────────
POST /api/launch {"path": "HD:App"}
  guest_bridge::request(slot,
    "CMD 11\rLAUNCH HD:App")
                                       Time Manager task, every 50 ms:
                                         AGENT_PENDING → 1: WakeUpProcess
                                       main loop:
                                         NEXT  → the request
                                         run_command: LaunchApplication
                                         REPLY "OSERR 0"
  ← {"success": true, "error_code": 0}
```

### The bridge trap, `$ABE5`

Guest code calls it like a register-based Toolbox trap (D0 selector, A0
pointer, D1 value; D0 back) and lands in the emulator:
`guest_bridge::call()` (`src/common/include/guest_bridge.h`), one
implementation for every machine. The ROM machines point its trap table
entry at a stub `[EmulOp BRIDGE_COMM; rts]` written after startup where the
text feed patches (`bridge_trap_install()`, `text_feed.cpp`;
`platform_make_emulop` gives each machine its own encoding, so the PowerPC
machine's 68k emulator works too); Executor at a callback
(`romlib/init.cpp`). `$ABE5` is unimplemented on 7.5.5 and 7.6.1 (68k and
PowerPC), 9.0.4 and in Executor; the install checks the entry is still
`_Unimplemented` and logs `[bridge2]` either way. (`$AB7E`, the first
choice, is a real trap on these Systems.) `tests/guest/bridgedemo` (ctest
`bridge_trap{,_ppc,_executor}`) exercises it alone.

| Selector | Who | What |
|----------|-----|------|
| `AGENT_HELLO` (0x10) | agent | D1 = protocol (2), 0 = going away; answers a magic, 0 if another agent has the bridge |
| `AGENT_PENDING` (0x13) | agent's Time Manager task | 1 if a request waits |
| `NEXT` (0x11) | agent | copies the request to A0 (D1 bytes); -1 none, `0x80000000\|len` if it doesn't fit |
| `REPLY` (0x12) | agent | the answer, A0/D1 |
| `STATE` (0x14) | agent | the front process's name |
| `LAUNCH_POST` (0x18), `LAUNCH_CHECK` (0x15), `LAUNCHED` (0x16) | the launcher | see Launching the agent |
| `BRIDGE_DIR` (0x17) | agent, MacBrowser | the bridge dir's guest path |

* **The slot** (`IPCBridgeSlot`, `ipc_protocol.h`): one request at a time,
  with ids. In the IPC buffer when the CPU runs in a child process (the ROM
  machines behind the web server, Executor always), a static one when it
  runs in-process (`--headless-http` ROM machines). The host side
  (`guest_bridge::request`) writes the request and waits for it to be taken
  (15 s) and answered (the request's timeout), serialized by a mutex. A
  request made before the agent has said hello waits up to 15 s for it.
* **Waking the agent without polling.** The Time Manager task asks
  `AGENT_PENDING` every 50 ms and calls `WakeUpProcess`. Measured from the
  host asking to the application running: 16 ms on the 68k machine, 2-27 ms
  on the PowerPC machine, 0-8 ms in Executor, in front and in the
  background. (Executor's `WakeUpProcess` was a no-op and is now
  implemented: `romlib/process.cpp`, `toolevent.cpp`.) The task is removed
  when the agent's loop ends.
* **Connected** (`/api/status` `bridge_agent_connected`, the UI's choice
  between graceful and hard Power Off / Reset; `bridge_agent_protocol`)
  means the agent said hello and hasn't said goodbye since; a guest reset
  clears it.

### Requests

| Request | Answer |
|---------|--------|
| `EXEC <creator> <seconds>\r<script>` | `STDOUT <n>\r<bytes>STDERR <n>\r<bytes>STATUS <n>\r`, or `OSERR n` |
| `CMD <length>\r<line><payload>` | `OSERR n`, the command's result |

`CMD`'s line (the length because COPY's holds a CR):

| Command | Payload | Guest action |
|---------|---------|--------------|
| `LAUNCH <path>` | — | `LaunchApplication` on the FSSpec |
| `OPEN <path>` | — | Find owning app via Desktop DB, launch it, send generic `'aevt'/'odoc'` with the doc as a typeAlias list — same AE Finder sends on double-click. Works for any registered creator (Frontier `LAND`, MacPerl `McPL`, ResEdit, …) |
| `SCRIPT <creator>` | the script | Find app by creator (a running process first), launch it, send `'misc'/'dosc'` with the payload as `typeChar` direct parameter, fire-and-forget. Generic over any "do script" host — MacPerl, Frontier UserLand 5, MPW Shell, AppleScript editor |
| `COPY <src>\r<dst folder>[\rREPLACE]` | — | File Manager copy, both forks and Finder info; REPLACE merges into a folder of the same name, files replaced |
| `RESIZE <w> <h> <appleID>` | — | `DMSetDisplayMode` |
| `QUIT` | — | `kAEQuitApplication` to the front process |
| `EXIT` | — | The agent quits (back at the next boot) |
| `SHUTDOWN` | — | Best-effort Quit AE to every other app, then `ShutDwnPower()` |
| `RESTART` | — | Same, but `ShutDwnStart()` |
| `SET_CLIPBOARD` | the bytes | `ZeroScrap` + `PutScrap('TEXT', …)` after a SetFrontProcess round-trip |

SHUTDOWN, RESTART and EXIT may end the agent before it answers: done once
taken. EXEC waits for the AE reply the request's seconds (`/api/exec`
`timeout`), stdout from `keyDirectObject`, stderr from `keyErrorString`,
`{Status}` from `keyErrorNumber`, as ToolServer (`MPSX`) fills them.
`tools/mpw/mpw` sends one EXEC per command: the script's output goes to a
file (`∑`; its diagnostics to `Dev:StdOut` hang ToolServer in a build that
runs MrC), its `{Status}` comes in the reply.

Language-specific quirks (e.g. MacPerl needs `\n`, not Mac-style `\r`, in
`eval` bodies to install sub defs) are the **host's** responsibility, not
the bridge's. The agent just delivers the source verbatim.

**Why `ShutDwnPower`/`ShutDwnStart` instead of an AppleEvent to Finder?** Inside Macintosh Vol VI shows that Finder only handles the four Required Apple Events (Open App, Open Docs, Print Docs, Quit App). `kAEShutDown`/`kAERestart` are events Finder *sends*, not ones it receives — so an AE-to-Finder shutdown silently no-ops. The Shutdown Manager is the entry point Finder itself uses after quitting apps.

### The front process (`STATE`)

The agent reports the front process's name (`GetFrontProcess`) on every
pass of its loop; `/api/app` and `/api/wait app=` answer from it while the
last report is under a second old, else from a peek at `CurApName`, which
is whichever process runs at that tick (BridgeAgent itself about half the
time). No recent report means the agent isn't getting time, an application
in a modal loop that doesn't yield (ZTerm's serial port alert), and then
the peek knows better. A reset, or the agent quitting (`AGENT_HELLO` 0),
drops it.

### History

The bridge used to be a file mailbox in the bridge dir (`_bridge_cmd`,
`_bridge_result`, `_bridge_script`, `_bridge_reply`, `_bridge_clipboard`,
a `bridge_heartbeat` the agent rewrote every 2 s and a host watchdog that
looked at it): one slot, no ids, polled at both ends, and "done" when the
agent had deleted the command file rather than when it had run it. It
also patched `_WaitNextEvent` globally, keeping the original at a fixed
address (`0x0210FFC0`) that is plain RAM at the default 64 MB; that was
what killed the agent under Apple's Installer. Both are gone.

The HTTP server doesn't hold every request behind a long one: a pool of
worker threads serves connections, the bridge's routes run concurrently,
the rest one at a time.

## Lifecycle

### Init (`command_bridge_init()`)

Called once from `main.cpp` after `EmulatorConfig` is finalized. No-op when `bridge_enabled` is false; otherwise logs the bridge dir and unpacks `BridgeAgent.bin` (MacBinary) into it as the Host volume keeps a Mac file: `BridgeAgent`, `.rsrc/BridgeAgent`, `.finf/BridgeAgent`. Executor runs on a System Folder on the host and puts the agent in its Startup Items (`executor_systems.cpp`).

### Launching the agent (ROM machines)

`bridge_launcher_install()` (`text_feed.cpp`, after startup with the trap)
puts two pieces of 68k code in the system heap:

* a **Time Manager task**, every 500 ms: asks the trap `LAUNCH_POST`
  (-1 stop, 0 not now, 1 post) and posts a Notification Manager request
  (`NMInstall` is interrupt-safe);
* the request's **response procedure**, which the Notification Manager
  calls at event time in the current application's context (native ones
  too: the classic way an INIT launches an application). It removes the
  request and asks `LAUNCH_CHECK`: yes once per boot, a second after the
  Finder was first seen running (by the task or the procedure: 8.6's Setup
  Assistant keeps the Finder in the back), from whatever application is
  current; then `FSMakeFSSpec` of
  `Host:MacPhoenix:<pid>:BridgeAgent` and `_Launch` (launchContinue,
  launchDontSwitch), the result reported with `LAUNCHED`.

Not before System 7 (no Process Manager: a launch would replace the
Finder), and not if an agent has said hello already (one installed in
Startup Items by hand or by an older emulator): the trap refuses a second
agent's `AGENT_HELLO`, and that agent quits; a new agent asks any older
one (creator `MxBr`) to quit, every 5 s (Startup Items open after it).
Launched in the background, the agent leaves the front process as it is;
started in front (from Startup Items) it gives the front to the Finder.
`/api/ui` answers for the front process the agent reports (`STATE`): on
8.6 and 9 a background-only one (Control Strip Extension) often runs at
the sampled tick, its window list looking like a front window. The agent learns the bridge dir with
`BRIDGE_DIR` (`MacPhoenix.cfg` in Preferences is the fallback,
`bridge_cfg.c`). A `_WaitNextEvent` patch was tried first: Mac OS 9's
native Finder doesn't call it through the trap table.

### Per-command flow (`bridge_command()` in `api_handlers.cpp`)

1. Wait up to 15 s for the agent (`bridge_agent_protocol` ≥ 2), else `{success: false, error: "bridge agent not running"}`.
2. `guest_bridge::request()` the `CMD`: taken within 15 s, answered within the route's timeout.
3. Return `{success: true, error_code: 0}` on `OSERR 0`, otherwise the Mac error code.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/app` | GET | Current app name (`STATE`, else read command) |
| `/api/windows` | GET | Window list as JSON (read command) |
| `/api/wait` | POST | Block on `boot=Finder` or `app=Name` (host-side polling) |
| `/api/launch` | POST | `{"path":"Host:foo","open":true?}` — `LAUNCH` or `OPEN` via the agent. With `open:true`, the file is delivered as a generic `'aevt'/'odoc'` (Frontier `LAND`, MacPerl `McPL`, ResEdit, anything registered for the doc's creator). |
| `/api/script` | POST | `{"creator":"<4-char>","script":"<utf-8 source>"}` (or `"script_b64"` for exact MacRoman bytes) — `SCRIPT <creator>` via the agent. Generic over MacPerl, Frontier, MPW Shell, AppleScript editor. 1 MiB cap. |
| `/api/exec` | POST | `{"creator":"MPSX","command":"...","timeout":N}` — `EXEC`: `exit_status`, `stdout_b64`, `stderr_b64` |
| `/api/copy` | POST | `{"from":"Vol:Item","to":"Vol:Folder:"}` — `COPY`; `"replace":true` merges into a folder of the same name, files replaced |
| `/api/quit` | POST | `QUIT` front app via the agent |
| `/api/shutdown` | POST | `SHUTDOWN` via the agent (`ShutDwnPower`) |
| `/api/restart` | POST | `RESTART` via the agent (`ShutDwnStart`) |
| `/api/clipboard` | POST | `{"text_b64":"<base64 macroman>"}` — `SET_CLIPBOARD` via the agent |

`/api/launch`, `/api/script`, `/api/copy`, `/api/quit`, `/api/shutdown`,
`/api/restart`, `/api/clipboard` all return:

```json
{ "success": true, "error_code": 0 }
{ "success": false, "error_code": -43, "message": "bridge command failed (Mac OS error -43)" }
{ "success": false, "error": "bridge not enabled" }
{ "success": false, "error": "bridge agent not running" }
{ "success": false, "error": "no reply from the agent (bridge agent)" }
```

## UI

The toolbar exposes split buttons:

- **Power Off ▾** — hard kill (immediate `SIGTERM` to the emulator) / **Shut Down…** (graceful via `/api/shutdown`).
- **Reset ▾** — hard reset / **Restart…** (graceful via `/api/restart`).

A checkbox in Settings → "Enable automation bridge (BridgeAgent + ExtFS)" toggles `bridge_enabled`. **The toggle takes effect on the next emulator start** because the subprocess `--bridge` flag is set at spawn time.

## Files

| File | Role |
|------|------|
| `src/common/include/guest_bridge.h` | The trap's protocol: `call()`, `request()`, the slot |
| `src/ipc/ipc_protocol.h` | `IPCBridgeSlot` in the IPC buffer |
| `src/core/text_feed.cpp` | `bridge_trap_install()` (ROM machines) |
| `src/executor/romlib/init.cpp` | The trap in Executor |
| `src/core/command_bridge.{cpp,h}` | Read commands; init |
| `src/webserver/api_handlers.cpp` | HTTP endpoints; `bridge_command()` |
| `src/config/emulator_config.cpp` | `bridge_enabled`, `bridge_dir`, `--bridge` CLI flag, auto extfs mount |
| `BridgeAgent/BridgeAgent.c` | BridgeAgent source (Retro68, m68k) |
| `BridgeAgent/BridgeAgent.bin` | Pre-built MacBinary (committed) |
| `provisioning/install_bridge_agent.sh` | By hand: hfsutils install of `BridgeAgent.bin` into an image's `:System Folder:Startup Items:` |
| `tests/guest/bridgedemo/` | The trap alone, on every machine |
| `tests/guest/suite/` | MacPhoenixSuite, the guest test suite (fat C app, `tools/mpw/mpw build tests/guest/suite -o tests/guest/suite`) |
| `tests/test_command_bridge.sh` | Integration test for `/api/app`, `/api/windows`, `/api/wait`, `/api/launch` |
| `tests/test_guest_suite.sh` | Boot → dispatch script → collect results |

## Process Topology

In webserver mode the host runs **two processes**: a parent that owns the HTTP server and a forked child that runs the CPU. They share the IPC SHM region (boot phase, mouse position, framebuffer, the bridge slot). In headless mode (`--no-webserver`, `--headless-http`) there's only one process and the slot is a static one.
