# Architecture Overview

How mac-phoenix fits together: the Platform API, the CPU cores behind
`cpu::Core`, the three backends, memory layout, EmulOps, interrupts,
processes and the web/WebRTC plumbing.

## Core principle: everything goes through the Platform API

The `Platform` struct (`src/common/include/platform.h`) is a function-pointer
table. Core code never calls a backend directly; it dispatches through
`g_platform`. `platform_init()` (`src/common/platform.cpp`) fills the driver
entries with defaults: null drivers for SCSI, video, audio and Ethernet
(`src/drivers/*/*_null.cpp`), the Unix file/disk driver
(`src/drivers/platform/platform_unix.cpp`) and the Unix serial driver. Real
drivers and the CPU installer overwrite entries at startup.

```c
typedef struct {
    /* Drivers: scsi_*, video_init/exit/refresh, audio_init/exit,
       serial_init/exit, ether_*, sys_* (file and CD-ROM I/O), mount_volume … */

    /* CPU */
    const char *cpu_name;
    bool (*cpu_init)(void);
    void (*cpu_reset)(void);
    void (*cpu_set_type)(int cpu_type, int fpu_type);
    int  (*cpu_execute_one)(void);
    void (*cpu_execute_fast)(void);          /* run until request_stop */
    void (*cpu_request_stop)(void);
    uint32_t (*cpu_get_pc)(void);            /* + get_sr, get_dreg, get_areg */
    void (*cpu_trigger_interrupt)(int level);
    void (*invoke_debug)(void);              /* NMI on 68k, Cmd+Power on PPC */
    void (*cpu_execute_68k_trap)(uint16_t trap, struct M68kRegisters *r);
    void (*cpu_execute_68k)(uint32_t addr, struct M68kRegisters *r);
    void (*flush_code_cache)(void);

    /* PPC only (NULL on 68k): cpu_get_gpr/cr/lr/ctr, cpu_execute_ppc,
       ppc_execute_macos_code, ppc_emulop_handler, ppc_native_op, … */

    /* Guest memory, big-endian: mem_read_*/mem_write_*,
       mem_mac_to_host, mem_host_to_mac */
} Platform;

extern Platform g_platform;
```

## CPU cores and backends

Two separate choices:

