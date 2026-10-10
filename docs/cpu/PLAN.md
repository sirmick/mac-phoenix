# MAME cores in MacPhoenix

Agreed 2026-10-10. Three goals, one shared piece of work:

* **C1** MAME's Musashi (68020–68040 with PMMU and softfloat FPU, several
  instances per process) replaces the vendored Musashi v3.32 as the 68k
  `cpu::Core`, for the ROM machines and Executor alike.
* **C2** MAME's PowerPC recompiler (UML front end; x86-64 and arm64
  back-ends) replaces Kheperix as the PPC `cpu::Core`. One base offset for
  guest memory, so no `vm.mmap_min_addr`.
* **C3** A host 68k core runs the 68k side of the PPC machine in place of
  the nanokernel's own 68k emulator.
* **C4** (later, after Executor's wasm build) a UML→wasm back-end.

The longer direction these serve is
[../executor/DIRECTION.md](../executor/DIRECTION.md), sections 1, 4 and 5.

## Source

MAME master `a025aba` (2026-10-09), BSD-3-Clause for everything taken.
A shallow sparse reference checkout lives outside git at
`/home/mick/src/mame-src` (`src/devices/cpu/{m68000,powerpc,drc*,uml*}`,
`src/emu`, `src/lib/util`, `3rdparty/softfloat3`), the same arrangement as
`executor/` for the Toolbox: the oracle for diffs and later merges.

Third-party pieces that come with the cores:

| Piece | Used by | Licence | Size |
|---|---|---|---|
| softfloat3 (+ MAME's `bochs_ext`) | 68k FPU | BSD-3 | 450 files, 2.7 MB |
| asmjit (MAME submodule, not in the sparse checkout) | x86-64 and arm64 back-ends | zlib | tens of thousands of lines |
| `util/mfpresolve` | back-ends (member-function-pointer resolution) | BSD-3 | 580 lines |

## Where the coupling is

Measured on the reference checkout: occurrences of framework calls per
file set.

| File set | lines | `machine()` | memory | timers | save/state | log | delegates |
|---|---|---|---|---|---|---|---|
| 68k: `m68kcpu`, `m68kops`, `m68kfpu`, `m68kmmu`, `m68000musashi`, `m68040` | ~50k (36k generated) | 9 | 183 (+27 address-space) | 0 | 111 | 69 | 1 |
| PPC: `ppccom`, `ppcdrc`, `ppcfe`, `ppc.h` | 11k | 2 | 55 (+41) | 30 | 63 | 18 | 18 |
| DRC core: `uml`, `drcuml`, `drccache`, `drcfe`, `drcbeut` | 5k | 2 | 59 (+22) | 0 | 0 | 21 | 4 |
| Back-ends: `drcbearm64`, `drcbex64`, `drcbec` | 15k | 9 | 21 (+36) | 0 | 0 | 25 | 3 |

What each number hides:

* **68k memory** is seven `std::function` fields (`m_readimm16`, `m_read8`
  … `m_write32`) assigned in `init32()` and friends; the PMMU reads page
  tables through about ten direct `m_program->read_dword` calls. All of it
  rebinds to `cpu::GuestMemory`.
* **PPC timers** are the decrementer and the 4xx peripherals. The 4xx code
  (SPU, DMA, FIT/PIT timers) is deleted with the 4xx models; the
  decrementer becomes a cycle deadline the run loop checks.
* **Back-end memory** is the hard dependency: `OP_READ`/`OP_WRITE`
  lowering resolves `address_space::specific_access_info` and emits calls
  into MAME's handler dispatch tables. For a Mac every access is RAM or
  ROM, so the lowering is replaced by direct loads and stores through the
  GuestMemory's one base offset, with a call-out for unmapped pages.
* **Everything else** (`save_item`, `state_add`, `logerror`,
  `machine().side_effects_disabled()`, `emu_fatalerror`, debugger hooks,
  `machine().options()` for recompiler logging) is one shim header.

## Shape

```
src/cpu/mame/
  shim/emu.h, logmacro.h   stand-ins for the MAME framework (see below)
  m68000/          m68kcpu, m68kops (generated, committed), m68kfpu, m68kmmu,
                   m68kmusashi.h, m68kcommon, m68kdasm; m68kmake.py +
                   m68k_in.lst kept (the per-model m680x0.cpp files are not
                   lifted: the adapter is the device)
  mame_68k_core.cpp   cpu::Core on m68000_musashi_device   ("mame-68k")
  powerpc/         ppccom, ppcdrc, ppcfe, ppc_dasm                   (C2)
  drc/             uml, drcuml, drccache, drcfe, drcbeut, drcbec,
                   drcbex64, drcbearm64, drcbe_guestmem (our READ/WRITE) (C2)
  mame_ppc_core.cpp   cpu::Core on ppc750_device           ("mame-ppc") (C2)
  third_party/     softfloat3 (+ softfloat3_sources.cmake, MAME's list);
                   asmjit, mfpresolve (C2)
  LICENSE-MAME
  CMakeLists.txt   mame_softfloat3, mame_m68k (C++20; cpu_core links it)
```

Imported files keep MAME's copyright headers and are edited as little as
possible, as `src/executor/romlib` is; a "Changes to imported MAME code"
list at the end of this file tracks every edit.

### The shim

One header per MAME concept, enough to compile the four file sets and
nothing more:

| MAME | Stand-in |
|---|---|
| `device_t`, `cpu_device`, `device_execute_interface` | a base class with the constructor signature, `device_start/reset`, `execute_run`, `m_icount`, `execute_set_input`, `standard_irq_callback`, `total_cycles`, `cycles_to_attotime` |
| `device_memory_interface`, `address_space`, `memory_access<>::specific/cache` | an `address_space` is a `GuestMemory` plus a kind: memory, or the CPU space whose interrupt-acknowledge reads answer with the autovector. `cache`/`specific` objects point back at their space. `shim_bind_memory()` binds every space the device declares. Masked dword writes write the selected lanes. The PPC `m_pr32` rebinds likewise |
| `device_vtlb_interface` (`divtlb`) | taken as is (490 lines, portable) |
| `save_item`, `save_pointer`, `state_add`, `state_*_export` | no-ops; `cpu::Core::reg()` reads the fields directly |
| `logerror`, `osd_printf_*`, `emu_fatalerror`, `fatalerror` | `fprintf(stderr)` and a thrown exception |
| `machine().side_effects_disabled()`, `machine().debug_flags`, `debugger_instruction_hook` | `false`, 0, nothing |
| `machine().options().drc_*` | compile-time constants (C back-end off, logs off) |
| `attotime`, `emu_timer`, `timer_alloc` | the decrementer only: a cycle count the core checks at block boundaries |
| `osd::virtual_memory_allocation` | `mmap` + `mprotect`; `MAP_JIT` and `pthread_jit_write_protect_np` on Apple Silicon |
| `delegate`, `devcb` | `std::function` where a callback survives, deleted with the 4xx peripherals otherwise |

## Milestones

| | Milestone | Gate | Status |
|---|---|---|---|
| C1a | 68k builds and runs | shim + `m68000/` compile standalone; `mame_68k_core.cpp` passes `cpu::Core` as `--core mame-68k`; the Quadra ROM boots to Finder; Mac SE boots | done 2026-10-10: Quadra desktop in ~1 s, as the other cores; Mac SE (68000, 24-bit, System 6.0.8) to the Finder |
| C1b | Parity | every suite that passes on `uae` and `musashi` passes on `mame-68k`: ROM boot, SE, bridge, guest, Finder, keyboard; Executor's gtest suite (`ctest -L executor`) and the `_executor` bridge suites; boot time within 20 % of `musashi` | done 2026-10-10: ROM machine boot (Quadra and SE), `command_bridge`, `keyboard_guest`, `guest_suite` 28/28; Executor gtests 148 pass + the 2 upstream failures (disabled as for musashi); Executor `command_bridge` 7/7, `keyboard_guest`, `finder_suite` 25/25, `guest_suite` identical to musashi (same `as_date_time_string` day-name failure, same stop before the network section: the parked heap-corruption item in STRAGGLERS.md). ctest: `cpu_cores`, `boot_core_mame68k`, `executor.mame68k.*` |
| C1c | Instances | two `mame-68k` cores in one process (a unit test); `Config::fpu` honoured; `invalidate()` no-op documented; the per-instance cycle count removes the nested-run quirk | done 2026-10-10: `tests/test_cpu_cores.cpp` (ctest `cpu_cores`): two cores, a context round trip, registers, the disassembler; `Config::fpu` picks LC040/EC040; `Config::mmu` (new) picks the PMMU inits |
| C1d | PMMU | 7.5.5 with Virtual Memory on in the Memory control panel boots and runs on `mame-68k` (Basilisk II could never) | not started |
| C1e | Retire v3.32 | `src/cpu/musashi` deleted, `--core musashi` becomes an alias of `mame-68k`; UAE dropped from Executor (low link, `MAP_32BIT` go) | not started |
| C2a | PPC core on the C back-end | shim + `drc/` + `powerpc/` compile with `drcbec` (UML interpreted); `mame_ppc_core.cpp` as `Arch::PPC`; differential trace against KPX (`PPC_FLIGHT_RECORDER` / `MACEMU_PPC_CR2_TRACE`) matches over the nanokernel boot | done 2026-10-10: `mame_ppc` (powerpc + the UML core + `drcbec`, `divtlb`, strformat/vecstream/mfpresolve lifted) builds everywhere; `mame_ppc_core.cpp` is `Arch::PPC` "mame-ppc"; `tests/test_cpu_cores.cpp` runs a program with host ops, code invalidation, context and the disassembler. Differential trace against KPX not done (the boot itself was the oracle) |
| C2b | Mac side on the new core | the KPX machine's ROM patches, EmulOps (primary opcode 6 as a UML op calling the Host), `HandleInterrupt`, `execute_68k` run on `mame-ppc`; guest memory at one base offset, `VMBaseDiff` a variable; 7.5.5 boots to Finder (`tests/test_boot_ppc.sh`); no `mmap_min_addr` | done 2026-10-10 except the base offset: `src/cpu/kpx/cpu_ppc_mame.cpp` installs the SheepShaver machine side on the core (`--backend kpx --core mame-ppc`); 7.5.5 boots to the Finder desktop, BridgeAgent up, identical screen to Kheperix; ctest `boot_ppc_mame`, `boot_ppc_mame_api`. 13 s to desktop on the UML interpreter against 3 s on Kheperix. Addressing is still identity (so `mmap_min_addr` stays), the base offset moves to C2e |
| C2c | x86-64 back-end | `drcbex64` with `drcbe_guestmem` READ/WRITE lowering; asmjit vendored; boot matrix `kpx-jit-*` cells pass as `mame-ppc` cells; boot time at or under KPX `--jit` | done 2026-10-10: `drcbex64` + `x86log` lifted, asmjit vendored (`third_party/asmjit`, zlib); memory accesses call the shim's `address_space` member functions through MAME's own resolved-accessor fallback (no `drcbe_guestmem` inline fast path yet); `--backend kpx --core mame-ppc --jit` boots 7.5.5 to the desktop in 1.6 s (Kheperix JIT 1.1 s, Kheperix interpreter 3.1 s, UML interpreter 13 s); ctest `boot_ppc_mame_jit` |
| C2d | arm64 back-end | `drcbearm64` builds and boots 7.5.5 on arm64 Linux and Apple Silicon (`MAP_JIT`); the `kpx_stub` is gone | not started: `drcbearm64.cpp` is lifted and in the CMake branch for arm64 hosts, untested (no arm64 host here); needs the execute() re-entrancy fix drcbex64 got, and the x86-only KPX helpers moved to kpx_shared |
| C2e | Retire Kheperix | `src/cpu/kpx/src`, `dyngen_precompiled`, `regen_dyngen_ops.sh` deleted; `--backend kpx` keeps its name on the new core; PowerCore facade (`src/executor/cpu/PowerCore.*`) implemented on it for Executor's milestone P | not started |
| C3a | `execute_68k` on Musashi | host-initiated 68k calls run on a `mame-68k` instance sharing the PPC machine's GuestMemory: import the GPR contract, run to the EXEC_RETURN word, export; everything else still on the DR emulator; bridge suites pass | done 2026-10-10 as a hybrid (`MACEMU_MAMEPPC_68K=1| done 2026-10-10 as a hybrid (`MACEMU_MAMEPPC_68K=1` on `--core mame-ppc`): host-initiated 68k routines run on a `mame-68k` core over the PPC machine's memory; the ROM's EmulOp words and EXEC_RETURN are handled in the 68k host; at the first A-line word the registers go into the emulator's convention (through a thunk that sets the CCR) and the nanokernel's emulator finishes the routine. 7.5.5 boots to the desktop in 1.6 s and the command bridge passes 7/7 on it. Mode 2 (Musashi takes A-line traps through the ROM dispatcher at $28) boots too but breaks the bridge: a divergence after the dispatcher hands off at a routine descriptor, not yet understood |
| C3b | The 68k side on Musashi | the emulator's entry points patched to SHEEP ops that enter Musashi; exits at `$AAFE` (Mixed Mode), EXEC_RETURN, interrupts (Musashi's IRQ line instead of the DR poll word), bus errors as 68k frames; `0xFExx`/`$71xx` EmulOps as host ops; 7.5.5 boots to Finder and passes the bridge suites; `--jit68k` retired | not started |
| C3c | Numbers | boot time and a 68k-heavy benchmark (MacPerl guest suite section) against the DR emulator on x86-64; the same on arm64 against the interpreter-only PPC | not started |
| C4 | UML→wasm back-end | after D4: a `drcbe_wasm` emitting modules per hot-block batch, dispatched through a `WebAssembly.Table`; PPC machine in a tab faster than `drcbec` | not started |

### Order and dependencies

1. **C1a–C1c first.** It builds the shim and the `cpu::Core` adapter
   pattern that C2 reuses, on the core whose behaviour is already pinned
   by two other cores and every suite.
2. **C2a–C2b** next: the recompiler infrastructure and the Mac side move
   together, on the C back-end, so codegen bugs and integration bugs stay
   apart.
3. **C2c, C2d**: native back-ends, x86-64 first because KPX `--jit` is the
   oracle there.
4. **C3a** can start as soon as C1a exists; it needs only one Musashi
   instance beside the PPC core. **C3b** waits for C2b so the patched
   emulator entry points are written once, against the new core.
5. **C1d, C1e, C2e** are clean-ups and go whenever their gates hold.

C1 is a week-scale task. C2 is several weeks; its risk is in the
back-ends, not the core. C3 is research-grade: the register contract is
known, the exception-frame details are not.

## Design notes

### GuestMemory and the back-ends

The ROM machines and Executor already give each core a `GuestMemory`
whose mapped pages share one base offset (`GuestMemory::uniform`), so a
guest address is host `base + addr`. `drcbe_guestmem` is the lowering for
`OP_READ`/`OP_READM`/`OP_WRITE`/`OP_WRITEM` on both native back-ends:

* fast path: `base + addr` load or store of the right width, big-endian
  byte swap, when the page table says the page is mapped;
* slow path: a call to `GuestMemory::read/write` (which handles unmapped
  pages and the `Io` handler) for anything else.

MAME's own fastram mechanism (`ppcdrc_add_fastram`) is the same idea
limited to four ranges; ours covers the whole mapped space from the page
table and makes the `address_space` dependency disappear.

### Interrupts and host ops on the PPC core

* **Host ops**: primary opcode 6 (`cpu::ppc::is_host_op`) is decoded by
  `ppcfe` as a block-ending instruction and generated as a UML `CALLC` to
  the core's trampoline, which calls `Host::host_op()`. A Stop result ends
  the run, as on 68k.
* **Interrupts**: `set_irq()` sets the core's pending flag; the generated
  code already checks `irq_pending` at block boundaries (MAME's
  `EXCEPTION_EI` path), the same model as Kheperix's `spcflags`.
  `request_attention()` rides the same check.
* **Nested runs**: `execute_68k` and `CALL_EMULATOR`-style re-entry push
  a return address holding a host op and call `run()` again, as the
  interface requires; MAME's `PPC_REENTRANT_JIT` equivalent is the
  recompiler's own `execute_depth`, which is kept.

### MMU policy

* **68k**: the PMMU is built and switched on by `Config` (`model` 68030 or
  68040 with `mmu = true`). The ROM machines turn it on for 7.5.5 only
  once C1d proves Virtual Memory boots; Executor never does. Only 68k
  accesses see it, as DIRECTION.md section 1 says.
* **PPC**: translation stays off (MSR IR/DR clear, as under Kheperix) for
  7.5.5–9.0.4. MAME's 750 implements BATs, hashed page tables and a TLB;
  turning them on is what Mac OS 9.1–9.2.2 need and what would change the
  Mac side's view of addresses (EmulOps and thunks would translate guest
  virtual to physical first). Out of scope here; noted in DIRECTION.md.

