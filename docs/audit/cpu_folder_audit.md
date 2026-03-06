# CPU Folder Audit

Audit of `src/cpu/` — dead files, obsolete code, unused APIs.

**Policy**: Minimize changes to `uae_cpu/` (upstream UAE code). Keep JIT compiler code for future use.

## Dead Files (delete)

### 1. `cpu_backend.h` + `cpu_backend.cpp` — DEAD CODE
The `CPUBackend` struct and `cpu_backend_select()`/`cpu_backend_get()` API is **completely unused**. Nothing includes `cpu_backend.h` except `cpu_backend.cpp` itself. The actual backend selection uses the Platform API (`cpu_uae_install`/`cpu_unicorn_install`/`cpu_dualcpu_install` called from `emulator_init.cpp`). This was an earlier abstraction that got superseded. Also contains a **duplicate** UAE backend implementation (separate from the real one in `cpu_uae.c`).

- **Action**: Delete both files, remove from `meson.build`

### 2. `uae_stubs.c` — DEAD CODE
Weak-symbol stubs for `uae_cpu_init`, `uae_get_dreg`, etc. Not compiled (not in any `meson.build`). The real implementations live in `uae_wrapper.cpp`. This was scaffolding from early development.

- **Action**: Delete

### 3. `m68k_interrupt.c` — DEAD CODE
Contains `deliver_m68k_interrupt()`, `deliver_timer_interrupt()`, `deliver_autovector_interrupt()`, `check_and_deliver_interrupts()`, `handle_rte()`, and `build_exception_frame()`. **None of these functions are called from anywhere outside this file.** The actual interrupt delivery uses `uc_m68k_trigger_interrupt()` in `unicorn_wrapper.c:hook_block()` and UAE's native interrupt handling. This was an early manual interrupt implementation replaced by QEMU's native mechanism.

- **Action**: Delete, remove from `meson.build`

### 4. `uae_cpu/cpuemu.cpp.old` + `uae_cpu/cpustbl.cpp.old` — BACKUP FILES
Old copies of generated files. Not referenced anywhere, not compiled.

- **Action**: Delete both

## Minor Issues in Live Files

### 6. `unicorn_wrapper.c` — redundant forwarding layer
`unicorn_mem_write()` / `unicorn_mem_read()` are one-line forwarders to `unicorn_write_memory()` / `unicorn_read_memory()`. Both names exist in the same file. Confusing but harmless.

- **Action**: Low priority. Could inline later.

### 7. `regstruct` forward-declared in multiple files
Partial copies of the UAE `regstruct` in:
- `cpu_backend.cpp:28` (dead file — goes away with #1)
- `cpu_uae.c:21`
- `cpu_dualcpu.c:28`

Fragile if the real struct in `newcpu.h` changes — these copies silently diverge.

- **Action**: Note for later. Fixing requires touching UAE headers (out of scope).

## Summary

| File | Status | Action |
|------|--------|--------|
| `cpu_backend.h` | Dead | **Delete** |
| `cpu_backend.cpp` | Dead | **Delete** |
| `uae_stubs.c` | Dead, not compiled | **Delete** |
| `m68k_interrupt.c` | All exported functions uncalled | **Delete** |
| `uae_cpu/cpuemu_ff_aliases.cpp` | Not compiled, kept (UAE code) | Keep |
| `uae_cpu/cpuemu.cpp.old` | Backup | **Delete** |
| `uae_cpu/cpustbl.cpp.old` | Backup | **Delete** |
| `uae_cpu/compiler/` | JIT disabled but planned | Keep |
| `uae_cpu/fpu/fpu_x86.*` | Not compiled | Keep (UAE code) |
| `uae_cpu/fpu/fpu_uae.*` | Not compiled | Keep (UAE code) |
| `uae_cpu/cpuopti.c` | Not compiled | Keep (UAE code) |
| `uae_cpu/build68k.c`, `gencpu.c` | Code generators | Keep (UAE code) |
| `uae_cpu/build68k`, `gencpu` (binaries) | Generated | Keep (UAE code) |

---

# Common Folder Audit

Audit of `src/common/` — dead files, unused headers.

**Policy**: Minimize changes to legacy BasiliskII code (sigsegv.cpp platform ifdefs, etc.).

## Dead Files (deleted)

### 1. Legacy duplicate `.cpp` files — NOT IN BUILD
Five `.cpp` files in `src/common/` are duplicates of files that live in `src/core/` and are compiled from there. The `src/common/` copies are not in any `meson.build`.

| Dead file | Active copy |
|-----------|-------------|
| `common/user_strings.cpp` | `core/user_strings.cpp` |
| `common/macos_util.cpp` | `core/macos_util.cpp` |
| `common/prefs.cpp` | `core/prefs.cpp` |
| `common/prefs_items.cpp` | `core/prefs_items.cpp` |
| `common/rom_patches.cpp` | `core/rom_patches.cpp` |

### 2. Dead headers — never included

| Header | Why dead |
|--------|----------|
| `include/pict.h` | Never included, `ConvertRGBAToPICT` never referenced |
| `include/platform_memory.h` | Never included, superseded by `memory_access.h` |
| `include/color_scheme.h` | GTK/D-Bus only, GTK disabled, never included |

## Kept (live or needed)

| File | Status |
|------|--------|
| `crash_handler_init.cpp` | LIVE — compiled from `core/meson.build` |
| `platform.cpp` | LIVE — compiled from `core/meson.build` |
| `sigsegv.cpp` | LIVE — has dead platform ifdefs (HP-UX, Solaris, AIX, etc.) but left alone (legacy code) |
| `include/prefs_editor.h` | Included by `null_drivers.cpp` (stub) — keep |
| All other `include/*.h` | LIVE — actively included |
