# Executor core in MacPhoenix

Goal: run the real Finder on a Toolbox reimplemented in C++, inside the
MacPhoenix shell (web UI, WebRTC video, HTTP API, bridge), on the UAE
68k JIT instead of syn68k.

Source: Executor 2000 (autc04/executor), MIT licence. Imported from
upstream commit `b2569eb1` into `src/executor/`. The upstream checkout in
`executor/` stays outside git as the reference and differential oracle.

## Shape

```
mac-phoenix (parent: web UI, HTTP, WebRTC, bridge)
   │ exec `mac-phoenix --ipc --backend executor` + SHM/socket
   ▼
mac-phoenix (child, same binary)
   ├── host/executor_child.cpp   frames → SHM, control socket → input hooks
   ├── romlib                    Executor Toolbox, imported nearly unchanged
   ├── syn68k facade             syn68k_public.h implemented over UAE
   ├── PowerCore facade          stub now, KPX later
   └── front-ends/phoenix        render + cursor composite, executor_host API
```

The swap happens behind two facades so the 120k lines of Toolbox code
compile with minimal edits:

* **syn68k facade** (`src/executor/cpu/`). Same header name and API:
  `cpu_state`, `EM_Dn/EM_An`, `READ*/WRITE*`, `callback_install`,
  `trap_install_handler`, `interrupt_generate`, `CALL_EMULATOR`,
  `destroy_blocks`. Implemented on UAE:
  * **Addressing is identity.** Guest address == host address. Guest
    RAM is mapped at host 0 (needs `vm.mmap_min_addr=0`, same as KPX),
    the binary links below 4GB like `mac-phoenix`, the emulator thread
    stack and framebuffer are allocated below 4GB. UAE runs
    `DIRECT_ADDRESSING` with `MEMBaseDiff = 0`, which keeps the JIT
    usable. Executor's four 1GB windows collapse into one.
  * **Callbacks** are 2-byte cells in a guest-visible callback page,
    each holding one EmulOp opcode. The handler finds the slot from the
    PC, syncs UAE registers into `cpu_state`, calls the host function,
    syncs back and jumps to the returned address.
  * **A-line traps** use the real exception: vector 10 at `$28` points
    at a callback, which reads the trap PC from the 68040 frame and calls
    Executor's `alinehandler`. The guest runs in supervisor mode, as
    Mac OS does, so the frame lands on A7.
  * **Interrupts**: `interrupt_generate(4)` raises a UAE level-4
    interrupt; vector 28 points at a callback; `MAGIC_RTE_ADDRESS` is a
    real `RTE` in the callback page.
  * **Nested execution**: `CALL_EMULATOR` pushes the exit address (an
    EmulOp cell) and re-enters `m68k_execute`, the same path
    BasiliskII's `Execute68k` uses.
  * `destroy_blocks` flushes the JIT cache range.
* **PowerCore facade**. PowerCore has no licence and does not come
  across. A stub header keeps the PPC code paths compiling; executing
  PPC aborts with a clear message. KPX later implements this facade.

Dropped from the import: syn68k, PowerCore, cxmon debugger, the
qt/x/sdl/sdl2/wayland front ends, LMDB (its CNID mapper was already
disabled), the bundled Browser app stays as a resource for now.

Memory-map vocabulary for M1: [MEMORY_MAP.md](MEMORY_MAP.md).

**M1 approach (decided 2026-10-07):** no ROM. Executor's C++ builds a
System memory area that looks identical to a real one — vectors,
lowmem, trap tables at `$400`/`$E00`, System heap holding Apple System
file resources, Process Manager heap, A5 worlds. Snapshots of real
7.5.5 boots (`POST /api/snapshot`, `tools/memmap/memmap.py`) are the
reference; nothing from them is copied in at runtime, and no Apple
boot or `ptch`/`lpch` code runs.

**Known incompatibility (test case):** WorldBuilder's *Demo World*
(games.img, 1987) corrupts its app heap between `LoadSeg` and its first
`MoreMasters`; Executor then hits a fatal heap error. Same on upstream
Executor/syn68k, so not the UAE facade. Likely direct heap/lowmem
pokes by a pre-32-bit-clean app; revisit with the M1 heap work.

**Where we left off:** snapshot API and a first reader (lowmem, heap
walk, trap classification) work; reference snapshot
`~/storage/snapshots/7.5.5-finder-idle`. Next: find the Process
Manager heap and partitions, tag System heap blocks with resource
type/ID, disassemble via Retro68 objdump, snapshot the Executor child,
then `diff-world`.

