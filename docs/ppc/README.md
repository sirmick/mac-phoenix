# PowerPC

PPC (PowerPC) Mac emulation in mac-phoenix targeting OldWorld 4 MB ROMs
(Gossamer / Beige G3). One backend, `--backend kpx`, with two CPU cores
chosen by `--core`:

- **`kpx`** (default) — the Kheperix interpreter from SheepShaver, with its
  dyngen PPC JIT on `--jit`. x86/x86-64 hosts only (dyngen is precompiled
  x86 code); elsewhere `kpx_stub.cpp` stands in and exits.
- **`mame-ppc`** — MAME's PowerPC recompiler (`src/cpu/mame/`, a 750): the
  UML interpreter on `--no-jit`, the native x86-64 or arm64 back-end on
  `--jit`. Its installer is `src/cpu/kpx/cpu_ppc_mame.cpp`. See
  [../cpu/PLAN.md](../cpu/PLAN.md) (C2).

Mac OS 7.5.5 to the desktop, measured on this x86-64 host
([../cpu/PLAN.md](../cpu/PLAN.md), 2026-10-10): `mame-ppc --jit` 0.9 s,
Kheperix `--jit` 1.1 s, Kheperix interpreter about 3 s, the UML
interpreter 8–13 s. Mac OS 9.0.4 takes 9.7 s on `mame-ppc --jit`.

Both cores sit behind the same `g_platform` shim as the m68k backend. The
Mac side (memory and kernel-data setup in `CPUContext::init_ppc`; ROM
patches, EmulOps, the NativeOp table in `native_ops_ppc.cpp`, thunks and
drivers in the `kpx_shared` library) and the BridgeAgent automation
surface (`/api/launch`, `/api/shutdown`, etc.) are shared; the two installers differ only in how
the CPU is driven.

## Boot

```bash
# Kheperix, default
./build/mac-phoenix --backend kpx --rom ~/storage/roms/g3.rom \
    --disk ~/storage/images/7.6.1.img --ram 128

# Kheperix with the dyngen JIT
./build/mac-phoenix --backend kpx --jit --rom ~/storage/roms/g3.rom \
    --disk ~/storage/images/7.6.1.img --ram 128

# MAME's PowerPC recompiler, native back-end
./build/mac-phoenix --backend kpx --core mame-ppc --jit \
    --rom ~/storage/roms/g3.rom --disk ~/storage/images/9.0.4.img --ram 128
```

`--backend kpx` must be space-separated: the parser does not recognise the
`--backend=kpx` form; it prints `Unknown argument` and the backend stays
the `uae` default.

Targets:

- ROM: OldWorld 4 MB Gossamer (Beige G3). The PVR the guest is told is
  SheepShaver's `0x000c0000` (7400, G4) on Kheperix and MAME's 750,
  `0x00080301`, on `mame-ppc`, which has no AltiVec (PLAN.md S20).
- OS: Mac OS 7.5.5, 7.6.1, 8.6 and 9.0.4. ctest boots 7.5.5 on both cores
  (`boot_ppc_interp`, `boot_ppc_jit`, `boot_ppc_mame`, `boot_ppc_mame_jit`)
  and runs the guest suite on 9.0.4 (`guest_suite_ppc`,
  `guest_suite_ppc_mame`); `tests/run_boot_matrix.sh` boots 7.5.5 and
  7.6.1 on both. The web UI's PPC profiles (7.5.5 to 9.0.4) run Kheperix
  `--jit` with 128 MB.
- RAM: 64 MB minimum (128 MB for headroom on 7.6.1 + apps).

## Memory layout (Gossamer)

Set up in `CPUContext::init_ppc` (`src/core/cpu_context.cpp`). On Kheperix
`VMBaseDiff = 0` (REAL_ADDRESSING: Mac address == host address, needs
`vm.mmap_min_addr=0`). On `mame-ppc` the same Mac addresses are mapped into
a 4 GB reservation aligned to 4 GB, host = `VMBaseDiff` + Mac address, so
no sysctl is needed; holes stay `PROT_NONE` and fault as on Kheperix.

| Region              | Mac addr        | Size  | Notes |
|---------------------|-----------------|-------|-------|
| RAM                 | `0x00000000`    | 16 MB and up (`--ram`) | Mapped with 8 MB of padding above for the nanokernel's memory probes |
| ROM                 | `0x50000000`    | 4 MB  | `ROM_AREA_SIZE = 5 MB` (extra for patched code), signal stack after it; write-protected after patching |
| XLM (extra-low-mem) | `0x00002800`    | 2 KB  | Inside RAM. Host control words; see below. |
| KernelData          | `0x68FFE000`    | 8 KB  | Shared mapping, aliased at `0x5FFFE000`. 4 KB kernel vars + EmulatorData at +0x1000. |
| DR emulator / cache | `0x68070000` / `0x69000000` | 64 KB / 512 KB | Mapped only after the nanokernel's init, which probes them |
| SheepMem            | from `vm_acquire` | 512 KB | Thunks + zero page |
| Framebuffer         | from `vm_acquire` | largest mode × 4 bytes | Address handed to the nanokernel by the video driver |

