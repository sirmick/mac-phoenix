# PPC Legacy SheepShaver Comparison

> **Updated session 10 (March 30 2026).** Correct reference is the IPC video
> driver, not SDL. All interrupt/timer/video code confirmed identical.
> Remaining issue is 68k never reaching SystemTask.

## Reference Targets

| File | Use for | NOT for |
|------|---------|---------|
| `legacy/SheepShaver-clean/src/IPC/video_ipc_sheep.cpp` | Video init, display_type, cursor | — |
| `legacy/SheepShaver-clean/src/SDL/video_sdl2.cpp` | — | Wrong display_type, wrong cursor |
| `legacy/SheepShaver-clean/src/kpx_cpu/sheepshaver_glue.cpp` | CPU bridge, HandleInterrupt | — |
| `legacy/SheepShaver-clean/src/Unix/main_unix.cpp` | Tick thread, init sequence | — |
| `legacy/SheepShaver-clean/src/video.cpp` | VideoDoDriverIO, Control, Status | — |
| `legacy/SheepShaver-clean/src/emul_op.cpp` | EmulOp dispatch | — |
| `legacy/SheepShaver-clean/src/gfxaccel.cpp` | NQD acceleration | — |

---

## Verified Identical (Session 10)

### PPC CPU Core (17 files)
All files in `src/cpu/kpx/src/cpu/ppc/` match legacy character-for-character:
ppc-cpu.cpp, ppc-cpu.hpp, ppc-decode.cpp, ppc-execute.cpp, ppc-execute.hpp,
ppc-translate.cpp, ppc-jit.cpp, ppc-jit.hpp, ppc-dyngen.cpp, ppc-dyngen.hpp,
ppc-dyngen-ops.cpp, ppc-bitfields.hpp, ppc-blockinfo.hpp, ppc-instructions.hpp,
ppc-operands.hpp, ppc-operations.hpp, ppc-registers.hpp.

### Glue Code
| Area | Status |
|------|--------|
| HandleInterrupt (3 modes) | Identical |
| execute_68k / execute_emul_op / execute_sheep | Identical |
| check_spcflags / trigger_interrupt | Identical |
| EmulOp dispatch (all 40+ selectors) | Identical |
| NativeOp dispatch (all 38) | Identical |
| OP_IRQ handler (all INTFLAG checks) | Identical |
| CheckLoad / resource patches | Identical |
| ROM patches (PatchROM_PPC) | Identical |
| VideoDoDriverIO / Control / Status | Identical |
| VideoInstallAccel / NQD hooks | Identical (namespace ppc) |
| DiskPrime / DiskControl / SonyControl | Shared code |
| ThunksInit / thunks | Identical |
| InitAll_PPC sequence | Identical |
| KernelData + XLM init | Identical |

### Interrupt/Timer Subsystem
| Area | Status |
|------|--------|
| Tick thread (60Hz) | Identical logic (nanosleep vs select) |
| SetInterruptFlag / ClearInterruptFlag | Both atomic |
| TriggerInterrupt | Both call ppc_cpu->trigger_interrupt() |
| PRECISE_TIMING timer thread | Identical (clock_nanosleep + virtual clock) |
| timer_current_time | Both use ppc_insn_counter * 4ns |
| PrimeTime scheduling | Identical |
| idle_wait / idle_resume | Both pthread_cond_wait/signal |

---

## Remaining Differences

### Behavioral (could affect boot)

| # | Area | mac-phoenix | Legacy IPC | Impact |
|---|------|-------------|-----------|--------|
| 1 | SDL_PumpEvents | Absent | Absent (IPC has none either) | N/A for IPC |
| 2 | ControlIPC input | Not connected in headless | Has socket input thread | No events in headless |
| 3 | accRun / SystemTask | Never fires naturally | Fires during boot | **Workaround: forced** |
| 4 | Video refresh thread | tick_thread callback | Own 60fps thread | Different timing |
| 5 | video_set_cursor | No-op | Writes to SHM | Cursor not visible |
| 6 | SHM frame conversion | Not present | ARGB→BGRA every frame | No output in headless |

### Stubs (no boot impact)

| Function | mac-phoenix | Legacy |
|----------|-------------|--------|
| Serial (7 functions) | Return 0 | Real serial driver |
| Ethernet (10 functions) | Return 0 | Real network |
| Audio (14 functions) | No-op | SDL audio |
| Clipboard (4 functions) | No-op | Real clipboard |
| B2_mutex lock/unlock | No-op | Real pthread mutex |
| VideoQuitFullScreen | No-op | SDL cleanup |

### Configuration

| Setting | mac-phoenix | Legacy IPC | Legacy SDL |
|---------|-------------|-----------|------------|
| display_type | DIS_SCREEN | DIS_SCREEN | DIS_WINDOW |
| video_can_change_cursor | true | true | PrefsFindBool("hardcursor") |
| VModes | 1x 32bit APPLE_CUSTOM | 1x 32bit APPLE_CUSTOM | Multi-depth |
| PrefsFindBool | Hardcoded defaults | N/A (IPC) | Reads prefs file |
| PrefsFindInt32 | Returns 0 | N/A (IPC) | Reads prefs file |

---

## Boot Sequence Comparison

### EmulOp Stream (non-IRQ, first 600)
Both systems produce the same EmulOp sequence with only 3 minor differences:
1. Mac-phoenix has 1 extra NVRAM1 read (position 44)
2. Mac-phoenix has 1 extra CHECKLOAD + ADBOP (the extra accl id=0)
3. Legacy has 5 extra SCSI_DISPATCH calls (position ~598)

### Resource Loading (CheckLoad)
1462 resources load in nearly identical order. Only 2 differences:
1. Mac-phoenix has extra `accl id=0` at CL#39 (display probe)
2. CL#846: `KMAP id=0` vs `KMAP id=5` (keyboard type config)

### End State
- **Legacy**: 9127 non-IRQ EmulOps → DISK_PRIME (5300) → DISK_CONTROL → IDLE_TIME_2
- **Mac-phoenix**: 5260 non-IRQ EmulOps → DISK_PRIME (2896) → PRIMETIME loop → stall

---

## RAM Divergence (Session 10 Bisection)

With `srand(1)` in both ADBInit functions:

| Checkpoint | RAM CRC | LM CRC | Status |
|-----------|---------|--------|--------|
| CL#1 | Match | Match | Identical |
| CL#10 | Match | Match | Identical |
| CL#38 | Match | Match | Last match |
| CL#39 | **Differ** | Match | ADB mouse data (SDL event in legacy) |
| CL#50+ | Differ | Differ | Cascading heap layout differences |
| CL#1462 | 62% differ | Differ | All heap pointers offset |

The CL#39 divergence is caused by `SDL_PumpEvents()` generating a mouse event
in legacy that writes ADB register data to Mac RAM. The Lv1Int handler code at
the different addresses is byte-identical.
