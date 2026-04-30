# PowerPC

PPC (PowerPC) Mac emulation in mac-phoenix targeting OldWorld 4 MB ROMs
(Gossamer / Beige G3). Two backends:

- **KPX** (default) — Kheperix interpreter from SheepShaver. Stable. Boots
  Mac OS 7.5.5 / 7.6.1 to Finder in ~45 s (interpreter; `--jit` available
  but blocked by a GCC codegen difference in the block dispatch loop).
- **Unicorn-PPC** — QEMU TCG JIT via Unicorn. Reaches Finder under 7.6.1 but
  is unstable. **Not recommended for normal use.** Live status, debug knobs,
  and known crashes are tracked in [UnicornPpcStatus.md](UnicornPpcStatus.md).

Both backends share the same `g_platform` shim, the same ROM/kernel-data
init in `cpu_context::init_ppc`, the same `execute_native_op_pure`, and the
same BridgeAgent automation surface — `/api/launch`, `/api/shutdown`, etc.
work identically on either.

## Boot

```bash
# KPX, default
./build/mac-phoenix --backend kpx --rom ~/storage/roms/g3.rom \
    --disk ~/storage/images/macos-7.6.1.img --ram 128

# KPX with dyngen JIT (compiled but blocked — interpreter is the working default)
./build/mac-phoenix --backend kpx --jit --rom ~/storage/roms/g3.rom \
    --disk ~/storage/images/macos-7.6.1.img --ram 128

# Unicorn-PPC (debugging only — see UnicornPpcStatus.md)
./build/mac-phoenix --backend unicorn-ppc --rom ~/storage/roms/g3.rom \
    --disk ~/storage/images/macos-7.6.1.img --ram 128 --timeout 60
```

Targets:

- ROM: OldWorld 4 MB Gossamer (Beige G3), PVR `0x000c0000`.
- OS: Mac OS 7.5.5 / 7.6.1 verified; 8.x / 9.x in scope but not regularly tested.
- RAM: 64 MB minimum (128 MB for headroom on 7.6.1 + apps).

## Memory layout (Gossamer)

A 512 MB `mmap` region with `VMBaseDiff = 0` (REAL_ADDRESSING — Mac addr ==
host addr).

| Region              | Mac addr        | Size  | Notes |
|---------------------|-----------------|-------|-------|
| RAM                 | `0x00000000`    | 64–256 MB | Configurable |
| ROM                 | `0x00400000`    | 4 MB  | Inside RAM address space; `ROM_AREA_SIZE = 5 MB` (extra for patched code) |
| ROM alias           | `0x40800000`, `0x50000000` | — | Nanokernel addresses both ranges |
| XLM (extra-low-mem) | `0x00002800`    | 2 KB  | Inside RAM. Host control words; see below. |
| KernelData          | `0x68FFE000`    | 8 KB  | Aliased at `0x5FFFE000`. 4 KB kernel vars + EmulatorData at +0x1000. |
| SheepMem            | `0x80000000` (KPX) / top of RAM (KPX legacy layout) | ~64 KB | Thunks + zero page |
| Framebuffer         | from video driver | ~4 MB | Host-allocated, address handed to nanokernel |
| Grand Central I/O   | `0xf3000000..0xf3020000` | — | MMIO stubs (Unicorn-PPC) |

**XLM offsets** (initialised by `InitXLM`): `XLM_SIGNATURE = 0x2800` ("Baah"),
`XLM_KERNEL_DATA = 0x2804`, `XLM_RUN_MODE = 0x2810`
(`MODE_68K=0` / `MODE_NATIVE=1` / `MODE_EMUL_OP=2`), `XLM_PVR = 0x281c`,
`XLM_BUS_CLOCK = 0x2820`, `XLM_EMUL_RETURN_PROC = 0x2824`,
`XLM_EXEC_RETURN_OPCODE = 0x284C`, `XLM_ZERO_PAGE = 0x2850`, ethernet
callbacks at `0x28B0..0x28C8`, video DOIO at `0x28C8`. Full list in
`src/cpu/kpx/compat/xlowmem.h`.

