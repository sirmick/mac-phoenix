# PPC Execution Model

Boot sequence, mode switching, interrupt handling, and the 68k↔PPC bridge.

## Boot Sequence

### Phase 1: Host Setup (cpu_context.cpp init_ppc)

1. Allocate 512MB VM space (mmap)
2. Map ROM into VM at 0x400000
3. Load ROM file, detect type via ID string at 0x30d064
4. Decode ROM if compressed (LZSS/parcels)
5. Set globals: RAMBaseHost, ROMBaseHost, RAMBaseMac, ROMBaseMac, VMBaseDiff
6. init_mac_subsystems() — shared with M68K (disk, video, serial, ether, audio)
7. CheckROM_PPC() / DecodeROM_PPC()
8. PatchROM_PPC() — all four phases (nanokernel boot, DR emul, nanokernel, 68k)
9. InitXLM() — set up XLM area at 0x2800
10. InitKernelData() — set up nanokernel data structures at KernelDataAddr
11. SheepMem::Init() — allocate thunks area at top of RAM
12. Install CPU backend (cpu_ppc_kpx_install)
13. Set GPR3 = ROMBase + 0x30d000, GPR4 = KernelDataAddr + 0x1000
14. Start execution: cpu_ppc_execute(ROM + 0x310000)

### Phase 2: Nanokernel Boot (PPC code in ROM)

Patched nanokernel executes:
1. Read boot structure pointers from 0x30d000+
2. Skip SR/BAT/SDR init (patched to NOPs)
3. Read PVR from XLM_PVR (patched, no mfpvr instruction)
4. Initialize cache size registers from ROM table
5. Set up kernel data structures, create TWO EmulatorData context blocks
6. Jump to 68k DR emulator entry at ~0x460000

### Phase 3: 68k Emulator Boot (PPC code interpreting 68k)

The ROM contains a PPC-native 68k interpreter (DR emulator):
1. Reads 68k opcodes from memory
2. Dispatches through opcode table at ~0x480000
3. Executes 68k ROM startup code (same as M68K Mac boot)
4. 68k code encounters EMUL_OP traps → host handles I/O
5. Boot progresses: OP_RESET → OP_FIX_MEMSIZE → OP_NAME_REGISTRY →
   OP_INSTALL_DRIVERS → boot blocks → extensions → Finder

### Phase 4: Steady State

- 68k Mac OS runs via ROM's built-in DR interpreter
- 60Hz timer fires → HandleInterrupt delivers VBL
- User input via ADB EmulOps
- Disk I/O via SCSI/Sony EmulOps
- Native PPC apps use Mixed Mode Manager to switch to MODE_NATIVE

## Mode Switching

Three execution modes tracked at XLM_RUN_MODE (0x2810):

```
MODE_68K (0)
  │
  ├─ EMUL_OP trap ──→ MODE_EMUL_OP (2)
  │                      │
  │                      ├─ Host handler runs (disk I/O, etc.)
  │                      │
  │                      └─ Return ──→ MODE_68K
  │
  └─ Mixed Mode ──→ MODE_NATIVE (1)
                      │
                      ├─ PPC native code runs
                      │
                      ├─ NATIVE_OP trap ──→ host handler ──→ MODE_NATIVE
                      │
                      └─ Return ──→ MODE_68K
```

### EMUL_OP Transition (68k → host)

1. 68k code executes synthetic opcode (e.g., M68K_EMUL_OP_DISK_OPEN)
2. ROM's DR emulator dispatches to PPC handler at 0x380000 + (selector << 3)
3. PPC handler executes SHEEP opcode (0x18000000 | (selector + 3))
4. KPX `execute_sheep()` catches the opcode
5. `execute_emul_op()` maps GPR8-15 → d[0-7], GPR16-22 → a[0-6], GPR1 → a[7]
6. Calls `ppc_emul_op(selector, &regs)`
7. Writes modified registers back to GPRs
8. Sets XLM_RUN_MODE back to MODE_68K
9. Returns to KPX execution loop

