# Executor: where it goes next

Agreed in discussion 2026-10-09. [PLAN.md](PLAN.md) has the milestones;
this file is the longer direction they feed into. Where it stands
(2026-11-07; Order of work): D1–D4 done (MAME cores, minicoro, a
compiler-only build with host edges, the tab), D6 well along (PowerPC
applications and Mac OS 8.6), D5 partly (2x drawing), D7 not started;
C1, C2 and C3a done, C4 left to M2.

## Why build it

As a "run everything" emulator, a reimplemented Toolbox is worse than
what exists: Basilisk II, SheepShaver and QEMU run Apple's ROM and
System, so nearly every application works on day one. Executor never
quite gets there. Every new application turns up Toolbox gaps (the
commit log is the evidence).

What a C++ Toolbox buys is everything that follows from the system
software being our code instead of Apple's binaries:

1. **Integration.** The Window Manager is ours, so Mac windows can be
   host or browser windows (rootless). Host files are Mac files, with no
   disk images. Clipboard, fonts (TrueType via FreeType), printing (to
   PDF) and networking (host sockets, no NAT) are native rather than
   bridged.
2. **Rendering past the hardware.** QuickDraw is C++, so it can draw at
   2× for HiDPI: real vector text and crisp lines, not upscaled pixels.
   Impossible when Apple's QuickDraw runs from ROM.
3. **No ROM, possibly no Apple binaries.** If Executor's own System
   resources and a Finder of our own suffice, it is distributable and
   runs out of the box. Today's path (Apple's System file and Finder)
   gives this up; it is a choice to make deliberately (see Open
   questions).
4. **Footprint.** No boot sequence, instant start, small. In wasm, a
   static page that runs on a phone.
5. **Observability and automation at the API level.** Every trap is
   visible and structured (preservation, reverse engineering).
   Automation acts on windows, menus and controls as objects, which beats
   the BridgeAgent file protocol. Text arrives through `DrawText`, so
   accessibility is possible.
6. **One substrate.** 68k, PPC and Unix userlands on one CPU core, one
   scheduler and one Toolbox.

### What it runs

| | Runs | Doesn't, or badly |
|---|---|---|
| Executor 68k | System 6–7.5 era applications: Word 5.1, ClarisWorks, Excel 4, PageMaker, early Photoshop, HyperCard, MPW, THINK C, CodeWarrior 68k, MacPerl, Kid Pix, games | INITs patching ROM internals, copy protection poking hardware, software that programs video hardware, apps leaning on unwritten managers (QuickTime, Open Transport, Appearance, Navigation Services) |
| Executor PPC (PowerCore) | Mac OS 8/9 era PPC applications, as the Toolbox coverage allows | the same, plus whatever CFM/InterfaceLib gaps turn up |
| A/UX userland | SysV tools, X11R4/R5 clients, A/UX's Mac environment | anything needing real devices beyond tty/pty, framebuffer and input |

A/UX adds little unique software. It proves the kernel-recreation
architecture, which could later run NetBSD/mac68k or Linux/m68k user
binaries too.

### The demo that sets priorities

**HyperCard in a browser tab, crisp at retina resolution, saving to your
real files, no ROM, opened from a link.** No existing emulator can show
it. It ranks the work: the wasm build, the HiDPI QuickDraw path and the
host-file story come before more Apple extensions loading.

## Architecture changes

### 1. CPU: Musashi with translated addressing

**Done 2026-10-23** (D1, W0): Executor runs on `mame-68k` in its four
windows. The plan as written:

Executor ran on UAE with identity addressing: guest RAM mapped at
host address 0, everything the guest sees below 4GB. That costs
`vm.mmap_min_addr=0`, a low non-PIE binary, the `syn68k_alloc_low`
stack and framebuffer pools, and portability: 64-bit macOS reserves the
low 4GB (`__PAGEZERO`) and Windows won't map page 0, so it is Linux-only.

The identity map was only there for UAE's JIT. Upstream `romlib` was
written for a translating core: a guest address's top 2 bits pick one of
four windows, `ROMlib_offsets[0..3]`, and every access goes through
`SYN68K_TO_US` / `US_TO_SYN68K`. The facade collapsed the windows to one
at 0.

Plan: Musashi behind the existing syn68k facade, with the four windows
restored. `romlib` doesn't change (one stray identity cast in
`menu/sysmenu.cpp`). 68k instruction speed is not a priority: most of the
time goes in C++ traps.

Musashi calls our `m68k_read_memory_*` for every access, so the same
hook gives a software MMU for debugging: watchpoints, catching 68k
writes to low memory or the host-only windows, dirty-page tracking for
the framebuffer. Only 68k accesses go through it: `romlib`'s C++ uses
host pointers and copies across whole handles, so guest ranges stay
contiguous in host memory (big windows, never 4K remapping).

