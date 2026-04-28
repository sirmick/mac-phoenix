# MacBrowser — modern web inside System 7

## Goal

Let a classic-Mac user inside the emulator browse the modern web (HTTPS,
JS, fonts, images, video) by **moving the actual rendering off the
guest** onto a host-side Firefox running on a virtual X server, and
shipping the rendered pixels into a guest-native window.

The guest sees a normal Mac app — `Browser.app` — with a window, URL bar,
back/forward buttons, scrollbars. Inside the window is a `PixMap` that
shows whatever Firefox just rendered. Mouse/keyboard events from the
guest are forwarded to Firefox via WebDriver BiDi; rendered pixels come
back via XShm + XDamage. Downloads land in the guest filesystem via the
existing ExtFS share.

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
│  ┌─ src/drivers/browser/ ─────────────────────────────┐  │
│  │                                                    │  │
│  │  supervisor   ─ spawns + supervises Xvfb + Firefox │  │
│  │  bidi         ─ WebSocket client → ws:9222         │  │
│  │  xshm         ─ XShm + XDamage on Xvfb root        │  │
│  │                  (COMPOSITE redirects subwindows)  │  │
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
│  │  BrowserShm@ 0x02910000 (~1.7 MiB)                 │  │
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
                                 │ xcb (X protocol + MIT-SHM + DAMAGE
                                 │      + COMPOSITE)
                                 │ ws  (WebDriver BiDi control)
                                 │
                ┌────────────────┴───────────────┐
                │  Xvfb :99    (headless X)      │
                │  Firefox     (--display=:99,   │
                │   --kiosk --no-remote          │
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

A single `BrowserShm` struct contains:

| Field | Direction | Description |
|---|---|---|
| `magic` | guest writes | `'BRWS'`, lets host validate the handshake |
| `version` | guest writes | `BR_VERSION = 1` |
| `flags` | guest writes | capability bits (future use) |
| `h2g` | host → guest ring | events (frame-ready, status, downloads, selection) |
| `g2h` | guest → host ring | commands (nav, click, key, paste, scroll) |
| `log` | guest writes | single-slot lossy debug log; host polls + prints |
| `fb.seq` | host writes | bumped per frame; guest detects new frames |
| `fb.dirty[]` | host writes | damage rectangles for the current `seq` |
| `fb.pixels[]` | host writes | RGB555 pixel data, top-down |

### Handshake (allocation discovery)

`BrowserShm` lives in **guest memory**, allocated by Browser.app via
`NewPtrClear(sizeof(BrowserShm))` out of its application heap (the SIZE
resource advertises ≥ 4 MiB). On startup Browser.app stamps the magic +
version, then writes the buffer's Mac address as ASCII hex into
`Host:MacPhoenix:browser_shm.txt` on the ExtFS share. mac-phoenix's host
shm watcher polls that file, parses the address, validates magic +
version, and translates Mac → host via `Mac2HostAddr()` to obtain a
writable host pointer.

This dodges the per-backend banking work — the buffer is just normal
guest RAM, mapped uniformly on every backend (UAE, Unicorn-m68k,
Unicorn-PPC, KPX). No fixed `BR_BASE_ADDR`, no UAE `ram_bank` extension,
no Unicorn `uc_mem_map_ptr` for the region. All four backends use the
same handshake code path.

### SPSC ring discipline

Both rings use single-producer / single-consumer with separate
write_idx / read_idx, plus a 4-byte gap so empty (`write == read`) is
distinguishable from full. Messages are `[u16 type][u16 len][payload]`,
payload **padded to 4-byte alignment**. A `BR_MSG_WRAP` sentinel
(normal 4-byte header with type=0, len=0) jumps the read pointer back
to ring offset 0 when a message wouldn't fit before the buffer end. The
producer accounts for both the WRAP header AND the unused tail bytes
between write_idx and the ring boundary when checking free space —
forgetting the tail bytes lets write_idx land exactly on read_idx after
a successful push, which then *looks* empty and silently loses the
just-pushed payload. (Caught by the unit test; both implementations now
match.)

`tests/test_browser_shm.cpp` round-trips messages through both rings
under wraparound, fill-to-full, interleaved push/pop, and truncation
scenarios. Wired into ctest under the `unit` label.

### Debug log channel

`BrowserShm.log` is a single-slot lossy buffer for guest-side
`printf`-style breadcrumbs. The guest writes a line into `log.buf`,
sets `log.len` and `log.level`, release-fences, and bumps `log.seq`.
The host polls `log.seq` once per tick (gradient writer thread for
now, VBL hook in M3); on any change it prints the line to its own
stderr with a `[BrowserGuest <level>]` tag. Drops are observable: if
seq jumps by more than 1, the host emits "dropped N log lines" before
the latest entry. Lossy by design — bursts get dropped instead of
clogging the command rings.

Total region size: ~1.7 MiB (mostly the framebuffer).

The full layout, message types, and accessor helpers live in
`src/common/include/MacBrowser.h`. Both the host module and the guest
app `#include` that file. Host defines `BR_HOST` to enable byte-swap on
multi-byte field access; guest is native big-endian and the swap is a
no-op. Per-direction barriers (`__sync_synchronize` on host, `eieio` /
`lwsync` on PPC guests, compiler fences on m68k) are wrapped in
`BR_FENCE_RELEASE`/`BR_FENCE_ACQUIRE` macros so callers don't have to
remember backend-specific instructions.

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

| File | Role | Status |
|---|---|---|
| `ring.{h,cpp}` | SPSC ring push/pop, shared with the unit test | ✅ M2 |
| `shm.{h,cpp}` | ExtFS handshake watcher; `send_event`, `read_command`; will gain `publish_frame`, `on_pre_vbl`, log polling | ✅ M1+M2 |
| `browser_spike.{h,cpp}` | M1/M2 gradient writer + bidirectional ring exercise | ✅ |
| `supervisor.{h,cpp}` | spawns + supervises Xvfb and Firefox child processes; lifecycle | ✅ M3 |
| `xshm.{h,cpp}` | XShm + XDamage subscription on Xvfb root window with COMPOSITE redirect on root subwindows; emits damage events for child-window repaints | ✅ M4 |
| `pipeline.{h,cpp}` | receive damage event → `xcb_shm_get_image` → BGRX→RGB555-BE convert → write into fb.pixels → publish dirty rect + bump fb.seq | ✅ M4 |
| `module.{h,cpp}` | `BrowserModule` top-level lifecycle; constructed when `--browser` is set | ✅ M3 |
| `bidi.{h,cpp}` | minimal WebDriver BiDi WebSocket client; `session.new`, `browsingContext.navigate`, `input.performActions`, `script.evaluate`, `browsingContext.print` (or `network.*` for downloads) | M4.5 |

Build deps used: `libxcb`, `libxcb-shm`, `libxcb-damage`, `libxcb-composite`
(all `apt`-installable on Ubuntu). The BiDi client is a small WebSocket
+ JSON-RPC client we write by hand — one less third-party dep, and
libdatachannel already gives us a WebSocket implementation we can
reuse if hand-rolling becomes a hassle.

### Guest: `MacBrowser/`  (top-level, peer to `src/`)

Following the BridgeAgent pattern.

| File | Role |
|---|---|
| `MacBrowser.c` | main app: window, URL bar, toolbar, viewport, event loop, BlockMove + CopyBits from shm |
| `browser_shm.c` | guest-side helpers for ring read/write, magic/version check |
| `MacBrowser.r` | resources: SIZE, WIND, MENU, ALRT, icon family |
| `Makefile` | Retro68 + UI 3.4 build, produces `MacBrowser.bin` |

Pre-built `MacBrowser.bin` committed to repo (same pattern as `BridgeAgent.bin`). Auto-installs in the guest's `:System Folder:Startup Items:` so it launches when a `--browser`-mode disk boots.

### Wiring

| File | Change | Status |
|---|---|---|
| `src/main.cpp` | `--browser` CLI flag; `browser::shm_init()` + `browser_spike_start()` after `init_m68k` (parent + IPC child) | ✅ M1 |
| `src/config/emulator_config.{h,cpp}` | `browser_enabled` boolean, JSON serialize, CLI parse | ✅ M1 |
| `src/core/emulator_subprocess.cpp` | propagate `--browser` to IPC child argv | ✅ M1 |
| `src/drivers/platform/timer_interrupt.cpp` | call `BrowserModule::on_pre_vbl()` before firing VBL | M3 |
| `tests/run_boot_matrix.sh` | browser-mode boot smoke test | M5+ |

## Implementation milestones

### M0 — Shared contract ✅

- `MacBrowser.h` with full struct layout, accessors, message-type
  constants.
- Compiles cleanly under host C++ and Retro68 m68k C.
- `static_assert` on struct sizes catches layout drift at build time.

### M1 — Memory region + spike ✅

- `--browser` CLI flag + config wiring (parent + IPC child).
- Browser.app allocates `BrowserShm` itself (`NewPtrClear`), publishes
  the Mac address via ExtFS handshake; host watcher resolves it through
  `Mac2HostAddr()`. Backend-agnostic.
- `BrowserSpike.bin` (Retro68 m68k, 4 MiB SIZE) opens one window, polls
  `fb.seq`, and `CopyBits` the host's RGB555 gradient into a window
  each tick. Validated end-to-end on UAE; pattern extends to all
  backends without code changes.

### M2 — SPSC ring buffers ✅

- Locked SPSC contract in `MacBrowser.h`: 4-byte WRAP sentinel, 4-byte
  payload alignment, explicit release/acquire fence discipline.
- Host helpers (`browser::send_event` / `browser::read_command`) and
  guest helpers (`br_ring_push` / `br_ring_pop`) share the same
  wraparound math.
- Host-only protocol unit test (`tests/test_browser_shm.cpp`, 10
  cases / 47 assertions, ctest `unit` label) covers empty/full,
  varying sizes, fill-until-full, 5000-iteration wraparound, 1000-batch
  interleaved push/pop, bidirectional independence, oversized
  rejection, truncation reporting. Caught a real wraparound bug where
  `total_needed` forgot the SIZE-write_idx tail bytes consumed by
  WRAP+padding.
- BrowserSpike upgraded for both directions: pushes `BR_CMD_BACK` once
  per second, drains `BR_EV_STATUS` from h2g; status bar shows both
  counters. End-to-end confirmed: 12 commands pushed with monotonic
  counters, 80 events received, no losses.

### M3 — Xvfb + Firefox supervisor ✅

Initially scoped as Xvfb + Chromium-headless. Pivoted to Firefox: stock
Chromium-headless on Ubuntu fails HTTPS during GPU init (Xwayland holds
DRM master, `amdgpu_query_info` fails, TLS handshake hangs). Firefox in
kiosk mode against a real Xvfb display sidesteps the headless GPU code
path entirely.

- `supervisor.cpp` picks a free `:N` in 99..119, spawns `Xvfb :N -screen
  0 640x480x24`, waits for the X socket, then spawns
  `/opt/firefox/firefox --no-remote --kiosk --profile <dir> <url>` with
  `DISPLAY=:N`.
- Profile pre-seeded with `user.js` to silence first-run, what's-new,
  data-collection, default-browser-check, vpn-promo. Persistent (reused
  across launches → cookies/logins survive).
- Fork-child env stripped to a strict allowlist (HOME, USER, LANG,
  PATH, DISPLAY, GDK_BACKEND, MOZ_DISABLE_GMP_SANDBOX). Without this,
  inherited SSH/VS-Code env vars (WAYLAND_DISPLAY, GNOME_*) trigger
  Firefox's headless detection regardless of DISPLAY.
- All CrashHandler signals reset to SIG_DFL in the fork child, signal
  mask cleared. Inherited fds closed via `closefrom(3)` /
  `/proc/self/fd` walk.
- `setpgid(0, 0)` on each child so `kill(-pid, SIGTERM)` sweeps the
  whole tree on stop. (Reaping is currently best-effort — Firefox
  sometimes leaks helper processes through this; cleanup task tracked
  separately.)

Prefers `/opt/firefox/firefox` (deb tarball install) over
`/usr/bin/firefox` (which is the snap stub on modern Ubuntu and brings
sandbox / profile-path / auto-update behaviors that don't compose with
Xvfb).

**Deliverable:** ✅ `--browser --browser-url <URL>` brings up Firefox
on Xvfb and navigates to the URL.

### M4 — Pixel pipeline ✅

- `xshm.cpp` opens an xcb connection to Xvfb, allocates a single
  shared-memory segment sized to the root, attaches via MIT-SHM.
- **Critical fix:** `xcb_composite_redirect_subwindows(root,
  AUTOMATIC)` *before* `xcb_damage_create`. Without COMPOSITE,
  GetImage on a root drawable returns only root's own pixels — child
  windows are not included. Firefox renders into a child window of
  root, so XShm was returning all-zero pixels and dirty=87% was 87%
  of zeros. With AUTOMATIC redirect the X server allocates a backing
  pixmap on root and composites every child window into it; GetImage
  on root returns the visible scene, and damage on root reflects
  child-window repaints.
- A background thread blocks on `xcb_wait_for_event`; on each
  `DAMAGE_NOTIFY` it unions the rect into a pending bbox and calls
  `xcb_damage_subtract`.
- `pipeline.cpp` polls `XShmCapture::drain()` on a 60 Hz timer; on
  each non-empty drain, BGRX→RGB555-BE converts each row, writes into
  `BrowserShm.fb.pixels` at the screen-coord offset, publishes the
  rect into `fb.dirty[0]`, release-fences, bumps `fb.seq`.

**Deliverable:** ✅ a fresh `--browser-url https://example.com` run
shows the page rendered through Firefox+Xvfb in the Mac OS 7.5
BrowserSpike window via CopyBits.
Screenshots: `docs/plan/screenshots/m4-firefox-composite-{xvfb,mac}.png`.

### M4.5 — WebDriver BiDi control (1 day)

The supervisor spawns Firefox with a fixed initial URL and that's it.
To navigate, click, scroll, type, or query the page we need a control
channel. WebDriver BiDi is the right tool: WebSocket, W3C standard,
event-subscription support (page-load notifications, navigation start,
network requests), session-scoped, native to Firefox via
`--remote-debugging-port=N`.

- Update `supervisor.cpp` to add `--remote-debugging-port=9222` to the
  Firefox command line.
- `bidi.{h,cpp}`: WebSocket client connects to
  `ws://127.0.0.1:9222/session`. Sends `session.new` with
  `webSocketUrl: true` capability to upgrade the connection to BiDi.
  Reads back the session's `webSocketUrl`, reconnects to that, and
  parks there.
- Synchronous request/response over the socket: each command gets an
  incrementing id, response matched on id, the worker thread blocks
  on a `condition_variable` keyed by id. Async events
  (`browsingContext.load`, `network.responseStarted`, etc.) get
  dispatched into the h2g ring as `BR_EV_*` messages.
- Map `BR_CMD_*` ring messages from the guest into BiDi calls:
  - `BR_CMD_NAV` → `browsingContext.navigate { url, wait: "complete" }`
  - `BR_CMD_BACK` / `_FORWARD` / `_RELOAD` → corresponding
    `browsingContext.traverseHistory` / `reload`
  - `BR_CMD_CLICK` → `input.performActions` with a pointer-source
    `pointerDown` + `pointerUp` at coords
  - `BR_CMD_KEY_*` → `input.performActions` with a key-source
  - `BR_CMD_SCROLL` → `script.evaluate` of `window.scrollTo(...)` (or
    a wheel pointer-source action)
  - `BR_CMD_GET_SELECTION` → `script.evaluate` of
    `window.getSelection().toString()`, send result back as
    `BR_EV_SELECTION`

**Deliverable:** sending a `BR_CMD_NAV` through the g2h ring causes
Firefox to load a different URL and the new pixels reach the guest
window.

### M5 — Browser.app (3 days)

Real guest app: one window with URL bar, back/forward/stop/reload
toolbar, status text strip, scrollable PixMap viewport. Bookmarks menu
populated from a plain-text `Browser Prefs` file in
`<extfs>/MacPhoenix/`. Cmd-key shortcuts: Cmd+L (focus URL bar), Cmd+R
(reload), Cmd+\[ / Cmd+\] (back/forward), Cmd+W (close = quit), Cmd+Q
(quit). Firefox runs with a persistent `--profile <dir>` so cookies +
logins survive launches (already true since M3).

Pre-built `Browser.bin` committed; auto-installed in
`:System Folder:Apple Menu Items:` like BridgeAgent.

**UI discipline (look idiomatically Mac OS 7.5):**

- **Toolbar = text labels**, not icons. `NewControl(pushButProc)` with
  "Back" / "Forward" / "Stop" / "Reload" labels, ~52 px wide. More
  period-correct than glyphs (Netscape Navigator 3, iCab 1, Cyberdog
  all used text); avoids the "icons that don't quite match Susan Kare
  voice" uncanny valley.
- **Loading spinner** drawn live with QuickDraw — six lines at 60°
  intervals, rotate which one is darkest each tick. Universal Mac
  idiom, no resources.
- **App icon family**: 32×32 + 16×16 across `'ICN#'`, `'icl4'`,
  `'icl8'`, `'ics#'`, `'ics4'`, `'ics8'`. 4-bit palette drawn from the
  System 7 system colors — that's what gives the unmistakable 1995-Mac
  look. Black 1-px outline, light from upper-left, isometric "floating
  object" perspective. Subject TBD; default is a stylized globe with a
  page floating over it.
- **Document icons** for the downloads list use Finder's Desktop
  Database via `PBDTGetIconSync` — free, native, perfectly consistent
  with whatever the Finder shows.
- **Fonts**: `TextFont(systemFont)` + `TextSize(0)` for menus and
  toolbar (Chicago 12), `TextFont(applFont) + TextSize(9)` for body
  (Geneva 9), `TextFont(monaco) + TextSize(9)` for the URL bar.
- **HIG spacing constants** in `browser_hig.h` — 13 px window edge
  margin, 8 px between items, 16 px between groups, 68×20 buttons —
  used everywhere instead of magic numbers.
- **Color discipline**: outside the app icon, chrome uses only
  `whiteColor`, `blackColor`, `grayColor`, `ltGray`, `dkGray` — no
  `RGBForeColor`. One rule prevents most "looks ugly" failures.
- **Default + Cancel keyboard binding**: Return → default button
  (highlighted in dialogs via `kControlPushButtonDefaultTag`),
  Esc / Cmd-`.` → Cancel.

Pipeline for adding pixel art: `tools/png2icn.py` (one-shot, ~30 LOC)
takes a 32×32 PNG and emits the `data 'ICN#' (...)` / `data 'icl8' (...)`
blocks for `browser.r`. Hand-pixel a PNG in any editor, pipe it through
the script, drop the output into the resource file.

**Deliverable:** user double-clicks `Browser` in Apple menu, types URL,
reads modern web pages.

### M6 — Forms, selection, clipboard (2 days)

- Forward `KEY_DOWN` / `KEY_UP` for typing in form fields via
  `input.performActions` (key-source).
- `BR_CMD_GET_SELECTION` → `script.evaluate` of
  `window.getSelection().toString()` → `BR_EV_SELECTION` back.
- Cmd+C / Cmd+V via existing TEScrap sync infrastructure (no BiDi
  call needed — guest TEScrap stays the source of truth, host
  pushes/pulls via `script.evaluate` of clipboard read/write).

**Deliverable:** can fill out a search form, copy text out, paste
text in.

### M7 — Downloads (1 day)

- BiDi: subscribe to `network.responseStarted` events, watch
  `Content-Disposition: attachment` or non-HTML mime types; trigger a
  redirect to the local file via `script.evaluate(fetch + save)`. Or
  set Firefox prefs `browser.download.dir` + `browser.download.folderList=2`
  in the profile to point at the ExtFS share's downloads dir, and
  watch the dir from the host.
- On detection, push `BR_EV_DOWNLOAD` (start, progress, done) into the
  ring.
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

**1. Endianness handling.** *(M0–M2 scaffolded.)* All multi-byte shm
access goes through `br_u16/u32_load/store` accessors with `BR_HOST`
gating the byte-swap. The wraparound bug caught by the unit test
involved tail-byte accounting, not endianness — accessors did their
job. Continue: no raw `*(uint32_t*)p` anywhere.

**2. PPC weak memory ordering.** *(M2 mitigated for the rings.)*
`BR_FENCE_RELEASE` / `BR_FENCE_ACQUIRE` macros emit `eieio` / `lwsync`
on PPC guests, `__sync_synchronize` on host, compiler fences on m68k.
Both `ring.cpp` and `browser_shm.c` use them around index updates.
Worth re-validating once we run the spike on Unicorn-PPC / KPX.

**3. VBL handler restrictions.** Standard classic-Mac pattern: VBL
task does memory access only, sets a flag, main loop does real work.
Documented in IM:Processes. Will validate when M3 wires the host VBL
hook.

**4. A5 world.** *(BridgeAgent solved this; same pattern applies.)*
VBL handlers stash A5 in their `VBLTask` struct, restore at handler
entry. ~10 LOC of boilerplate.

**5. Framebuffer tearing.** Host writes pixels while guest is
mid-CopyBits → torn frames. Mitigation: gate host pixel writes to
inter-VBL interval. Host owns the VBL clock so this is straightforward.
Fall back to double-buffer (+1.5 MiB) if tearing shows up in practice.

**6. ~~Memory region mapping on every backend.~~** *(Mitigated by
design pivot in M1.)* Browser.app allocates BrowserShm out of its app
heap; the host translates Mac → host via `Mac2HostAddr()`. No
per-backend banking work. Pattern works identically on UAE,
Unicorn-m68k, Unicorn-PPC, KPX.

**7. AI iteration cost on guest code.** Each build cycle is ~30 sec
(build → MacBinary → inject → boot → test). Plan ~3× the cycles of
comparable Linux C work. Mitigation: the in-shm log channel cuts
debug-cycle time by replacing "rebuild + reinstall + reboot to add a
printf" with "log lines stream to host stderr live."

**8. Firefox dependency at runtime.** Adds a non-trivial dep when
`--browser` is set: Firefox itself plus Xvfb + xcb-composite. Mitigation:
feature-gated by `BUILD_BROWSER` (CMake) and `--browser` (runtime), off
by default. The supervisor only fails when the user explicitly opts in
without the binaries installed; mac-phoenix without `--browser` doesn't
care.

**9. Linux-only initially.** Xvfb doesn't run on macOS without XQuartz.
Mitigation: Linux is the primary dev/deploy platform anyway. macOS users
can use XQuartz or wait for a future macOS-native screencapture path.

## Mouse model — host-side polling (zero guest events)

We do **not** push mouse moves through the g2h ring. Both pieces of
state the host needs — cursor position and window geometry — are
already in guest memory at fixed locations the host can read directly,
no guest cooperation required.

### What the host pulls each VBL

| Source | What | Notes |
|---|---|---|
| `LMGetMouse()` (`Mouse.v` at $082C, `Mouse.h` at $082E) | Screen-space cursor | Mac low-memory global, always current |
| `LMGetMBState()` ($0172) | Mouse button state | Single byte, 0xFF = up, 0x00 = down |
| `LMGetWindowList()` ($09D6) → walk `windowList` | Front-window struct + `portRect` + `portBits.bounds` | Gives screen-space content rect of any window |
| `BrowserShm.viewport_scroll` | Current scroll offset within the page | Browser.app updates whenever its scrollbars move |
| `LMGetCurrentA5()` / `BrowserShm.flags` BR_FRONT bit | Is Browser.app frontmost? | Cheap pre-check before the rest |

`page_xy = mouse_screen_xy - window.content_topleft + viewport_scroll`,
then `input.performActions` (BiDi pointer-source `pointerMove`) to
Firefox. Same per-VBL cost as the existing command_bridge `/api/app`
peek path; the `WindowList`-walking helper already exists in
`boot_progress.cpp`.

### What the guest still has to send

Only events with *intrinsic semantics* the host can't infer:

- `BR_CMD_CLICK` (mouse down/up — must be tied to the frame the user
  saw, so the guest is the authority on timing)
- `BR_CMD_KEY_DOWN` / `BR_CMD_KEY_UP` (text input, modifiers)
- `BR_CMD_NAV` / `_BACK` / `_FORWARD` / `_STOP` / `_RELOAD`
- `BR_CMD_SCROLL` if the user uses Page Down or arrow keys (scrollbar
  drag is reflected via `viewport_scroll` instead)
- `BR_CMD_PASTE` / `BR_CMD_GET_SELECTION` (clipboard)

No `BR_CMD_MOUSE_MOVE`. No `BR_CMD_MOUSE_OUT`. No periodic viewport
re-publication. Saves ~60 ring pushes per second of hover and removes
the round-trip latency on hover-driven UI.

### Window geometry maintenance

The host's per-VBL walk reads `WindowList` directly, so window
move/resize events don't need to reach the host through the ring at
all — the next VBL just sees the new `portRect`. The one piece
Browser.app does have to publish is `viewport_scroll`, since that's an
internal app concept (it's the offset applied during `CopyBits`, not
a Mac OS-tracked thing). Cheap: one `br_u32_store` whenever the
scrollbar value changes, which is at most a few hundred times per
second under aggressive scrolling.

If we ever discover a path where `WindowList` is mid-update during the
VBL peek (Drag Manager? unlikely on 7.5/7.6), we can add a single
`generation` counter that the guest bumps before/after window mutation
and the host re-reads on mismatch. Defer until observed.

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
  need multiple BiDi browsing contexts + multiple `BrowserShm` regions.
  Out of scope.

## File layout summary

```
docs/plan/MacBrowser.md                  ← this document
src/common/include/MacBrowser.h          ← shared layout ✅ M0
src/drivers/browser/
  ring.{h,cpp}                           ← SPSC ring helpers ✅ M2
  shm.{h,cpp}                            ← handshake watcher + send_event/read_command ✅ M1+M2
  browser_spike.{h,cpp}                  ← M1/M2 spike writer ✅
  supervisor.{h,cpp}                     ← Xvfb + Firefox lifecycle ✅ M3
  xshm.{h,cpp}                           ← X capture w/ COMPOSITE redirect ✅ M4
  pipeline.{h,cpp}                       ← BGRX → RGB555 + dirty-rect publish ✅ M4
  module.{h,cpp}                         ← top-level lifecycle ✅ M3
  bidi.{h,cpp}                           ← WebDriver BiDi client (M4.5)
src/drivers/platform/timer_interrupt.cpp ← +on_pre_vbl hook (M5)
src/main.cpp                             ← +--browser flag ✅ M1

tools/png2icn.py                         ← PNG → 'ICN#'/'icl8' .r blocks (M5)

tests/test_browser_shm.cpp               ← SPSC protocol unit test ✅ M2
MacBrowser/                              ← top-level guest app dir
  browser_shm.{h,c}                      ← ring helpers ✅ M2
  MacBrowser.{c,r}                       ← guest app source
  MacBrowser.bin                         ← committed binary ✅ M5-A
  browser_hig.h                          ← HIG spacing constants (M5-E)
  Makefile                               ← Retro68 build ✅
```
