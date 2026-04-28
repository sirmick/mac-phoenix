# MacBrowser — modern web inside System 7

## Goal

Let a classic-Mac user inside the emulator browse the modern web (HTTPS,
JS, fonts, images, video) by **moving the actual rendering off the
guest** onto a host-side headless Chromium, and shipping the rendered
pixels into a guest-native window.

The guest sees a normal Mac app — `Browser.app` — with a window, URL bar,
back/forward buttons, scrollbars. Inside the window is a `PixMap` that
shows whatever Chromium just rendered. Mouse/keyboard events from the
guest are forwarded to Chromium; rendered pixels come back. Downloads
land in the guest filesystem via the existing ExtFS share.

This avoids three intractable problems with running a 1996 browser
against the modern web:

- TLS handshake compatibility (no SSLv3/RC4 anywhere).
- Custom-CA-import UI (NN3/iCab/NN4 stripped have no working path).
- Modern HTML/CSS/JS rendering on a 25 MHz 68040.

It also reuses infrastructure we already have: the 60 Hz VBL clock,
guest-visible host memory regions (the existing framebuffer at
`0x02110000`), ExtFS, the BridgeAgent install pattern, and the existing
clipboard sync.

## Architecture

```
┌─ mac-phoenix process ────────────────────────────────────┐
│                                                          │
│  ┌─ src/drivers/browser/ (new module) ────────────────┐  │
│  │                                                    │  │
│  │  supervisor   ─ spawns + supervises Xvfb + Chrome  │  │
│  │  cdp          ─ WebSocket client → ws:9222         │  │
│  │  xshm         ─ XShm + XDamage on Xvfb root        │  │
│  │  shm          ─ owns BrowserShm region             │  │
│  │  pipeline     ─ damage → convert → mark_dirty      │  │
│  │                                                    │  │
│  └────────────────────────────────────────────────────┘  │
│         │                                                │
│         │ writes to BrowserShm at host pointer           │
│         ▼                                                │
│  ┌─ guest-visible memory layout ──────────────────────┐  │
│  │  RAM       @ 0x00000000 (32 MiB)                   │  │
│  │  ROM       @ 0x02000000 (1 MiB)                    │  │
│  │  Scratch   @ 0x02100000 (64 KiB)                   │  │
│  │  FrameBuf  @ 0x02110000 (8 MiB)                    │  │
│  │  BrowserShm@ 0x02910000 (~1.7 MiB) ← NEW           │  │
│  └────────────────────────────────────────────────────┘  │
│         ▲                                                │
│         │ guest reads/writes via normal memory access    │
│         │                                                │
│  ┌─ timer_interrupt.cpp ──────────────────────────────┐  │
│  │  60 Hz tick:                                       │  │
│  │    1. browser->on_pre_vbl()  ← drain h2g events    │  │
│  │    2. fire VBL into guest                          │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
                                 ▲
                                 │ xcb (X protocol + MIT-SHM + DAMAGE)
                                 │ ws  (CDP control)
                                 │
                ┌────────────────┴───────────────┐
                │  Xvfb :99    (headless X)      │
                │  Chromium    (--display=:99,   │
                │   --remote-debugging-port=9222)│
                └────────────────────────────────┘

Inside the guest:

┌─ Browser.app (Retro68 m68k, ~400 LOC) ─────────────────┐
│  Window: URL bar | back | fwd | reload | viewport      │
│                                                        │
│  VBL task (interrupt-level):                           │
│    1. drain h2g ring → set pending_main flag           │
│    2. push pending commands → g2h ring                 │
│                                                        │
│  Main loop:                                            │
│    1. WaitNextEvent                                    │
│    2. translate user events → command queue            │
│    3. if pending_main: handle events + CopyBits        │
│       dirty rects from BrowserShm.fb.pixels → window   │
└────────────────────────────────────────────────────────┘
```

**Net-bridge is not involved.** No TCP, no NAT. All host-guest
communication goes through `BrowserShm`.

