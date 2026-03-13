# PPC Emulation Plan

Adding PowerPC Mac emulation as a first-class citizen alongside the existing m68k support, using both **Unicorn PPC** and **KPX (Kheperix)** CPU backends, with a DualCPU-PPC validation mode.

## Documents

| Document | Contents |
|----------|----------|
| [Architecture](architecture.md) | How PPC fits into mac-phoenix's existing abstractions |
| [Unicorn PPC](unicorn_ppc.md) | Unicorn engine PPC API, registers, CPU models, hooks |
| [ROM Patching](rom_patching.md) | Nanokernel patches, EmulOp mechanism, Gossamer ROM |
| [Memory Layout](memory_layout.md) | PPC Mac memory map, kernel data, low-memory globals |
| [Execution Model](execution_model.md) | Boot sequence, mode switching, interrupt handling |
| [Implementation Plan](implementation_plan.md) | Phased work breakdown: KPX, Unicorn PPC, DualCPU-PPC, name collisions, testing |

## Target

- **ROM**: Gossamer (Beige Power Macintosh G3)
- **CPU**: PowerPC 750 (G3)
- **OS**: Mac OS 8.1 – 9.2.2
- **Backends**: Unicorn PPC (QEMU JIT), KPX (Kheperix JIT), DualCPU-PPC (validation)

## Reference Code

- `legacy/SheepShaver/src/rom_patches.cpp` — ROM patching (2483 lines)
- `legacy/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp` — CPU bridge (1252 lines)
- `legacy/SheepShaver/src/emul_op.cpp` — EmulOp dispatch
- `legacy/SheepShaver/src/include/xlowmem.h` — Kernel data layout
- `legacy/SheepShaver/src/include/cpu_emulation.h` — Memory constants
