# Executor core in MacPhoenix

Goal: run the real Finder on a Toolbox reimplemented in C++, inside the
MacPhoenix shell (web UI, WebRTC video, HTTP API, bridge), on the UAE
68k JIT instead of syn68k.

Source: Executor 2000 (autc04/executor), MIT licence. Imported from
upstream commit `b2569eb1` into `src/executor/`. The upstream checkout in
`executor/` stays outside git as the reference and differential oracle.

Longer direction (Musashi with translated addressing, minicoro
processes, wasm, A/UX user-mode kernel): [DIRECTION.md](DIRECTION.md).

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
7.5.5 boots (`POST /api/snapshot`, `tools/macdecode/macdecode.py`) are the
reference; nothing from them is copied in at runtime, and no Apple
boot or `ptch`/`lpch` code runs.

**Known incompatibility (test case):** WorldBuilder's *Demo World*
(games.img, 1987) corrupts its app heap between `LoadSeg` and its first
`MoreMasters`; Executor then hits a fatal heap error. Same on upstream
Executor/syn68k, so not the UAE facade. Likely direct heap/lowmem
pokes by a pre-32-bit-clean app; revisit with the M1 heap work.

**Decoder:** `tools/macdecode/macdecode.py SNAPSHOT [--serve]` decodes a
snapshot using multiversal (`src/executor/multiversal/defs/*.yaml`) as
its database: lowmem globals by multiversal name and type, zones
(including the Process Manager heap and app partitions, found by
scanning), resource maps, and trap names. `--serve` (port 8765) shows the
decode next to multiversal as HTML and reloads when a YAML file changes.
Multiversal stays untouched for now: what we learn goes in the overlay
`tools/macdecode/learned.yaml` (lowmem names/types and trap names, each
with a status: known, guess or placeholder), which the decoder layers
on top. Unnamed nonzero lowmem becomes placeholder entries for it. First
entry: `$2B6` is `ExpandMem` (multiversal calls it `AE_info`).

Origins: the decoder matches the System heap against the resources of
the boot disk's System Folder (hfsutils), so trap entries and heap
addresses say where their code came from — ROM, `System 'lpch' 31`,
`'ptch'`, `'scod'` (Process Manager), `'gpch'`, or an INIT by file.
On 7.5.5 at Finder idle: 595 entries in ROM, 350 of 464 patched entries
traced, 114 unknown. Linked patches are relocated and compacted by the
loader, so a masked match (address fields as wildcards) backs up the
exact one.

**Diff:** the Executor child takes snapshots too (at its event loop,
`/api/snapshot`), and `macdecode.py REF --diff CANDIDATE` (or
`--serve --diff`, page `/diff`) scores Executor's memory against a real
boot. Baseline, 7.5.5 Finder idle vs Executor's Browser idle: lowmem 76
same + 19 same shape of 247 (100 differ, 52 unset); System resources
loaded 9 of 78; trap tables at `$400`/`$E00` 5 of 1059; no Process
Manager heap (one 61 MB ApplZone instead).

**Next (agreed 2026-10-07):** Executor sideloads Apple's System file and
its C++ builds the memory a real boot leaves, step by step, each checked
with the diff: (1) System heap contents and lowmem setup from Apple's
System file, with a mapping file choosing Apple's 68k code or
Executor's C++ per code resource; (2) our own Process Manager in C++
with Apple's layout (PM heap up to BufPtr, SIZE-sized partitions, A5
world at the top, per-process lowmem swap); (3) trap tables at
`$400`/`$E00` once extensions need to patch.

**Step 1 started:** `tools/macdecode/sideload_system.py IMAGE DIR` puts
Apple's System file into an Executor data directory, and
`--executor-data DIR` (config `executor_data_dir`) points Executor at it.
Apple's 68k definition procedures would replace Executor's native stubs
(Apple's MBDF calls unimplemented `$A81E` and recursed into a crash), so
a resource policy (`src/executor/res/resource-policy.txt`, embedded;
`<data_dir>/resource-policy.txt` overrides) serves Executor's version of
listed System resources from `ROMlib_mgetres2`; the map stays Apple's.
Executor's Browser now runs on Apple's 7.5.5 System file; System
resources loaded 21 of 78 (was 9). The rest come from components we
don't have yet: Process Manager (`scod`, `proc`, `lstr`, `lmem`),
Script Manager (`itl0-2`, `KSWP`), `PACK` 6/14, `dcmp` 0. The
Process Manager's per-process lowmem list is `lmem` -16458 (see
MEMORY_MAP.md): the spec for our context switch.