**XLM offsets** (initialised by `InitXLM`): `XLM_SIGNATURE = 0x2800` ("Baah"),
`XLM_KERNEL_DATA = 0x2804`, `XLM_RUN_MODE = 0x2810`
(`MODE_68K=0` / `MODE_NATIVE=1` / `MODE_EMUL_OP=2`), `XLM_PVR = 0x281c`,
`XLM_BUS_CLOCK = 0x2820`, `XLM_EMUL_RETURN_PROC = 0x2824`,
`XLM_EXEC_RETURN_OPCODE = 0x284C`, `XLM_ZERO_PAGE = 0x2850`, ethernet
callbacks at `0x28B0..0x28D4`, video DOIO at `0x28D8`. Full list in
`src/cpu/kpx/compat/xlowmem.h`.

**KernelData critical offsets** (initialised by `InitKernelData` and the
nanokernel itself during boot): `+0x0634` emulator-data ptr, `+0x0658`
emulator context block 2, `+0x065c` current context block (must differ from
`+0x0658` — if they're equal the nanokernel didn't fully init), `+0x0674`
interrupt CR bits, `+0x067c` interrupt level poll word, `+0x1000`
EmulatorData start. The 68k emulator's main loop polls `+0x067c`; that's how
HandleInterrupt delivers a 60 Hz tick in MODE_68K — write `0` to the poll
word, set CR bits at `+0x0674`, return.

## Execution model (boot sequence)

1. **Host setup** (`CPUContext::init_ppc`): allocate VM, map ROM, decode if
   compressed, set `RAMBaseHost`/`ROMBaseHost`/`VMBaseDiff`, run shared
   subsystem init, `CheckROM_PPC` / `DecodeROM_PPC` / `PatchROM_PPC` (all
   four phases), `InitXLM`, `InitKernelData`, `SheepMem::Init`. Install
   the core's Platform table (`cpu_ppc_kpx_install` or
   `cpu_ppc_mame_install`). Set `GPR3 = ROMBase + 0x30d000`,
   `GPR4 = KernelDataAddr + 0x1000`. Start at `ROMBase + 0x310000`.
2. **Nanokernel boot** (patched ROM PPC code): reads boot structures from
   `0x30d000+`, skips SR/BAT/SDR init (NOPed by patches), loads PVR from
   `XLM_PVR`, sets up cache parameters from a per-CPU table, creates two
   `EmulatorData` context blocks, jumps to the 68k emulator entry at
   `ROMBase + 0x460000`.
3. **68k emulator** (the ROM's interpretive 68k emulator, PPC code; Apple's
   DR emulator is never switched on, see `../cpu/PLAN.md` S15): runs the
   same 68k ROM startup as a real Mac. EMUL_OP traps from 68k code dispatch into PPC
   handlers at `0x380000 + (selector << 3)`, which raise SHEEP opcodes that
   the PPC backend catches.
4. **Steady state**: 60 Hz timer fires → `HandleInterrupt` → the 68k emulator
   picks up the IRQ. ADB / disk / SCSI / serial run via EMUL_OPs. Native PPC
   apps switch to MODE_NATIVE via Mixed Mode Manager and use NATIVE_OPs
   (39 of them).

## Mode switching

`XLM_RUN_MODE = MODE_68K → EMUL_OP → MODE_68K → MODE_NATIVE → …`.
HandleInterrupt branches by mode:

- **MODE_68K**: write 0 to the poll word at `KD+0x67c`, `rlwimi` the CR bits at
  `KD+0x674`. The 68k emulator does the rest.
- **MODE_NATIVE**: save PC/LR/CTR/SP, set up nanokernel registers
  (`r1 = KernelDataAddr`, `r6 = KD+0x65c`), call `execute(ROMBase + 0x312a3c)`
  (Gossamer interrupt entry).
- **MODE_EMUL_OP**: synthesise a 68k exception frame and re-enter via
  `execute_68k`, but only if `XLM_68K_R25` says interrupts are enabled.

Copied verbatim from upstream SheepShaver — do not modify.

## SHEEP opcodes (PPC's EmulOp encoding)