**Which Musashi (decided 2026-10-10).** MAME's 68020–68040 core is
Musashi too, with a working 030/040 PMMU, a softfloat 68881/040 FPU, and
no globals: the CPU is a class instance and its memory accessors are
`std::function` fields, so several can exist in one process. The vendored
v3.32 copy (one `m68ki_cpu`, link-time memory callbacks, a global cycle
counter, our own line-F patch) is replaced by a lift of MAME's copy behind
the same `cpu::Core`. That also settles two open questions below: the FPU
for A/UX, and the ROM side following (one 68k core everywhere). Plan and
milestones: [../cpu/PLAN.md](../cpu/PLAN.md) (C1).

### 2. Process switching: minicoro

`romlib` is ordinary blocking C++ that calls into 68k code and is called
back from it. A process must suspend inside `WaitNextEvent` with C++ and
68k frames interleaved, e.g. MacPerl → `CallComponent` (C++) →
AppleScript (68k) → `AESend` in `PACK` 8 (68k) → `WaitNextEvent` (C++).
C++20 `co_await` would mean making every `romlib` function that can
reach a 68k callback async: nearly all of it (definition procedures,
`dcmp` decompressors, grow-zone procs, filter and action procs).
Asyncify is that transformation done by a tool.

Plan: replace the `QThread` + mutex baton in `process.cpp` with minicoro
(stackful, asymmetric). The main stack runs a scheduler loop that
`mco_resume`s the next process; `WaitNextEvent` and friends
`mco_yield`. Stacks come from `alloc_cb`, inside a guest window, because
the guest is handed pointers into C++ frames. Only the scheduler
resumes. minicoro over libco for its WebAssembly backend (Emscripten
Asyncify); libco would be the smallest diff natively.

The 60Hz timer thread (`time/syncint.cpp`) becomes a clock check at
yield points. `romlib` is then single-threaded.