- `--backend` picks the machine, and with it the architecture (there is no
  `--arch` flag): `uae` (ROM-based 68k Mac), `kpx` (Power Mac G3, PPC ROM),
  `executor` (Executor 2000's Toolbox in C++, no ROM).
- `--core` picks the CPU core that runs the guest instructions for that
  architecture. `EmulatorConfig::normalize_cpu_core()` resets it to the
  backend's default when it doesn't fit.

| Backend | Arch | Cores (`--core`) | Installer |
|---------|------|------------------|-----------|
| `uae` (default) | m68k | `uae` (default; `--jit` for the UAE JIT on x86), `mame-68k` (MAME's Musashi; `musashi` is an alias) | `cpu_m68k_install()` (`src/cpu/cpu_m68k.cpp`) |
| `kpx` | ppc | `kpx` (default; Kheperix, `--jit` for its dyngen JIT), `mame-ppc` (MAME's PowerPC recompiler; `--jit` native x86-64/arm64 back-end, `--no-jit` UML interpreter) | `cpu_ppc_kpx_install()` (`src/cpu/kpx/cpu_ppc_kpx.cpp`) or `cpu_ppc_mame_install()` (`src/cpu/kpx/cpu_ppc_mame.cpp`) |
| `executor` | m68k | `uae` (default) or `mame-68k` | `src/executor/` (`executor_child_main()`, `executor_direct_main()`) |

`cpu::Core` (`src/cpu/core/cpu_core.h`) is the one interface for the cores
`uae`, `mame-68k` and `mame-ppc`: `GuestMemory` (the 32-bit guest address
space in 64 KB pages, each mapped to host memory or an I/O handler), `Host`
(callbacks: `host_op()` for an instruction in the host-call space,
`attention()` at an instruction boundary after `request_attention()`), and
`Core` (reset, `run()`, `step()`, `set_irq()`, registers, context
save/restore). `cpu::create(name, ...)` (`src/cpu/core/registry.cpp`) makes
one by name. `src/cpu/core/platform_cpu.cpp` fills `g_platform`'s memory,
register, `Execute68k`/`Execute68kTrap`, interrupt and code-cache entries
from a 68k core (the ROM machine and Executor); the PPC installers fill
their own.

Kheperix (`kpx`) is not a `cpu::Core`: `cpu_ppc_kpx.cpp` drives SheepShaver's
`sheepshaver_cpu` directly. Both PPC installers share the SheepShaver
machine side (ROM patches, EmulOps, NativeOps, thunks, drivers) in
`src/cpu/kpx/`.

Docs: `cpu/PLAN.md` (MAME cores), `ppc/README.md` (the PPC machine),
`executor/PLAN.md` (Executor).

## Memory

### 68k ROM machine

`CPUContext::init_m68k()` (`src/core/cpu_context.cpp`) allocates one block
(with `MAP_32BIT` where available, for the UAE JIT) and maps the whole of it
at guest address 0 in the core's `GuestMemory`:

```
RAM           0                        --ram MB (default 64)
ROM           RAMSize                  the ROM file, kept big-endian (1 MB reserved)
ScratchMem    ROM + ROMSize            64 KB (host scratch, unit tables)
FrameBuffer   ROM + ROMSize + 64 KB    8 MB (outside RAM so the guest can't corrupt it)
```

With 32 MB of RAM and the 1 MB Quadra ROM that puts the ROM at
`0x02000000` and the frame buffer at `0x02110000`. Machines with a fixed ROM base put the ROM there instead and
cap RAM: the SE (ROM `0x400000`, 4 MB), the Mac II and IIci (ROM
`0x800000`, 8 MB), all 24-bit. The profile comes from the ROM's version and
checksum (`set_machine_profile()`, `src/config/machine_profile.cpp`).

Guest memory is stored big-endian, as on a Mac. Data accesses go through
`g_platform.mem_*` to `GuestMemory`; UAE fetches instructions through
`regs.pc_p` and byte-swaps with `do_get_mem_*` — see
`deepdive/cpu/UaeQuirks.md` and `deepdive/MemoryArchitecture.md`.

### PPC (G3) machine

`CPUContext::init_ppc()`: RAM at 0 (at least 16 MB, plus 8 MB padding for
the nanokernel's memory probe), ROM at `0x50000000` (5 MB area,
write-protected after patching), KernelData at `0x68FFE000`, shared with
`0x5FFFE000`, DR emulator at `0x68070000` and DR cache at `0x69000000`
(mapped after nanokernel init). With `--core kpx` addressing is identity
(host = Mac address; needs `vm.mmap_min_addr=0`). With `--core mame-ppc`
everything sits in a 4 GB-aligned 4 GB reservation, host = `VMBaseDiff` +
Mac address, and no sysctl is needed.

### Executor

No ROM. On the `uae` core guest addresses are host addresses (needs
`vm.mmap_min_addr=0`); `--core mame-68k` uses four address windows and does
not. See `executor/MEMORY_MAP.md`.

## EmulOps and traps

- **EmulOps** — host calls the ROM patcher (`src/core/rom_patches.cpp`)
  writes into the ROM: `$71xx`, a MOVEQ form that is illegal on every 68k
  (`cpu::m68k::is_host_op()`). The core hands them to its `Host`
  (`RomHost` in `cpu_m68k.cpp`): `$7100` (`M68K_EXEC_RETURN`) returns to the
  host and ends an `Execute68k`; every other one goes to `m68k::EmulOp()`
  (`src/core/emul_op.cpp`). The numbers are in
  `src/common/include/emul_op.h`. The patcher writes every one through
  `platform_make_emulop()`, so a backend could remap the encoding.
- **PPC** uses SheepShaver's encoding, primary opcode 6 (unused on every
  PowerPC; `cpu::ppc::is_host_op()`), split into EMUL_RETURN / EXEC_RETURN /
  EXEC_NATIVE / EMUL_OP. 68k EmulOps run by the ROM's 68k emulator arrive
  through the same path.
- **A-line traps** are the Mac's own: the guest's trap dispatcher runs them.
  The emulator patches a few trap table entries: `_StdText`, `_CopyBits`
  and `_ScrollRect` for `/api/text` (`src/core/text_feed.cpp`) and the
  bridge trap `$ABE5` (`src/common/include/guest_bridge.h`). `mame-68k` can
  also offer A-line words to `Host::trap()` before the exception.

### Calling guest code from the host

`g_platform.cpu_execute_68k()` / `cpu_execute_68k_trap()`
(`platform_cpu.cpp`) push a `$7100` return op, set the PC and call
`Core::run_until()`; the nested `run()` ends at that op. On Kheperix,
`sheepshaver_cpu::execute_68k()` enters the ROM's 68k emulator with an
`EXEC_RETURN` frame.

## Interrupts

On the 68k machine a 60.15 Hz tick thread
(`src/drivers/platform/timer_interrupt.cpp`) sets `INTFLAG_60HZ` (and
`INTFLAG_1HZ` once a second) and calls `g_platform.cpu_trigger_interrupt()`,
which is `core->request_attention()`. At the next instruction boundary the
core calls `RomHost::attention()`, which sets the interrupt line to level 1
while any `InterruptFlags` bit is set (an NMI from `invoke_debug` is a
level-7 edge). The ROM's level-1 handler is patched to the `IRQ` EmulOp,
which drains the flags. The tick thread also refreshes video and exports
cursor, app name and Mac state to the IPC buffer.

The PPC installers run their own 60 Hz tick threads (`cpu_ppc_kpx.cpp`,
`cpu_ppc_mame.cpp`) and deliver interrupts through the nanokernel
(`HandleInterrupt`, gated on `XLM_IRQ_NEST`); see `ppc/README.md`.

## Process and thread topology

In webserver mode there are **two processes**:

- **Parent** (`mac-phoenix`): HTTP server (also the `/ws` WebSocket and
  WebRTC signaling), video encoder thread, audio reader and encoder
  threads.
- **Child**: the same binary, started by `EmulatorSubprocess`
  (`src/core/emulator_subprocess.cpp`) with
  `--config /dev/null --ipc --no-webserver --backend …`. It runs the
  emulator — ROM machine or Executor — writes frames and sound into the IPC
  shared memory and takes input over a local socket.

The action commands (`/api/launch` etc.) reach the in-guest BridgeAgent
through the bridge slot in the IPC shared memory, which the guest reads
with the bridge trap `$ABE5` (`CommandBridge.md`). `--no-webserver` runs
everything in one process, with the slot in process memory; add
`--headless-http` for the HTTP API without video (Executor still runs as a
child then).

See `ThreadingArchitecture.md` for threads and IPC.

## Web stack

One TCP listener on `--port` (default 11000), `src/webserver/http_server.cpp`:
a QThread accept loop hands connections to 16 worker QThreads. It serves:

- The static client (`client/`).
- REST API at `/api/*` (`api_handlers.cpp`; see CLAUDE.md for the table).
- `/api/frame` long-poll for the `httpstream` codec.
- WebSocket upgrade at `/ws` (`websocket.cpp`): the fd is `dup()`'d to a
  thread that owns the socket. Signaling JSON, input events, PNG/WebP
  frames and, with those codecs, Opus audio packets ride it.

WebRTC RTP (H.264 / VP9 video, Opus audio) negotiates over `/ws` and goes
over UDP ports libdatachannel binds, not through the HTTP listener.

## Backend selection flow

In the IPC child (`src/main.cpp`), and the same in-process for
`--no-webserver`:

```c++
if (backend == Backend::EXECUTOR)
    return executor_child_main(emu_config, ipc_buf);   // src/executor/host/
if (is_ppc())
    g_cpu_ctx.init_ppc(emu_config);   // cpu_ppc_mame_install if --core mame-ppc,
                                      // else cpu_ppc_kpx_install
else {
    cpu_m68k_install(platform, emu_config.cpu_core.c_str(), emu_config.jit);
    g_cpu_ctx.init_m68k(emu_config);
}
platform->cpu_execute_fast();         // the CPU runs on the main thread
```

## File map

```
src/main.cpp                         — entry point: parent, IPC child, headless
src/common/include/platform.h        — Platform struct (function pointers)
src/common/platform.cpp              — platform_init(): driver defaults
src/common/sigsegv.cpp               — host SIGSEGV skip-instruction handler
src/common/include/guest_bridge.h    — the bridge trap $ABE5

src/cpu/core/cpu_core.h              — cpu::Core, GuestMemory, Host
src/cpu/core/registry.cpp            — cpu::create(): uae, mame-68k, mame-ppc
src/cpu/core/platform_cpu.cpp        — g_platform CPU/memory entries on a Core
src/cpu/core/uae_core.cpp            — cpu::Core on UAE
src/cpu/uae_cpu/                     — UAE interpreter (+ JIT in compiler/)
src/cpu/mame/                        — MAME's Musashi and PowerPC cores
src/cpu/cpu_m68k.cpp                 — the ROM-based 68k Mac on a Core
src/cpu/kpx/cpu_ppc_kpx.cpp          — PPC machine on Kheperix
src/cpu/kpx/cpu_ppc_mame.cpp         — PPC machine on mame-ppc
src/cpu/kpx/src/                     — Kheperix sources (from SheepShaver)

src/core/cpu_context.cpp             — memory layout, init_m68k, init_ppc
src/core/main.cpp                    — Basilisk II InitAll/ExitAll
src/core/rom_patches.cpp             — 68k ROM patches + EmulOp insertion
src/core/emul_op.cpp                 — 68k EmulOp handlers
src/core/emulator_subprocess.cpp     — starts and stops the IPC child
src/core/command_bridge.{cpp,h}      — read commands + init
src/core/boot_progress.{cpp,h}       — boot phase / CHECKLOAD tracking
src/ipc/                             — IPC protocol, child control socket, IPC video

src/executor/                        — Executor 2000 Toolbox; host/ is its IPC child
src/webserver/                       — HTTP server, /ws, API handlers
src/webrtc/                          — peer-connection plumbing
```

## Related

- `CLAUDE.md` — high-density project overview.
- `Commands.md` — build/run/test commands.
- `JsonConfig.md` — config schema.
- `ThreadingArchitecture.md` — threads, IPC, shutdown.
- `deepdive/MemoryArchitecture.md`, `deepdive/cpu/*` — quirks and gotchas.
- `ppc/README.md` — the PPC machine in detail.
- `cpu/PLAN.md` — the MAME cores.
- `executor/PLAN.md` — the Executor backend.
