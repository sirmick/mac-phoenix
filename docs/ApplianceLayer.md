# Appliance Layer Design

Turn MacPhoenix into a programmable appliance: boot Mac OS, launch apps, control input, read state, run tests — all without touching the desktop. Support both CLI scripting and web UI.

---

## Goals

1. **Command interface** — control a running emulator via CLI or web UI through a single command dispatcher
2. **Cross-compile Mac apps** — build 68k Mac binaries on the Linux host with Retro68
3. **File injection** — get compiled binaries into the running Mac filesystem
4. **Automated testing** — boot → bypass dialogs → launch test app → assert results → exit
5. **Appliance mode** — hide Mac OS, run a custom app full-screen as if the emulator IS the app

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    mac-phoenix process                   │
│                                                         │
│  ┌───────────────────────────────────────────────────┐  │
│  │              Command Dispatcher                    │  │
│  │  thread-safe queue, drained by 60Hz IRQ handler   │  │
│  │  execute: immediate (memory read) or deferred     │  │
│  │           (trap call on next IRQ tick)             │  │
│  └──────────▲─────────────────────▲──────────────────┘  │
│             │                     │                      │
│  ┌──────────┴──────────┐  ┌──────┴───────────────────┐  │
│  │ Unix Socket Server  │  │ HTTP API (:8080)          │  │
│  │ /tmp/mac-phoenix.sock│  │ /api/* endpoints         │  │
│  │ line protocol        │  │ JSON request/response    │  │
│  └──────────▲──────────┘  └──────────▲───────────────┘  │
└─────────────┼────────────────────────┼───────────────────┘
              │                        │
   ┌──────────┴──────────┐  ┌─────────┴─────────┐
   │  mac-phoenix-ctl    │  │  Web UI / curl     │
   │  $ ctl click 100 200│  │  browser / scripts │
   └─────────────────────┘  └───────────────────┘
```

**One dispatcher, two frontends.** The Unix socket speaks a terse line protocol for CLI use. The HTTP endpoints accept JSON for web UI and curl. Both enqueue into the same command queue.

---

## Command Interface

### Unix Socket Line Protocol

Connect: `socat - UNIX:/tmp/mac-phoenix.sock` or use `mac-phoenix-ctl` wrapper.

```
command [args...]     → JSON or plain text response
                      → OK on success, ERR: message on failure
```

### Commands

#### Input

| Command | Example | Description |
|---------|---------|-------------|
| `click X Y [double]` | `click 100 200` | Move cursor and click |
| `mouse X Y` | `mouse 300 150` | Set cursor position (no click) |
| `key KEYCODE down\|up` | `key 0x0C down` | Press/release by ADB keycode |
| `type TEXT` | `type Hello World` | Type ASCII string (auto keycode conversion) |
| `keycombo MOD... KEY` | `keycombo cmd q` | Keyboard shortcut |

#### Introspection

| Command | Example | Response |
|---------|---------|----------|
| `app` | `app` | `Finder` (current app name) |
| `windows` | `windows` | JSON array of window titles, rects, visibility |
| `menus` | `menus` | JSON array of menu bar contents |
| `status` | `status` | `{"boot_phase":"desktop","ticks":184021}` |
| `memory ADDR [LEN]` | `memory 0x0910 32` | Hex dump |

#### Control

| Command | Example | Description |
|---------|---------|-------------|
| `launch PATH` | `launch "Macintosh HD:myapp"` | Launch app via _Launch trap |
| `quit` | `quit` | Quit current app (Cmd+Q or _ExitToShell) |
| `shutdown` | `shutdown` | Graceful shutdown via _Shutdown trap |
| `screenshot [PATH]` | `screenshot /tmp/shot.png` | Save PNG (or return base64) |
| `inject HOST_PATH MAC_PATH` | `inject ./myapp.bin "Macintosh HD:myapp"` | Copy file into Mac filesystem |
| `wait CONDITION [TIMEOUT]` | `wait app=Finder 30` | Block until condition met |

#### Wait Conditions

`wait` is the key primitive for test automation:

```
wait boot=desktop 60        # wait for Finder desktop (up to 60s)
wait app=SimpleText 10      # wait for app to become current
wait window="Untitled" 5    # wait for window with title to appear
wait idle 5                 # wait for Mac to be idle (tick count advancing, no disk activity)
```

Returns `OK` on condition met, `ERR: timeout` on expiry.

### mac-phoenix-ctl CLI

Thin wrapper that connects to the socket, sends `argv` as the command, prints the response, and exits:

```bash
$ mac-phoenix-ctl click 100 200
OK
$ mac-phoenix-ctl app
Finder
$ mac-phoenix-ctl screenshot /tmp/shot.png
OK /tmp/shot.png
```

Implemented as ~50 lines of C (or even a shell function wrapping socat).

### HTTP Endpoints (for web UI)

Same commands, JSON interface. Web UI calls these via `fetch()`.

| Endpoint | Method | Body | Maps to |
|----------|--------|------|---------|
| `/api/click` | POST | `{"x":100,"y":200,"double":false}` | `click 100 200` |
| `/api/mouse` | POST | `{"x":100,"y":200}` | `mouse 100 200` |
| `/api/key` | POST | `{"keycode":12,"down":true}` | `key 0x0C down` |
| `/api/type` | POST | `{"text":"Hello"}` | `type Hello` |
| `/api/keycombo` | POST | `{"modifiers":["cmd"],"key":"q"}` | `keycombo cmd q` |
| `/api/app` | GET | — | `app` |
| `/api/windows` | GET | — | `windows` |
| `/api/menus` | GET | — | `menus` |
| `/api/launch` | POST | `{"path":"Macintosh HD:SimpleText"}` | `launch ...` |
| `/api/inject` | POST | multipart file upload + mac_path | `inject ...` |
| `/api/wait` | POST | `{"condition":"app=Finder","timeout":30}` | `wait ...` |

---

## Cross-Compiling Mac Apps

### Toolchain: Retro68

Retro68 is a GCC-based cross-compiler for classic Mac OS 68k. Runs on Linux, produces MacBinary files.

```bash
# One-time setup
git clone https://github.com/autc04/Retro68.git
cd Retro68 && mkdir build && cd build
../build-toolchain.sh --target-m68k

# Compile a Mac app
m68k-apple-macos-gcc -o myapp myapp.c -lRetroConsole
```

### Test App Structure

Test apps are small Mac programs that exercise a specific subsystem, report results, then quit. They follow a common pattern:

```c
// tests/mac-apps/test_tcpip.c
#include <MacTCP.h>
#include <Devices.h>

// Result convention: write status to a known memory address
// or to a file at a known path, then quit
#define RESULT_ADDR 0x02100000  // ScratchMem — host can read this

int main() {
    short refNum;
    OSErr err;

    // Open MacTCP driver
    err = OpenDriver("\p.IPP", &refNum);

    // Write result code to scratch memory
    *(int32_t*)RESULT_ADDR = (int32_t)err;

    // Quit back to Finder
    ExitToShell();
    return 0;
}
```

**Result reporting options** (simplest to most complex):

1. **ScratchMem convention** — write result to `0x02100000` (ScratchMem, 64KB, not used by Mac OS). Host reads it via `memory 0x02100000 4`. Zero dependencies, works for simple pass/fail.

2. **File on disk** — write results to `Macintosh HD:test_results.txt`. Host reads via extfs shared folder or `inject`/extract commands. Good for structured output.

3. **Serial port** — write to the emulated serial port, host captures. Needs serial emulation wired up.

4. **Custom EmulOp** — add a trap opcode that the test app calls to signal results directly to the host. Most reliable, ~10 lines of host code.

### Recommended: Custom EmulOp for Test Reporting

Add a dedicated trap (e.g., `M68K_EMUL_OP_TEST_RESULT`) that test apps call to report results:

```c
// Mac-side (test app):
// A-line trap 0xAEF0 (or whatever is next available)
// D0 = result code, A0 = pointer to message string
asm("move.l %0, %%d0" : : "g"(result_code));
asm("move.l %0, %%a0" : : "g"(message));
asm(".short 0xAEF0");  // trigger EmulOp

// Host-side (emul_op.cpp):
case M68K_EMUL_OP_TEST_RESULT:
    test_result_code = m68k_dreg(regs, 0);
    test_result_msg = ReadMacString(m68k_areg(regs, 0));
    test_complete.store(true);
    break;
```

This way the host knows the instant a test finishes, with no polling.

---

## File Injection

Getting compiled binaries into the Mac filesystem:

### Option A: extfs Shared Folder (already in codebase)

`src/core/extfs.cpp` implements a virtual Mac volume backed by a host directory. BasiliskII used this as "Unix" or "Host" volume.

```bash
# Configure a shared directory
mac-phoenix --extfs /home/mick/mac-shared /home/mick/quadra.rom

# Drop files in from host side — they appear as a Mac volume
cp myapp.bin /home/mick/mac-shared/
# Mac sees: "Host:myapp"
```

**Status**: extfs.cpp is in the build but may need wiring up to the current config system and testing. This is the lowest-friction option — no disk image manipulation, no restarts.

### Option B: hfsutils (offline disk image manipulation)

```bash
hmount /path/to/disk.img
hcopy myapp.bin ":myapp"
humount
# Requires emulator restart or disk re-mount
```

Works but requires a restart cycle. Fine for initial disk image preparation, not for iterative development.

### Option C: inject command (runtime, via command dispatcher)

The `inject` command would write file data into the Mac filesystem at runtime, using extfs or by constructing HFS catalog entries. Extfs is far simpler.

---

## Automated Test Pipeline

### Test Flow

```bash
#!/bin/bash
# tests/integration/test_tcpip.sh

# 1. Compile test app
m68k-apple-macos-gcc -o tests/mac-apps/build/test_tcpip \
    tests/mac-apps/test_tcpip.c

# 2. Stage binary in shared folder
cp tests/mac-apps/build/test_tcpip /home/mick/mac-shared/

# 3. Boot emulator headless with clean PRAM (suppresses dirty disk dialog)
./build/mac-phoenix --no-webserver --zappram --extfs /home/mick/mac-shared \
    --timeout 60 /home/mick/quadra.rom &
EMU_PID=$!

# 4. Wait for desktop
mac-phoenix-ctl wait boot=desktop 30

# 5. Launch test app
mac-phoenix-ctl launch "Host:test_tcpip"

# 6. Wait for test to complete (custom EmulOp sets flag)
mac-phoenix-ctl wait test-complete 15

# 7. Read result
RESULT=$(mac-phoenix-ctl memory 0x02100000 4)
kill $EMU_PID

# 8. Assert
if [ "$RESULT" = "00000000" ]; then
    echo "PASS: MacTCP opened successfully"
    exit 0
else
    echo "FAIL: MacTCP returned error $RESULT"
    exit 1
fi
```

### Meson Integration

```meson
# Test apps (cross-compiled separately, checked into build or built by script)
test('tcpip',
  find_program('tests/integration/test_tcpip.sh'),
  timeout: 60,
  suite: 'integration',
)
```

### What to Test

| Test | Exercises | Pass condition |
|------|-----------|----------------|
| `test_tcpip` | Open MacTCP driver | `OpenDriver` returns noErr |
| `test_file_io` | Create/write/read/delete file | All File Manager calls succeed |
| `test_memory` | Allocate/use/free heap memory | NewPtr/DisposePtr work correctly |
| `test_resource` | Open/read resource fork | GetResource returns valid handle |
| `test_quickdraw` | Draw to offscreen GWorld | No crash, known pixel values |
| `test_sound` | Open Sound Manager | SndNewChannel succeeds |
| `test_scsi` | SCSI Manager inquiry | Returns device info |

---

## Appliance Mode

Hide Mac OS entirely. The emulator boots, launches a specific app, and presents only that app's UI.

### CLI

```bash
./build/mac-phoenix --appliance "Macintosh HD:MyApp" /home/mick/quadra.rom
```

### What --appliance Does

1. **Boot with zapped PRAM** — suppress shutdown dialog automatically
2. **Wait for desktop** — detect Finder via boot phase tracking
3. **Launch the target app** — via _Launch trap through command dispatcher
4. **Auto-quit on app exit** — if CurApName changes back to "Finder", shut down emulator

The Mac desktop, menu bar, and system chrome all remain as-is — the app is a real Mac app running in a real Mac environment. The "appliance" part is just the automation: boot, launch, and lifecycle management.

### Appliance Config

```json
{
  "appliance": {
    "app": "Macintosh HD:MyApp",
    "auto_quit": true
  }
}
```

---

## Implementation Order

### Phase 1: Command Dispatcher + CLI

1. Command dispatcher (thread-safe queue + 60Hz drain)
2. Unix socket server thread (~150 lines)
3. Wire existing functions: `click`, `mouse`, `key`, `type`, `keycombo`, `status`, `screenshot`
4. `mac-phoenix-ctl` CLI wrapper (~50 lines)
5. HTTP endpoints that delegate to same dispatcher

### Phase 2: Introspection

6. `app` — read CurApName from low-memory global
7. `windows` — walk WindowList linked list
8. `menus` — read MenuList structure
9. `memory` — peek/poke via ReadMacInt/WriteMacInt
10. `wait` — polling loop with condition parser

### Phase 3: File Injection + Cross-Compilation

11. Wire up extfs to current config system (--extfs flag)
12. Test extfs actually works with current memory layout
13. Set up Retro68 toolchain, build first test app
14. `inject` command (copy file into extfs directory)

### Phase 4: Trap Calls

15. `Execute68kTrap()` integration in IRQ handler (command queue drain)
16. `launch` — build FSSpec + LaunchParamBlockRec, call _Launch
17. `quit` — _ExitToShell trap
18. `shutdown` — _Shutdown trap
19. Test result EmulOp (M68K_EMUL_OP_TEST_RESULT)

### Phase 5: Appliance Mode

20. `--appliance` flag: auto-boot + auto-launch + auto-quit
21. Integration test suite using the full pipeline

---

## Existing Infrastructure

| What | Status | Location |
|------|--------|----------|
| ADB mouse/keyboard functions | Working | `src/core/adb.cpp` |
| 60Hz IRQ handler | Working | `src/core/emul_op.cpp` (M68K_EMUL_OP_IRQ) |
| Boot phase detection | Working | `src/core/boot_progress.cpp` |
| Zap PRAM (suppress dirty dialog) | Working | `--zappram` flag, `emulator_init.cpp` |
| extfs virtual volume | In build, needs testing | `src/core/extfs.cpp` |
| HTTP API server | Working | `src/webserver/api_handlers.cpp` |
| Screenshot/framebuffer | Working | `GET /api/screenshot` |
| Execute68k | Exists in legacy code | Needs verification in current codebase |
| ScratchMem (64KB @ 0x02100000) | Allocated | `src/core/cpu_context.cpp` |

---

## Key Low-Memory Globals

### Application State

| Address | Name | Size | Description |
|---------|------|------|-------------|
| `0x0910` | CurApName | 32 | Current app name (Pascal string) |
| `0x0900` | CurApRefNum | 2 | Current app resource file ref number |
| `0x0904` | CurrentA5 | 4 | App's A5 world pointer |
| `0x0908` | CurStackBase | 4 | App's stack base |

### Windows / Menus

| Address | Name | Size | Description |
|---------|------|------|-------------|
| `0x09D6` | WindowList | 4 | Pointer to first WindowRecord |
| `0x0A1C` | MenuList | 4 | Handle to menu list |
| `0x0BAA` | MBarHeight | 2 | Menu bar height (set to 0 to hide) |

### System

| Address | Name | Size | Description |
|---------|------|------|-------------|
| `0x016A` | Ticks | 4 | Tick count (60x/sec) |
| `0x020C` | Time | 4 | Seconds since 1904-01-01 |
| `0x0CFC` | WarmStart | 4 | `0x574C5343` when Mac OS initialized |

---

## Constraints

- **`Execute68kTrap()` only works inside EmulOp handlers** — all Mac OS calls go through the command queue, executed on the 60Hz IRQ tick (~16ms latency)
- **Pascal strings** — all Mac OS strings are length-prefixed (first byte = length)
- **Color vs classic GrafPort** — check bit 14 of `rowBytes` to determine WindowRecord layout
- **extfs resource forks** — Retro68 outputs MacBinary which includes resource fork; extfs stores resource forks as `._filename` AppleDouble files on the host
- **Test app linking** — Retro68 apps need proper startup code to call InitGraf/InitFonts/etc. or use RetroConsole for minimal apps