**Done 2026-10-23** (W0): processes and Thread Manager threads are
minicoro coroutines on Executor's host thread (stacks from `alloc_cb` in
window 1); the root (the first process's main thread, on that thread's
own stack) is the scheduler: a context whose turn it isn't yields, and
the root, waiting, resumes whoever's turn it is until its own comes back.
mame-68k's innermost-run stop flag, `thread_local` while contexts were
host threads, travels with the saved 68k context. No timer thread: the
deadline is checked between the cores' time slices (`cpu::Host::
slice_end`, both MAME cores), wherever host code checks for interrupts,
and by the idle wait, which sleeps until it. Qt is gone from `romlib`
(`executor_tests` links no Qt). Host threads that remain, for the host
edges (D3): the resolver lookups (`mactcp/dnr.cpp`, `ot_inet.cpp`), the
socket watchers that raise interrupts (MacTCP, OT) and `vdriver`'s
rendering workers. Not yet: two PowerPC processes parked inside mame-ppc
at once (its stop flag is still `thread_local`; milestone P's open item).

### 3. Dependencies and host edges

About 225 of `romlib`'s 256 `.cpp` files touch nothing on the host. The
rest become interfaces:

* video and input (`vdriver/`, already an interface),
* sound (`sound/SoundDriver`, already an interface; implement it, M3g),
* files (`file/localvolume/`, `hfs/`, `hostdisk.cpp`, `mkvol/`;
  `std::filesystem` in `extensions.cpp` and `res/resPolicy.cpp`),
* clock (`osutil.cpp`, `time/`),
* process exit and faults (`mman/mmansubr.cpp`, `shutdown.cpp`,
  `base/fault_handler.cpp`, `sane/float4.cpp`'s `SIGFPE`) behind host
  callbacks.

Optional modules: network (`mactcp/`), serial, printing.

Dependencies to drop:
* Qt6::Core: only threads, gone with minicoro.
* Boost: filesystem → `std::filesystem`; program_options, any,
  lexical_cast → `std`, or delete with the vestigial CLI and `prefs/`
  (Bison) parser.
* Ruby, Perl, Bison at build time: commit the generated multiversal
  headers and blitters; keep the generators for people editing the YAML.

Target: a C++17 compiler is the only build requirement.

### 4. WebAssembly

Emscripten, single-threaded, no `SharedArrayBuffer` (so plain static
hosting):

* wasm32 linear memory is the guest address space's natural home;
  Emscripten's own data, heap and stack move clear of guest RAM
  (`--global-base` and friends), or guest RAM is a fixed carve-out.
* minicoro's Asyncify backend for processes; JSPI or wasm stack switching
  once browsers ship them.
* Framebuffer to `<canvas>`, DOM input to `EventSink`, the System
  Folder and applications in the Emscripten filesystem, WebAudio for
  sound. What the tab changes is kept by its server (`executor/server`:
  Standalone.md); the browser only caches.
* `WaitNextEvent` idle yields to the browser event loop.
* No native JIT runs in wasm, and not because of a W^X policy: wasm has no
  executable linear memory at all; code exists only inside compiled
  modules. The only JIT is to emit wasm bytecode at run time, compile it
  with `WebAssembly.Module` (asynchronously, or synchronously in a
  Worker), share the guest memory as an imported `Memory` and dispatch
  through a `WebAssembly.Table` (v86 does this). Compilation is per
  module, milliseconds, so hot blocks are batched per module and never
  patched. The 68k needs none of this: Musashi interpreting in wasm is
  about where a 68k Mac sits. For PPC in a tab the interpreter comes
  first; a UML→wasm back-end for MAME's recompiler is the upgrade path
  (C4 in [../cpu/PLAN.md](../cpu/PLAN.md)).

First gate: the built-in Browser app headless to a canvas, then the
Finder.

**Decided 2026-10-23:**

* **Processes: minicoro, Asyncify in the browser.** Plain static hosting,
  no COOP/COEP headers, no `SharedArrayBuffer`. Emscripten pthreads
  keeping today's thread baton was the alternative (no instrumentation,
  but cross-origin isolation); rejected. Asyncify's cost (code size,
  instrumented run loops) is measured early and trimmed with
  `ASYNCIFY_ONLY`/`ASYNCIFY_REMOVE` lists; JSPI when it can carry fibers.
* **CPU: `mame-68k` and `mame-ppc` only.** UAE leaves Executor first
  (D1's last step). On `mame-ppc` the portable C back-end (`drcbec`):
  PowerCore already sits on Executor's own windows
  (`syn68k_guest_memory()`), so the ROM machine's 4GB reservation never
  comes into it; `drccache`'s RWX allocation becomes plain memory, the
  asmjit/x64/arm64 back-ends are left out. wasm32, not memory64.
* **The module runs in a dedicated Worker.** Frames to an
  `OffscreenCanvas`, input and uploads by `postMessage`, audio to an
  AudioWorklet; the page stays responsive through long traps. No
  `SharedArrayBuffer` needed, so static hosting holds.
* **No Apple software on the site.** It boots Executor's own System; users
  add disk images (theirs: an Apple System, applications) to the
  server's library, or upload them for a visit.
* **One server: the standalone** (decided 2026-11-02). `executor/server`
  (Go, one binary with the tab inside: Standalone.md, WebSite.md) is the
  only server; it keeps what the tab changes, and the first browser to
  open it has it until it stops. One mode only, the tab on its server.
  (Before it, until the merge of `wash/auth`: a hosted Python server with
  accounts, and a static site keeping everything in the browser's OPFS;
  both are in git history only.)
* **Networking: none at first; the socket layer becomes a host
  interface.** A page can't open TCP/UDP, only fetch, WebSocket, WebRTC,
  WebTransport. Executor's MacTCP and OT are C++ over host sockets, so the
  browser backend is a socket-level relay (connect/send/recv/DNS over one
  WebSocket; no TCP/IP stack, no NAT): self-hostable, a small Rust
  service or a Cloudflare Worker (`connect()`, DNS over HTTPS; no UDP),
  port-restricted since a public relay is an open proxy. TLS stripping
  belongs there too: period software can't speak modern TLS. Later,
  WebRTC data channels between tabs for AppleTalk (multiplayer, file
  sharing), which needs only a signalling server.

**Layout: one source tree, two builds** (agreed 2026-10-23).
`cmake -B build` is mac-phoenix with Executor inside, as today;
`emcmake cmake -B build-wasm` builds `executor.wasm` + `.js` and nothing
else, with `CMAKE_CROSSCOMPILING_EMULATOR=node` so `ctest -L executor`
runs the gtest suite under node.

| Layer | Native | wasm |
|---|---|---|
| `romlib/` | the same | the same |
| Front end | `frontends/phoenix/` (`executor_host.h`) | `frontends/wasm/`: the same `executor_host` contract, exported to the Worker |
| Host process | `frontends/phoenix/executor_child.cpp` (IPC to the parent) | not built |
| CPU | `cpu/` + cpu_core: `mame-68k`, `mame-ppc` with every back-end | the same; `drcbec` only |
| Host edges | POSIX implementation | Emscripten implementation |
| Page | `client/` (mac-phoenix's UI) | `executor/web/`: static HTML/JS, Worker, upload |

Not in the wasm build: the mac-phoenix parent (Qt, webserver, WebRTC),
net-bridge, the ROM machines, UAE, asmjit.

Rules:

1. No `__EMSCRIPTEN__` in `romlib/`. A host edge is one interface header
   and one `.cpp` per platform (`host/posix/`, `host/wasm/`), picked by
   CMake. `#ifdef __EMSCRIPTEN__` only in the wasm front end and wasm host
   files.
2. Optional modules always compile, with a stub backend (MacTCP/OT on a
   "no network" socket backend), not behind a define. CMake options only
   where code drags in a dependency (`MAME_PPC_BACKENDS=c;x64;arm64`).
3. minicoro hides its own backend: one `process.cpp`.
4. Upstream's dead platform conditionals (`CYGWIN32`, `MSDOS`,
   `OLD_BROKEN_NEXTSTEP_SOUND`, most `__APPLE__`/`_WIN32`) go in D3.

Two things in today's code to fix on the way: the generated sources
(8 custom commands, `map_to_c`/`mkseed*` build tools, the Ruby/Perl/Bison
generators) must be committed before the wasm build, since `emcmake` would
compile the tools to wasm; and `syn68k_common.cpp` reserves each 1GB
window with `mmap`, which Emscripten commits, so window backing is sized
to use (RAM + 64KB tail, a 16–32MB pool) behind the memory host edge.

**The W steps (D4 in detail):**