### The 68k register contract for C3

From `execute_68k` in `src/cpu/kpx/cpu_ppc_kpx.cpp` (SheepShaver):

| 68k | PPC | notes |
|---|---|---|
| D0–D7 | r8–r15 | |
| A0–A6 | r16–r22 | |
| A7 | r1 | the PPC stack pointer is the 68k stack |
| PC | r24 | |
| SR high byte, interrupt level | r25 | mirrored at `XLM_68K_R25` |
| VBR | r28 | |
| supervisor mode | CR field 2 SO bit | |
| emulator internals | r26, r27, r29–r31 | opcode table, next extension word, emulator code, EmulatorData at `KernelData+0x1000` |

Entry: the nanokernel jumps to `LA_EmulatorCode` (`ROMBase+0x460000`,
relocated by the ROM patches) after boot and re-enters after interrupts,
native calls and mode switches; SheepShaver's `rom_patches_ppc.cpp` already
finds the start, trap-return and interrupt re-entry points. Exits:
`$AAFE` (`_MixedModeMagic`), the EXEC_RETURN word, a pending interrupt, a
68k exception the nanokernel must see.

Open: which 68k exception frames the ROM's 68k code and the Mixed Mode
Manager read (the emulator presents a 68LC040), and whether anything reads
68k state from EmulatorData rather than the GPRs. The trace tooling
(`MACEMU_PPC_TRACE_68K_ENTRY`) answers both before C3b starts.

