# Architecture Overview

How mac-phoenix fits together: Platform API, the CPU backends,
memory layout, traps, interrupts, web/WebRTC plumbing.

## Core principle: everything goes through the Platform API

The `Platform` struct (`src/common/include/platform.h`) is a function-pointer
table. Core code never calls a backend directly — it dispatches through
`g_platform`. Backend installers fill in the pointers; null drivers (in
`src/drivers/*/`*_null.cpp`) provide safe defaults so unimplemented hooks
never crash.

```c
typedef struct Platform {
    /* CPU lifecycle + execution */
    bool (*cpu_init)(void);
    CPUExecResult (*cpu_execute_one)(void);
    void (*cpu_execute_fast)(void);            /* optional run-to-completion */
    uint32_t (*cpu_get_pc)(void);
    /* … register accessors, m68k + ppc variants … */

    /* Trap / EmulOp dispatch */
    bool (*emulop_handler)(uint16_t opcode, bool probe);
    void (*trap_handler)(int type, uint16_t opcode, bool probe);
    void (*cpu_execute_68k_trap)(uint16_t trap, struct M68kRegisters *r);

    /* Interrupts */
    void (*cpu_trigger_interrupt)(int level);

    /* Driver subsystems (video, audio, scsi, serial, ether, …) */
    /* All start as null-driver pointers; real drivers swap in at startup. */
} Platform;

extern Platform g_platform;
```

## CPU backends

Selected by `--backend`. The token also implies architecture — there is no
separate `--arch` flag.

| Backend         | Arch | Implementation | Speed (Quadra boot) | Use |
|-----------------|------|----------------|---------------------|------|
| `uae`           | m68k | Hand-tuned interpreter from BasiliskII (`src/cpu/uae_cpu/`, `cpu_uae.c`) | ~5s (interp), ~3s (`--jit`) | Default for end users |
| `kpx`           | ppc  | Kheperix interpreter from SheepShaver (`src/cpu/kpx/`) | ~15s (interp), ~7s (`--jit`) | Default for PPC |

Backend installers — `cpu_uae_install`, `cpu_ppc_kpx_install` — each write
into the same `g_platform` table. The `Backend` enum and `--backend` flag are
the extension point for adding further backends.

## Memory

### m68k (Quadra) layout

```
RAM           0x00000000  32 MB
ROM           0x02000000  1 MB    (writable for patching)
ScratchMem    0x02100000  64 KB   (unit tables, host scratch)
FrameBuffer   0x02110000  4 MB    (outside RAM so CPU can't corrupt heap)
```

Direct addressing: `host_ptr = mac_addr + MEMBaseDiff`. Mac SE uses 24-bit
addressing with ROM at `0x400000`; the actual layout is selected by the
machine profile (`src/config/machine_profile.cpp`), auto-detected from the
ROM version.

UAE keeps RAM in big-endian and byte-swaps inside `do_get_mem_*` — see
`deepdive/cpu/UaeQuirks.md` and `deepdive/MemoryArchitecture.md`.

### PPC (Gossamer) layout

A 512 MB `mmap` region with `VMBaseDiff = 0` (REAL_ADDRESSING). RAM at 0,
ROM at `0x00400000`, KernelData at `0x68FFE000` (aliased at `0x5FFFE000`),
SheepMem at top of RAM. See `ppc/MemoryLayout.md`.

## Traps and EmulOps

Three flavours of trap, all dispatched through `g_platform`:

1. **EmulOps** — synthetic illegal opcodes (`0x71xx`) inserted by ROM
   patching. The CPU raises an illegal-instruction exception and the
   platform's `emulop_handler()` runs in C++.
2. **A-line traps** (`0xAxxx`) — Mac OS Toolbox calls (an 87-entry trap table
   is populated during boot).
3. **F-line traps** (`0xFxxx`) — FPU emulation.

PPC uses **SHEEP opcodes** (`0x18000000` family, an undefined PPC instruction)
for the equivalent of EmulOps; the encoding splits into EMUL_RETURN /
EXEC_RETURN / EXEC_NATIVE / EMUL_OP. KPX catches them through its decoder
(major opcode 6 is undefined in real PPC).

### Native trap execution

When a host EmulOp needs to call back into Mac code (e.g. running a device
driver), the backend builds a 68k frame and runs the inner interpreter:

- UAE: native `Execute68kTrap()`.
- KPX: `sheepshaver_cpu::execute_68k()` enters the ROM's PPC-native 68k
  emulator with a fake stack containing `EXEC_RETURN`.

## Interrupts

Timer/device code calls `g_platform.cpu_trigger_interrupt(level)`. The 60 Hz
tick comes from `src/drivers/platform/timer_interrupt.cpp` (m68k) or
`src/cpu/kpx/cpu_ppc_kpx.cpp`'s tick thread (PPC).

- **UAE**: sets `SPCFLAG_INT`, processed by `do_specialties()`. UAE's native
  `Interrupt()` builds the m68k stack frame, switches to supervisor mode, reads
  the autovector, and jumps.
- **KPX**: nanokernel IRQ entry via `sheepshaver_cpu::interrupt(entry)`.

PPC interrupt delivery (`XLM_IRQ_NEST` gating, per-mode entry for MODE_68K /
MODE_NATIVE / MODE_EMUL_OP) is covered in `ppc/README.md`.

## Process and thread topology

In webserver mode there are **two processes**:

- **Parent** (`mac-phoenix`): HTTP server, WebRTC signaling, video encoder
  thread, video relay thread, BridgeAgent watchdog, MacBrowser supervisor.
- **CPU subprocess** (`--ipc`): runs the actual emulator, writes frames into
  the IPC SHM, reads input over a Unix socket.

Both processes see the same bridge directory on disk, which is how
`/api/launch` etc. communicate with the in-guest BridgeAgent without an
in-memory mailbox.

`--no-webserver` runs single-process; the bridge still goes through files
because the agent inside the guest is a separate Mac OS process.

See `ThreadingArchitecture.md` for thread-by-thread roles and ownership rules.

## Web stack

One TCP listener on `--port` (default 11000) serves:

- The static client (HTML/JS/CSS in `client/`).
- REST API at `/api/*` (see CLAUDE.md for the full table).
- `/api/frame` long-poll for the `httpstream` codec.
- WebSocket upgrade at `/ws` — signaling JSON, input events, and PNG/WebP
  frames all ride this socket.

WebRTC RTP (H.264 / VP9 video, Opus audio) negotiates over `/ws` and ends up
on independently-bound UDP ports — that traffic doesn't pass through the
HTTP listener.

## Backend selection flow

```c
switch (config.cpu_backend) {
    case Backend::KPX:         cpu_ppc_kpx_install(&g_platform);     break;
    case Backend::UAE:
    default:                   cpu_uae_install(&g_platform);         break;
}
```

## File map

```
src/common/include/platform.h        — Platform struct (function pointers)
src/common/platform.cpp              — wires null drivers
src/common/sigsegv.cpp               — host SIGSEGV skip-instruction handler

src/cpu/cpu_uae.c                    — UAE backend installer
src/cpu/uae_cpu/                     — UAE interpreter sources
src/cpu/uae_wrapper.{cpp,h}          — UAE wrapper
src/cpu/kpx/cpu_ppc_kpx.cpp          — KPX install + sheepshaver_cpu glue
src/cpu/kpx/src/cpu/ppc/             — KPX interpreter (verbatim from SheepShaver)

src/core/main.cpp                    — entry point
src/core/cpu_context.cpp             — RAM/ROM allocation, init_m68k, init_ppc
src/core/rom_patches.cpp             — m68k ROM patches + EmulOp insertion
src/core/emul_op.cpp                 — m68k EmulOp dispatcher
src/core/command_bridge.{cpp,h}      — read commands + watchdog
src/core/boot_progress.{cpp,h}       — boot phase / CHECKLOAD tracking

src/webserver/                       — HTTP server, /ws, API handlers
src/webrtc/                          — peer-connection plumbing
```

## Related

- `CLAUDE.md` — high-density project overview.
- `Commands.md` — build/run/test commands.
- `JsonConfig.md` — config schema.
- `ThreadingArchitecture.md` — per-thread responsibilities.
- `deepdive/MemoryArchitecture.md`, `deepdive/cpu/*` — quirks and gotchas.
- `ppc/` — PPC integration in detail.