## Milestones

| | Milestone | Gate | Status |
|---|---|---|---|
| M0a | Core builds in-tree on the UAE facade | `mac-phoenix-executor` links | done |
| M0b | CPU parity | Executor's gtest suite on UAE matches syn68k | done: same two upstream failures, PPC tests skipped |
| M0c | Headless app run | built-in Browser reaches its event loop, screenshot | done: `executor.smoke.browser_screenshot` |
| M0d | Shell integration | `--backend executor` in the web UI shows video, takes input | done: one binary, UI option, `executor.smoke.web_backend` |
| M1 | Finder desktop | our boot phase, Apple System file as resource root, real Finder draws |
| M2 | Launch apps | Process Manager ours; Finder launches SimpleText, Kid Pix |
| M3 | Borrowed managers | QuickTime 1.6 Component Manager, Thread Manager, AE 1.0.1 |
| M4 | 7.5.5 + Drag Manager | |
| P | PPC | KPX behind the PowerCore facade, InterfaceLib to native |

Tools carried alongside:

* **Differential trap logs**: multiversal-driven logger in MacPhoenix
  (real ROM) versus Executor `--logtraps`.
* **Fault-on-ROM probe**: a 1MB faulting ROM range to enumerate every
  ROM address software reads.
* **Dual-mode tests**: Executor's Retro68 tests also run under real
  System 7 in MacPhoenix.

## Open questions

* Open Transport: parked. MacTCP plus a `dnrp` resolver first.
* Boot phase: reproduce the post-boot-2 state from a snapshot of real
  System 7.1 under MacPhoenix.

## Working notes

* Run: pick **Executor** as Emulator Mode or CPU in the web UI, or
  `./build/mac-phoenix --backend executor`. No ROM. Needs `vm.mmap_min_addr=0`.
* Executor's files live in `<storage>/executor/` (System Folder, prefs).
  That folder and the configured shared folder are the only host folders the
  guest sees; Executor's default of mounting `/` is disabled. Disk images
  from the config mount read-only.
* Status: `boot_phase` goes `Finder` at the first frame and `desktop` once
  the application first polls for events. `/api/app` reads `CurApName`.
* Input arrives on the IPC control socket and goes to Executor's EventSink
  through `IPCInputHooks`; the cursor is drawn into the frame by the front
  end, since Executor leaves cursors to the host.
* Executor menus are "sticky": releasing on a menu title leaves the menu
  open for a second click.
* Tests: `ctest --test-dir build -L executor`.
* Callbacks redirect PC by setting `next - 2` because UAE advances PC by 2
  after an EmulOp returns.
* UAE routes every memory access through `g_platform.mem_*` function
  pointers; the facade installs identity versions. Inlining them is an easy
  speed win. Interrupt delivery goes through `g_platform.m68k_intlev` and
  `m68k_poll_interrupts`.
* JIT is not enabled yet: every guest run is a nested call from host code,
  which uses UAE's interpreter loop.
* Never call `setenv` in the child once threads exist: Executor's path
  settings go through `ROMlib_path_overrides` instead.

## Changes to imported Executor code

Kept small so upstream fixes can be merged by hand:

* `mman/mman.cpp`: guest RAM via `syn68k_map_guest_ram()`; window setup removed.
* `vdriver/vdriver.cpp`: framebuffer via `syn68k_alloc_low()`.
* `base/logging.cpp`: pointer validity via `syn68k_is_guest_mapped()`.
* `main.cpp`: entry renamed `executor_main_entry`, low-stack emulator
  thread, no cxmon, phoenix front end.
* `file/fileMisc.cpp`: `ROMlib_path_overrides` before `getenv`.
* `file/localvolume/localvolume.cpp`: `ROMlib_local_volume_roots` instead of `/`.
* `hfs/hfsHelper.cpp`: `ROMlib_readonly_images`.
* `toolevent.cpp`: `ROMlib_app_polled_events` set by Get/WaitNextEvent.
* `mman/mmansubr.cpp`: `ROMlib_heap_death_dialog`; off in MacPhoenix, so a
  fatal heap error logs and `_exit(70)`s instead of crashing in the dialog.
* `file/localvolume/localvolume.cpp`: LMDB include dropped.
* `tests/`: low-stack test thread, PPC tests skip, fixture path, ctest names.