| | Step | Gate |
|---|---|---|
| W-spike | minicoro on Emscripten fibers + `emscripten_sleep` under Asyncify, in a Worker | two coroutines switching, each yielding to the browser, a C++→callback→yield stack resumed. **Passed 2026-10-23** (Emscripten 5.0.7, node 22 and a headless-Chromium Worker): yield from C++ → indirect call → `std::function` → `mco_yield`, coroutine stacks from our allocator (`alloc_cb`), one coroutine writing into another's frame, `emscripten_sleep` inside a coroutine; the page's main thread kept ticking. A hot switch-and-indirect-call loop: native 246 ms, wasm 245 ms, wasm with Asyncify instrumenting the loop 350 ms (≈1.4×). minicoro's header includes `<emscripten/fiber.h>` inside its `extern "C"`, which Emscripten 5's C++ templates reject: include `<emscripten.h>` and `<emscripten/fiber.h>` first. Yield to the browser at idle and per frame, not on a counter: each `emscripten_sleep(0)` is a `setTimeout` (≈0.7 ms) |
| W0 | Native clean-up: UAE out of Executor, minicoro (D2), Boost → `std`, generated files committed, host edges (files, clock, sockets, faults, windows) as interfaces (D3) | the native suites pass as today. **UAE out 2026-10-23**: the facade has only the four windows (no identity layout, `MAP_32BIT` pool or low link for `executor_tests`), `--core` for Executor is `mame-68k` (a saved `uae` is replaced), the `_mame68k` twin cells merged into the plain ones; UAE is its own library (`cpu_core_uae`, `cpu::register_uae()` from the ROM machine), so `executor_tests` links no UAE. `--deterministic` moved with it: the virtual clock is core-independent (`src/common/vclock.cpp`), mame-68k counts instructions through the shim's instruction hook. **minicoro (D2) 2026-10-23**: section 2 |
| W1 | wasm under node | `ctest --test-dir build-wasm -L executor`. **Passed 2026-10-23** (Emscripten 5.0.7, node 22): 197/197, the two upstream failures disabled as natively, the network gtests skipped (`hostnet::available()`); 16 s. `emcmake cmake -B build-wasm` takes `cmake/ExecutorWasm.cmake`: Executor, the MAME cores (mame-ppc on `drcbec`, no asmjit), `cpu/host/wasm` (aligned allocations; window 1 64MB; window 3 all memory below `__heap_base`, so the C stack is guest-addressable and the tests run on it), `EXECUTOR_NET=none`, Asyncify, JS exceptions, `NODERAWFS`. What it took besides: libc++ (no `char_traits<unsigned char>`: `mac_string` has `mac_char_traits`; `vector` of an incomplete type in `LocalVolume`; narrowing), SANE's extended conversion by value where `long double` isn't the x87's (also arm64 Linux, macOS arm64), the blitters called through their real type (wasm checks indirect calls' signatures), termios on every POSIX host, `SetDateTime` never setting the host clock, `GuestMemory::PAGE_SIZE` renamed `PAGE_BYTES` (a libc macro), no `execinfo` backtrace. Not exercised yet: process and thread switches (coroutines under Asyncify) |
| W2 | A tab: Worker, `OffscreenCanvas`, DOM input, Executor's own System | the built-in Browser app in a tab. **Passed 2026-10-23**: `executor/web` (page, Worker, key map) beside `executor-web.js`/`.wasm` (`frontends/wasm/web_main.cpp`: the `executor_host` contract mac-phoenix uses, on the Worker's one thread). Executor runs on the calling thread wherever its stack is guest-addressable (`main.cpp`), its idle waits and a tick about once a frame between the cores' slices yield to the browser (`rsys/hostidle.h`, `host/wasm/idle.cpp`), and the yield hook takes the page's queued input and renders what changed (`PhoenixVideoDriver::pump`) as RGBA to the canvas. The render pool runs without threads where they can't start. First frame 0.2 s after load (local), menus track the mouse; ctest `executor.web.tab` (headless Chromium: frames, and a press on File opens the menu; skips without playwright). Not yet: process switches (the Browser launches nothing here), sound, files from the user |
| W3 | A usable site: Finder, disk images, WebAudio, JS automation | Playwright drives an uploaded application. **Passed 2026-10-23** (`executor.web.apps`): PPCHello and MacPhoenixSuite added through the page, launched from the Browser with the `launch` request (process switches: the coroutines under Asyncify), PPCHello's PowerPC code passing on mame-ppc's C back-end, the suite's keyboard section getting "hello 123" typed on the canvas, its SysBeep reaching the page's AudioWorklet. Automation is `window.executor.request(op, arg)`: `app`, `windows`, `launch` at the application's next event call, `read`/`write` of the module's files. Apple's System and Finder from a disk image: `executor_host::sideload_system` (`frontends/phoenix/hfs_sideload.cpp`, an HFS and HFS+ reader) copies the blessed folder's System, Finder and folders into Executor's volume, and the Finder starts (7.5.5's in 0.6 s; `executor.web.finder`). Boot disks: `tools/web/make_boot_disk.py` cuts one from a System image (7.5.5.img 250 MB -> 16 MB, 6 MB gzipped; APPLE-DISKS.md). The first site kept disks, overlays and Executor's volume in the browser's OPFS and was served by a Python server or any static host; since P1 (2026-11-02) the Go server keeps them all and the browser only caches (Standalone.md). Games checked on the tab then (7.5.5's Finder, 33 games): 27 ran. Glider PRO overflowed the stack: its sound callback queues more with `SndDoCommand` mid-mix, and the mix started there nested without end; `PhoenixSoundDriver` mixes no more inside a mix |
| W4 | PPC applications on `mame-ppc` + `drcbec` | a PPC application in a tab (StdCLib/MathLib native since 2026-10-26, D6). **Measured 2026-10-31** (`executor.web.ppc`: Apple's 8.6 System from its HFS+ image through the page's System choice and the PowerPC box, `--executor-ppc`'s equivalent; headless Chromium, 640x480). The cores (`test_cpu_cores --bench 50`, a lwz/addi/stw/addic./bne loop, memory through calls as PowerCore has it, inline memory in brackets): native x86-64 back-end 1156 MIPS (1678), native `drcbec` 79 (89), `drcbec` in wasm under node 22 40 (62) before and 77 (66) after dropping `execute()`'s try/catch and fesetround pair on Emscripten (the try sent every call in the loop through a JS `invoke_*`); without Asyncify it would be 86, the instrumentation's price (a host op switches processes from inside `execute()`, so it can't be left out; JSPI later). 8.6, native (x86-64 back-end / `drcbec`) and the tab: the Finder's desktop 1.3 / 1.4 s and 3.0–4.4 s (first visit: 0.4 s of it copies the System Folder out of the image), a disk's window 0.21 / 0.21 and 0.21–0.25 s, PPCHello's report 0.57 / 0.62 and 0.63–0.69 s, Ferazel's Wand (games-ppc) to its CD prompt 0.26 and 0.48 s. The tab's boot profile: 3.2 s busy, 49% of it Executor's File Manager on Emscripten's JS MEMFS (every `stat`/`open` a path walk in JS: `LocalVolume`'s item factories stat and open each entry), 14% the System copy, `drcbec` itself 7%, Asyncify and the yield hook under 1%. Taken: the try/catch, the 68k disassembler built once (alloc-failure reports built a new one per line, 160 ms), the factories use the directory entry's type. A program's `DisposePtr` of a non-block read through a zone word outside memory and killed the tab (natively it reads stray host memory): `legit_addr_p` checks guest memory now. Open: AppleWorks 5.0.3 runs natively on both back-ends but in the tab jumps to 0 after four 68k-to-PowerPC callbacks (wasm only). Mars Rising and Slithereens crash in `FindSymbol`, EV Nova runs into garbage, natively too. **Recommendation: `drcbec` is enough for M1**, C4 stays in M2: in the tab PowerPC applications are bound by the Toolbox and the file system, not by the interpreter (native `drcbec` is within 10% of the x86-64 back-end on all of the above); the next M1 wins are the File Manager on MEMFS (fewer stats, or WASMFS) and the AppleWorks failure. C4's size: a `drcbe_wasm` emitting one module per hot-block batch (the x86-64 back-end is 6.7k lines, `drcbec` 2.6k): about 3–4k lines of C++ and 200 of JS, plus generated code that exits to `execute()` before every host op (Asyncify can't unwind through modules made at run time) |