### NATIVE_OP Transition (PPC → host)

1. PPC code executes SHEEP opcode with EXEC_NATIVE (lower bits = 2)
2. NATIVE_OP_field (bits 20-25) selects handler
3. FN_field (bit 19) controls return: 0 = continue, 1 = return via LR
4. `execute_native_op()` dispatches to C++ function (VideoDoDriverIO, EtherSend, etc.)
5. If FN=1, set PC = LR; else PC += 4

## Interrupt Handling (KPX)

### Timer Thread (tick_thread_func)

A separate thread fires at ~60Hz (16625µs period). Each tick:
1. `ADBMouseMoved(320, 240)` — once at boot for mouse device presence
2. 1Hz: `WriteMacInt32(0x20c, time + Mac epoch)` — update Mac clock
3. `SetInterruptFlag(INTFLAG_VIA)` — atomic OR on InterruptFlags
4. `TriggerInterrupt()` → `ppc_cpu->trigger_interrupt()` → sets SPCFLAG_CPU_TRIGGER_INTERRUPT
5. `g_platform.video_refresh()` — capture frame for WebRTC/screenshot

### PRECISE_TIMING Timer Thread (timer_func)

A second thread handles PrimeTime tasks with virtual-clock precision:
1. `clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wakeup_time)` — returns
   immediately since wakeup_time (virtual ns) << CLOCK_REALTIME
2. `timer_current_time()` → checks virtual clock (ppc_insn_counter * 4ns)
3. If virtual time >= wakeup_time: fire timer, set INTFLAG_TIMER, TriggerInterrupt
4. If not: busy-wait (loop back to step 1)
5. When no timer pending: wakeup_time = MAX → sleeps until resumed by signal

### KPX spcflags Check

In the KPX decode cache loop, after each basic block, spcflags are checked. When
SPCFLAG_CPU_TRIGGER_INTERRUPT is set, `HandleInterrupt()` is called.

### HandleInterrupt Dispatch by Mode

This function is copied from legacy `sheepshaver_glue.cpp`. It reads XLM_RUN_MODE
and dispatches accordingly:

**MODE_68K:**
- Write 0 to interrupt level word at KD + 0x67c
- Set CR bits at KD + 0x674 (rlwimi pattern from legacy)
- The DR emulator's main loop checks this poll word and vectors to the interrupt handler
- That's ALL — the DR emulator handles the rest

**MODE_NATIVE:**
- Save PC/LR/CTR/SP
- Set up nanokernel registers:
  - r1 = KernelDataAddr
  - r6 = KD + 0x65c (context block)
  - Various CR bit manipulation
- Call `execute(ROMBase + 0x312a3c)` (Gossamer interrupt entry)
- Restore saved state

**MODE_EMUL_OP:**
- Check if 68k interrupts enabled (XLM_68K_R25 bit check)
- If enabled, trigger 68k interrupt via execute_68k with synthetic exception frame
- Save/restore 68k interrupt level (XLM_68K_R25)

## Execute68k (Host → 68k)

Used by EmulOp handlers that need to call 68k Mac toolbox routines. Copied from
legacy `sheepshaver_glue.cpp::execute_68k()`:

1. Save PPC context (GPR1, LR, etc.)
2. Set up DR emulator registers:
   - GPR8-15 from M68kRegisters d[0-7]
   - GPR16-22 from M68kRegisters a[0-6]
   - GPR1 = a[7] (68k stack pointer)
   - r24 = 68k entry address (68k PC)
   - r25 = SR
   - r27 = prefetched opcode
   - r29/r30 = dispatch tables
   - r31 = KernelData + 0x1000
3. Push EXEC_RETURN opcode on 68k stack
4. Set XLM_RUN_MODE = MODE_68K
5. Call `execute(XLM_EMUL_RETURN_PROC)` — enters DR emulator
6. DR emulator runs until it hits EXEC_RETURN SHEEP opcode
7. Copy GPR8-22 back to M68kRegisters
8. Restore PPC context

