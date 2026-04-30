# MacBrowser

`--browser` runs a host-side Firefox on a virtual X server (Xvfb), captures
its rendered pixels via XShm + XDamage, converts them to RGB555-BE, and
streams them through a guest-allocated shared-memory region into a
guest-native Mac app called **MacBrowser**. The guest sees a normal
classic-Mac window with a URL bar, back / forward / stop / reload toolbar,
status strip, and a scrollable PixMap viewport. Mouse, keys, scroll, paste,
and select-all are forwarded to Firefox over WebDriver-BiDi. Downloads land
in the guest filesystem via the existing ExtFS share. Cookies / logins /
prefs survive launches because Firefox runs with a persistent profile.

This avoids three intractable problems with running a 1996 browser against
the modern web — TLS handshake compatibility, custom-CA-import UI, and
modern HTML/CSS/JS rendering on a 25 MHz 68040.

## Architecture

```
┌─ mac-phoenix process ────────────────────────────────────┐
│                                                          │
│  ┌─ src/drivers/browser/ ─────────────────────────────┐  │
│  │  supervisor   — spawns + supervises Xvfb + Firefox │  │
│  │  bidi         — WebSocket client → ws://127:9222   │  │
│  │  xshm         — XShm + XDamage on Xvfb root        │  │
│  │                  (COMPOSITE redirects subwindows)  │  │
│  │  shm          — owns BrowserShm region             │  │
│  │  pipeline     — damage → BGRX→RGB555 → dirty rect  │  │
│  │  mouse_poll   — peek LMGetMouse / WindowList /     │  │
│  │                 viewport_scroll, forward via BiDi  │  │
│  │  cmd          — dispatch BR_CMD_* → BiDi calls     │  │
│  │  module       — lifecycle when --browser is set    │  │
│  └────────────────────────────────────────────────────┘  │
│         │                                                │
│         │ BrowserShm (~1.7 MiB) — guest-allocated;       │
│         │ host gets a writable pointer via Mac2HostAddr  │
│         ▼                                                │
│  ┌─ guest VM ─────────────────────────────────────────┐  │
│  │  RAM       @ 0x00000000 (32 MiB)                   │  │
│  │  ROM       @ 0x02000000 (1 MiB)                    │  │
│  │  Scratch   @ 0x02100000 (64 KiB)                   │  │
│  │  FrameBuf  @ 0x02110000 (8 MiB)                    │  │
│  │  BrowserShm — Browser.app's app heap, location     │  │
│  │               published via ExtFS handshake file   │  │
│  └────────────────────────────────────────────────────┘  │
│                                                          │
│  timer_interrupt.cpp 60 Hz tick:                         │
│    BrowserModule::on_pre_vbl()  ← drain ev → h2g ring    │
│    fire VBL into guest CPU                               │
└──────────────────────────────────────────────────────────┘
                         ▲
                         │ xcb (X protocol + MIT-SHM + DAMAGE + COMPOSITE)
                         │ ws  (WebDriver BiDi control)
                         │
        ┌────────────────┴───────────────┐
        │  Xvfb :N  (headless X)         │
        │  Firefox  --kiosk --no-remote  │
        │           --remote-debugging-port=9222 │
        │           --profile <persistent dir>   │
        └────────────────────────────────┘

Inside the guest:

┌─ MacBrowser.app (Retro68 m68k) ──────────────────────────┐
│  Window: URL bar | Back | Fwd | Stop | Reload | viewport │
│  V/H scrollbars; status strip with loading spinner;      │
│  Cmd-L / Cmd-R / Cmd-[ / Cmd-] / Cmd-W / Cmd-Q / Cmd-+/− │
│                                                          │
│  VBL task (interrupt-level, A5 stashed in VBLTask):      │
│    drain h2g ring → set pending_main flag                │
│    drain queued user-events → g2h ring                   │
│                                                          │
│  WaitNextEvent main loop:                                │
│    translate user events → enqueue commands              │
│    if pending_main: handle events; on BR_EV_FRAME,       │
│      CopyBits each fb.dirty[] rect from BrowserShm into  │
│      the window                                          │
└──────────────────────────────────────────────────────────┘
```

Net-bridge is **not** involved — host ↔ guest goes entirely through
`BrowserShm` plus the small ExtFS handshake file.

## The shared memory contract

