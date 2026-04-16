# Command Bridge

Host-side command dispatcher for controlling the running Mac OS. Provides API endpoints for querying app state, window lists, and executing actions like launching apps and running scripts.

## Architecture

The bridge has two layers: passive read commands satisfied entirely from the host, and active commands relayed to a guest-side helper app.

### 1. Read Commands (IRQ context)

Read commands peek Mac memory directly from the 60Hz IRQ handler. No Toolbox calls needed, safe from any context.

- **GET_APP_NAME** — reads CurApName at low memory 0x0910
- **GET_WINDOW_LIST** — walks WindowRecord linked list at 0x09D6
- **GET_TICKS** — reads Ticks counter at 0x016A
- **READ_MEMORY** — hex dump of arbitrary Mac address

### 2. Action Commands (BridgeAgent)

Action commands (`/api/launch`) are dispatched via **BridgeAgent**, a tiny m68k Mac application installed in `:System Folder:Startup Items:` on each test disk image. Finder auto-launches BridgeAgent at desktop time. The same 68k binary runs natively on System 7.x and under Mac OS 9's built-in 68k emulator on PPC.

BridgeAgent runs an event loop that polls a magic file in the host-shared ExtFS volume, reads a JSON-ish command, and dispatches an AppleEvent (`'aevt'`/`'odoc'` to a creator code, or `'misc'`/`'dosc'` to MacPerl with a Perl source payload). The host never has to construct a 68k context — the guest does the launch under its own Process Manager.

```
Host API                           BridgeAgent (guest app)
────────                           ───────────────────────
POST /api/launch
  → write _bridge_cmd to ExtFS
  → poll _bridge_resp
                                   WaitNextEvent loop reads _bridge_cmd
                                   Resolve creator (Desktop DB walk)
                                   Send AppleEvent to MacPerl/Finder
                                   Write _bridge_resp
  ← return JSON
```

### Transport — magic filenames in ExtFS

`src/core/extfs.cpp` intercepts a small set of filenames in the shared volume and routes I/O to in-memory buffers instead of the host directory:

- `_bridge_cmd` — host writes, guest reads
- `_bridge_resp` — guest writes, host reads
- `_bridge_heartbeat` — guest pings, host watches for liveness

Real files land on the host filesystem normally; only this `_bridge_*` namespace is intercepted.

## Shared Memory Transport (Fork Mode)

The webserver runs in the parent process; the CPU runs in a forked child. They communicate through `SharedState` (MAP_SHARED anonymous mmap):

**Passive fields** (child writes at 60Hz, parent reads instantly):
- `cur_app_name[32]` — CurApName, updated every tick

**Command queue** (SPSC ring buffer):
- `cmd_queue[16]` — parent writes commands
- `result_queue[16]` — child writes results
- Atomic read/write positions for lock-free operation

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/app` | GET | Current app name (passive field) |
| `/api/windows` | GET | Window list (command queue) |
| `/api/wait` | POST | Poll condition: `boot=Finder`, `app=Name` |
| `/api/launch` | POST | Launch app or open document: `{"path":"Host:foo.pl","open":true}` |
| `/api/keypress` | POST | Send key event: `{"key": "return"}` |
| `/api/mouse` | POST | Move cursor: `{"x":N,"y":N}` |

## Files

| File | Role |
|------|------|
| `src/core/command_bridge.h` | Command/Result structs, CommandBridge class |
| `src/core/command_bridge.cpp` | Read commands, SHM queue plumbing |
| `src/core/extfs.cpp` | `_bridge_*` filename interception |
| `src/webserver/api_handlers.cpp` | HTTP endpoint handlers |
| `tests/guest/bridge/bridge_agent.c` | BridgeAgent source (Retro68, m68k) |
| `tests/guest/bridge/BridgeAgent.bin` | Pre-built MacBinary (committed) |
| `provisioning/install_bridge_agent.sh` | hfsutils install into Startup Items |
| `tests/guest/MacTestSuite.pl` | MacPerl test script (runs on m68k & PPC) |
| `tests/guest/install_perl_test.py` | Lay out `.pl` + `.finf` for ExtFS |
| `tests/test_guest_suite.sh` | Boot → dispatch script → collect results |

## Running the Guest Suite

```bash
ctest --test-dir build -L guest             # m68k + PPC
ctest --test-dir build -R guest_suite       # m68k only
ctest --test-dir build -R guest_suite_ppc   # PPC (kpx + Mac OS 9)
```

Both invoke `tests/test_guest_suite.sh`, which boots the emulator with `--extfs` pointing at a temp dir containing `MacTestSuite.pl`, waits for desktop, posts to `/api/launch` with `Host:MacTestSuite.pl`, and reads `test_results.txt` back from the same shared dir.