**Step 2, slice 1 (one process, real layout):** `ROMlib_InitZones`
makes the region above the System heap the Process Manager heap
(`BufPtr` at its top; the boot stack sits above `BufPtr`). At launch,
`process_layout_partition` peeks the app's SIZE and CODE 0, allocates a
locked partition (preferred + 16K) at the top of the PM heap, and lays
out `ApplZone`, `ApplLimit`, the stack, the A5 world and `MemTop` inside
it as 7.5.5 does (MEMORY_MAP.md, *Application partitions*); the app's
resource map then opens inside its heap. Diff against the Finder-idle
reference: lowmem same shape 19 → 33, differs 100 → 87; zones now show
a PM heap holding `ApplZone:Browser`. Slices next: process records and
the `lmem` swap (2), per-process host stacks and switching (3).

**What Finder needs (trace):** `mac-phoenix --trace-atraps` (UAE) records
every A-trap call site as a routine: trap, dispatcher selector
(`tools/macdecode/dispatch.py`: multiversal + Universal Interfaces glue +
`learned.yaml`), component instance or driver + csCode, the enclosing
routine, and the resource holding the caller. `tools/macdecode/atraps.py`
and the web view's `/needs` page classify each routine: needed if code
Executor runs calls it (Finder, System defprocs not in the resource
policy, optionally extensions), then done / basilisk (host code in the
Basilisk II core) / todo; struck out if only Apple ROM/patch code,
hardware/boot or replaced System code calls it. Compressed resources are
decompressed with `rsrcfork` (venv at `~/.venvs/macdecode`). 7.5.5 to
Finder: 16 todo, 2 basilisk (`.Disk` status/control), 171 done.
The tracer also records every trap install (`trap_installs.tsv`: slot,
old/new address, installer and target resources), shown as each slot's
patch history on `/traps`. `/entrypoints` lists all 4098 known entry
points (traps, selectors, drivers, components) with checkbox filters;
the work list (needed, not in Executor) is 28. Hardware/boot/debug tags
live in `learned.yaml` (`tags:`) for review.

**Where we left off:** snapshot API and a first reader (lowmem, heap
walk, trap classification) work; reference snapshot
`~/storage/snapshots/7.5.5-finder-idle`. Next: find the Process
Manager heap and partitions, tag System heap blocks with resource
type/ID, disassemble via Retro68 objdump, snapshot the Executor child,
then `diff-world`.

**M3 shape (decided 2026-10-08): extensions on our own trap tables.**

* **Trap tables.** Executor's C++ builds real tables at `$400`/`$E00`.
  Every entry is a 68k-callable address (a stub into the C++ trap), so
  `GetTrapAddress` returns something callable and a head or tail patch
  can jump to the old entry. Seeding comes first, then
  `trap_installs.tsv` from a real boot to check each patch history.
  Come-from patches that test ROM return addresses never fire. That is
  expected; they patch ROM bugs we don't have.
* **INITs from a whitelist** (`res/extension-policy.txt`, overridable by
  `<data_dir>/extension-policy.txt`, same style as the resource policy). Extensions and control panels load in
  the Start Manager's order, through our INIT loader (`INIT` resources,
  `ShowInitIcon`, `cdev` INITs). Files that aren't listed are skipped,
  so one bad INIT can't take the boot down. Apple's `ptch`/`lpch`/`gpch`
  code is still never run.
* **Component Manager: a C++ rewrite.** On 7.5.5 it isn't in the
  Quadra ROM. It's `System 'gpch' 667`, entered from the `$A82A` slot
  that `lpch` 31 installs, so it can't run as is without the
  linked-patch loader. Executor never had one (`$A82A` hits the fatal
  unimplemented-trap handler). The manager is a registry and
  dispatcher (`_ComponentDispatch` selectors: register/unregister,
  count/find/info, open/close, instance storage/A5/refcon/error,
  resource files, list seed, `CallComponent`), so it's written in C++.
  The components (`thng` + code from the System file, `thng` files,
  QuickTime, Easy Open) run as Apple's 68k code; `CallComponent` calls
  their entry points with a `ComponentParameters` block. Gestalt `'cpnt'`
  reports the 7.5.5 version. Until it lands, `$A82A` is a stub with no
  components (`CountComponents` 0, `FindNextComponent` 0,
  `OpenComponent` nil), so a stray call fails softly. On a real boot,
  Finder never calls `$A82A`. The callers are Script Manager `ptch` 27
  (text services), the System file's own components, and Easy Open,
  QuickTime, AppleScript, Color Picker, Speech Manager and Color SW Pro.
