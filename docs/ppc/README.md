# PPC Emulation

Adding PowerPC Mac emulation to mac-phoenix using the **KPX (Kheperix)** interpreter,
targeting OldWorld 4MB ROMs (Gossamer / Beige G3).

## Documents

| Document | Contents |
|----------|----------|
| [Implementation Guide](implementation_guide.md) | **Start here.** Phased porting plan with copy/paste instructions from legacy SheepShaver |
| [Architecture](architecture.md) | How PPC fits into mac-phoenix's abstractions (Platform API, config, memory) |
| [ROM Patching](rom_patching.md) | Nanokernel patches, EmulOp mechanism, Gossamer ROM details |
| [Memory Layout](memory_layout.md) | PPC Mac memory map, kernel data, XLM globals |
| [Execution Model](execution_model.md) | Boot sequence, mode switching, interrupt handling |
| [Unicorn PPC](unicorn_ppc.md) | Unicorn engine PPC API reference (for future Unicorn PPC backend) |

## Target

- **ROM**: OldWorld 4MB (Gossamer / Beige Power Macintosh G3)
- **CPU**: PowerPC 750 (G3)
- **OS**: Mac OS 8.1 - 9.2.2
- **Backend**: KPX (Kheperix interpreter). Unicorn PPC and DualCPU-PPC are future work.

## Reference Code

- `legacy/SheepShaver/src/kpx_cpu/sheepshaver_glue.cpp` -- CPU bridge (1252 lines, primary reference)
- `legacy/SheepShaver/src/rom_patches.cpp` -- ROM patching (2483 lines)
- `legacy/SheepShaver/src/emul_op.cpp` -- EmulOp dispatch
- `legacy/SheepShaver/src/macos_util.cpp` -- FindLibSymbol, CallUniversalProc
- `legacy/SheepShaver/src/name_registry.cpp` -- Name Registry patching
- `legacy/SheepShaver/src/video.cpp` -- VideoDoDriverIO (PPC video driver)
- `legacy/SheepShaver/src/include/thunks.h` -- Native op selectors and SheepMem
- `legacy/SheepShaver/src/include/xlowmem.h` -- Kernel data layout