**Do NOT modify this function.** Legacy SheepShaver's version is ~20 lines and works.

## Boot Progress and the SystemTask Gap (Session 10)

### What Loads Successfully

The boot sequence progresses through these phases:

1. **ROM init** (0.00s): OP_RESET, nanokernel setup
2. **Warm start** (0.32s): WLSC flag set after 51 resources
3. **Driver install** (0.33s): .Sony, .Disk, .AppleCD, serial
4. **Extension loading** (0.33-1.0s): 1462 OP_CHECKLOAD resources
5. **Font loading** (1.0-1.5s): FOND, sfnt, NFNT resources
6. **ntrb patches**: Native toolbox resources including ntrb 17
7. **PatchNativeResourceManager**: Fires correctly from ntrb 17 detection
8. **NQD acceleration**: Installed (69 calls) via forced PatchAfterStartup
9. **Post-extension resources**: MBDF, clut, snd, itlb (2313 total)

### Where It Stalls

After loading all resources, the 68k code enters an endless loop:
- DISK_PRIME (2896 reads, all succeed, positions advance)
- Then PRIMETIME repeating (timer task rescheduling)
- MODE_NATIVE drops to 0 and never recovers
- `app=''` (CurApName empty — Finder never launches)

### Why: SystemTask Never Called

In Mac OS 9, `SystemTask()` is the periodic driver action dispatcher. It scans
drivers with the `dNeedTime` flag (bit 0x2000 in dCtlFlags) and calls their
`Control(65)` handler (accRun). The Sony driver's accRun triggers:
- `mount_mountable_volumes()` — mounts pending disks
- `PatchAfterStartup()` — installs NQD + ExtFS (we force this as workaround)

`SystemTask()` is called by the Mac OS event loop (`WaitNextEvent`/`GetNextEvent`)
and also explicitly during certain boot phases. In legacy SheepShaver, it fires
within the first second (NQD=53 in NOP-RATE #1). In mac-phoenix, it never fires.

The 68k code that should call SystemTask is stuck in a loop — likely waiting for
a condition that depends on interrupt delivery timing. Different interrupt
interleaving in mac-phoenix vs legacy causes the 68k code to take a different
branch at a critical point, entering an infinite loop instead of proceeding to
SystemTask.

### Key Evidence

EmulOp comparison (non-IRQ):
- First 600 ops: match with only 3 trivial differences
- Legacy total: 9127 → ends with OP_IDLE_TIME_2 (Finder)
- Mac-phoenix total: 5260 → ends with DISK_PRIME + PRIMETIME loop
- Divergence is gradual: same ops, different interleaving with IRQs
No context fixups, no extra register saves, no KernelData manipulation needed.

## execute_macos_code (Host → PPC via TVECT)

Used to call PPC Mac toolbox functions by TVECT address. Reads proc entry and TOC
from the TVECT, saves/restores r2 and arguments, calls `execute(proc)`.

Copy verbatim from legacy — no modifications needed.

## Comparison with M68K Execution Model

| Aspect | M68K | PPC |
|--------|------|-----|
| CPU engine | UAE interpreter or Unicorn M68K | KPX (Kheperix) interpreter |
| 68k execution | Direct (native M68K) | Via ROM's built-in DR interpreter |
| EmulOp encoding | 0x71xx (UAE) or 0xAExx (Unicorn) | 0x18000000+ (SHEEP opcodes) |
| EmulOp detection | A-line exception | KPX opcode handler |
| Interrupt delivery | Push exception frame, jump to vector | Write to KD poll word + CR bits |
| Mode switching | N/A (always 68k) | MODE_68K / MODE_NATIVE / MODE_EMUL_OP |
| Timer mechanism | timer_interrupt.cpp (same) | timer_interrupt.cpp + spcflags |
| Boot ROM | 68k code runs directly | PPC nanokernel → DR interpreter |