* **Whitelist order** (small and self-contained first): Color Picker →
  Speech Manager + MacinTalk 3 → AppleScript (+ Finder Scripting
  Extension) → QuickTime. Macintosh Easy Open ships its own `lpch` 31
  (linked patch code), so it likely won't load as a plain INIT; skip it.

**Write or borrow (decided 2026-10-08).** Apple's code runs as is when
it's a self-contained resource (`PACK`, definition procedure,
component, INIT, a standalone pre-7.5 extension) *and* reaches the rest
of the system only through public traps; the resource policy and the
INIT whitelist are the switch. It's written in C++ when it exists only
as linked patch code (`lpch`/`gpch`: loading it means faking the ROM it
binds to, so no linked-patch loader), when it's tied to internals we
own in C++ (Process Manager context switch, Window Manager layers,
Memory/File Manager internals), or when it's a thin dispatcher on a hot
path. The trace and diff tools make a C++ rewrite cheap to check against
a real boot; debugging Apple's 68k against a non-Apple internal state is
the expensive direction.

| Piece | Decision | Why |
|---|---|---|
| Component Manager | C++ (M3) | `gpch` 667; dispatcher |
| Thread Manager | C++ (M3) | ~20 selectors, all stack switching inside our Process Manager |
| Drag Manager | C++ (M4) | the pre-7.5 extension hooks Window/Process Manager internals |
| Help Manager | Apple's `PACK` 14 (M3) | 68k calling public traps; resource policy `apple` |
| Apple Event Manager | try Apple's `PACK` 8 (M3) | may close Finder's `'aevt'` wire-format gap; depends on HLE delivery matching Apple's (unverified) |
| TSM, Dictionary, Collection Managers | stub | Gestalt absent, error results, until a real caller shows up |
| Translation Manager / Easy Open | skip | its own `lpch` |
| Third-party INITs poking ROM or private structures | never written | whitelist what works; mimic a private structure only if a popular INIT needs it and it's cheap |
| Come-from patches | nothing to do | they fix ROM bugs we don't have |
| Apple Guide | as is, last, if ever | patches Help/Menu/Window Managers and drives apps through AEs |

**Gaps to close while building the System world (agreed 2026-10-08).**
These are boot-time structures or loading decisions, so they go in
alongside the M1–M3a work, in this order (status as of 2026-10-09):

1. **Device Manager unit table.** `UTableBase`, `UnitNtryCnt` and the
   boot-time drivers (`.Disk`, `.Sound`, `.IPP`, the System file's
   DRVRs) laid out as a real boot leaves them, diffed against a
   snapshot. Today `UnitNtryCnt` stays 0, so `C_SystemTask` never sends
   `accRun` to any driver; that blocks async MacTCP (see
   `mactcp/mactcp.cpp`) and every periodic driver. *Open.*
2. **Shadowing switch** (with M3a). When a whitelisted INIT provides a
   manager, Executor's built-in version withdraws its Gestalt selector
   and leaves the trap slot to the INIT: `quicktime.cpp` (stubs),
   `SpeechManager.cpp` (Mac-host only), `qColorPicker.cpp`. Otherwise
   the real INIT sees "already installed" and backs off. *Open* (not
   needed yet: none of the allowed extensions provides one of these).
3. **DA Handler** (with the M2 Process Manager slices). Opening a desk
   accessory starts the DA Handler process from the System file;
   Calculator, Key Caps, Scrapbook and the Chooser need it. *Done*
   (27e956ba): DAs run in their own processes; Key Caps draws like the
   real one.
4. **Alias resolution, boot path.** `ResolveAlias` is a stub
   (`alias.cpp`). Startup Items and Apple Menu Items are mostly
   aliases: resolve alias record → FSSpec by volume and directory IDs
   now; relative-path and cross-volume search later. *Done* (21e95977,
   c6013824): relative path first, then volume + full path, then volume +
   directory ID; Finder alias files and chains; file ID references.
5. **MacTCP resolver as a resource.** Apps load the DNR from a `dnrp`
   resource; serve a `dnrp` that calls native code, through the
   resource policy. *Open*: `guest_suite_executor`'s `dns_resolve`
   fails on it (and `udp_send` on the missing UDP).

Independent, any time: **sound output.** `sound/sounddriver.cpp`
always picks `SoundFake`, so Executor is silent. A host `SoundDriver`
feeding MacPhoenix's audio pipeline (from `host/executor_child.cpp`) is
needed before M3f's QuickTime and Speech gates mean anything.