**KernelData critical offsets** (initialised by `InitKernelData` and the
nanokernel itself during boot): `+0x0634` emulator-data ptr, `+0x0658`
emulator context block 2, `+0x065c` current context block (must differ from
`+0x0658` — if they're equal the nanokernel didn't fully init), `+0x0674`
interrupt CR bits, `+0x067c` interrupt level poll word, `+0x1000`
EmulatorData start. The DR emulator's main loop polls `+0x067c`; that's how
HandleInterrupt delivers a 60 Hz tick in MODE_68K — write `0` to the poll
word, set CR bits at `+0x0674`, return.

## Execution model (boot sequence)

1. **Host setup** (`cpu_context::init_ppc`): allocate VM, map ROM, decode if
   compressed, set `RAMBaseHost`/`ROMBaseHost`/`VMBaseDiff`, run shared
   subsystem init, `CheckROM_PPC` / `DecodeROM_PPC` / `PatchROM_PPC` (all
   four phases), `InitXLM`, `InitKernelData`, `SheepMem::Init`. Install
   backend (KPX or Unicorn-PPC). Set `GPR3 = ROMBase + 0x30d000`,
   `GPR4 = KernelDataAddr + 0x1000`. Start at `ROMBase + 0x310000`.
2. **Nanokernel boot** (patched ROM PPC code): reads boot structures from
   `0x30d000+`, skips SR/BAT/SDR init (NOPed by patches), loads PVR from
   `XLM_PVR`, sets up cache parameters from a per-CPU table, creates two
   `EmulatorData` context blocks, jumps to the DR emulator entry around
   `0x460000`.
3. **DR emulator** (PPC code interpreting 68k): runs the same 68k ROM
   startup as a real Mac. EMUL_OP traps from 68k code dispatch into PPC
   handlers at `0x380000 + (selector << 3)`, which raise SHEEP opcodes that
   the PPC backend catches.
4. **Steady state**: 60 Hz timer fires → `HandleInterrupt` → DR emulator
   picks up the IRQ. ADB / disk / SCSI / serial run via EMUL_OPs. Native PPC
   apps switch to MODE_NATIVE via Mixed Mode Manager and use NATIVE_OPs
   (38 of them).

## Mode switching

`XLM_RUN_MODE = MODE_68K → EMUL_OP → MODE_68K → MODE_NATIVE → …`.
HandleInterrupt branches by mode:

- **MODE_68K**: write 0 to the poll word at `KD+0x67c`, `rlwimi` the CR bits at
  `KD+0x674`. The DR emulator does the rest.
- **MODE_NATIVE**: save PC/LR/CTR/SP, set up nanokernel registers
  (`r1 = KernelDataAddr`, `r6 = KD+0x65c`), call `execute(ROMBase + 0x312a3c)`
  (Gossamer interrupt entry).
- **MODE_EMUL_OP**: synthesise a 68k exception frame and re-enter via
  `execute_68k`, but only if `XLM_68K_R25` says interrupts are enabled.

Copied verbatim from legacy SheepShaver — do not modify.

## SHEEP opcodes (PPC's EmulOp encoding)

```
0x18000000 | xx          xx == 0  → EMUL_RETURN  (QuitEmulator)
                         xx == 1  → EXEC_RETURN  (return from execute_68k)
                         xx == 2  → EXEC_NATIVE  (NATIVE_OP[20:25], FN[19])
                         xx >=  3 → EMUL_OP      (selector = xx - 3)
```

Major opcode 6 is undefined in real PPC. KPX hooks its decoder to dispatch.
Unicorn-PPC adds a `mac_emulop` TCG helper via patch 0004 in
`subprojects/unicorn-patches/`.

## Verified identical to legacy SheepShaver (KPX)

The KPX integration was audited file-by-file against
`legacy/SheepShaver/`. The PPC CPU core (17 files in
`src/cpu/kpx/src/cpu/ppc/`), `HandleInterrupt`, `execute_68k` /
`execute_emul_op` / `execute_sheep`, all 40+ EmulOps, all 38 NativeOps, the
ROM patches (`PatchROM_PPC`, four phases), resource patches, KernelData /
XLM init, video driver (`VideoDoDriverIO`/`Control`/`Status`/NQD hooks),
disk/SCSI/Sony/CDROM/serial/ADB driver dispatch, the tick + PRECISE_TIMING
threads — all match character-for-character. Reference target for the video
driver is the legacy IPC build (`legacy/SheepShaver/src/IPC/video_ipc_sheep.cpp`),
**not** the SDL build.

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
- `EtherIRQ` — interrupt counter.
- `ppc_ether_dispatch_frame(buf, len)` — RX callback, `CallMacOS2` into the
  stored tvect.

