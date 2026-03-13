# PPC Execution Model

Boot sequence, mode switching, interrupt handling, and the 68k↔PPC bridge.

## Boot Sequence

### Phase 1: Host Setup (cpu_context.cpp init_ppc)

1. Allocate memory buffer (RAM + ROM + ScratchMem + FrameBuffer)
2. Load Gossamer ROM, detect type via ID string at 0x30d064
3. Decode ROM if compressed (LZSS/parcels)
4. Allocate Kernel Data at 0x68FFE000 (8KB)
5. Initialize XLM globals at 0x2800:
   - XLM_SIGNATURE = "Baah"
   - XLM_KERNEL_DATA = KernelDataAddr
   - XLM_PVR = 0x00080200 (750 v2.0)
   - XLM_RUN_MODE = MODE_68K
   - XLM_IRQ_NEST = 0
6. Initialize shared subsystems (XPRAM, drivers, video, ADB, etc.)
7. Install Unicorn PPC backend (`cpu_ppc_unicorn_install`)
8. Apply ROM patches (all four phases)
9. Initialize Unicorn engine, map memory regions
10. Set initial registers:
    - R1 = stack pointer (RAM + 4MB)
    - R3 = ROMBase + 0x30d000 (boot structure)
    - R4 = KernelDataAddr + 0x1000 (emulator data)
    - PC = ROM entry point (nanokernel boot)
11. Start execution

### Phase 2: Nanokernel Boot (PPC code in ROM)

Patched nanokernel executes:
1. Read boot structure pointers from 0x30d000+
2. Skip SR/BAT/SDR init (patched to NOPs)
3. Read PVR from XLM_PVR (patched, no mfpvr instruction)
4. Initialize cache size registers from ROM table
5. Set up kernel data structures
6. Jump to 68k emulator entry at 0x460000

### Phase 3: 68k Emulator Boot (PPC code interpreting 68k)

The ROM contains a PPC-native 68k interpreter at 0x460000:
1. Reads 68k opcodes from memory
2. Dispatches through opcode table at 0x480000
3. Executes 68k ROM startup code (same as m68k Mac boot)
4. 68k code encounters EMUL_OP traps → host handles I/O
5. Boot progresses: ROM init → drivers → extensions → Finder

### Phase 4: Steady State

- 68k Mac OS runs via ROM's built-in interpreter
- 60Hz timer fires → interrupt handler delivers VBL
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

1. 68k code executes synthetic opcode (e.g., M68K_EMUL_OP_DISK_OPEN = 0xFE57)
2. ROM's 68k interpreter dispatches to PPC handler at 0x380000 + (selector << 3)
3. PPC handler executes SHEEP opcode (0x18000000 | selector)
4. Unicorn's invalid instruction hook catches it
5. Host reads PPC GPR 8-22 → M68kRegisters struct
6. Calls `EmulOp(selector, &regs)`
7. Writes modified registers back to GPR 8-22
8. Sets XLM_RUN_MODE back to MODE_68K
9. Advances PC past SHEEP opcode
10. Resumes Unicorn execution

### NATIVE_OP Transition (PPC → host)

1. PPC code executes SHEEP opcode with EXEC_NATIVE (lower bits = 2)
2. NATIVE_OP_field (bits 20-25) selects handler
3. FN_field (bit 19) controls return: 0 = continue, 1 = return via LR
4. Host dispatches to native function (VideoVBL, EtherSend, etc.)
5. If FN=1, set PC = LR; else PC += 4

## Interrupt Handling

### 60Hz Timer

Same as m68k: a timer fires at ~60Hz. In the Unicorn block hook:

1. Check if 60Hz interval has elapsed
2. If `XLM_IRQ_NEST == 0` (interrupts enabled):
3. Read `XLM_RUN_MODE` to determine dispatch method

### Dispatch by Mode

**MODE_68K:**
- Write interrupt level to KernelData + 0x67c
- Set condition bits at KernelData + 0x674
- The 68k interpreter's main loop checks this and vectors to interrupt handler

**MODE_NATIVE:**
- More complex: need to interrupt PPC execution
- Call nanokernel interrupt routine:
  - Gossamer (OldWorld): ROMBase + 0x312a3c
  - NewWorld: ROMBase + 0x312b1c
- This activates the 68k interrupt handler from native context