## The shared memory contract

A single `BrowserShm` struct, mapped at guest address `0x02910000`,
contains:

| Field | Direction | Description |
|---|---|---|
| `magic` | host writes | `'BRWS'`, lets guest detect whether browser support is enabled |
| `version` | host writes | `BR_VERSION = 1` |
| `flags` | host writes | capability bits (future use) |
| `h2g` | host → guest ring | events (frame-ready, status, downloads, selection) |
| `g2h` | guest → host ring | commands (nav, click, key, paste, scroll) |
| `fb.seq` | host writes | bumped per frame; guest detects new frames |
| `fb.dirty[]` | host writes | damage rectangles for the current `seq` |
| `fb.pixels[]` | host writes | RGB555 pixel data, top-down |

Both rings use single-producer / single-consumer with separate read/write
indices. Messages are `[u16 type][u16 len][payload]`. A `BR_MSG_WRAP`
sentinel jumps the read pointer back to ring offset 0 when a message
would straddle the buffer end.

Total region size: ~1.7 MiB (mostly the framebuffer).

The full layout, message types, and accessor helpers live in
`src/common/include/MacBrowser.h`. Both the host module and the guest
app `#include` that file. Host defines `BR_HOST` to enable byte-swap on
multi-byte field access; guest is native big-endian and the swap is a
no-op.

## VBL synchronization

mac-phoenix already simulates a 60 Hz vertical-blank interrupt via
`src/drivers/platform/timer_interrupt.cpp`. We hook it:

```
Each 60 Hz tick (host):
  1. browser_module->on_pre_vbl():
     - lock out_mtx
     - drain pending events from host worker threads → h2g ring
     - publish damage list (fb.dirty[]) and bump fb.seq if FB updated
     - release barrier on h2g.write_idx
     - unlock
  2. fire VBL interrupt into guest CPU
```

```
On VBL (guest, interrupt level):
  1. acquire barrier on h2g.write_idx
  2. drain h2g ring → mark pending_main = true
  3. drain queued user-events → g2h ring
  4. release barrier on g2h.write_idx
  5. re-arm vblCount
```

```
Main loop (guest, normal level):
  1. WaitNextEvent
  2. translate user events → enqueue commands
  3. if pending_main:
       - handle events (status, selection text, download progress)
       - if BR_EV_FRAME seen: CopyBits each fb.dirty[] rect to window
```

This is the entire synchronization story. No locks on the guest side.
On the host side, one mutex serializes worker threads writing to the h2g
ring. Pixel writes to `fb.pixels` are gated to the inter-VBL interval —
host owns the timer, so it knows exactly when the guest is mid-blit.

## Components

### Host: `src/drivers/browser/`

| File | Role |
|---|---|
| `shm.{h,cpp}` | `BrowserShmHost` — wraps the `BrowserShm` region, exposes `send_event`, `read_command`, `mark_dirty`, `publish_frame`, `on_pre_vbl` |
| `supervisor.{h,cpp}` | spawns + restarts Xvfb and Chromium child processes; lifecycle |
| `cdp.{h,cpp}` | minimal CDP WebSocket client; `Page.navigate`, `Input.dispatchMouseEvent`, `Input.dispatchKeyEvent`, `Browser.setDownloadBehavior`, `Runtime.evaluate` |
| `xshm.{h,cpp}` | XShm + XDamage subscription on Xvfb root window; emits damage events |
| `pipeline.{h,cpp}` | orchestrates: receive damage event → XShmGetImage → RGBA→RGB555 convert → write to fb.pixels → mark_dirty → publish_frame |
| `module.{h,cpp}` | `BrowserModule` top-level lifecycle; constructed when `--browser` is set |

Build deps added: `libxcb`, `libxcb-shm`, `libxcb-damage`, `tungstenite` or `tokio-tungstenite` for WebSocket (or just write a minimal client — CDP framing is simple).

### Guest: `tests/guest/browser/`