A single `BrowserShm` struct (~1.7 MiB, mostly framebuffer) lives in
guest memory. Browser.app `NewPtrClear`s it from its own application heap
(SIZE resource ≥ 4 MiB) and writes the buffer's Mac address as ASCII hex
into `Host:MacPhoenix:browser_shm.txt` on the ExtFS share. The host's
shm watcher reads that file, validates `magic == 'BRWS'` and
`version == BR_VERSION`, and translates Mac → host via `Mac2HostAddr()`.
That dodges per-backend banking work — every backend (UAE, Unicorn-m68k,
Unicorn-PPC, KPX) sees ordinary guest RAM, no fixed `BR_BASE_ADDR`, no
`uc_mem_map_ptr` for the region, no UAE `ram_bank` extension.

| Field | Direction | Description |
|---|---|---|
| `magic` | guest writes | `'BRWS'` |
| `version` | guest writes | `BR_VERSION = 1` |
| `flags` | guest writes | capability bits |
| `h2g` | host → guest ring | events (frame-ready, status, downloads, selection, page metrics) |
| `g2h` | guest → host ring | commands (nav, click, key, paste, scroll, zoom, select-all, resize) |
| `log` | guest writes | single-slot lossy debug log; host polls + prints to stderr with `[BrowserGuest <level>]` |
| `viewport_scroll` | guest writes | scroll offset for host-side mouse-poll math |
| `fb.seq` | host writes | bumped per frame; guest detects new frames |
| `fb.dirty[]` | host writes | damage rectangles for the current `seq` |
| `fb.pixels[]` | host writes | RGB555 pixel data, top-down |

Layout, message types, and accessors are in
`src/common/include/MacBrowser.h`. Host defines `BR_HOST` to enable
byte-swap on multi-byte field access; the guest is native big-endian and
the swap is a no-op. `BR_FENCE_RELEASE` / `BR_FENCE_ACQUIRE` macros emit
`__sync_synchronize` on the host, `eieio` / `lwsync` on PPC guests, and
compiler fences on m68k.

### SPSC ring discipline

Each ring is single-producer / single-consumer with separate
`write_idx` / `read_idx` and a 4-byte gap so empty (`write == read`) is
distinguishable from full. Messages are `[u16 type][u16 len][payload]`,
payload padded to 4-byte alignment. A `BR_MSG_WRAP` sentinel
(`type=0, len=0`) jumps the read pointer back to ring offset 0 when a
message wouldn't fit before the buffer end. The producer accounts for
both the WRAP header **and** the unused tail bytes between `write_idx`
and the ring boundary when checking free space — without that accounting,
`write_idx` can land exactly on `read_idx` after a successful push,
which then *looks* empty and silently loses the just-pushed payload.

`tests/test_browser_shm.cpp` (10 cases / 47 assertions, ctest `unit`
label) round-trips messages under wraparound, fill-to-full, interleaved
push/pop, bidirectional independence, oversized rejection, and
truncation reporting. Compiles `ring.cpp` directly so we exercise the
exact code `shm.cpp` links against.

## Wire protocol

| Command (g2h) | Meaning |
|---|---|
| `BR_CMD_NAV` | Navigate to URL — `bidi.navigate` |
| `BR_CMD_CLICK` / `_MOUSE_MOVE` / `_MOUSE_OUT` | Pointer input (move/out are mostly host-poll-driven) |
| `BR_CMD_KEY_DOWN` / `_KEY_UP` | Key events; mods passed through, special keys remapped to W3C codepoints |
| `BR_CMD_SCROLL` | Wheel scroll, dx/dy in CSS px |
| `BR_CMD_BACK` / `_FORWARD` / `_RELOAD` / `_STOP` | Toolbar nav |
| `BR_CMD_RESIZE` | Window grew/shrunk → resize Firefox window inside Xvfb |
| `BR_CMD_GET_SELECTION` / `_PASTE` / `_SELECT_ALL` | Clipboard + select-all bridge |
| `BR_CMD_ZOOM_IN` / `_ZOOM_OUT` / `_ZOOM_RESET` | CSS-zoom step |

| Event (h2g) | Meaning |
|---|---|
| `BR_EV_STATUS` | Loading / Ready / Error + URL — drives URL bar |
| `BR_EV_FRAME` | Framebuffer updated — guest re-blits |
| `BR_EV_SELECTION` | Reply to `GET_SELECTION` → guest writes to TEScrap |
| `BR_EV_PAGE_METRICS` | `page_w/h`, `scroll_x/y`, `viewport_w/h` — drives V/H scrollbar thumb + active state |
| `BR_EV_DOWNLOAD` | Start / progress / done; file lands in the ExtFS shared folder |

## VBL synchronization

