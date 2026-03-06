# Appliance Layer Design

Abstracting Mac OS 7.5.5 behind a programmatic API so the emulator behaves like an appliance — launch apps, take screenshots, read system state, control input — without users needing to understand the Mac desktop.

---

## What Already Works

| Capability | Mechanism | Endpoint / Function |
|---|---|---|
| Detect current app | `CurApName` at `0x0910` (Pascal string) | `GET /api/status` → `boot_phase` |
| Mouse position (read) | Low-memory globals `0x828`/`0x82a` | `GET /api/mouse` → `{x, y}` |
| Mouse position (set) | `ADBMouseMoved(x, y)` absolute mode | WebRTC data channel only |
| Keyboard input | `ADBKeyDown(code)` / `ADBKeyUp(code)` | WebRTC data channel only |
| Screenshot | Triple-buffer snapshot → fpng | `GET /api/screenshot` → PNG |
| Boot phase tracking | `boot_progress.cpp` milestone detection | `GET /api/status` |
| Shutdown (hard) | `QuitEmulator()` or `M68K_EMUL_OP_SHUTDOWN` | Process exit |
| Start/stop CPU | Atomic flag + condition variable | `POST /api/emulator/start\|stop` |

---

## Tier 1: HTTP API for Existing Functions

These need only new API endpoints — the underlying C functions already exist in `adb.cpp`.

### POST /api/mouse

Set absolute cursor position.

```json
{"x": 100, "y": 200}
```

Calls `ADBMouseMoved(x, y)` with `relative_mouse = false`.

### POST /api/click

Move cursor and click.

```json
{"x": 100, "y": 200, "button": 0, "double": false}
```

Sequence: `ADBMouseMoved(x, y)` → `ADBMouseDown(button)` → short delay → `ADBMouseUp(button)`. For double-click, repeat. Button 0 = left, 1 = middle, 2 = right.

### POST /api/key

Press or release a key.

```json
{"keycode": 12, "down": true}
```

Calls `ADBKeyDown(code)` or `ADBKeyUp(code)`. Mac ADB keycodes (0x00–0x7F).

### POST /api/type

Type a string by converting ASCII to ADB keycode sequences.

```json
{"text": "Hello World"}
```

Converts each character to its ADB keycode + shift state, queues press/release pairs with inter-key delay. Needs a lookup table (ASCII → ADB keycode + modifiers).

### POST /api/keycombo

Send a keyboard shortcut.

```json
{"modifiers": ["cmd"], "key": "q"}
```

Hold modifier(s), press key, release all. Common combos: Cmd+Q (quit), Cmd+O (open), Cmd+W (close window), Cmd+Shift+3 (screenshot to disk).

### GET /api/app

Return current application info.

```json
{
  "name": "Finder",
  "refnum": 2,
  "a5": "0x01F8A000",
  "stack_base": "0x01FA0000"
}
```

Reads low-memory globals:

| Address | Name | Type | Description |
|---|---|---|---|
| `0x0910` | CurApName | Str31 | Current app name (Pascal string) |
| `0x0900` | CurApRefNum | int16 | Resource file ref number |
| `0x0904` | CurrentA5 | uint32 | App's A5 world pointer |
| `0x0908` | CurStackBase | uint32 | App's stack base |
| `0x02AA` | ApplZone | uint32 | App's heap zone pointer |

---

## Tier 2: System State Introspection

Reading Mac OS data structures from host memory. No traps needed — just `ReadMacInt*()` on known addresses.

### GET /api/windows

Walk the `WindowList` linked list and return all windows.

```json
[
  {"title": "Macintosh HD", "rect": {"top": 40, "left": 10, "bottom": 300, "right": 400}, "visible": true, "hilited": true},
  {"title": "Untitled", "rect": {"top": 60, "left": 50, "bottom": 200, "right": 350}, "visible": true, "hilited": false}
]
```

**WindowList** starts at `0x09D6` (pointer to first WindowRecord). Each WindowRecord:

| Offset | Field | Type | Notes |
|---|---|---|---|
| +0 | portRect | Rect (8 bytes) at GrafPort+8 | Window bounds (top, left, bottom, right) |
| +0x6C | windowKind | int16 | Negative = desk accessory, positive = app window |
| +0x6E | visible | Boolean | Window is visible |
| +0x6F | hilited | Boolean | Window is active/frontmost |
| +0x88 | titleHandle | StringHandle | Dereference twice: handle → pointer → Pascal string |
| +0x90 | nextWindow | WindowPeek | Pointer to next window in list (0 = end) |