```
0x18000000 | xx          xx == 0  → EMUL_RETURN  (QuitEmulator)
                         xx == 1  → EXEC_RETURN  (return from execute_68k)
                         xx == 2  → EXEC_NATIVE  (NATIVE_OP[20:25], FN[19])
                         xx >=  3 → EMUL_OP      (selector = xx - 3)
```

Major opcode 6 is undefined in real PPC. Kheperix hooks its decoder to
dispatch; on `mame-ppc` it is a block-ending host op (`cpu::Host::host_op()`).

## Verified identical to upstream SheepShaver (KPX)

The KPX integration was audited file-by-file against upstream
SheepShaver. The PPC CPU core (`src/cpu/kpx/src/cpu/ppc/`),
`HandleInterrupt`, `execute_68k` / `execute_emul_op` / `execute_sheep`,
SheepShaver's EmulOps, the NativeOps, the
ROM patches (`PatchROM_PPC`, four phases), resource patches, KernelData /
XLM init, video driver (`VideoDoDriverIO`/`Control`/`Status`/NQD hooks),
disk/SCSI/Sony/CDROM/serial/ADB driver dispatch, the tick + PRECISE_TIMING
threads — all match character-for-character. Reference target for the video
driver is upstream SheepShaver's IPC build (`src/IPC/video_ipc_sheep.cpp`),
**not** the SDL build.

Since then mac-phoenix has added EmulOps of its own (`OP_TEXT_PRE`,
`OP_TEXT_POST` for `/api/text`, `OP_BRIDGE_COMM` for the bridge), and the
NativeOp table moved unchanged to `native_ops_ppc.cpp` so both installers
share it.

## Networking (FULL_DRIVER mode)

PPC Mac OS networks via NDRV + STREAMS/DLPI. `name_registry_ppc.cpp`
registers SheepShaver's `EthernetDriverFull.i` blob, which contains its own
PPC build of `ether.cpp` running the full DLPI state machine inside the guest.
The host only needs:

- `InitStreamModule` / `TerminateStreamModule` — store the NDRV's RX
  dispatch tvect.
- `AO_get_ethernet_address` — write our MAC into Mac memory.
- `AO_transmit_packet(mp)` — walk the mblk chain, hand the flat frame to
  `ether_socket`.
- `EtherIRQ` — drains the socket's RX queue, `call_macos2` into the
  stored tvect for each frame.

The 1748-line DLPI state machine in upstream `ether.cpp` is **not** ported.
18/18 guest network tests pass on PPC (DNS, UDP echo, TCP echo). Implementation
is `src/cpu/kpx/compat/ppc_ether.cpp`.

## File map

```
src/cpu/kpx/
  cpu_ppc_kpx.cpp           — Kheperix installer: sheepshaver_cpu,
                              HandleInterrupt, tick thread, Platform install
                              (x86 hosts only)
  cpu_ppc_mame.cpp          — mame-ppc installer: the same Platform table on
                              a cpu::Core; C3a/C3b 68k host (--m68k-host)
  kpx_stub.cpp              — stands in for cpu_ppc_kpx.cpp on non-x86 hosts
  host_ppc.cpp              — MakeExecutable, interrupt nesting, signal stack
                              (both cores)
  native_ops_ppc.cpp        — NativeOp table (ppc_native_op_pure) and the
                              EmulOp remap, shared by both installers
  emul_op_ppc.cpp           — EmulOp dispatch (OP_IRQ, OP_RESET, OP_CHECKLOAD …)
  init_ppc.cpp              — InitAll_PPC, ExitAll_PPC, PatchAfterStartup_PPC
  rom_patches_ppc.cpp       — PatchROM_PPC (4 phases), nanokernel + 68k patches
  rsrc_patches_ppc.cpp      — Resource manager CheckLoad patches
  video_ppc.cpp             — VideoInit, VideoDoDriverIO, VideoVBL
  gfxaccel_ppc.cpp          — NQD acceleration hooks (namespace ppc)
  name_registry_ppc.cpp     — Open Firmware device tree
  macos_util_ppc.cpp        — CFM (FindLibSymbol, InitCallUniversalProc)
  thunks_ppc.cpp            — Native op thunks, ExecuteNative
  serial_ppc.cpp            — Serial driver
  ppc_memory.cpp            — SheepMem::Init/Exit, Microseconds (virtual clock)
  compat/                   — Header shims bridging KPX into mac-phoenix
  compat/ppc_ether.cpp      — Ethernet glue (FULL_DRIVER mode)
  src/                      — KPX interpreter engine (verbatim from upstream)
  dyngen_precompiled/       — JIT bytecode (x86_64)
  CMakeLists.txt            — kpx_shared (both cores), kpx_interp/kpx_dyngen
                              (x86 only), `-DSHEEPSHAVER=1`

src/common/include/ppc_boundary_trace.h — per-EmulOp / per-instruction CR
                              tracers (MACEMU_PPC_TRACE, MACEMU_PPC_CR2_TRACE,
                              MACEMU_PPC_TRACE_68K_ENTRY)
```

