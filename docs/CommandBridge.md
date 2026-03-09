# Command Bridge

Host-side command dispatcher for controlling the running Mac OS. Provides API endpoints for querying app state, window lists, and executing actions like launching apps.

## Architecture

The command bridge has two layers:

### 1. Read Commands (IRQ context)

Read commands peek Mac memory directly from the 60Hz IRQ handler. No Toolbox calls needed, safe from any context.

- **GET_APP_NAME** — reads CurApName at low memory 0x0910
- **GET_WINDOW_LIST** — walks WindowRecord linked list at 0x09D6
- **GET_TICKS** — reads Ticks counter at 0x016A
- **READ_MEMORY** — hex dump of arbitrary Mac address

### 2. Action Commands (application context via jGNEFilter)

Action commands (_Launch, _ExitToShell) require full Toolbox context — they can't run from an interrupt handler. These use a two-phase handoff:

```
Parent (webserver)                 Child (CPU)
    │                                  │
    ├─ POST /api/launch ──►            │
    │   write to SHM cmd queue         │
    │                                  │
    │                           60Hz IRQ:
    │                             validate file (_GetFileInfo)
    │                             write path to ScratchMem mailbox
    │                             set cmd_flag = 1
    │                             write result to SHM result queue
    │                                  │
    │                           App event loop (GetNextEvent):
    │                             jGNEFilter checks mailbox
    │                             cmd_flag set → EmulOp 0x7139
    │                             → command_bridge_dispatch()
    │                             → _Launch in app context (safe!)
    │                                  │
    │   ◄── read result from SHM       │
    ├─ return JSON response            │
```

## jGNEFilter (the "INIT")

A 28-byte 68k program injected into ScratchMem at boot. Not a real INIT — no disk file, no resource fork. The host writes machine code directly into memory and hooks `jGNEFilter` (low memory global 0x29A).

`jGNEFilter` is called every time any Mac app calls `GetNextEvent` or `WaitNextEvent` — this is application context, where all Toolbox calls are safe.

The filter code:

```asm
MOVEA.L  #0x02100800, A0   ; mailbox address
TST.B    (A0)               ; cmd_flag set?
BEQ.S    chain              ; no → skip (5 cycles overhead)
DC.W     0x7139             ; EmulOp CMD_DISPATCH (host handles it)
chain:
MOVEA.L  oldFilter(PC), A0  ; chain to previous filter
TST.L    A0
BEQ.S    done
JMP      (A0)
done:
RTS
oldFilter: DC.L 0           ; saved previous filter pointer
```

Installed by `install_jgne_filter()` on the first IRQ tick after boot.

## Shared Memory Transport (Fork Mode)

The webserver runs in the parent process; the CPU runs in a forked child. They communicate through `SharedState` (MAP_SHARED anonymous mmap):

**Passive fields** (child writes at 60Hz, parent reads instantly):
- `cur_app_name[32]` — CurApName, updated every tick

**Command queue** (SPSC ring buffer):
- `cmd_queue[16]` — parent writes commands
- `result_queue[16]` — child writes results
- Atomic read/write positions for lock-free operation

**ScratchMem mailbox** (for action commands):
- `0x02100800 + 0x00`: cmd_flag (uint8)
- `0x02100800 + 0x01`: result_flag (uint8)
- `0x02100800 + 0x02`: cmd_type (int16)
- `0x02100800 + 0x04`: result_err (int16)
- `0x02100800 + 0x06`: cmd_arg (Pascal string, 256 bytes)

## API Endpoints

| Endpoint | Method | Description | Status |
|----------|--------|-------------|--------|
| `/api/app` | GET | Current app name (passive field) | ✓ Working |
| `/api/windows` | GET | Window list (command queue) | ✓ Working |
| `/api/wait` | POST | Poll condition: `boot=Finder`, `app=Name` | ✓ Working |
| `/api/launch` | POST | Launch app: `{"path": "HD:SimpleText"}` | ⚠ WIP |
| `/api/quit` | POST | Quit current app | ⚠ WIP |

### Launch status

File validation works (`_GetFileInfo` in IRQ context). The jGNEFilter and EmulOp dispatch work. The remaining issue is the `_Launch` trap calling convention — the old-style A0-pointer form bombs on System 7. Needs the extended `LaunchParamBlockRec` with FSSpec, or a cross-compiled 68k helper routine.

## Files

| File | Role |
|------|------|
| `src/core/command_bridge.h` | Command/Result structs, CommandBridge class, CmdType enum |
| `src/core/command_bridge.cpp` | All bridge logic: reads, mailbox, jGNEFilter install, SHM drain |
| `src/core/emul_op.cpp` | EmulOp handlers: IRQ drain + CMD_DISPATCH |
| `src/common/include/emul_op.h` | M68K_EMUL_OP_CMD_DISPATCH (0x7139) |
| `src/core/shared_state.h` | ShmCommand/ShmResult queues, cur_app_name, helpers |
| `src/webserver/api_handlers.cpp` | HTTP endpoint handlers |
| `tests/test_command_bridge.sh` | Integration tests (7 checks) |

## Next Steps

1. **Fix _Launch**: Use extended LaunchParamBlockRec with FSSpec and `launchContinue` flag, or cross-compile a 68k helper with Retro68
2. **Apple Events**: Send `kAEOpenDocuments` to Finder instead of calling _Launch directly — the proper System 7 way
3. **More commands**: Send keystrokes, click at coordinates, menu selection
4. **Unix socket**: Terse line protocol for shell scripting (`echo "app" | socat - UNIX:/tmp/mac-phoenix.sock`)