For Color windows (CGrafPort), bit 14 of `rowBytes` at GrafPort+4 is set, and offsets shift by 48 bytes (CGrafPort is 156 bytes vs GrafPort's 108).

### GET /api/menus

Read the menu bar structure.

| Address | Name | Description |
|---|---|---|
| `0x0A1C` | MenuList | Handle to array of menu handles |
| `0x0A20` | MBarEnable | Bitmask of enabled menus |
| `0x0A26` | TheMenu | ID of currently highlighted menu |
| `0x0BAA` | MBarHeight | Menu bar height in pixels |

Each MenuHandle dereferences to a MenuInfo record containing the menu title (Pascal string) and item list.

### GET /api/memory

Raw memory read for debugging and introspection.

```
GET /api/memory?addr=0x0910&len=32
```

Returns hex dump or JSON array of bytes. Uses `ReadMacInt8()` in a loop.

### POST /api/memory

Raw memory write.

```json
{"addr": "0x0910", "bytes": [6, 70, 105, 110, 100, 101, 114]}
```

Uses `WriteMacInt8()`. Dangerous — no validation. Useful for patching globals.

---

## Tier 3: Mac OS Trap Calls

Calling Mac OS Toolbox/OS traps from the host. Requires `Execute68kTrap()` which can only run inside an EmulOp handler — specifically the 60Hz IRQ (`M68K_EMUL_OP_IRQ`).

### Architecture: Command Queue

```
HTTP API → enqueue command (host-side struct)
          ↓
60Hz IRQ → dequeue, call Execute68kTrap()
          ↓
          write result to response struct
          ↓
HTTP API → return result (poll or block)
```

The IRQ handler already fires every ~16ms. Adding a command queue check is a few lines of code.

### Launch Application

Build a `LaunchParamBlockRec` in Mac memory, call `_Launch` (trap `A9F2`).

```json
POST /api/launch
{"path": "Macintosh HD:Applications:SimpleText"}
```

Steps:
1. Allocate Mac heap memory for FSSpec + LaunchParamBlockRec
2. Populate FSSpec with volume ref, directory ID, filename
3. Set `launchBlockID` = `0x4C43` ('LC'), control flags
4. Call `Execute68kTrap(0xA9F2, &regs)` from IRQ handler
5. Free allocated memory

**Requires**: Resolving the file path to an FSSpec. Could use `_FSMakeFSSpec` (trap `A9F1`) first, or pre-compute volume/directory IDs.

### Graceful Shutdown

```json
POST /api/shutdown
{"mode": "shutdown"}  // or "restart"
```

Call `_Shutdown` trap (`A895`) with D0 = 1 (power off) or D0 = 2 (restart). This runs the full shutdown sequence: notifies drivers, unmounts volumes, runs the shutdown queue at `0x0BBC`.

### Quit Current App

```json
POST /api/quit
```

Two approaches:
- **Simple**: Synthesize Cmd+Q via `ADBKeyDown(0x37)` + `ADBKeyDown(0x0C)` (works for most apps)
- **Direct**: Call `_ExitToShell` (trap `A9F4`) from IRQ handler (kills current app immediately, returns to Finder)

### Send Apple Event

The most powerful but most complex mechanism. System 7.5.5 fully supports Apple Events.

```json
POST /api/appleevent
{
  "target": "FNDR",
  "class": "aevt",
  "id": "odoc",
  "params": {"----": "Macintosh HD:ReadMe"}
}
```

Implementation: Build a small 68k code stub in Mac memory that calls `AECreateAppleEvent()` → `AEPutParamDesc()` → `AESend()`. Execute it via `Execute68k()`.

Useful events:
- `aevt/odoc` — Open document (Finder opens file with correct app)
- `aevt/quit` — Quit application (polite, allows save dialog)
- `FNDR/shut` — Finder shutdown
- `FNDR/rest` — Finder restart

---

## Key Low-Memory Globals Reference

### Application State

| Address | Name | Size | Description |
|---|---|---|---|
| `0x0910` | CurApName | 32 | Current app name (Pascal string, len + 31 chars) |
| `0x0900` | CurApRefNum | 2 | Current app resource file ref number |
| `0x0902` | LaunchFlag | 1 | Launch state flag |
| `0x0904` | CurrentA5 | 4 | Current app's A5 world pointer |
| `0x0908` | CurStackBase | 4 | Current app's stack base |
| `0x02AA` | ApplZone | 4 | Pointer to current app's heap zone |
| `0x0130` | ApplLimit | 4 | App heap limit |
| `0x0AEC` | AppParmHandle | 4 | Finder info handle (files to open/print) |

### Mouse / Cursor

| Address | Name | Size | Description |
|---|---|---|---|
| `0x0828` | MTemp.v | 2 | Raw mouse Y |
| `0x082A` | MTemp.h | 2 | Raw mouse X |
| `0x082C` | RawMouse.v | 2 | Unprocessed mouse Y |
| `0x082E` | RawMouse.h | 2 | Unprocessed mouse X |
| `0x0830` | Mouse.v | 2 | Processed mouse Y (what apps see) |
| `0x0832` | Mouse.h | 2 | Processed mouse X |
| `0x0172` | MBState | 1 | Mouse button state (bit 7: 0=down, 1=up) |
| `0x08CE` | CrsrNew | 1 | Trigger for cursor redraw |
| `0x08CF` | CrsrCouple | 1 | Cursor coupling flag |
| `0x08D0` | CrsrState | 2 | Cursor hide count (0=visible, <0=hidden) |

### Windows / Menus

| Address | Name | Size | Description |
|---|---|---|---|
| `0x09D6` | WindowList | 4 | Pointer to first WindowRecord |
| `0x0A1C` | MenuList | 4 | Handle to menu list |
| `0x0A20` | MBarEnable | 4 | Bitmask of enabled menus |
| `0x0A26` | TheMenu | 2 | Currently highlighted menu ID |
| `0x0BAA` | MBarHeight | 2 | Menu bar height in pixels |
| `0x09EE` | GrayRgn | 4 | Desktop region handle |

### System State

| Address | Name | Size | Description |
|---|---|---|---|
| `0x012F` | CPUFlag | 1 | CPU type (0=68000, 1=010, 2=020, 3=030, 4=040) |
| `0x016A` | Ticks | 4 | Tick count (incremented 60x/sec) |
| `0x020C` | Time | 4 | Seconds since midnight Jan 1, 1904 |
| `0x0CFC` | WarmStart | 4 | `0x574C5343` ('WLSC') when Mac OS is initialized |
| `0x014A` | EventQueue | 4 | Pointer to OS event queue header |
| `0x0BBC` | ShutDwnQHdr | 4 | Shutdown queue header |

### Traps Used by Appliance Layer

| Trap | Number | Purpose |
|---|---|---|
| `_Launch` | `A9F2` | Launch application from FSSpec |
| `_ExitToShell` | `A9F4` | Terminate current app, return to Finder |
| `_Shutdown` | `A895` | System shutdown (D0: 1=off, 2=restart) |
| `_PostEvent` | `A02F` | Post event to OS event queue |
| `_FSMakeFSSpec` | `A9F1` | Build FSSpec from path components |
| `_GetFrontProcess` | `A831` | Get frontmost process serial number |
| `_AESend` | `A816` | Send Apple Event |

---

## Implementation Order

1. **Tier 1 HTTP endpoints** — mouse, click, key, type, keycombo, app info. Pure wiring to existing ADB functions. Immediately scriptable via curl.

2. **Tier 2 introspection** — windows, menus, memory peek/poke. Read-only memory walking. Enables building a "what's on screen" API.

3. **Command queue in IRQ handler** — the plumbing for Tier 3. Check a host-side queue on each 60Hz tick, dispatch to `Execute68kTrap()`.

4. **Tier 3 trap calls** — launch app, graceful shutdown, quit app. Requires constructing Mac data structures in guest memory.

5. **Apple Events** — the endgame. Full scriptability: open documents, quit apps, shutdown, any inter-app communication System 7.5.5 supports.

---

## Constraints

- **`Execute68kTrap()` only works inside EmulOp handlers** — specifically the 60Hz IRQ. All Mac OS calls must go through the command queue.
- **Mac memory allocation** — constructing FSSpecs, LaunchParamBlockRecs, AEDescs requires allocating memory in the Mac heap. Can use `_NewPtr` trap or reserve a scratch area.
- **Timing** — commands execute on the next 60Hz tick (~16ms latency). Good enough for automation, not for real-time control.
- **Window struct offsets differ** between classic GrafPort (108 bytes) and Color CGrafPort (156 bytes). Must check bit 14 of `rowBytes` to detect which.
- **Pascal strings** — all Mac OS strings are length-prefixed (first byte = length, no null terminator).
