# CPU core and backend API

Two layers. The **Platform CPU entries** (`src/common/include/platform.h`)
are what the rest of the emulator calls. Behind them, every core except
Kheperix implements **`cpu::Core`** (`src/cpu/core/cpu_core.h`), and a
machine file turns a `Core` into Platform entries.

## cpu::Core

```c++
namespace cpu {
class GuestMemory;   /* 32-bit guest space, 64 KB pages → host memory or Io */
class Host {         /* what the core calls back */
    virtual OpResult host_op(Core &, uint32_t opcode, uint32_t op_pc) = 0;
    virtual bool illegal(Core &, uint32_t opcode, uint32_t op_pc);  /* mame-68k */
    virtual void attention(Core &);   /* after request_attention() */
    virtual bool trap(Core &, uint32_t opcode, uint32_t op_pc);     /* A/F-line, mame-68k */
};
class Core {
    virtual void reset() = 0;
    virtual StopReason run() = 0;          /* HostOp, Requested, Halted */
    virtual void step() = 0;
    virtual void request_stop() = 0;       /* any thread */
    virtual void request_attention() = 0;  /* any thread */
    virtual void set_irq(int level) = 0;   /* 68k 0..6 level, 7 NMI edge; ppc on/off */
    virtual uint32_t pc() const = 0;       /* + set_pc, reg, set_reg, fpr */
    virtual size_t context_size() const = 0;   /* + save/restore_context */
    virtual void invalidate(uint32_t addr, uint32_t len);   /* JIT caches */
    void run_until(uint32_t addr, uint32_t return_pc);
};
std::vector<std::string> available(Arch);
std::unique_ptr<Core> create(const std::string &name, const Config &,
                             GuestMemory &, Host &);
}
```

`Config` carries the architecture, model (68000–68040; 601–7400), FPU,
PMMU (`mame-68k` only), address bits (24 or 32), JIT, clock and a few
PPC switches. Host ops are `$71xx` on 68k and primary opcode 6 on PPC; by
convention `$7100` means "return to host", and `run()` may be re-entered
from inside a host op (host code calling guest code).

| Core | File | Notes |
|------|------|-------|
| `uae` | `src/cpu/core/uae_core.cpp` over `src/cpu/uae_cpu/` | Needs one uniform host offset (`MEMBaseDiff`); JIT on x86 only, outermost `run()` only |
| `mame-68k` | `src/cpu/mame/mame_68k_core.cpp` | MAME's Musashi: PMMU, softfloat FPU, several instances per process; `musashi` is an alias |
| `mame-ppc` | `src/cpu/mame/mame_ppc_core.cpp` | MAME's PowerPC recompiler: x86-64 and arm64 back-ends with `--jit`, UML interpreter without |

Kheperix (`--core kpx`) is driven directly by `src/cpu/kpx/cpu_ppc_kpx.cpp`
through SheepShaver's `sheepshaver_cpu`.

## Platform CPU entries

```c
const char *cpu_name;
bool (*cpu_init)(void);
void (*cpu_reset)(void);
void (*cpu_set_type)(int cpu_type, int fpu_type);   /* before cpu_init */
int  (*cpu_execute_one)(void);
void (*cpu_execute_fast)(void);       /* run until cpu_request_stop */
void (*cpu_request_stop)(void);       /* any thread */
uint32_t (*cpu_get_pc)(void);         /* + cpu_get_sr, cpu_get_dreg, cpu_get_areg */
void (*cpu_trigger_interrupt)(int level);
void (*invoke_debug)(void);
void (*cpu_execute_68k_trap)(uint16_t trap, struct M68kRegisters *r);
void (*cpu_execute_68k)(uint32_t addr, struct M68kRegisters *r);
void (*flush_code_cache)(void);
/* mem_read_*, mem_write_*, mem_mac_to_host, mem_host_to_mac */
/* PPC only: cpu_get_gpr/set_gpr/cr/lr/ctr, cpu_execute_ppc,
   ppc_execute_macos_code, ppc_emulop_handler, ppc_native_op, … */
```

Who fills them:

| Machine | Installer | Notes |
|---------|-----------|-------|
| 68k ROM machine | `cpu_m68k_install(p, core, jit)` (`src/cpu/cpu_m68k.cpp`) | `cpu::platform_install()` (`src/cpu/core/platform_cpu.cpp`) supplies memory, registers, `Execute68k`, interrupt and cache entries for any `Core`; `cpu_m68k.cpp` adds init, reset (PC = ROM + `$2A`, SSP = `$2000`), the run loop and the NMI |
| PPC on Kheperix | `cpu_ppc_kpx_install()` | |
| PPC on mame-ppc | `cpu_ppc_mame_install()` (`src/cpu/kpx/cpu_ppc_mame.cpp`) | its own entries on a `mame-ppc` `Core` |
| Executor | `src/executor/cpu/syn68k_common.cpp` | a syn68k API facade on a `Core`; `platform_cpu.cpp` for the memory entries |

Which installer runs: `src/main.cpp` calls `cpu_m68k_install()` for
`--backend uae`; `CPUContext::init_ppc()` (`src/core/cpu_context.cpp`)
picks `cpu_ppc_mame_install()` when `--core mame-ppc`, else
`cpu_ppc_kpx_install()`; `--backend executor` goes to
`executor_child_main()` / `executor_direct_main()`.

## Execution loop (src/main.cpp)

```c++
if (platform->cpu_execute_fast) {
    platform->cpu_execute_fast();     /* every installer provides it */
} else {
    while (true)
        platform->cpu_execute_one();
}
```

On the 68k machine `cpu_execute_fast()` is
`while (core->run() != StopReason::Requested) ;` and `cpu_execute_one()` is
`core->step()` (it always returns 0).

## Adding a core

Implement `cpu::Core` for the architecture, add its factory to
`cpu::create()` / `available()` in `src/cpu/core/registry.cpp`, and accept
its name in `EmulatorConfig::normalize_cpu_core()`
(`src/config/emulator_config.cpp`). A 68k core then runs the ROM machine
without changes to `cpu_m68k.cpp`; a PPC core needs `init_ppc()` to choose
an installer for it. Tests: `ctest -R cpu_cores`
(`tests/test_cpu_cores.cpp`).