The 1748-line DLPI state machine in legacy `ether.cpp` is **not** ported.
18/18 guest network tests pass on PPC (DNS, UDP echo, TCP echo). Implementation
is `src/cpu/kpx/compat/ppc_ether.{h,cpp}`.

## File map

```
src/cpu/kpx/
  cpu_ppc_kpx.cpp           — sheepshaver_cpu, HandleInterrupt, tick thread,
                              execute_native_op, Platform install,
                              shared execute_native_op_pure
  emul_op_ppc.cpp           — EmulOp dispatch (OP_IRQ, OP_RESET, OP_CHECKLOAD …)
  init_ppc.cpp              — InitAll_PPC, ExitAll_PPC, PatchAfterStartup_PPC
  rom_patches_ppc.cpp       — PatchROM_PPC (4 phases), nanokernel + 68k patches
  rsrc_patches_ppc.cpp      — Resource manager CheckLoad patches
  video_ppc.cpp             — VideoInit, VideoDoDriverIO, VideoVBL
  gfxaccel_ppc.cpp          — NQD acceleration hooks (namespace ppc)
  name_registry_ppc.cpp     — Open Firmware device tree
  macos_util_ppc.cpp        — CFM (FindLibSymbol, InitCallUniversalProc)
  thunks_ppc.cpp            — Native op thunks, ExecuteNative
  ppc_memory.cpp            — SheepMem::Init/Exit, Microseconds (virtual clock)
  compat/                   — Header shims bridging KPX into mac-phoenix
  compat/ppc_ether.{h,cpp}  — Ethernet glue (FULL_DRIVER mode)
  src/                      — KPX interpreter engine (verbatim from upstream)
  dyngen_precompiled/       — JIT bytecode (x86_64)
  CMakeLists.txt            — `-fno-weak`, `-DSHEEPSHAVER=1`

src/cpu/cpu_unicorn_ppc.cpp — Unicorn-PPC backend (memory map, EmulOp callback,
                              execute loop, IRQ injection)
subprojects/unicorn-patches/ — 10 numbered patches against pristine 2.1.4
                               (PPC scaffolding, RAM at host 0, mac_emulop
                               helper, nested uc_emu_start fix, …)
```

## ROM patching summary

`PatchROM_PPC` runs four phases (verbatim from
`legacy/SheepShaver/src/rom_patches.cpp`):

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
`OP_SONY_*`, `OP_DISK_*`, `OP_CDROM_*`, `OP_SERIAL_*`, `OP_ADBOP = 29`,
`OP_INSTIME = 30`, `OP_MICROSECONDS = 33`, `OP_INSTALL_DRIVERS = 37`,
`OP_NAME_REGISTRY = 38`, `OP_RESET = 39`, `OP_IRQ = 40`,
`OP_SCSI_DISPATCH/1/2`, `OP_CHECK_SYSV = 48`, `OP_NTRB_17_PATCH = 49`,
`OP_CHECK_LOAD_INVOC = 53`, `OP_EXTFS_COMM = 54`, `OP_EXTFS_HFS = 55`,
`OP_IDLE_TIME = 56`.

NativeOp selectors in `src/cpu/kpx/compat/thunks.h`:
`NATIVE_PATCH_NAME_REGISTRY = 0`, `NATIVE_VIDEO_INSTALL_ACCEL = 1`,
`NATIVE_VIDEO_VBL = 2`, `NATIVE_VIDEO_DO_DRIVER_IO = 3`,
`NATIVE_ETHER_*` (4–10), `NATIVE_SERIAL_*` (11–16), `NATIVE_NQD_*` (18–26),
`NATIVE_CHECK_LOAD_INVOC = 27`, `NATIVE_GET_RESOURCE` etc. (28–31).

## Related

- [UnicornPpcStatus.md](UnicornPpcStatus.md) — live Unicorn-PPC status.
- `../Architecture.md` — full Platform API + interrupt overview.
- `../../CLAUDE.md` — project-wide cheat sheet.