Since then: the network relay (below), 2x drawing in the tab
(HiDPI.md), the server as the store (P1, Standalone.md). Not yet: C4
(M2), WebRTC AppleTalk.

#### The network relay

The tab's MacTCP and Open Transport are the native C++ over
`rsys/hostnet.h`; in the tab that header's backend is
`host/wasm/net.cpp` (`EXECUTOR_NET=wasm`, the wasm build's default), a
table of the tab's own sockets. What leaves the tab goes over one
WebSocket to `/relay` on `executor/server` (`relay.go`), opened at the
first socket that needs it, served by it only (the page tells the Worker,
`executor/web/net.js`; served elsewhere, or under node, `available()` is
false). What stays in the tab:

* **Listeners.** `bind`/`listen`/`accept` are the tab's; the relay never
  listens (a public relay can't take connections for its users, and a
  relay behind NAT couldn't). A connect to our own address (10.0.2.15,
  what `ipctlGetAddr` and OT's interface report), or to 127/8 where a
  socket of the tab has the port, is a pair of sockets here: MacTCP's
  passive open and OT's `OTListen`/`OTAccept` with a client in the same
  tab (the suite's `mactcp_passive`, OTTest's `tcp_listen`) work.
* **Loopback nobody here has** goes to the relay, which takes 127.0.0.1
  as its own host's and refuses it (as every private address) unless
  `allow_private`. The tests' echo server is on the relay's host so.
* UDP to our address with no socket on the port is lost; to another tab
  socket it is delivered here.

Waiting is yielding: messages arrive while the module yields to the
browser; each yield (`host/wasm/idle.cpp`) pumps them into the sockets and
looks at the `hostnet::Watcher`s (no thread: a watcher's interrupt is
raised from there, or at once when a call in the tab makes its socket
ready), and `poll`, lookups and blocking sockets yield until done.
`run_detached` runs its work at once, waiting the same way.

**Protocol** (version 1). One binary WebSocket message is one message:
`op` (u8), `id` (u32), the op's fields; big-endian, addresses IPv4 as
u32. `id` is the tab's: a socket's from `CONNECT`/`UDP` to `CLOSE`, a
lookup's to its answer.

| Tab → relay | Fields | |
|---|---|---|
| `0x01 CONNECT` | addr, port (u16) | TCP out; `CONNECTED` or `ERROR` |
| `0x02 UDP` | | a UDP socket (the relay binds a port) |
| `0x03 SEND` | bytes | TCP data; at most `window` unacknowledged |
| `0x04 SENDTO` | addr, port, bytes | a datagram (dropped if the policy refuses it) |
| `0x05 SHUTDOWN` | | our FIN, after what was sent |
| `0x06 CLOSE` | flags (u8; 1: reset) | frees the id; also after `ERROR` |
| `0x07 CREDIT` | n (u32) | n more bytes of `DATA` taken |
| `0x10 LOOKUP` | name | A records → `ADDRS` |
| `0x11 REVERSE` | addr | its name → `NAME` |
| `0x12 QUERY` | class (u16), type (u16), name | `res_query` → `ANSWER` (the reply, wire format) |

| Relay → tab | Fields | |
|---|---|---|
| `0x80 HELLO` | version (u16), max sockets (u16), window (u32) | first, id 0 |
| `0x81 CONNECTED` | | |
| `0x83 DATA` | bytes | at most `window` not yet credited |
| `0x84 DATAFROM` | addr, port, bytes | only from hosts the socket sent to |
| `0x85 EOF` | | the peer's FIN |
| `0x86 ERROR` | code (u8) | the socket is dead: 1 refused, 2 network unreachable, 3 host unreachable, 4 timed out, 5 reset, 6 not allowed (policy), 7 too many sockets, 8 other |
| `0x87 ACK` | n (u32) | n bytes of `SEND` written to the peer |
| `0x90 ADDRS` | code (u8), addrs | code: 0 found, 1 no such name, 2 try again, 3 failed |
| `0x91 NAME` | code, name | |
| `0x92 ANSWER` | code, reply | |

Flow control is the window both ways (64 KB a socket): the relay reads
from a peer only while the tab has room, and holds no more than a window
of the tab's. The relay pings every 30 s and drops a connection silent for
90; a protocol error closes it (1008), its sockets with it. Policy
(`network.relay_*` in its settings, Standalone.md): destination ports from an allow-list
(the classic services by default), no private, loopback, link-local,
multicast or broadcast hosts unless `allow_private`, `max_sockets`
across the tabs, `max_connections` WebSockets; the Origin must be the
server's. Gates: `executor.web.standalone_server` (go test: the protocol
from a Go client: echo with flow control, refusals, the policy, UDP, DNS,
limits) and `executor.web.relay` (MacPhoenixSuite's network and mactcp
sections and OTTestRetro68K in the tab against `tests/lib/echo_server.py`).
Not yet (v2): a connect-rate limit (only concurrency is capped now, so a
tab could scan the allowed public hosts and ports), TLS
stripping, WebRTC AppleTalk.

### 5. PPC: MAME's PowerPC recompiler

Decided 2026-10-10. Upstream Executor's PowerCore has no licence and did
not come across; KPX (SheepShaver's Kheperix) has a dyngen JIT whose
precompiled x86 machine code builds nowhere else, so on arm64 the PPC
machine is a stub today. MAME's PowerPC core (BSD-3) is a UML front end
with x86-64 and arm64 back-ends (the arm64 one since MAME 0.274, tested
on Apple Silicon), 603/604/750 models, and a memory model that is
indirect by design. It is lifted as the PPC `cpu::Core` (C2): first
behind the KPX machine's Mac side (ROM patches, EmulOps, nanokernel
interrupt delivery), replacing the Kheperix core, then as the
`PowerCore.*` facade for Executor's milestone P. One base offset
(SheepShaver's `DIRECT_ADDRESSING` idea) ends the `mmap_min_addr` need.

With a host 68k core beside it, the nanokernel's own 68k emulator (the
DR recompiler, PPC code interpreting 68k) becomes redundant: the
nanokernel enters and leaves its emulator through a fixed register
contract that `execute_68k` in `cpu_ppc_kpx.cpp` already drives from the
host (D0–D7 in r8–r15, A0–A6 in r16–r22, PC in r24, SR/interrupt level
in r25). Patching the emulator's entry points to import that state into
a Musashi instance, and exporting it back at the Mixed Mode switch, the
exec-return word and interrupts, runs all 68k code on the host core (C3).
On arm64 that is most of System 7 at native-core speed with the PPC side
interpreting. Mac OS 9.1+ needs a real PPC MMU (why SheepShaver stops at
9.0.4); MAME's 750 has one, but turning translation on changes the Mac
side's view of addresses and is out of scope until something needs it.

### 6. A/UX: recreate the kernel

A/UX by machine emulation needs a 68030/040 PMMU, an FPU, real VIA,
SCSI, SCC and NuBus video, and Apple's ROM unpatched. Shoebill and QEMU
`q800` do that. Instead, recreate the kernel's interface, as qemu-user
and WINE do:

* COFF loader; argc/argv/envp on the stack.
* System calls (`trap #0`, number in a register, to be confirmed against
  A/UX's libc) handled in C++ on the host: SysV R2 with BSD additions,
  sockets, SysV IPC, signals, A/UX extras.
* `ioctl`s: SysV termios (`TCGETA` …) onto host termios, tty and pty.
* An extracted A/UX root tree on the host, with a fakeroot-style sidecar
  for ownership, permissions and setuid.
* Each Unix process has its own address space: switching the window
  table switches address spaces. Processes are minicoro coroutines; a
  blocking `read` or `wait` yields; `fork` copies the memory block and
  guest state. (Natively, one host process per A/UX process with real
  `fork()` is simpler, but doesn't work in wasm.)
* A/UX's Mac environment is a Unix process calling the Toolbox: that's
  Executor, running as a process inside the recreated kernel, with no
  ROM.
* The real CPU requirement is the FPU: A/UX's compiler emits 68881
  instructions, so the core needs solid 80-bit floating point. Executor
  itself reports no FPU (Gestalt `gestaltFPUType` 0).

Order: COFF + basic syscalls (`/bin/sh`, `ls`) → terminals, signals,
`fork`/`exec`/`wait`, pipes (interactive shell, `vi`, `make`, `cc`) →
sockets and SysV IPC → framebuffer and input devices (X server) → the
Mac environment on Executor.

## Order of work

Two tracks. D is Executor; C is the CPU cores, shared with the ROM
machines, detailed in [../cpu/PLAN.md](../cpu/PLAN.md). C1 comes before
D2's clean-up of the 68k side; C2 and C3 are independent of D.

| | Step | Gate |
|---|---|---|
| D1 | Musashi behind the syn68k facade, four windows restored | done 2026-10-23: `ctest -L executor` and the `_executor` bridge suites match UAE; no `mmap_min_addr`; UAE out of Executor (W0) |
| D2 | minicoro Process Manager; timer at yield points | Finder + several applications switching as today; Qt gone from `romlib`. **Done 2026-10-23** (section 2) |
| D3 | Dependencies and host edges | builds with only a C++17 compiler (generated files committed). **Compiler-only build done 2026-10-23**: Boost → `std` (`rsys/filesystem.h`: `fs` over `std::filesystem` with `time_t` modification times and `unique_path`; `commandline/options.h` for program_options), generated sources in `romlib/generated` (`-DEXECUTOR_REGENERATE=ON`, target `executor-generated`). **Host edges 2026-10-23**: the network (`rsys/hostnet.h`: POSIX-shaped sockets, the resolver calls, `run_detached` for lookups and one socket `Watcher` for MacTCP and OT; `romlib/host/posix/net.cpp` or `host/none/net.cpp` by `-DEXECUTOR_NET=posix|none`; on none MacTCP and OT load and answer with errors, no host threads) and the guest windows' memory (`cpu/syn68k_host.h`: reserve, window 1's pool size, the binary's data range; `cpu/host/posix/memory.cpp`). Left as they are, since Emscripten has them: files (`std::filesystem`, POSIX I/O), the clock (`std::chrono`), faults (`base/fault_handler.cpp`, one file already) |
| D4 | wasm build (W steps in section 4; decided 2026-10-23) | built-in Browser, then the Finder, in a browser tab. **Done** (W0–W4, 2026-10-23 to 10-31); the site since: WebSite.md |
| D5 | Demo features | HiDPI QuickDraw (2×), host files as Mac files, rootless windows; HyperCard from a link. **2× done** (HiDPI.md, natively and in the tab); rootless windows not started |
| D6 | PPC applications on the C2 core behind the PowerCore facade | a PPC application runs (milestone P). **Started 2026-10-21**: PowerPC-only applications on InterfaceLib run (`ppc_hello_executor`); StdCLib and MathLib native since 2026-10-26 (docs/executor/PPCLibs.md; `clib_executor`, `guest_suite_ppc_executor`). Since 2026-10-28 Mac OS 8.6's System and Finder with its PowerPC applications (MacOS8-9.md, SCOREBOARD.md); 9.0.4 under way |
| D7 | A/UX user-mode kernel | the five steps above |
| C1 | MAME's Musashi (PMMU, softfloat FPU, instanceable) as the 68k core | `--core mame-68k` passes every suite UAE and Musashi pass today; two instances in one process; vendored v3.32 retired. **C1a–c done 2026-10-10** (`src/cpu/mame/`); C1d PMMU parked (assessed 2026-10-11); **C1e done 2026-10-11** (v3.32 deleted, `musashi` an alias of `mame-68k`) |
| C2 | MAME's PowerPC recompiler as the PPC core, one base offset | boot matrix PPC cells pass with the UML interpreter, then the x86-64 back-end, then arm64 on Apple Silicon; no `mmap_min_addr`. **C2a–c done 2026-10-10**: `--backend kpx --core mame-ppc` boots 7.5.5 to the Finder, 1.6 s with the x86-64 back-end (`--jit`), and Mac OS 9.0.4 in 10 s with the guest suite passing as on Kheperix; merged to main. Since: arm64 Linux (C2d, 2026-10-11, under qemu; Apple Silicon not tried) and the base offset (mame-ppc's guest memory in a 4GB reservation, no `mmap_min_addr`) |
| C3 | Host 68k core in place of the nanokernel's 68k emulator | PPC machine boots 7.5.5 to Finder and passes the bridge suites with Musashi running the 68k side; boot and benchmark numbers against the DR emulator. **C3a done 2026-10-10** as a hybrid: host-initiated routines start on Musashi, Apple's 68k dispatcher included, and hand off at routine descriptors; C3b (the main 68k thread) experimental, does not boot yet |
| C4 | UML→wasm back-end | PPC machine in a tab faster than the interpreter (after D4). Not needed for M1 (W4's measurements, 2026-10-31): M2 |

D1–D3 and C1–C2 also make the native build portable (macOS, Windows).

**D1 so far** (the vendored v3.32 was deleted on 2026-10-11, C1e; MAME's Musashi took its place). Musashi (`src/cpu/musashi`, upstream v3.32, unmodified) is a
second 68k core. Both cores sit behind `cpu::Core` (`src/cpu/core/cpu_core.h`),
one interface meant for every core MacPhoenix runs, 68k or PowerPC: a
`GuestMemory` of 1MB pages (windows onto host memory, or an I/O handler),
host ops (the architecture's reserved host-call opcodes: `$71xx`, PPC primary
opcode 6), re-entrant `run()`, `step()`, an interrupt line, a thread-safe
`request_attention()` served at instruction boundaries, registers by id, and
context save/restore. `syn68k_common.cpp` is a `cpu::Host` on whichever core
`--core uae|mame-68k` names (default `uae`; `musashi` was the vendored v3.32,
retired 2026-10-11). Addressing is still identity
(one window over the whole space).

The ROM-based 68k Mac runs on the same interface (`src/cpu/cpu_m68k.cpp`,
Basilisk's EmulOps as its Host), so `--core mame-68k` boots the Quadra 650
and Mac SE ROMs too; every machine fills g_platform's memory and CPU
entries the same way (`src/cpu/core/platform_cpu.cpp`), and UAE's private
plumbing left the Platform table. The whole test suite passes on both
cores (ROM boot, SE, bridge, guest, Finder, keyboard; Executor's guest
suite has the same two network failures on both). Musashi needed one local
fix: 68040 line F (CINV/CPUSH/PFLUSH/PTEST, FDBcc/FTRAPcc). Since
2026-10-23 Executor runs on `mame-68k` alone (W0).

**Four windows (done for Musashi).** On the Musashi core Executor uses
upstream's four 1GB windows (`syn68k_public.h`): 0 guest RAM (plus a 64KB
tail so the end of RAM converts unambiguously), 1 a pool for
`syn68k_alloc_low` (stacks, framebuffer), 3 the binary's data; window 2 is
spare. No `vm.mmap_min_addr` needed. The UAE core keeps identity (all
offsets 0), since it fetches through one base pointer. Turning identity
off flushed out host code that relied on guest == host: a null process
record at startup, a nil `FSMakeFSSpec` name, the boot stack pointer and
the `.Disk` drive icon passed as truncated host pointers, `PBMakeFSSpec`
casting `ioMisc` straight to a pointer. Known issue: on Musashi a MacTCP
connect that times out (`openFailed` after the 60s ULP timeout) never
returns to MacPerl; UAE does. On Musashi the gtest suite matches UAE
(`ctest -L musashi`), Apple's 7.5.5 Finder boots, `command_bridge`,
`keyboard_guest` and `finder_suite` pass, `guest_suite` matches UAE (the
same two network failures). UAE left Executor on 2026-10-23 (W0), and
the identity layout, the `MAP_32BIT` pool and the test binary's low link
with it. (mac-phoenix itself stays linked low for Kheperix.)

## Open questions

* **Apple binaries or our own.** The current milestones run Apple's
  System file, Finder and extensions. A distributable build needs
  Executor's own System resources and a Finder replacement. Decide which
  the demo uses. Today both run: the site's library offers Apple's
  Systems as disk images the owner provides, and the public showcase
  (executor/SHOWCASE.md) uses Executor's own System or Apple's free
  7.5.3.
* **Musashi's FPU** (settled 2026-10-10 by C1). MAME's Musashi brings its
  softfloat FPU and the 030/040 PMMU.
* **Musashi re-entrancy** (settled in D1). `CALL_EMULATOR` runs nested
  68k from inside a trap; Musashi's cycle count is global, so a nested run
  ends the outer time slice early. The run loop checks for the exit cell,
  not the count, so that only costs an extra trip round the loop. MAME's
  copy keeps the count per instance, which removes the quirk.
* **Does the ROM side follow?** (settled 2026-10-10 by C1). Yes: one 68k
  core everywhere, with UAE kept only as long as its JIT is worth having.
