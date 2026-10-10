# Executor: where it goes next

Agreed in discussion 2026-10-09. [PLAN.md](PLAN.md) tracks the current
milestones (Finder, Apple's extensions, AppleScript); this file is the
longer direction those milestones feed into. D1 is under way on the
`executor-musashi` branch (see Order of work); the rest is not started.

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

Executor runs on UAE today with identity addressing: guest RAM mapped at
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
* Framebuffer to `<canvas>`, DOM input to `EventSink`, a preloaded System
  Folder and applications in the Emscripten filesystem, OPFS or
  IndexedDB for persistence, WebAudio for sound.
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
| D1 | Musashi behind the syn68k facade, four windows restored | done except dropping UAE from Executor: `ctest -L executor` and the `_executor` bridge suites match UAE; no `mmap_min_addr` on Musashi |
| D2 | minicoro Process Manager; timer at yield points | Finder + several applications switching as today; Qt gone from `romlib` |
| D3 | Dependencies and host edges | builds with only a C++17 compiler (generated files committed) |
| D4 | wasm build | built-in Browser, then the Finder, in a browser tab |
| D5 | Demo features | HiDPI QuickDraw (2×), host files as Mac files, rootless windows; HyperCard from a link |
| D6 | PPC applications on the C2 core behind the PowerCore facade | a PPC application runs (milestone P) |
| D7 | A/UX user-mode kernel | the five steps above |
| C1 | MAME's Musashi (PMMU, softfloat FPU, instanceable) as the 68k core | `--core mame-68k` passes every suite UAE and Musashi pass today; two instances in one process; vendored v3.32 retired. **C1a–c done 2026-10-10** (`src/cpu/mame/`); C1d PMMU gate and C1e retirement open |
| C2 | MAME's PowerPC recompiler as the PPC core, one base offset | boot matrix PPC cells pass with the UML interpreter, then the x86-64 back-end, then arm64 on Apple Silicon; no `mmap_min_addr`. **C2a–c done 2026-10-10**: `--backend kpx --core mame-ppc` boots 7.5.5 to the Finder, 1.6 s with the x86-64 back-end (`--jit`), and Mac OS 9.0.4 in 10 s with the guest suite passing as on Kheperix; merged to main; arm64 (C2d) and the base offset open |
| C3 | Host 68k core in place of the nanokernel's 68k emulator | PPC machine boots 7.5.5 to Finder and passes the bridge suites with Musashi running the 68k side; boot and benchmark numbers against the DR emulator. **C3a done 2026-10-10** as a hybrid: host-initiated routines start on Musashi, Apple's 68k dispatcher included, and hand off at routine descriptors; C3b (the main 68k thread) open |
| C4 | UML→wasm back-end | PPC machine in a tab faster than the interpreter (after D4) |

D1–D3 and C1–C2 also make the native build portable (macOS, Windows).

**D1 so far.** Musashi (`src/cpu/musashi`, upstream v3.32, unmodified) is a
second 68k core. Both cores sit behind `cpu::Core` (`src/cpu/core/cpu_core.h`),
one interface meant for every core MacPhoenix runs, 68k or PowerPC: a
`GuestMemory` of 1MB pages (windows onto host memory, or an I/O handler),
host ops (the architecture's reserved host-call opcodes: `$71xx`, PPC primary
opcode 6), re-entrant `run()`, `step()`, an interrupt line, a thread-safe
`request_attention()` served at instruction boundaries, registers by id, and
context save/restore. `syn68k_common.cpp` is a `cpu::Host` on whichever core
`--core uae|musashi` names (default `uae`). Addressing is still identity
(one window over the whole space).

The ROM-based 68k Mac runs on the same interface (`src/cpu/cpu_m68k.cpp`,
Basilisk's EmulOps as its Host), so `--core musashi` boots the Quadra 650
and Mac SE ROMs too; every machine fills g_platform's memory and CPU
entries the same way (`src/cpu/core/platform_cpu.cpp`), and UAE's private
plumbing left the Platform table. The whole test suite passes on both
cores (ROM boot, SE, bridge, guest, Finder, keyboard; Executor's guest
suite has the same two network failures on both). Musashi needed one local
fix: 68040 line F (CINV/CPUSH/PFLUSH/PTEST, FDBcc/FTRAPcc).

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
same two network failures). Next: drop UAE from Executor (then the low
link and `MAP_32BIT` go too), which C1 makes the natural moment.

## Open questions

* **Apple binaries or our own.** The current milestones run Apple's
  System file, Finder and extensions. A distributable build needs
  Executor's own System resources and a Finder replacement. Decide which
  the demo uses.
* **Musashi's FPU** (settled 2026-10-10 by C1). MAME's Musashi brings its
  softfloat FPU and the 030/040 PMMU.
* **Musashi re-entrancy** (settled in D1). `CALL_EMULATOR` runs nested
  68k from inside a trap; Musashi's cycle count is global, so a nested run
  ends the outer time slice early. The run loop checks for the exit cell,
  not the count, so that only costs an extra trip round the loop. MAME's
  copy keeps the count per instance, which removes the quirk.
* **Does the ROM side follow?** (settled 2026-10-10 by C1). Yes: one 68k
  core everywhere, with UAE kept only as long as its JIT is worth having.