## ROM patching summary

`PatchROM_PPC` runs four phases (verbatim from upstream SheepShaver's
`src/rom_patches.cpp`):

1. **`patch_nanokernel_boot`** — boot structure pointers at `0x30d000+`,
   bypass SR/BAT/SDR init, load PVR from XLM, fill cache/TLB params from a
   per-CPU table, NOP out supervisor-mode SPR access, install the 68k
   emulator entry sequence (`lwz r3, 0x0634(r1); lwz r4, 0x119c(r1);
   lwz r0, 0x1184(r1); mtctr r0; bctr`).
2. **`patch_nanokernel`** — disable virt→phys translation (`mr r31, r27`),
   exception tables flip `XLM_RUN_MODE`, FPU enable bits NOPed, `rfi`→`bctr`
   trampoline at `0x318000`.
3. **`patch_68k_emul`** — replace TWI traps at `0x36e600..0x36ea00` with a
   branch table; populate the EMUL_OP dispatch table at
   `0x380000 + (selector << 3)`; entry routines save R7-R13/CR/CC.
4. **`patch_68k`** — boot code (NOP RESET, fake PowerMac ID, VIA init, skip
   RunDiags), NVRAM/XPRAM EMUL_OP redirects, memory setup (SysZone,
   bootstack), Gossamer-specific (UniversalInfo at `0x12d20`, GC interrupt
   mask suppression, SCSI variable init, force floppy driver install,
   AddrMap at `0x2fd140`).

ROM type detection runs first via the ID string at offset `0x30d064`. The
backend supports `ROMTYPE_GOSSAMER` (Beige G3) only in tested form;
`ROMTYPE_TNT`, `_ALCHEMY`, `_ZANZIBAR`, `_GAZELLE`, `_NEWWORLD` may be
detectable via the same code but are not validated.

## EmulOp + NativeOp selectors

EmulOp selectors live in `src/cpu/kpx/compat/emul_op.h`. Highlights:
`OP_BREAK = 0`, `OP_XPRAM1/2`, `OP_NVRAM*`, `OP_FIX_MEMTOP/SIZE/BOOTSTACK`,
`OP_SONY_*`, `OP_DISK_*`, `OP_CDROM_*`, `OP_AUDIO_DISPATCH = 22`,
`OP_SOUNDIN_*`, `OP_ADBOP = 28`, `OP_INSTIME = 29`, `OP_MICROSECONDS = 32`,
`OP_INSTALL_DRIVERS = 37`, `OP_NAME_REGISTRY = 38`, `OP_RESET = 39`,
`OP_IRQ = 40`, `OP_SCSI_DISPATCH = 41`, `OP_SCSI_ATOMIC = 42`,
`OP_CHECK_SYSV = 43`, `OP_NTRB_17_PATCH` .. `4` (44–47),
`OP_CHECKLOAD = 48`, `OP_EXTFS_COMM = 49`, `OP_EXTFS_HFS = 50`,
`OP_IDLE_TIME = 51`, `OP_IDLE_TIME_2 = 52`, and mac-phoenix's
`OP_TEXT_PRE = 53`, `OP_TEXT_POST = 54`, `OP_BRIDGE_COMM = 55`.

NativeOp selectors in `src/cpu/kpx/compat/thunks.h`:
`NATIVE_PATCH_NAME_REGISTRY = 0`, `NATIVE_VIDEO_INSTALL_ACCEL = 1`,
`NATIVE_VIDEO_VBL = 2`, `NATIVE_VIDEO_DO_DRIVER_IO = 3`,
`NATIVE_ETHER_*` (4–14), `NATIVE_SERIAL_*` (15–21), `NATIVE_GET_RESOURCE`
etc. (22–26), `NATIVE_MAKE_EXECUTABLE = 27`, `NATIVE_CHECK_LOAD_INVOC = 28`,
`NATIVE_NQD_*` (29–35), `NATIVE_NAMED_CHECK_LOAD_INVOC = 36`,
`NATIVE_GET_NAMED_RESOURCE`, `NATIVE_GET_1_NAMED_RESOURCE` (37–38).

## Related

- `../Commands.md` — `MACEMU_PPC_*` debug environment variables.
- `../Architecture.md` — full Platform API + interrupt overview.
- `../cpu/PLAN.md` — the MAME cores (mame-ppc, the C3 68k host), measurements.
- `../../CLAUDE.md` — project-wide cheat sheet.