Following the BridgeAgent pattern.

| File | Role |
|---|---|
| `browser.c` | main app: window, controls, VBL task install, event loop, BlockMove + CopyBits from shm |
| `browser_shm.c` | guest-side helpers for ring read/write, magic/version check |
| `browser.r` | resources: WIND, MENU, ALRT, icon family |
| `Makefile` | Retro68 + UI 3.4 build, produces `Browser.bin` |

Pre-built `Browser.bin` committed to repo (same pattern as `BridgeAgent.bin`).

### Wiring

| File | Change |
|---|---|
| `src/main.cpp` | new `--browser` CLI flag; construct `BrowserModule` if set |
| `src/config/emulator_config.cpp` | add `browser_enabled` boolean |
| `src/core/cpu_context.cpp` | reserve `BrowserShm` region in memory layout |
| `src/drivers/platform/timer_interrupt.cpp` | call `browser_module->on_pre_vbl()` before firing VBL |
| `tests/run_boot_matrix.sh` | (later) browser-mode boot smoke test |

## Implementation milestones

### M0 — Shared contract (1 day)

- Write `MacBrowser.h` with full struct layout, accessors, message-type constants.
- Verify it compiles cleanly under both host C++ (`-Wall -Wextra`) and Retro68 m68k C.
- `static_assert` on struct sizes so layout drift gets caught at build time.
- Tiny host unit test that round-trips a few messages through a fake ring.

**Deliverable:** the contract is locked. Both sides build against it.

### M1 — Memory region + spike (1 day)

- Allocate the 1.7 MiB region in mac-phoenix at startup behind `--browser`.
- Expose at guest address `0x02910000` (extend the existing memory layout in `cpu_context.cpp`).
- Verify both UAE (m68k) and Unicorn-PPC backends can read/write at that address.
- Write `BrowserSpike.bin` (~100 LOC Retro68): one window, blits from
  `0x02910000` directly. Host writes a gradient pattern to `fb.pixels`,
  bumps `fb.seq`. Guest blits.

**Deliverable:** end-to-end pixel pipe proven on every backend. If the
gradient animates correctly, the architecture is viable.

### M2 — VBL ring buffers (2 days)

- Implement `BrowserShmHost` host-side wrapper (`send_event`,
  `read_command`, `on_pre_vbl`).
- Wire `on_pre_vbl()` into `timer_interrupt.cpp`.
- Guest-side `browser_shm.c` ring helpers + a VBL task that just
  toggles a counter visible in the window title.
- Verify the counter advances at 60 Hz via the ring, not via guest-side
  polling.

**Deliverable:** the control plane is working. We can shuttle bytes
both directions, synchronized on VBL.

### M3 — Xvfb + Chromium supervisor (2 days)

- `supervisor.cpp` spawns `Xvfb :99` and `chromium --headless=new
  --remote-debugging-port=9222 --display=:99`.
- Detect Xvfb ready (poll for socket), then spawn Chromium.
- Restart on exit; clean shutdown on mac-phoenix exit.
- CDP WebSocket connect, fetch first target, attach.

**Deliverable:** `--browser` gives you a running headless Chromium
controlled by mac-phoenix. No guest involvement yet.

### M4 — Pixel pipeline (2 days)

- `xshm.cpp`: `XShmGetImage` from Xvfb root, subscribe XDamage events.
- `pipeline.cpp`: on damage event, read changed region, RGBA→RGB555
  convert into `fb.pixels`, accumulate damage rects, call
  `publish_frame()` once per VBL.
- Update `BrowserSpike.bin` to navigate (via hard-coded URL in host) and
  blit Chromium pixels.

**Deliverable:** typing a URL in mac-phoenix CLI causes a real web page
to render in the guest window.

### M5 — Browser.app (3 days)

- Real guest app: window with URL bar, back/forward/reload buttons,
  scrollable PixMap viewport.
- Translate guest events → commands (URL Enter → NAV, mouseDown → CLICK,
  scroll → SCROLL, keyDown → KEY).
