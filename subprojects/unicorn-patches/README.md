# Unicorn patches

Local patches to [unicorn-engine/unicorn](https://github.com/unicorn-engine/unicorn)
(tag `2.1.4`) that the mac-phoenix backends depend on. Kept here so the
vendored `subprojects/unicorn/` tree (1) stays close to upstream and
(2) has a clear diff when we submit these upstream.

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

## Verification

The full series applies cleanly to a pristine `unicorn-engine/unicorn`
checkout at tag `2.1.4`:

```bash
git clone https://github.com/unicorn-engine/unicorn.git /tmp/u && cd /tmp/u
git checkout 2.1.4
git am /path/to/mac-phoenix/subprojects/unicorn-patches/*.patch
```

After `git am`, `diff -rq /tmp/u subprojects/unicorn/` reports only
docs (`docs/Unicorn_Engine_Documentation/*`) and one `README.md`
line — no source divergence.

## Patch set

Apply in numeric order. Later patches touch files the earlier patches
introduce or extend.

| # | Title | Files touched | Purpose |
|---|-------|---------------|---------|
| 0000 | m68k: implement RTR instruction | `qemu/target/m68k/translate.c` | Adds decoder for opcode `0x4E77` (Return and Restore). Classic Mac OS needs it. Standalone — can be applied in isolation. |
| 0001 | m68k: MacPhoenix host-integration patches | `qemu/accel/tcg/cpu-exec.c`, `qemu/accel/tcg/translate-all.c`, `qemu/target/m68k/cpu.h`, `qemu/target/m68k/op_helper.c`, `qemu/target/m68k/translate.c`, `qemu/target/m68k/unicorn.c` | Bundle of 68k deltas that predated the numbered series. A-line pre-read + EXCP_RTE fast-return in cpu-exec; non-static `m68k_interrupt_all` with auto-ack of HW IRQs; `uc_m68k_trigger_interrupt` helper + cc_op preservation on SR read; looser `use_goto_tb` (interrupt delivery via `cpu_exit()` instead of gating chained jumps); perf counters for the MacPhoenix boot progress + TB metrics. |
| 0002 | PPC: scaffold backend alongside KPX | `qemu/accel/tcg/cpu-exec.c`, `qemu/target/ppc/unicorn.c` | Promotes 0001's perf-counter globals to `__attribute__((weak))` so `m68k-softmmu.a` + `ppc-softmmu.a` can coexist in one link. Restore TB-flush on host-side MSR IR/DR flips. |
| 0003 | PPC: map memory, open engine, run 1000 insns at nanokernel entry | `uc.c` | Drop the `NULL`-pointer guard in `uc_mem_map_ptr` — `REAL_ADDRESSING` guests legitimately mmap guest RAM at host address 0 (MAP_FIXED with `vm.mmap_min_addr=0`). |
| 0004 | PPC: theoretically complete backend + first smoke test | `CMakeLists.txt`, `include/uc_priv.h`, `qemu/target/ppc/helper.h`, `qemu/target/ppc/mac_emulop_helper.c` (NEW), `qemu/target/ppc/translate.c`, `uc.c` | Major-opcode-6 `helper_mac_emulop` wired through translate.c so the guest can trap into host via a single opcode. Minimal C helper in `mac_emulop_helper.c`. Public hook plumbing. |
| 0005 | support REAL_ADDRESSING RAM at host 0 + register PPC EmulOp | `qemu/exec.c`, `qemu/include/exec/ram_addr.h`, `qemu/softmmu/memory.c`, `qemu/target/ppc/translate_init.inc.c` | `ram_block_add` / `qemu_ram_alloc_from_ptr` used `host != NULL` to decide whether a RAM block is preallocated. That rejected host-0 mappings. Switch to an explicit `RAM_PREALLOC` flag. Also register the `mac_emulop` opcode unconditionally in `create_ppc_opcodes` (was being filtered out by the feature gate). |
| 0006 | PPC: fix `qemu_ram_block_from_host` for host=NULL prealloc blocks | `qemu/exec.c` | Reverse-lookup path used `block->host` truthiness as an "is mapped" sentinel. Same host-0 problem as 0005 but on the lookup side — distinguish by `RAM_PREALLOC`. Without this, `qemu_ram_addr_from_host_nofail → abort()` fires as soon as TCG translates code at a low RAM address. |
| 0007 | PPC: clear `stop_request` on nested `uc_emu_start` return | `uc.c` | Nested `uc_emu_start` (needed for Execute68k / `execute_macos_code` reentrancy) left `stop_request` and `cpu->exit_request` asserted after the inner run. Outer frame then spuriously broke out at a non-EMUL_OP instruction. Call `revert_uc_emu_stop` on the nested return path. |
| 0008 | m68k: tighten hot paths — 25% boot-time reduction | `include/uc_priv.h`, `qemu/softmmu/memory.c`, `qemu/unicorn_common.h` | Add 4-way page-keyed LRU to `find_memory_mapping`. Softmmu hammers this on every TLB miss / notdirty write / unmapped probe (~8% of PPC wall time on the flame graph). Round-robin replacement, NULL results not cached, invalidated wholesale in memory_map / memory_unmap / memory_map_io / memory_cow / memory_moveout / memory_movein. Companion to non-Unicorn commit `1d0eb4f6` which also gates per-block `perf_now_ns()` behind `MACEMU_DEBUG_PERF` and refactors the SCSI probe accelerator. |
| 0009 | cache MR pointer in `CPUTLBEntry` (skip per-access `memory_mapping`) | `qemu/accel/tcg/cputlb.c`, `qemu/include/exec/cpu-defs.h` | Per-TLB-entry MR cache filled lazily on first miss and reused for every subsequent access to the same page. 5 call sites rewritten (notdirty_write, load_helper main + retry, store_helper main + retry). Invalidated wholesale via `tlb_flush()`, which `memory_map`/`memory_unmap` already trigger. Two-layer cache (this + 0008's 4-way LRU) delivered 5/10 Desktop success rate at ~2.81s vs prior ~14s. |

Total: ~31 KB of diff across 16 files. Of ten patches, seven are
bug-fixes / instruction-coverage / perf improvements useful to any
Unicorn user; 0001 and 0004 are the mac-phoenix-specific feature
additions (A-line pre-read / `mac_emulop` hook point).

## Upstream status

**Not yet submitted.** Target: `unicorn-engine/unicorn` main branch
(the 2.x line — our fork is based off the same Unicorn 2.x snapshot).

- **0000** is standalone and probably trivially acceptable.
- **0003, 0005, 0006** together form a coherent "host-NULL RAM mappings"
  bug report and are the cleanest candidates for first PPC submission.
- **0007** is a standalone nested-run fix.
- **0008, 0009** are generic softmmu perf improvements (page-keyed LRU
  + per-TLB-entry MR cache); should be acceptable after splitting 0008
  to drop MacPhoenix-specific pieces.
- **0001, 0002, 0004** are mac-phoenix-flavored and would need
  reshaping to land upstream independently.