```
Each 60 Hz tick (host, in timer_interrupt.cpp):
  BrowserModule::on_pre_vbl():
    lock out_mtx
    drain pending events from worker threads → h2g ring
    publish damage list (fb.dirty[]) and bump fb.seq if FB updated
    release barrier on h2g.write_idx
    unlock
  fire VBL interrupt into guest CPU

On VBL (guest, interrupt level):
  acquire barrier on h2g.write_idx
  drain h2g ring → mark pending_main = true
  drain queued user-events → g2h ring
  release barrier on g2h.write_idx
  re-arm vblCount

WaitNextEvent main loop (guest, normal level):
  translate user events → enqueue commands
  if pending_main:
    handle events (status, selection text, download progress)
    if BR_EV_FRAME seen: CopyBits each fb.dirty[] rect to the window
```

No locks on the guest side. One mutex on the host serialises worker
threads writing to the h2g ring. Pixel writes to `fb.pixels` are gated
to the inter-VBL interval — the host owns the timer, so it knows when
the guest is mid-blit. If tearing ever shows up, fall back to
double-buffering (+1.5 MiB).

## Mouse model — host-side polling, zero guest events

The host does **not** receive mouse-move events through the g2h ring.
Both pieces of state it needs — cursor position and window geometry —
are already in guest memory at fixed locations the host can read
directly.

| Source | What | Notes |
|---|---|---|
| `LMGetMouse()` (`Mouse.v` $082C, `Mouse.h` $082E) | Screen-space cursor | always current |
| `LMGetMBState()` ($0172) | Mouse button state | 0xFF = up, 0x00 = down |
| `LMGetWindowList()` ($09D6) → walk `windowList` | Front-window struct + `portRect` + `portBits.bounds` | screen-space content rect |
| `BrowserShm.viewport_scroll` | scroll offset within the page | guest publishes whenever its scrollbars move |
| `LMGetCurrentA5()` / `BrowserShm.flags` BR_FRONT bit | Is Browser.app frontmost? | cheap pre-check |

`page_xy = mouse_screen_xy − window.content_topleft + viewport_scroll`,
then `input.performActions` (BiDi pointer-source `pointerMove`) to
Firefox. Same per-VBL cost as the existing command_bridge `/api/app`
peek. The `WindowList`-walking helper already exists in
`boot_progress.cpp`.

The guest still sends events with intrinsic semantics the host can't
infer: `BR_CMD_CLICK` (must align with the frame the user saw),
`BR_CMD_KEY_*`, `BR_CMD_NAV` / `_BACK` / `_FORWARD` / `_STOP` /
`_RELOAD`, `BR_CMD_SCROLL` for keyboard scroll (Page Down / arrows),
and `BR_CMD_PASTE` / `_GET_SELECTION` for the clipboard bridge.

## Files

### Host: `src/drivers/browser/`

| File | Role |
|---|---|
| `module.{h,cpp}` | `BrowserModule` top-level lifecycle when `--browser` is set |
| `supervisor.{h,cpp}` | Spawns + supervises Xvfb (`:N` in 99..119) and Firefox child processes; persistent profile, env-allowlist, signal-mask reset, `setpgid` so `kill(-pid)` sweeps the tree |
| `xshm.{h,cpp}` | XShm + XDamage on Xvfb root with `xcb_composite_redirect_subwindows(root, AUTOMATIC)` so child-window paints actually land on the captured pixmap |
| `pipeline.{h,cpp}` | `xcb_shm_get_image` → BGRX→RGB555-BE → write into `fb.pixels` → publish dirty rect → bump `fb.seq` |
| `bidi.{h,cpp}` | WebDriver-BiDi WebSocket client; synchronous req/resp keyed by id, async events dispatched into the h2g ring |
| `cmd.{h,cpp}` | Maps `BR_CMD_*` → BiDi calls (navigate, traverseHistory, performActions, scrollTo, getSelection, zoom, …) |
| `mouse_poll.{h,cpp}` | Host-side mouse + window-geometry peek, called on each pre-VBL drain |
| `window_resize.{h,cpp}` | Watch viewport size, resize the Xvfb window |
| `shm.{h,cpp}` | ExtFS handshake watcher; `send_event`, `read_command`; log polling |
| `ring.{h,cpp}` | SPSC ring push/pop, shared with the unit test |
| `browser_spike.{h,cpp}` | Earlier gradient writer / ring exerciser, kept for diagnostics |

### Guest: `MacBrowser/` (top-level, peer to `src/`)