**MODE_EMUL_OP:**
- Check if 68k interrupts enabled (XLM_68K_R25 bit check)
- If enabled, build synthetic 68k interrupt routine:
  ```asm
  move.w  #$0000, -(sp)     ; fake format word
  pea     @return(pc)        ; return address
  move    sr, -(sp)          ; save SR
  move.l  $64, a0            ; vector for level-1 interrupt
  jmp     (a0)               ; dispatch
  @return:
  ```
- Execute via `execute_68k()` within EMUL_OP context
- Save/restore 68k interrupt level (XLM_68K_R25)

### Unicorn Implementation

Since there's no `uc_ppc_trigger_interrupt()`, we simulate exceptions directly:

```c
void ppc_deliver_interrupt(uc_engine *uc) {
    // Save state to SRR0/SRR1 (as hardware would)
    uint64_t pc;
    uint32_t msr;
    uc_reg_read(uc, UC_PPC_REG_PC, &pc);
    uc_reg_read(uc, UC_PPC_REG_MSR, &msr);

    uint32_t srr0 = (uint32_t)pc;
    uint32_t srr1 = msr;
    // Write SRR0/SRR1 (need SPR access or memory-mapped)

    // Set PC to external interrupt vector
    uint64_t vector = 0x500;  // POWERPC_EXCP_EXTERNAL vector address
    uc_reg_write(uc, UC_PPC_REG_PC, &vector);

    // Update MSR: clear EE (External Exception enable), set supervisor
    msr &= ~(1 << 15);  // Clear EE
    msr &= ~(1 << 14);  // Clear PR (set supervisor)
    uc_reg_write(uc, UC_PPC_REG_MSR, &msr);

    uc_emu_stop(uc);  // Stop so next uc_emu_start picks up at vector
}
```

**However**, the nanokernel patches redirect exception handling. The actual interrupt path may not go through vector 0x500 at all — it may use the patched nanokernel entry points. We need to trace SheepShaver's actual interrupt flow to determine the correct approach.

**Simpler approach**: Don't simulate PPC exceptions. Instead, write to the memory-mapped interrupt flags that the 68k interpreter checks in its main loop. The ROM's 68k interpreter already polls for pending interrupts — we just need to set the right flags in kernel data.

## Execute68k (Host → 68k)

Used by EmulOp handlers that need to call 68k Mac toolbox routines:

1. Save all PPC registers
2. Set up 68k register shadow (GPR 8-22 from M68kRegisters)
3. Set GPR 24 = 68k entry address
4. Push EXEC_RETURN sentinel on 68k stack
5. Set PC to ROM's 68k interpreter entry
6. Execute until EXEC_RETURN detected
7. Copy GPR 8-22 back to M68kRegisters
8. Restore saved PPC registers

This is the PPC equivalent of `cpu_execute_68k()` in our m68k platform API.

## Comparison with M68K Execution Model

| Aspect | M68K | PPC |
|--------|------|-----|
| CPU engine | UAE interpreter or Unicorn m68k | Unicorn PPC |
| 68k execution | Direct (native m68k) | Via ROM's built-in PPC→68k interpreter |
| EmulOp encoding | 0x71xx (UAE) or 0xAExx (Unicorn) | 0x18000000+ (SHEEP opcodes) |
| EmulOp detection | A-line exception (intno 10) | Invalid instruction hook |
| Interrupt delivery | Push exception frame, jump to vector | Write to kernel data flags |
| Mode switching | N/A (always 68k) | MODE_68K / MODE_NATIVE / MODE_EMUL_OP |
| Timer mechanism | Block hook polling | Block hook polling (same) |
| Boot ROM | 68k code runs directly | PPC nanokernel → 68k interpreter |

## Performance Considerations

PPC emulation will be slower than m68k for 68k Mac OS because:
1. **Double interpretation**: Unicorn interprets PPC, which interprets 68k
2. **No 68k JIT**: The ROM's 68k interpreter is a table-driven interpreter, not JIT
3. **SHEEP opcode overhead**: Each EmulOp requires Unicorn stop/start cycle

This is acceptable for the initial implementation. Performance optimization paths:
- Unicorn's TCG JIT for PPC code (already enabled)
- The ROM's 68k interpreter is reasonably fast (Apple optimized it for real G3 hardware)
- Most time is spent in PPC native code for later Mac OS versions (8.5+)