Later, not loading work: TrueType rendering (the `sfnt` resources are
already in the Fonts chain; the Font Manager and QuickDraw need a
FreeType-backed scaler), printing and the Chooser (after the DA
Handler), AppleTalk (after the unit table), CFM-68K and the Shared
Library Manager (INITs, after M3b), the rest of MacTCP (UDP, async
sockets, after the unit table). Open Transport stays parked: for 7.5.5
MacTCP is the API that matters; if OT is ever needed, its client API
goes in C++ on host sockets rather than Apple's stack on a fake
Ethernet driver.

## Milestones

| | Milestone | Gate | Status |
|---|---|---|---|
| M0a | Core builds in-tree on the UAE facade | `mac-phoenix-executor` links | done |
| M0b | CPU parity | Executor's gtest suite on UAE matches syn68k | done: same two upstream failures, PPC tests skipped |
| M0c | Headless app run | built-in Browser reaches its event loop, screenshot | done: `executor.smoke.browser_screenshot` |
| M0d | Shell integration | `--backend executor` in the web UI shows video, takes input | done: one binary, UI option, `executor.smoke.web_backend` |
| M1 | Finder desktop | our boot phase, Apple System file as resource root, real Finder draws | done: Finder 7.5.5 reaches its desktop on Apple's System file, draws icons, opens windows, Apple/Help/Application menus |
| M2 | Launch apps | Process Manager ours; Finder launches SimpleText, Kid Pix; DA Handler opens Calculator and the Chooser; Startup Items and Apple Menu Items aliases resolve | in progress: Finder launches applications (SimpleText, Note Pad, Jigsaw Puzzle, MacPerl, Script Editor) side by side; switching by click or Application menu; quit back to the launcher; desk accessories in DA Handler processes; aliases and Startup Items resolve; `command_bridge_executor` 7/7. Open: Chooser (printing), Kid Pix |
| M3a | Trap tables | real tables at `$400`/`$E00`, every entry 68k-callable; patch histories diff against `trap_installs.tsv`; `$A82A` stub (no components); unit table matches a real boot and drivers get `accRun` | not started: Executor's own trap tables (`trapglue.h`), `UnitNtryCnt` 0 |
| M3b | Whitelisted INITs | INIT loader in Start Manager order, `ShowInitIcon`, `cdev` INITs; shadowing switch withdraws Executor's built-ins; Color Picker loads (with M3c) | in progress: loader runs allowed INITs/cdevs/fext in Start Manager order (`extension-policy.txt`, phase 1 + AppleScript + Finder Scripting); `jGNEFilter` chain. Date & Time's menu bar clock and Color Picker 2.0 (2026-10-10). Open: `ShowInitIcon`, shadowing switch |
| M3c | Component Manager in C++ | System file components register; Color Picker's `GetColor` matches a real boot | done (2026-10-10): `component.cpp` (5cd8f3e2), System file / extension / `thng` file components register, AppleScript's components run; Color Picker 2.0's `GetColor` shows Apple's dialog with the HSL picker (dialog background: STRAGGLERS.md 17) |
| M3d | Thread Manager in C++ | Gestalt `'thds'`; a threaded app (Netscape 2/3, Fetch) runs (needs M3h for its network) | not started |
| M3e | Apple's packs | Help Manager `PACK` 14 shows balloons; Apple Event Manager `PACK` 8 tried against Finder `oapp`/`odoc` | in progress: Apple's `PACK` 8 runs (installed by AppleScript), `oapp`/AppParameters in Apple's wire format. Open: Help Manager balloons |
| M3f | Extension set | Speech Manager + MacinTalk 3, AppleScript + Finder Scripting Extension, QuickTime load and work (QuickTime movie plays in SimpleText) | in progress: AppleScript loads by default; Script Editor runs `3 + 4 -> 7`; scripting additions run from MacPerl; Finder Scripting Extension loads. Open: `tell application "Finder"` (target spec empty, `finder_suite_executor` fails), Speech, QuickTime |
| M3g | Sound output | host `SoundDriver` replaces `SoundFake`; `SysBeep` and `SndPlay` audible in the web UI | not started |
| M3h | MacTCP finished | `dnrp` resolver, UDP, async calls on the `accRun` pump; Fetch connects to a host name | in progress: TCP on host sockets (`executor.MacTCP.*`, `tcp_socket` passes in the guest suite). Open: `dnrp`, UDP, async |
| M4 | 7.5.5 + Drag Manager | Drag Manager in C++; drag between SimpleText and Finder; TSM/Dictionary/Collection stubs; TrueType via FreeType | not started |
| P | PPC | KPX behind the PowerCore facade, InterfaceLib to native | not started |

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
* System disk: `--executor-system IMAGE` (Settings > System disk) names a
  disk image in `<storage>/images`. On first use its System Folder is
  copied out (`sideload_system.py`) into
  `<storage>/executor-systems/IMAGE.clean/`, and the image is mounted
  read-only beside it so its applications are there. `IMAGE/` is the
  working copy the run uses, restored from `.clean` each run unless "start
  fresh" is off (file times are kept: Finder compares its own date with
  its preferences' segment cache). Re-extract (`POST /api/executor/system
  {image}`) after the image changes. A name that isn't an image picks a
  hand-made `NAME.clean`. `--executor-start finder|browser|path`,
  `--[no-]executor-fresh`; resolved by the parent
  (`src/core/executor_systems.cpp`) into `--executor-data/--executor-app`.
  The web UI offers a built-in "exec" profile.
* Without a System, Executor's files live in `<storage>/executor/` (System Folder, prefs).
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
* Tests: `ctest --test-dir build -L executor` (unit + smoke). On Apple's
  7.5.5 System through the bridge: `ctest --test-dir build -R _executor$`
  (`command_bridge_executor`, `guest_suite_executor`,
  `finder_suite_executor`).
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
* `executor.cpp`: launches `FNDR` files (Finder) as well as `APPL`/`MPST`.
* `alias.cpp`: `FindFolder` with `kCreateFolder` creates the folder (was a
  `paramErr` stub).
* `res/resGetinfo.cpp`: `GetResourceSizeOnDisk` returns the on-disk length
  (compressed size), not the loaded handle's size.
* `mman/tempmem.cpp`: temporary memory comes from the Process Manager heap.
* `file/localvolume/item.cpp`: Mac/Unix epoch counts 1904's leap day
  (host file dates were a day early).
* `toolevent.cpp`: no `/tmp/testvol` probe (upstream posted its mount
  result as a `diskEvt` on the first `GetNextEvent`).
* `drag.cpp` (new): private `DragDispatch` selector Finder needs.
* `process.cpp`: private `OSDispatch` selectors Finder needs ($14 desk
  layer, $31/$32 Apple menu items, $42, $47, $55; names are guesses, see
  `tools/macdecode/learned.yaml`); every application gets an initialised
  scrap.
* `scrap.cpp`: `ScrapName` points at `ScrapTag` ($970) in low memory.
* `mman/mman.cpp`: `HandleZone` places an empty handle whose master pointer
  is in the Process Manager heap in that heap (temp memory).
* `gestalt.cpp`: `gestaltOSAttr` on 68k too, with bit 10.
* `iu.cpp`: Type Select (`Pack6` $28-$2E).
* `icon.cpp`: private `IconDispatch` $218/$220.
* `file/localvolume`: folders keep Finder info in `.finf` (Basilisk II
  layout) instead of failing `PBSetCatInfo` with `paramErr`.
* `base/traps.impl.h`: an unknown selector snapshots (fatal hook) before
  aborting.
* `file/localvolume/localvolume.cpp`: positioning uses `ioPosMode & 3`
  (flag bits such as `noCacheBit` were read as an unknown mode).
* `error/error.cpp`: `ROMlib_fatal_hook`, called first on a fatal error (the
  child snapshots RAM and registers as `executor-fatal`).
* `res/resPrivate.cpp` (new): private `ResourceDispatch` selectors;
  multiversal `ResourceDispatch` selector mask `D0<0xFF>`.
* `config/front-ends/phoenix/executor_host.cpp`: the data folder (and the
  shared folders) are the volumes, so the boot volume's root holds the
  System Folder; volumes are named after their folder.
* `mman/mman.cpp`: `HandleZone` keeps an empty handle in `TheZone` when that
  zone (nested in the Process Manager heap) holds its master pointer.
* `icon.cpp`: `GetLabel` takes nil outputs and label 0; Icon Utilities
  rewritten around one icon source (suite, cache, `PlotIconMethod` getter):
  alignment, label/selected/open/offline/disabled transforms, regions, hit
  tests, icon caches, `ForEachIconDo`; suite layout in `rsys/icon.h`;
  multiversal `IconGetterUPP`/`IconActionUPP` are typed callbacks.
* `alias.cpp`: `FindFolder` desktop/trash/temporary folders are "Desktop
  Folder", "Trash" and "Temporary Items" at the volume root (Apple's Folder
  Manager names), created invisible; were host `/tmp`.
* `finder.cpp`: the Desktop Manager (`PBDT*`), one database per volume, kept
  in `.desktopdb` at a host volume's root; host volumes report
  `bHasDesktopMgr` (`hfs/hfsXbar.cpp` `PBHGetVolParms`).
* `hostdisk.cpp` (new): `.Disk` driver (unit -63, as Basilisk II) and a
  fixed drive per host volume: driver gestalt, drive info, drive icon;
  multiversal `NDEVICES` 96 (the real unit table's size).
* `quickdraw/qIMVI.cpp`: `BitMapToRegion` builds the region in host memory
  and sizes the handle once (it wrote past a handle it failed to grow).
* `res/resInit.cpp`: the Fonts folder's suitcases are opened and chained
  below the System file (System 7.1+); `launch.cpp` keeps the System file
  and everything below it open across launches (it closed every file but
  the System file).
* `quickdraw/font.cpp`: a family whose first FOND points at a missing face
  (Chicago 12 is `FONT` 12 in a real ROM) tries the same family's FOND in
  the other open files.
* `menu/sysmenu.cpp` (new): the Help and Application system menus, kept at
  the end of the menu list with icon-suite titles; `menu.cpp` inserts
  application menus before them and re-adds them after `ClearMenuBar`/
  `SetMenuBar`; `stdmbdf.cpp` right-aligns and draws them, hit-tests by
  title width; `balloon.cpp` `HMGetHelpMenuHandle` returns the Help menu;
  `stdmdef.cpp` key equivalent $1C means a script code, not an icon.
* multiversal `umacdriver` starts like a `DRVR` header (name at 18).
* `gestalt.cpp`: `dply` 7, `dplv` $00020006, `hdwr`, `vm  ` 0, as on a real
  7.5.5 boot.
* `quickdraw/displays.cpp`: the Display Manager calls Finder makes (first/
  next screen device, draw desktop rect/region, desk region, notify procs,
  private -6/-9/-10 as 7.5.5 implements them); multiversal
  `DisplayDispatch` and `DialogDispatch` select on D0's low byte.
* `dial/dialDispatch.cpp`: `DialogDispatch` 7 (`IsCancelEvent`, a guess) and
  8 (`CheckEventQueueForUserCancel`).
* `process.cpp`: the Process Manager runs several processes. Each but the
  first has a `QThread` whose `run()` moves onto a stack from a pool below
  4GB (guest code gets pointers into Toolbox C++ frames), so its frames
  survive while others run; one runs at a time (a baton, `QMutex` +
  `QWaitCondition`). A switch saves and restores the low memory 7.5.5's
  Process Manager switches (the System file's `lmem` -16458, plus
  ApplZone, ApplLimit, WindowList and, because Executor allocates them in
  the application heap, AuxWinHead, AuxCtlHead, MenuCInfo), the 68k context
  (`syn68k_save_context`), the trap tables (applications' own patches) and
  registered host state (HLE queue, palette list, AE handler tables).
  `LaunchApplication` with `launchContinue` creates the process; it first
  runs at the launcher's next event call (as on 7.5.5). A process that quits
  is tidied up (windows, resource files, VBL tasks, partition) and its
  launcher gets the uncovered screen redrawn and, with SIZE
  acceptAppDiedEvents, `'aevt'/'obit'` in 7.5.5's wire format. PSNs start at
  $2000. The Application menu is system-wide, lists the processes and
  switches between them.
  Layers: each process's window list is its layer, in a front-to-back
  order. The Window Manager clips out layers in front (`ClipAbove`,
  `CalcVis`; the desktop also those behind) and carries `PaintBehind` /
  `CalcVisBehind` on into the layers behind (`ROMlib_layers_*`, via a
  layer view that lends it another process's list). A click in another
  process's window, or on the desktop (Finder's), brings that process to
  the front at the front process's next event call; so does
  `SetFrontProcess`. The old front's window is unhilited, the new one's
  hilited, with activate events unless SIZE doesActivateOnFGSwitch and
  suspend/resume events with acceptSuspendResumeEvents; the newly front
  layer's uncovered parts are redrawn. Background processes get time when
  something waits for them (updates, suspend/resume, activate) and never
  mouse or keyboard events. The Apple menu items are the system's; each
  process's Apple menu catches up with them.
* `time/syncint.cpp`: the timer is a Qt thread; it wakes whichever thread
  is idle in `syncint_wait_interrupt()` (upstream aimed SIGALRM at one).
* `toolevent.cpp`: `WaitNextEvent` also checks its own deadline (its
  timeout flag is shared by all processes).
* `wind/windInit.cpp`: a later process's `InitWindows` leaves the Window
  Manager ports, desktop pattern, gray region and screen alone.
* `wind/windMisc.cpp`: painting the desktop is an update for the desktop
  layer (Finder redraws its icons).
* `quickdraw/qPaletteMgr.cpp`: the window/palette list is per process;
  `DisposePixPat`/`DisposePixMap` ignore a dead handle like 7.5.5
  (`ROMlib_live_handle_p`).
* `file/fileHighlevel.cpp`, `file/fileUnimplemented.cpp`: `FSpExchangeFiles`
  and `PBExchangeFiles` swap contents and dates, keeping names and Finder
  info (upstream only did it for one application).
* `menu/menu.cpp`, `menu/stdmbdf.cpp`: `MenuDispatch` ($A825, D0 low byte):
  `InsertFontResMenu`, `InsertIntlResMenu`, private -6 (`IsSystemMenu`) and
  -5 (`DrawMenuBarMessage`, names guessed) which, like 7.5.5, has the MBDF
  (message 15) clear the bar and draw a bold message across it until the
  next `MenuSelect`/`MenuKey`/`DrawMenuBar`.
* `appleevent/AE_hdlr.cpp`: ExpandMem+$1AE (System heap reserve) is 64K;
  multiversal `AE_info_t` names it.
* `file/localvolume/localvolume.cpp`: host volumes report a 2 GB HFS volume
  holding the folder's real contents (capped by host free space) and its
  file/folder counts (Finder only rebuilds the desktop database of a volume
  with files), and bless their System Folder (`vcbFndrInfo[0]`).
* `menu/sysmenu.cpp`, `process.cpp`: the Apple menu from Finder's Apple Menu
  Items list (OSDispatch $31/$32, 22-byte $31); `AppendResMenu('DRVR')`
  with Apple's System file marks the Apple menu instead of listing desk
  accessories and Executor's items; `MenuSelect`/`MenuKey` bring it up to
  date; `stdmdef.cpp` draws each entry's small icon (18-pixel rows) and
  sizes items as 7.5.5 does (10-pixel margin, command-key room only with a
  key).
* `menu/sysmenu.cpp`, `desk.cpp`: `OpenDeskAcc` of an Apple Menu Items entry
  posts its owner the raw 28-byte 'aevt'/'amis' event (key parameter) the
  7.5.5 Process Manager sends; Finder parses it itself and opens the item.
* `osevent/hle.cpp`, `process.cpp`: `AcceptHighLevelEvent` fills in the
  sender (the current process's port, no location; Finder drops events
  from other machines); `GetProcessSerialNumberFromPortName` and
  `GetPortNameFromProcessSerialNumber` for the current process.
* `version.cpp`: with Apple's System file (it has `lpch`), the system
  version comes from its `vers` 1 and its resources are left alone
  (`ROMlib_apple_system_file`); `gestalt.cpp` `mach` 20 as on the
  reference boot.
* `tests/`: low-stack test thread, PPC tests skip, fixture path, ctest names.
* `res/resOpen.cpp`: header version 9 compressed resources
  (`dcmp` 3 entry offsets, in-place from the end); `HOpenResFile` clears the
  map's handle fields before preload; the `dcmp` working buffer is sized as
  7.5.5 sizes it.
* `desk.cpp`, `device.cpp`, `hfs/hfsXbar.cpp`: desk accessories run in DA Handler processes
  (`LaunchDeskAccessory`, OSDispatch $36); RAM driver entry points fixed (no
  DRVR ever ran) and `JIODone` set; `PBClose`/`PBRead`/`PBWrite` route driver
  refnums to the Device Manager; `LayerDispatch` 9/$C/-10 (guessed names).
* `osevent/hle.cpp`: high-level events go to their receiver through one
  System-heap queue, always as `kHighLevelEvent`, in one block (header, then
  message); `GetSpecificHighLevelEvent`'s filter gets the sender's TargetID;
  `AcceptHighLevelEvent` copies what fits into a small buffer.
* `extensions.cpp` (new): `ROMlib_load_extensions` runs allowed INITs, cdevs,
  RDEVs and `fext` INITs before the Finder (Extensions, Control Panels, System
  Folder, name order; `sysz` checked); others move to `<folder> (Disabled)`.
  `jGNEFilter` chain; XPRAM from the Basilisk side.
* `alias.cpp`: `ResolveAlias`, `FollowFinderAlias`, `ResolveAliasFile`
  (relative path, then volume + path, then volume + directory ID);
  `AliasDispatch` $A (`FindFolderWithTable`, a guess).
* `file/fileUnimplemented.cpp`: file ID references (create, resolve, delete).
* `component.cpp` (new): the Component Manager (`$A82A`), see M3c.
* `appleevent/`: Executor's handler tables move out of ExpandMem (Apple's
  `PACK` 8 owns those fields); ExpandMem has its real header (version $144,
  $288 bytes); multiversal `bufferIsSmall`/`noOutstandingHLE` were swapped.
* `process.cpp`: `GetProcessInformation` fills name, spec and launcher;
  OSDispatch $40/$41 (`Begin`/`EndSystemMode`, guessed; system-mode files shared by every process since 2026-10-10); default directory
  per process; `LaunchApplication` of a running application returns it;
  Startup Items launch when Finder first idles (stand-in).
* `mman/mman.cpp`: `HandleZone` uses `TheZone` for an empty handle only when
  it is a real zone (Finder Scripting runs with `TheZone` set to a temp
  handle).
* `gestalt.cpp`: `GestaltValueDispatch` ($ABF1: `NewGestaltValue`,
  `ReplaceGestaltValue`, `SetGestaltValue`, `DeleteGestaltValue`), values
  on a host list consulted before the selector functions.
* `quickdraw/qMisc.cpp`: `ScrnBitMap` ($A833) as the Quadra ROM's: the main
  device's PixMap copied as a 14-byte BitMap.
* `menu/stdmbdf.cpp`, `menu/menu.cpp`: MBDF message 14 (`mbTitleRect`, a
  guess) and MenuDispatch -4..-1 on it (`GetSystemMenuTitlesRect`,
  `GetAppMenuTitlesRect`, `GetMenuBarRect`, `GetMenuTitleRect`, guesses),
  laid out as 7.5.5's MBDF 0 does; Date & Time's clock sits left of the
  system menus' rectangle.
* `process.cpp`: OSDispatch $5E stored (`SetHideDesktopInBackground`, a
  guess: General Controls' "Show Desktop when in background", inverted);
  `GetFrontProcess` is a plain multiversal function (its inline pushes a
  long -1).
* `extensions.cpp`, `init.cpp`: INITs run in the System heap (`NewGestalt`
  takes selector functions from there only); `AppPacks` holds the System
  file's `PACK` 0-7 as `InitAllPacks` leaves them, after every launch's
  lowmem reset too (Date & Time's clock checks `AppPacks[6]` before `Pack6`);
  stand-ins for the ROM packages 4, 5 and 7 (Apple's header, code = the
  package's trap), which Date & Time's panel checks before `NumToString`.
* `wind/windInit.cpp`: `CloseWindow` and a visible `NewWindow` call
  `CalcVisBehind` even with no window behind in their layer, so the
  layers behind get their visible regions recomputed.
* `sysroutines.cpp` (new): the System's low-memory routines $7B0
  (selector-table dispatch) and $668 (detached-package call) as 68k code
  copied from a 7.5.5 boot, set at every launch's lowmem reset; Color
  Picker 2.0 calls its PACK 12 through them (`Package.yaml` names them).
* `res/resGet.cpp`: `GetResource('PACK', n)` does the real lookup first;
  the ALRT stand-in (ResEdit on Executor's own System) only when nothing
  is found. `res/resOpen.cpp`: an open resource file is answered with its
  reference number for `fsCurPerm` too (`fsRdPerm` keeps a second map:
  the INIT loader's files).
* `component.cpp`: a file's `thng` resources register last first, so the
  head insertion keeps their order (Color Picker takes the first `cpkr`
  found: HSL on 7.5.5).
* `dial/dialHandle.cpp`: `ModalDialogMenuSetup` ($AA67), a no-op.
* `finder.cpp`: an HFS volume's "Desktop DB" read whole: icons (type 1,
  bitmaps from "Desktop DF"), applications (2), comments (3); bundle icons
  only for applications the database lacks.
* `process.cpp`, `res/resOpen.cpp`, `launch.cpp`: system mode (OSDispatch
  $40/$41) makes a resource file the system's: `HOpenResFile` puts it
  just above the System file, the Process Manager links it into every
  other process's chain and the new-process template, exit and launch
  leave it open, `CloseResFile` unlinks it everywhere.
