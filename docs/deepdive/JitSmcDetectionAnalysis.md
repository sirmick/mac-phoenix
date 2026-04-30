# JIT SMC detection in the Unicorn fork

What's wired and what's stubbed in Unicorn's QEMU fork around
self-modifying-code detection, what that means for us, and where the
safety nets are.

## Upstream QEMU's SMC machinery

```
tb_gen_code(page P)
  → tlb_reset_dirty_by_vaddr(P)             // sets TLB_NOTDIRTY on the entry
  → cpu_physical_memory_is_clean(P)         // checked, kept "clean" while compiled

guest write to page P (with TLB_NOTDIRTY set)
  → store_helper()
  → notdirty_write()
    → tb_invalidate_phys_page_fast(P)       // kills stale TBs
    → cpu_physical_memory_set_dirty_flag(P) // marks page dirty
    → tlb_set_dirty()                       // future writes go fast-path

uc_mem_write() / flatview_write_continue() to page P
  → memcpy
  → invalidate_and_set_dirty()
    → if is_clean(P), tb_invalidate_phys_range()
```

## What's stubbed in Unicorn's fork

`subprojects/unicorn/qemu/include/exec/ram_addr.h` — every dirty-bitmap
helper is a no-op or returns a hardcoded value:

```c
static inline bool cpu_physical_memory_is_clean(ram_addr_t)         { return true;  }
static inline void cpu_physical_memory_set_dirty_flag(...)          { /* nothing */ }
static inline void cpu_physical_memory_set_dirty_range(...)         { /* nothing */ }
static inline bool cpu_physical_memory_get_dirty(...)               { return false; }
```

`subprojects/unicorn/qemu/exec.c` — `invalidate_and_set_dirty()` is
empty; `flatview_write_continue()` does a raw memcpy with no
post-write invalidation; `cpu_physical_memory_test_and_clear_dirty`
hardcodes `false`.

`subprojects/unicorn/qemu/accel/tcg/translate-all.c` —
`page_collection_lock()` is `#if 0`'d out, but that's harmless on m68k
(`TARGET_HAS_PRECISE_SMC` isn't defined for m68k, only x86; the fast
path doesn't need the lock).

## What still works

Because `is_clean()` always returns true, every RAM page gets
`TLB_NOTDIRTY` set on first TLB fill. That means **every guest store**
goes through `store_helper` → `notdirty_write` → which **is wired
correctly**, including the `mr->perms & UC_PROT_EXEC` check that gates
TB invalidation. Guest m68k stores into executable pages *do* invalidate
stale TBs.

Cost: every RAM page stays in the slow path forever, because
`set_dirty_flag()` is a no-op so `notdirty_write` never transitions a
page to fast writes. That's a permanent perf tax — see
`UnicornPerformanceAnalysis.md`. Restoring `set_dirty_flag()` is the
top open lever.

## Three write paths, three answers

| Write path | TB invalidation | mac-phoenix usage |
|------------|-----------------|-------------------|
| Guest m68k stores (`MOVE`, `CLR`, …) | partial via `notdirty_write` → `tb_invalidate_phys_page_fast` — works | Mac OS heap manager overwriting EmulOp patches |
| `uc_mem_write()` API | broken — empty `invalidate_and_set_dirty`, raw memcpy | ROM patching at startup; not a problem since JIT hasn't run yet |
| Host pointer writes via `uc_mem_map_ptr` | invisible to QEMU | BasiliskII's `put_long()` etc. — these can produce stale TBs |

The third path is the loose end — when an EmulOp handler writes to RAM
via host pointer arithmetic, QEMU sees nothing. Most of the time the
written address isn't a code page; when it is, the STALE-TB detector
catches it.

## STALE-TB safety net

`hook_block()` validates expected EmulOp opcodes at execution time. If
a block's first instruction has been overwritten since compile, the
detector forces a **targeted** `uc_ctl_flush_tb()` on that block before
it runs.

Numbers from a 30 s boot:

- 0 blanket TB-cache flushes (we removed the earlier 60 Hz
  `uc_ctl_flush_tb()` workaround once the `notdirty_write` path was
  confirmed to handle most of the SMC).
- ~18 targeted STALE-TB flushes, all in the `0x0001ca…0x0001ce` system
  heap range. 100× fewer flushes than the old workaround.

The detector is **production code**. Don't strip it when cleaning up
`hook_block`. Final boot state with and without it is identical
(`$0b78 = 0xfd89ffff` either way), but the detector keeps that true
under workloads we haven't characterised.

## What it would take to fix properly

Restore the dirty bitmap. Re-implement the four `ram_addr.h` helpers
against an actual per-`RAMBlock` bitmap, hook `tb_invalidate_phys_page_fast`
on dirty→clean transitions, populate it via `tlb_reset_dirty_by_vaddr`
on TB compilation. Then drop the no-op stubs, drop the STALE-TB safety
net, and the slow-path tax on every RAM write goes away too. Big change
in deep QEMU infrastructure; not done.

## Files

- `subprojects/unicorn/qemu/include/exec/ram_addr.h:75` — the smoking gun.
- `subprojects/unicorn/qemu/exec.c` — empty `invalidate_and_set_dirty`,
  raw `memcpy` in `flatview_write_continue`.
- `subprojects/unicorn/qemu/accel/tcg/cputlb.c:877` — where `TLB_NOTDIRTY`
  gets set; `:1189` is `notdirty_write`; `:2362` is `store_helper`.
- `subprojects/unicorn/qemu/accel/tcg/translate-all.c:2019` —
  `tb_invalidate_phys_page_fast` (working).
- `src/cpu/unicorn_wrapper.c` — `hook_block` STALE-TB detector and
  targeted-flush call.
