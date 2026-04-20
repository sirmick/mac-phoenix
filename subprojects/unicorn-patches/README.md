# Unicorn PPC patches

Local patches to [unicorn-engine/unicorn](https://github.com/unicorn-engine/unicorn)
that the mac-phoenix PPC backend depends on. Kept here so the vendored
`subprojects/unicorn/` tree (1) stays close to upstream and (2) has a
clear diff when we submit these upstream.

## How this relates to the vendored tree

`subprojects/unicorn/` is vendored (checked in at the file level — no
submodule ref). Historically, fixes landed as edits directly in the
vendored tree; starting with the PPC port they're captured here as
well, generated via:

```bash
git format-patch <base>..<tip> \
  --relative=subprojects/unicorn/ \
  --output-directory subprojects/unicorn-patches/ \
  -- subprojects/unicorn/
```

The `--relative` flag strips the `subprojects/unicorn/` prefix so paths
in the diff headers are repo-root-relative from Unicorn's perspective
(`a/qemu/exec.c`, not `a/subprojects/unicorn/qemu/exec.c`) — ready to
`git am` directly on an unicorn checkout.

## Patch set

Apply in numeric order. Later patches touch files the earlier patches
introduce or extend. 0000 is independent (m68k target) and can be
applied in isolation.

| # | Title | Files touched | Purpose |
|---|-------|---------------|---------|
| 0000 | m68k: implement RTR instruction | `qemu/target/m68k/translate.c` | Adds decoder for opcode `0x4E77` (Return and Restore). Classic Mac OS needs it. Predates the numbered series — reconstructed from the vendored tree's current state so it's `git am`-ready. |
| 0001 | scaffold backend alongside KPX | `qemu/accel/tcg/cpu-exec.c`, `qemu/target/ppc/unicorn.c` | Weak perf-counter globals so m68k + ppc libs coexist; restore TB-flush on host-side MSR IR/DR flips. |
| 0002 | map memory, open engine, run 1000 insns at nanokernel entry | `uc.c` | Drop the `NULL`-pointer guard in `uc_mem_map_ptr` — `REAL_ADDRESSING` guests legitimately mmap guest RAM at host address 0 (MAP_FIXED with `vm.mmap_min_addr=0`). |
| 0003 | theoretically complete backend + first smoke test | `CMakeLists.txt`, `include/uc_priv.h`, `qemu/target/ppc/helper.h`, `qemu/target/ppc/mac_emulop_helper.c` (NEW), `qemu/target/ppc/translate.c`, `uc.c` | Major-opcode-6 `helper_mac_emulop` wired through translate.c so the guest can trap into host via a single opcode. Minimal C helper in `mac_emulop_helper.c`. Public hook plumbing. |
| 0004 | support REAL_ADDRESSING RAM at host 0 + register PPC EmulOp | `qemu/exec.c`, `qemu/include/exec/ram_addr.h`, `qemu/softmmu/memory.c`, `qemu/target/ppc/translate_init.inc.c` | `ram_block_add` / `qemu_ram_alloc_from_ptr` used `host != NULL` to decide whether a RAM block is preallocated. That rejected host-0 mappings. Switch to an explicit `RAM_PREALLOC` flag. Also register the `mac_emulop` opcode unconditionally in `create_ppc_opcodes` (was being filtered out by the feature gate). |
| 0005 | fix `qemu_ram_block_from_host` for host=NULL prealloc blocks | `qemu/exec.c` | Reverse-lookup path used `block->host` truthiness as an "is mapped" sentinel. Same host-0 problem as 0004 but on the lookup side — distinguish by `RAM_PREALLOC`. Without this, `qemu_ram_addr_from_host_nofail → abort()` fires as soon as TCG translates code at a low RAM address. |
| 0006 | clear `stop_request` on nested `uc_emu_start` return | `uc.c` | Nested `uc_emu_start` (needed for Execute68k / `execute_macos_code` reentrancy) left `stop_request` and `cpu->exit_request` asserted after the inner run. Outer frame then spuriously broke out at a non-EMUL_OP instruction. Call `revert_uc_emu_stop` on the nested return path. |

Total: ~25 KB of diff across 13 files. Six of seven patches are
bug-fixes / instruction-coverage useful to any Unicorn user; 0003 is
the only mac-phoenix-specific feature addition (the `mac_emulop` hook
point).

## Upstream status

**Not yet submitted.** Target: `unicorn-engine/unicorn` main branch
(the 2.x line — our fork is based off the same Unicorn 2.x snapshot).
0000 is standalone and probably trivially acceptable. 0002, 0004, 0005
together form a coherent "host-NULL RAM mappings" bug report and are
the cleanest candidates for first PPC submission. 0006 is a standalone
nested-run fix. 0001, 0003 are mac-phoenix-flavored and would need
reshaping to land upstream independently.