| File | Role |
|---|---|
| `MacBrowser.c` | App: window, URL bar, toolbar, viewport, event loop, BlockMove + CopyBits from shm |
| `browser_shm.{h,c}` | Guest-side ring helpers, magic/version check |
| `MacBrowser.r` / `icons.r` | Resources: SIZE, WIND, MENU, ALRT, icon family |
| `Makefile` | Retro68 + UI 3.4 build, produces `MacBrowser.bin` |
| `MacBrowser.bin` | Committed binary (same pattern as `BridgeAgent.bin`) |
| `MacBrowser.dsk` | Committed floppy image used by tests + the docs example boot |

CMake builds `MacBrowser.bin` from source when Retro68 is detected; opt
out with `-DBUILD_MAC_BROWSER=OFF` (the committed binary is the
fallback).

### Wiring

| File | Hook |
|---|---|
| `src/main.cpp` | `--browser` CLI flag; `browser::shm_init()` after `init_m68k` (parent + IPC child) |
| `src/config/emulator_config.{h,cpp}` | `browser_enabled` boolean, JSON serialise, CLI parse |
| `src/core/emulator_subprocess.cpp` | Propagate `--browser` to IPC child argv |
| `src/drivers/platform/timer_interrupt.cpp` | Calls `BrowserModule::on_pre_vbl()` before firing VBL |

## Design rationale (the bits that aren't obvious from the code)

- **Firefox over Chromium-headless.** Stock Chromium-headless on Ubuntu
  fails HTTPS during GPU init when Xwayland holds DRM master
  (`amdgpu_query_info` fails, the TLS handshake hangs). Firefox in
  kiosk mode against a real Xvfb display sidesteps the headless GPU
  code path entirely.
- **`/opt/firefox/firefox` over `/usr/bin/firefox`.** The deb tarball
  install composes with Xvfb; the snap stub on modern Ubuntu doesn't.
- **Strict env allowlist** when forking the supervisor children. Without
  it, inherited `WAYLAND_DISPLAY` / `GNOME_*` from a desktop terminal
  trigger Firefox's headless detection regardless of `DISPLAY`.
- **`xcb_composite_redirect_subwindows(root, AUTOMATIC)` *before*
  `xcb_damage_create`.** Without COMPOSITE, GetImage on a root
  drawable returns root's pixels only — child windows aren't included.
  Firefox renders into a child window of root, so XShm without
  COMPOSITE returns all-zero pixels. With AUTOMATIC redirect the X
  server keeps a backing pixmap and composites every child into it;
  GetImage on root returns the visible scene.
- **No raw multi-byte access in BrowserShm** — every load/store goes
  through `br_u16/u32_load/store` accessors gated on `BR_HOST`. The
  one wraparound bug that surfaced was tail-byte accounting in the
  ring producer, not endianness.
- **Toolbar = text labels**, not icons. Period-correct (Netscape Navigator 3,
  iCab 1, Cyberdog all used text), avoids "icons that don't quite match
  Susan Kare voice." Loading spinner is QuickDraw, six lines at 60°
  rotated each tick.

## Runtime requirements

`apt install xvfb libxcb1 libxcb-shm0 libxcb-damage0 libxcb-composite0
libxcb-randr0`, plus a Firefox install — Mozilla deb tarball at
`/opt/firefox/firefox` is preferred over the system `/usr/bin/firefox`.
The supervisor probes `/opt/firefox/firefox` first and falls back.

`--browser` itself is feature-gated: `BUILD_BROWSER=OFF` at CMake time
strips the host pipeline; `--browser` at runtime is the on-switch.
mac-phoenix without `--browser` doesn't care whether any of the runtime
deps are installed.

## Known limits

- **Linux only.** Xvfb doesn't run on macOS without XQuartz. Linux is
  the primary dev/deploy platform; macOS users can use XQuartz or wait
  for a future macOS-native screencapture path.
- **Single window.** Multiple windows would need multiple BiDi browsing
  contexts and multiple `BrowserShm` regions — out of scope.
- **No tile-diff yet.** The pipeline converts the full damage rect each
  frame instead of doing a hash-keyed tile diff. Matters under
  full-page repaints; fine for most browsing.
- **Audio is silent.** YouTube embeds, podcast players, web-radio, and
  the half of the modern web inside `<audio>` / `<video>` produce no
  sound. The plan is to terminate Firefox audio at a virtual PulseAudio
  null sink and feed the resulting PCM into the existing Sound Manager
  → Opus → WebRTC pipeline; not yet wired up.
