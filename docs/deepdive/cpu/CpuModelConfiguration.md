# CPU Model Configuration

## Why the CPU model matters

The Quadra 650 ROM expects a **68040** and uses 68040 features early on:

1. **CACR (Cache Control Register)**: read with `MOVEC CACR,D0` within the
   first few ROM instructions.
2. **Integrated FPU**: the 68040 has one built in; a 68020 has a separate
   68881/68882.
3. **Instruction set**: the 68040 adds instructions (MOVE16, …).

The older ROMs want older CPUs: the SE's ROM a 68000 with 24-bit
addresses, the IIci's a 68030 with an FPU. Running a ROM on the wrong
model fails in the ROM's own start-up code.

## Where the model comes from

There is no CPU flag. The machine profile (`src/config/machine_profile.cpp`)
is chosen from the ROM's version and checksum when
`CPUContext::init_m68k()` (`src/core/cpu_context.cpp`) loads it, and
carries the model:

| Profile | ROM | `cpu_type` | FPU | Addressing |
|---------|-----|-----------|-----|------------|
| `se` | version `0x0276` | 0 (68000) | no | 24-bit |
| `macii` | version `0x0178` | 2 (68020) | no | 24-bit |
| `iici` | checksum `0x368CADFE` | 3 (68030) | yes | 24-bit |
| `quadra` | anything else (`0x067c`) | 4 (68040) | yes | 32-bit |

The flow:

1. `init_m68k()` copies the profile into the globals `CPUType`,
   `FPUType` and `TwentyFourBitAddressing`, and calls
   `g_platform.cpu_set_type(cpu_type, fpu_type)`.
2. `cpu_init` (`m68k_cpu_init()` in `src/cpu/cpu_m68k.cpp`) builds a
   `cpu::Config`: `model = 68000 + 10 * CPUType` (4 → 68040), `fpu`,
   `address_bits` = 24 or 32, `jit`. It creates the core named by `--core`
   with it and logs `[CPU] <core> core, 68040 + FPU, 32-bit`.
3. The core configures itself from the `Config`:
   - **uae** (`src/cpu/core/uae_core.cpp`) sets `CPUType = model / 10 % 10`
     and `FPUType`, then `build_cpufunctbl()` (`src/cpu/uae_cpu/newcpu.cpp`)
     picks the opcode table by CPU level (see `UaeQuirks.md`); 24-bit
     machines set `address_reg_mask = 0x00FFFFFF`.
   - **mame-68k** (`src/cpu/mame/mame_68k_core.cpp`) sets MAME's Musashi
     up as a 68000, 68020, 68030 or 68040 (`init_cpu_m680x0()`); the PMMU
     only if `Config::mmu` (the ROM machine leaves it off).

`--model-id` is a different thing: the Gestalt machine type the Mac is told
(the profile's `model_id` by default; 5 = IIci for Systems before 7.5). It
does not change the CPU.

## CPU Model Reference

UAE `CPUType` values: `0` 68000, `1` 68010, `2` 68020, `3` 68030,
`4` 68040. `cpu::Config::model` takes the full number (68000 … 68040).

## Related Files

- `src/config/machine_profile.cpp` — profiles and `set_machine_profile()`.
- `src/core/cpu_context.cpp` — `init_m68k()`.
- `src/cpu/cpu_m68k.cpp` — `m68k_cpu_init()`: profile → `cpu::Config`.
- `src/cpu/core/uae_core.cpp`, `src/cpu/uae_cpu/newcpu.cpp` — UAE's level.
- `src/cpu/mame/mame_68k_core.cpp` — MAME's Musashi.