- Pre-built `Browser.bin` committed; auto-installed in
  `:System Folder:Apple Menu Items:` like BridgeAgent.

**Deliverable:** user double-clicks `Browser` in Apple menu, types URL,
reads modern web pages.

### M6 — Forms, selection, clipboard (2 days)

- Forward `KEY_DOWN` / `KEY_UP` for typing in form fields.
- `BR_CMD_GET_SELECTION` → `Runtime.evaluate('window.getSelection()')`
  → `BR_EV_SELECTION` back.
- Cmd+C / Cmd+V via existing TEScrap sync infrastructure.

**Deliverable:** can fill out a search form, copy text out, paste
text in.

### M7 — Downloads (1 day)

- `Browser.setDownloadBehavior` → save to ExtFS dir
  `/MacPhoenix/downloads/`.
- `Browser.downloadWillBegin` / `downloadProgress` → `BR_EV_DOWNLOAD`.
- Guest dialog shows progress; on completion, file is in the shared
  folder.

**Deliverable:** click a download link, file appears in guest filesystem.

### M8 — Polish (open-ended)

- Tile-diff optimization (only convert changed regions, not full frame).
- Mouse hover (rate-limited `MOUSE_MOVE` forwarding).
- Bookmarks (stored in guest as a `Browser Prefs` file).
- Multi-tab? (probably out of scope.)

---

Total to M5 (usable browser): ~10 days of focused work, plus the
AI-iteration tax on guest code.

## Risks

**1. Endianness handling.** Every multi-byte field crossing the shm
must go through the byte-swap accessors. One missed access = silent
data corruption that's brutal to debug. Mitigation: lint rule, code
review, accessor-only API, no raw `*(uint32_t*)p` anywhere.

**2. PPC weak memory ordering.** Lock-free ring needs explicit barriers
on PPC: `eieio` for stores, `lwsync` for loads, around `write_idx` /
`read_idx` updates. Mitigation: encapsulate in the accessors with
`#if defined(__ppc__)` from the start; never debug a hung guest later.

**3. VBL handler restrictions.** Interrupt-level code can't allocate,
can't move handles, can't call most of the Toolbox. Mitigation: VBL task
does memory access only, sets a flag, main loop does real work. Standard
classic-Mac pattern; documented in IM:Processes.

**4. A5 world.** VBL handlers must save/restore A5 to access app
globals. Mitigation: stash A5 in `VBLTask` struct, restore at handler
entry. ~10 LOC of boilerplate, well-known idiom.

**5. Framebuffer tearing.** Host writes pixels while guest is
mid-CopyBits → torn frames. Mitigation: gate host pixel writes to
inter-VBL interval. Host owns the VBL clock so this is straightforward.
Fall back to double-buffer (+1.5 MiB) if tearing shows up in practice.

**6. Memory region mapping on Unicorn-PPC.** New region at `0x02910000`
must be MMU-mapped on every backend. The existing FrameBuffer at
`0x02110000` works on all backends, so the precedent is good — but
worth verifying explicitly during M1.

**7. AI iteration cost on guest code.** Classic Mac C in Retro68 is
unfamiliar territory. Each build cycle is ~30 sec (build → MacBinary →
inject → boot → test). Plan ~3× the cycles of comparable Linux C
work. Mitigation: keep guest code minimal (~400 LOC total), lean on the
BridgeAgent template, use `BrowserSpike` to validate primitives before
adding UI.

**8. Chromium dependency at runtime.** Adds a non-trivial dep when
`--browser` is set. Mitigation: feature-gated, off by default. Builds
of mac-phoenix without `--browser` flag don't need Chromium.

**9. Linux-only initially.** Xvfb doesn't run on macOS without XQuartz.
Mitigation: Linux is the primary dev/deploy platform anyway. macOS users
can use XQuartz or wait for a future macOS-native screencapture path.

## Mouse model — host-side polling (no guest events)