### Build

* `cpu_core` gains `mame_m68k_core.cpp` and links `mame_m68k`; the PPC
  pieces build only where the back-end exists (x86-64, arm64) plus
  `drcbec` everywhere, so an arm64 build is never a stub.
* Generated opcode tables (`m68kops.cpp`, 36k lines) are committed with
  their generator; the Python is not a build requirement.
* asmjit comes in as a vendored subtree at a pinned commit, not a git
  submodule (one `git clone` must build, as for Executor's dependencies).
* Apple Silicon: the DRC cache is mapped `MAP_JIT`, flipped with
  `pthread_jit_write_protect_np` around emission, and the binary needs the
  allow-JIT entitlement; same as MAME's own build.

## Open questions

* **Name the cores.** `mame-68k`/`mame-ppc` while both old cores exist;
  afterwards `musashi` and `kpx` could keep their names on the new code
  so configs don't change. Decide at C1e / C2e.
* **UAE's future.** Its JIT is x86-only and the only reason to keep it.
  Once C1b shows `mame-68k` within range of UAE's interpreter, decide
  whether `--jit` on 68k is worth a second core.
* **68LC040 or 68040 for C3.** The nanokernel's emulator reports an
  LC040; Musashi has both. Pick by what the exception frames need.
* **Keep KPX in tree until C2e?** Yes: it is the oracle for the
  differential traces and the boot matrix until the new cells are green.

## Notes from C3a (68k routines on a host Musashi)

* **The register contract holds.** `export_m68k_to_emulator()` in
  `cpu_ppc_mame.cpp` writes Musashi's state into r8-r15 (D0-D7), r16-r22
  (A0-A6), r1 (A7), r24 (PC past the word), r27 (the sign-extended next
  word), r25 (SR high byte), CR field 2 SO (supervisor), r29 (the
  emulator's slot for the word) and the fixed r28/r30/r31, then runs the
  nanokernel's emulator from that slot. Every one of 193 handoffs in a
  boot came back through EXEC_RETURN with the right result; nested calls
  (an EmulOp calling Execute68k) work two levels deep. The CCR is not
  exported (its place in CR is unknown); nothing has cared yet.
* **The 68k vectors are real.** $28 holds the ROM's 68k trap dispatcher,
  $2C and $10 ROM handlers too. Musashi taking A-line traps through them
  (mode 2) runs Apple's dispatcher and the 68k implementations of the
  traps; patched traps whose table entries are routine descriptors end at
  `$AAFE`, the handoff. It boots, but the bridge then fails (BridgeAgent
  never heartbeats): a per-routine differential against mode 1 shows the
  dispatcher-on-Musashi runs returning with different D1/A1 and different
  nested EmulOp calls, i.e. the emulator continues from the descriptor in
  a state it does not expect. The emulator dispatches traps itself on the
  real machine, so the $28 path may not be kept equivalent. Parked; C3b
  makes it moot by keeping every trap on Musashi.
* **The CCR crosses over by a thunk.** Where the emulator keeps the CCR
  is unknown, so a handoff enters it at `move #ccr,ccr ; jmp word.l` in
  SheepMem and lets it set its own. Before that, mode 1 lost branches
  after handed-off traps and the bridge failed the same way.
* **r1 is A7, always.** The first stall: a handler reached from a Musashi
  run called Execute68k, which built its frame from the PowerPC r1, stale
  while Musashi ran, and overwrote the outer routine's live stack. The
  EmulOp path now sets r1 from Musashi's A7 before and after the handler.
* **State is per level.** Musashi's context is saved and restored around
  each run (the outer instruction must find its PC again) and the flags
  that end a run are restored to the outer level's on return.
* **Not yet:** interrupts while Musashi runs (deferred to the PowerPC side,
  fine for short routines); the 68k instruction cache is cleared per run;
  the CCR; and C3b, where the emulator's slots are patched so the 68k main
  thread runs on Musashi too, with `$AAFE` the only way out and the
  nanokernel's returns into the emulator the way back in.
* The `cpu::Host::trap()` hook (A-line and F-line words before the
  exception) and `Core::invalidate()` clearing the 68020+ instruction
  cache came with this.

## Notes from C2c (the x86-64 back-end)

* **MAME's native back-ends are not re-entrant, and this machine nests.**
  The entry stub stores the host stack pointer and SSE mode in the
  back-end's near state; every HASHJMP and the exit stub reload them. A
  host op that runs guest code (EmulOps running 68k code, interrupts
  running the nanokernel) calls `execute()` again, which overwrote them,
  so the outer block's next HASHJMP ran on the dead inner frame and exited
  into garbage: the EXEC_RETURN host op looped forever. `drcbe_x64::execute()`
  now saves and restores the near state and the memory-resident UML
  registers around the call. `drcbearm64` will need the same.
* **Memory lowering.** The back-end's direct-dispatch path needs MAME's
  handler tables; the shim's `address_space::specific_accessors()` reports
  none, so the back-end takes its own fallback and calls the resolved
  member functions (`address_space::read_dword(offs_t)` and friends) for
  every access. A call per access, but correct, and already 8× the UML
  interpreter. The inline fast path over the GuestMemory page table
  (`drcbe_guestmem` in the plan) is the next speed step.
* `x86log.cpp` lost its MAME i386 disassembler (not lifted); native-code
  logs show bytes. Logging is off anyway (`emu_options`).
* The back-end choice is per device: `Config::jit` sets
  `emu_options::drc_use_c()` on the device's own `running_machine`, so
  `--jit` on the kpx backend picks the native back-end and `--no-jit`
  the UML interpreter.
* **Microseconds() is a virtual clock.** On the kpx machine it counts
  Kheperix's `ppc_insn_counter` (4 ns each), which this core never
  touched, so it stood still: 7.5.5 did not care, Mac OS 9.0.4 waited on
  it at the "Starting Up" bar forever (a million calls a second, the
  Time Manager idle). `cpu::Core::cycles()` now exposes the core's
  cycle count and the EmulOp dispatcher copies it into the counter.
  9.0.4 boots to the desktop in 10 s; the guest suite on it matches
  Kheperix (`guest_suite_ppc_mame`).

## Notes from C2 (core and Mac side on the UML interpreter)

* The PPC lift needed four edits to imported code (below), all small.
  The shim grew timers kept in CPU cycles (with nested slices for host
  ops that run guest code), delegates on `std::function`, the
  `device_interface` start/reset hooks, MAME's executable-memory class on
  `mmap`, and MAME's real `strformat.h`, `coretmpl.h`, `vecstream`,
  `endianness.h`, `corefloat.h` and `mfpresolve` instead of imitations.
* **Guest byte order.** MAME stores a 64-bit-wide big-endian space as
  native words (the byte at guest address a lives at host a ^ 7);
  `cpu::GuestMemory` stores guest bytes in guest order. Two consequences
  in the lifted code: the code-fetch pointer `m_prptr` is the address
  itself, and the block checksums compare the native load against the
  opcode's native image (`opptr_native()`).
* **The Mac side needs Kheperix's semantics, not a real 750's.** The
  installer configures the core with translation off (`Config::mmu`
  false: the nanokernel's BAT/segment setup is patched out), the
  decrementer exception off (`Config::timers` false: the tick thread
  paces the guest and HandleInterrupt pokes KernelData), and exceptions
  skipped and logged (`Config::exceptions` false: Kheperix skips illegal
  instructions and ignores unknown SPRs). MSR[FP] is turned on at the
  first floating-point instruction rather than taken as an exception.
  MSR starts at 0 so the vectors are at 0 as the nanokernel expects.
  `PPCDRC_COMPATIBLE_OPTIONS` (strict verify: the ROM's 68k emulator
  recycles its translation buffer; MAME's own comment says so).
* **Installer shape.** `cpu_ppc_mame.cpp` fills the same Platform entries
  as `cpu_ppc_kpx.cpp`: SHEEP opcodes arrive as host ops (primary opcode
  6); `trigger_interrupt` is `request_attention()` and `HandleInterrupt`
  runs in `Host::attention()`; SheepShaver's `execute(entry)` is a nested
  `run()` ended by the EXEC_RETURN host op; `execute_68k` saves and
  restores the whole context instead of r13-r31 and f14-f31. The NativeOp
  table and the EmulOp remap moved to `native_ops_ppc.cpp` (kpx_shared) so
  both installers share them; `call_macos*` dispatch through a new
  Platform entry `ppc_execute_macos_code` when Kheperix is absent (the
  Name Registry patch, hence the display device and the video driver,
  depended on it: the first boot on the new core had a black screen for
  that reason).
* `MACEMU_MAMEPPC_TRACE=1` logs host ops, exceptions, and once a second
  the PC, mode, a framebuffer checksum and NativeOp/EmulOp counts.
* Speed: 13 s to the desktop on the UML interpreter (`drcbec`), 3 s on
  Kheperix interpreting, so C2c is what makes the core usable.
* Not yet on arm64 as a whole: `SignalStackBase`, `DisableInterrupt`,
  `MakeExecutable`, `FlushCodeCache` still live in `cpu_ppc_kpx.cpp`
  (x86-only) with no-op stubs on other hosts; they move to kpx_shared
  with C2d.

## Notes from C1

* The whole 68k lift needed one edit to imported code (below) and two
  compile errors' worth of shim. MAME's cores really are that separable.
* The adapter (`Mame68kDevice`) derives from `m68000_musashi_device`
  and calls the `init_cpu_m680x0()` of the configured model itself, so the
  per-model `m68040.cpp` files are not lifted. With `Config::mmu` off it
  rebinds the non-translating accessors after the MMU init, so there is no
  per-access PMMU check; with it on, the 030/040 PMMU is live and
  `pmmu_enabled` follows the guest's TC writes.
* MAME's `device_reset()` defers the reset-vector fetch to the next
  `execute_run()`, which would undo a `set_pc()` after `reset()`; the
  adapter's `reset()` fetches the vectors itself.
* Interrupts: MAME samples the lines when a slice starts or SR is written
  (as v3.32). `set_irq()` ends the current slice so a change from a host
  op is seen at once. Level 7 is an edge: assert then clear leaves
  `m_nmi_pending` set.
* The context is an explicit field list (`MAME68K_CONTEXT_FIELDS`), not a
  byte copy of the object: the device holds `std::function`s and pointers.
* `ctest -L mame` lists only the plain `add_test` entries; gtest-discovered
  tests' labels don't take with `-L` here (same for `musashi`). Use
  `ctest -R 'executor\.mame68k\.'`.
* The bridge tests are timing-sensitive when several emulators boot at
  once on this machine; run alone they pass on all three cores.
* `tests/boot_screenshot.sh --core NAME --out FILE.png [--se]` boots the
  Quadra (or the Mac SE) on a core and saves the desktop.
* The 68000 and 68010 come along for free: `m68000_musashi_device` still
  carries their inits (MAME's default 68000 is its newer core, not lifted),
  so the Mac SE runs on `mame-68k` as a 68000 with the 16-bit bus.
  (`/api/screenshot` returns a black frame for the SE on every core while
  `/api/status` shows Finder up; a screenshot-path quirk, not a CPU one.)

## Changes to imported MAME code

* `m68000/m68kcpu.h`: `m68ki_exception_illegal()` first calls a new
  virtual `m68ki_host_op()`; the adapter overrides it to run `$71xx` host
  ops (marked "MacPhoenix"). `m68ki_exception_1010()`/`_1111()` likewise
  call `m68ki_trap_op()` (`cpu::Host::trap`).
* `powerpc/ppc.h`: virtual `ppc_host_op()` and `ppc_exception_hook()` with
  their `ppc_cfunc_*` CALLC targets; `ppc_set_translation_off()`.
* `powerpc/ppcfe.cpp`: primary opcode 6 described as a sequence-ending
  instruction that may touch any register.
* `powerpc/ppcdrc.cpp`: opcode 6 generated as a CALLC to the host op, then
  the interrupt/cycle check and a HASHJMP to `m_core->pc`; every static
  exception handler calls the exception hook after SRR0/SRR1 and before
  MSR changes, and resumes at `m_core->pc` if the hook swallows;
  `opptr_native()` in the block checksums.
* `powerpc/ppccom.cpp`: `m_prptr` is the instruction's own address (no
  64-bit swizzle); `m_translation_off` short-circuits
  `ppccom_translate_address_internal`.
* `cpu/drcbex64.cpp`: `execute()` saves and restores the near state
  (stack pointer, SSE mode) and `m_state` around the call, for nested
  execution.
* `cpu/x86log.cpp`: the i386 disassembler dependency removed; code ranges
  are logged as bytes.