We do **not** push mouse moves through the g2h ring. The host already
knows the screen-space mouse position (it owns the cursor) and can read
it any time. To translate to page-space we need: (a) the window's
content rect in screen coords, and (b) the scroll offset.

VBL hook on the host side, each tick:

1. Check whether `Browser.app` is the front Mac process. Cheap test:
   read the front-process PSN from the existing `command_bridge`
   peek path, or have Browser.app set a "front" flag in
   `BrowserShm.flags` from its activate/deactivate handlers.
2. If front: read the host cursor position (already tracked by the
   ADB / web input pipeline).
3. Read the viewport's screen-space rect from `BrowserShm.viewport`.
   The app keeps that field current. Two valid disciplines:
     - **Event-driven (preferred):** Browser.app updates the field on
       every `windowMoved`/`windowResized`/scrollbar event and pushes a
       single `BR_CMD_VIEWPORT` so the host invalidates any cached
       transform. Cheap, and we already pay the cost of those handlers.
     - **VBL-resampled (fallback):** the app re-publishes the field
       every VBL even if nothing changed. Costs one ring push every
       tick — acceptable but wasteful, and safer if we ever add
       compositing tricks (System 7 Drag Manager moves windows without
       firing a high-level event, for example).
   Start with event-driven; add a periodic resample once we see if any
   classic-Mac path slips past the events.
4. Subtract: `page_xy = mouse_screen_xy - viewport_origin + scroll`.
5. Forward to Chromium via CDP `Input.dispatchMouseEvent`.

This eliminates all `BR_CMD_MOUSE_MOVE` / `BR_CMD_MOUSE_OUT` traffic
(saves ~60 ring pushes per second of hover) and removes the latency of
the round-trip. The guest only sends commands for events it
intrinsically owns: clicks, key presses, nav requests, scroll wheel.

Mouse-down / mouse-up still need to go through the ring — we want the
press to be timed against whatever frame was visible to the user.
Mouse position itself is stateless and pollable.

## Open questions (to revisit during implementation)

- **Form input UX.** Type-through to focused element via raw `KEY_DOWN`
  is M6 plan, but for complex inputs (autocomplete dropdowns, IME) we
  may need a guest-side modal. Defer until we see how M6 feels.
- **Scroll model.** Guest owns scroll position (it has the scrollbars);
  host renders the full page once per nav and ships visible-viewport
  tiles. Or host renders just the viewport and re-renders on scroll.
  The first is more responsive but uses more memory; the second is
  cheaper but adds latency on every scroll. Resolve in M5.
- **Default homepage.** What does Browser.app open to on launch? A
  "MacPhoenix Browser" landing page on the host? Google? Nothing?
- **Multi-window.** Single window only for v1. Multiple windows would
  need multiple CDP target attachments + multiple `BrowserShm` regions.
  Out of scope.

## File layout summary

```
docs/plan/MacBrowser.md                  ← this document
src/common/include/MacBrowser.h          ← shared layout (M0)
src/drivers/browser/
  shm.{h,cpp}                            ← BrowserShmHost (M2)
  supervisor.{h,cpp}                     ← Xvfb + Chrome (M3)
  cdp.{h,cpp}                            ← CDP client (M3)
  xshm.{h,cpp}                           ← X capture (M4)
  pipeline.{h,cpp}                       ← orchestration (M4)
  module.{h,cpp}                         ← lifecycle (M3)
src/drivers/platform/timer_interrupt.cpp ← +on_pre_vbl hook (M2)
src/core/cpu_context.cpp                 ← +BrowserShm region (M1)
src/main.cpp                             ← +--browser flag (M1)

tests/guest/browser/
  browser.c                              ← guest app (M5)
  browser_shm.c                          ← ring helpers (M2)
  browser.r                              ← resources (M5)
  Makefile                               ← Retro68 build
  Browser.bin                            ← committed binary
  BrowserSpike.bin                       ← M1/M4 test app
```
