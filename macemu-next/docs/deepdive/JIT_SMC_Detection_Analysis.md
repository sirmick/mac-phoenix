# JIT Self-Modifying Code Detection: What Unicorn Broke

## Executive Summary

Unicorn's QEMU fork has **systematically gutted** the dirty memory bitmap system that QEMU uses to detect self-modifying code (SMC). This is why we need the `uc_ctl_flush_tb()` 60x/second workaround. This document explains exactly what was removed, what still partially works, and the options for fixing it.

## The Problem

Mac OS writes over RAM containing our EmulOp patch code during heap allocation. QEMU's JIT retains compiled translations for the old code. Executing stale translation blocks (TBs) crashes at `PC=0x00000002`.

Our current workaround: flush the **entire** JIT cache 60 times per second via `uc_ctl_flush_tb()` in the timer interrupt. This works but defeats much of the JIT's benefit.

## How QEMU's SMC Detection Works (Upstream)

QEMU has a multi-layer system for detecting when guest code writes over previously-compiled code:

### Layer 1: Dirty Memory Bitmap

QEMU maintains a per-page dirty bitmap (`ram_list.dirty_memory[DIRTY_MEMORY_CODE]`). Each bit tracks whether a page containing compiled code has been written to since it was last compiled.

### Layer 2: TLB_NOTDIRTY Flag

When `tb_gen_code()` compiles a new translation block:

1. It calls `tlb_reset_dirty_by_vaddr()` (`cputlb.c:644-660`)
2. This sets `TLB_NOTDIRTY` on the TLB entry's `addr_write` for that page
3. Future guest stores to that page go through the slow path

### Layer 3: store_helper / notdirty_write

When guest code writes to a page with `TLB_NOTDIRTY` set:

1. `store_helper()` (`cputlb.c:2362`) checks: `if (tlb_addr & TLB_NOTDIRTY)`
2. Calls `notdirty_write()` (`cputlb.c:1189`)
3. `notdirty_write()` calls `tb_invalidate_phys_page_fast()` (`translate-all.c:2019`)
4. This looks up the `PageDesc` for the written page and invalidates affected TBs
5. `cpu_physical_memory_set_dirty_flag()` marks the page dirty
6. `tlb_set_dirty()` removes `TLB_NOTDIRTY` so future writes go fast

### Layer 4: invalidate_and_set_dirty (API writes)

When the host writes to guest memory via `uc_mem_write()` / `flatview_write_continue()`:

1. After the `memcpy()`, calls `invalidate_and_set_dirty()`
2. This checks if the page is clean (contains compiled code)
3. If so, calls `tb_invalidate_phys_range()` to invalidate affected TBs

### The Full Cycle

```
tb_gen_code() compiles code at page P
  → tlb_reset_dirty_by_vaddr(P) sets TLB_NOTDIRTY
  → cpu_physical_memory_is_clean(P) checked → true → flag stays

Guest writes to page P:
  → store_helper() sees TLB_NOTDIRTY
  → notdirty_write()
    → tb_invalidate_phys_page_fast() — INVALIDATES STALE TBs
    → cpu_physical_memory_set_dirty_flag(P) — marks page dirty
    → tlb_set_dirty() — removes TLB_NOTDIRTY for fast future writes

Next time tb_gen_code() compiles at page P:
  → Cycle repeats
```

## What Unicorn Gutted

### ram_addr.h — Every dirty bitmap function stubbed

File: `subprojects/unicorn/qemu/include/exec/ram_addr.h`

```c
// ALWAYS returns "clean" — every page looks like it has compiled code
static inline bool cpu_physical_memory_is_clean(ram_addr_t addr)
{
    return true;   // upstream: checks dirty bitmap
}

// NO-OP — dirty flag never gets set
static inline void cpu_physical_memory_set_dirty_flag(ram_addr_t addr,
                                                      unsigned client)
{
}   // upstream: sets bit in dirty_memory[client]

// NO-OP — dirty range never gets set
static inline void cpu_physical_memory_set_dirty_range(ram_addr_t start,
                                                       ram_addr_t length,
                                                       uint8_t mask)
{
}   // upstream: sets range of bits

// ALWAYS returns false — nothing is ever "dirty"
static inline bool cpu_physical_memory_get_dirty(ram_addr_t start,
                                                 ram_addr_t length,
                                                 unsigned client)
{
    return false;   // upstream: checks bitmap
}
```

### exec.c — invalidate_and_set_dirty emptied

File: `subprojects/unicorn/qemu/exec.c:1531-1534`

```c
static void invalidate_and_set_dirty(MemoryRegion *mr, hwaddr addr,
                                     hwaddr length)
{
}   // upstream: checks is_clean(), calls tb_invalidate_phys_range()
```

Also: `flatview_write_continue()` at line 1597 does a raw `memcpy()` for RAM writes. Upstream QEMU would call `invalidate_and_set_dirty()` after the memcpy — Unicorn both **removed the call site** and **emptied the function body**.

### translate-all.c — page_collection_lock disabled

File: `subprojects/unicorn/qemu/accel/tcg/translate-all.c:644`

```c
struct page_collection *
page_collection_lock(struct uc_struct *uc, tb_page_addr_t start, tb_page_addr_t end)
{
#if 0
    // ... entire implementation disabled ...
#endif
    return NULL;
}
```

This is actually **not harmful for M68K** because `TARGET_HAS_PRECISE_SMC` is not defined for M68K (only x86). The `page_collection_lock()` return value is passed through `notdirty_write()` → `tb_invalidate_phys_page_fast()`, but the fast path doesn't need the lock to work.

### exec.c — test_and_clear_dirty hardcoded

File: `subprojects/unicorn/qemu/exec.c:808`

```c
bool cpu_physical_memory_test_and_clear_dirty(ram_addr_t start,
                                              ram_addr_t length,
                                              unsigned client)
{
    return false;   // upstream: atomically tests and clears bitmap
}
```

## What Still Partially Works

Because `cpu_physical_memory_is_clean()` always returns `true`, the `TLB_NOTDIRTY` flag gets set on **every** RAM page (at `cputlb.c:877-878`). This means:

1. **Every guest write** goes through `store_helper()` → `notdirty_write()` slow path
2. `notdirty_write()` checks `mr->perms & UC_PROT_EXEC` (Unicorn addition at `cputlb.c:1199`)
3. If the page is executable, calls `tb_invalidate_phys_page_fast()` — **THIS WORKS**

So **guest-to-guest SMC detection partially works** for M68K instructions writing to executable pages. The Mac OS heap manager writing over our EmulOp patches with M68K `MOVE` instructions should trigger TB invalidation.

### But there are problems:

1. **`set_dirty_flag()` is a no-op**: After `notdirty_write()` invalidates TBs, it would normally mark the page dirty so future writes go through the fast path. Since `set_dirty_flag()` is empty, the page **never transitions out of the slow path**. Every write to every page goes through `notdirty_write()` forever. This is a permanent performance penalty.

2. **`tlb_set_dirty()` conditions**: The `notdirty_write()` at line 1211-1214 has conditions that may prevent `tlb_set_dirty()` from being called, keeping pages in the slow path even longer.

3. **Stale state between TLB flushes**: The dirty bitmap tracks state across TLB flushes. Without it, there's no persistent record of which pages need `TLB_NOTDIRTY`. When the TLB is flushed (via `tlb_flush()` on context switches or `uc_ctl_flush_tb()`), all entries are rebuilt from scratch. `is_clean()` returns `true` for everything, so `TLB_NOTDIRTY` gets set on everything again.

## Three Broken Write Paths

| Write Path | TB Invalidation? | macemu Usage |
|---|---|---|
| Guest M68K stores (`MOVE`, etc.) | **Partial** — via `notdirty_write()` → `tb_invalidate_phys_page_fast()` | Mac OS heap manager overwrites |
| `uc_mem_write()` API | **BROKEN** — `flatview_write_continue()` does raw memcpy, then `invalidate_and_set_dirty()` is empty | ROM patching at startup |
| Host pointer writes via `uc_mem_map_ptr()` | **BROKEN** — bypasses QEMU entirely | BasiliskII's `put_long()` etc. |

### For macemu specifically:

- **ROM patching** (`uc_mem_write()`): Only happens at startup before JIT runs. **Not a problem.**
- **EmulOp installation** (`uc_mem_write()`): Happens at startup. **Not a problem.**
- **Mac OS heap overwrites** (guest M68K stores): Partially handled by `notdirty_write()`. **May be sufficient** if the `UC_PROT_EXEC` check covers our RAM.
- **BasiliskII memory writes** (`put_long()` etc.): These go through host pointers. **Completely invisible to JIT.** If any EmulOp handler writes to RAM that was previously compiled... stale TBs.

## Known Unicorn Issues

Multiple GitHub issues document SMC detection problems:

- **#1148**: `uc_mem_write()` doesn't flush translation cache
- **#820**: Incorrect memory content after self-modifying code
- **#1561**: Editing instruction before execution doesn't take effect
- **#1344**: `uc_mem_map_ptr()` synchronization issues
- **#437**: Stale TB after `HOOK_CODE` modification

## Fix Options

### Option A: Restore Dirty Memory Bitmap

**Approach**: Re-implement the stubbed functions in `ram_addr.h` with actual bitmap tracking.

**Pros**: Most correct fix, matches upstream QEMU behavior.
**Cons**: Most work. Requires understanding QEMU's `ram_list.dirty_memory[]` allocation, the `DirtyMemoryBlocks` structure, and atomic bitmap operations. Risk of introducing new bugs.

**Effort**: High. Needs careful study of upstream QEMU's memory.c implementation.

### Option B: Add TB Invalidation to uc_mem_write()

**Approach**: In `flatview_write_continue()`, after the `memcpy()`, call `tb_invalidate_phys_range()` for the written range if the page contains compiled code.

**Pros**: Targeted fix for API writes. Relatively simple.
**Cons**: Only fixes `uc_mem_write()` path, not host pointer writes. For our case, startup-only writes via `uc_mem_write()` aren't the main problem.

**Effort**: Low. ~5 lines of code.

### Option C: Use uc_ctl_remove_cache() for Specific Ranges

**Approach**: From macemu, call `uc_ctl_remove_cache(start, end)` whenever we know we've written to memory that might contain compiled code. This is finer-grained than `uc_ctl_flush_tb()`.

**Pros**: No Unicorn fork changes needed. Precise invalidation.
**Cons**: Requires knowing exactly when and where writes happen. Mac OS heap writes happen inside the emulated CPU — we can't intercept them from the host side without hooks.

**Effort**: Medium. Would need memory write hooks on the EmulOp code ranges.

### Option D: Smarter Timer-Based Flush

**Approach**: Instead of flushing the entire TB cache, track which pages were written (via write hooks or bitmap) and only flush those.

**Pros**: Better than current 60Hz full flush. No Unicorn fork changes.
**Cons**: Still polling-based. Write hooks add overhead.

**Effort**: Medium.

### Option E: Verify Guest-to-Guest Path Works

**Approach**: The partial `notdirty_write()` path may already handle our main problem (Mac OS heap overwriting EmulOp patches). Verify this by:
1. Checking that RAM pages are mapped with `UC_PROT_EXEC`
2. Testing if removing the 60Hz `uc_ctl_flush_tb()` still works
3. If it does, the `notdirty_write()` path is handling it

**Pros**: Might require no code changes at all! Just removing the workaround.
**Cons**: May reveal edge cases. Need thorough testing.

**Effort**: Low. Just testing.

## Recommended Approach

**Start with Option E** — test if the existing `notdirty_write()` path already handles our case. If Mac OS heap writes are all M68K stores to executable pages, `tb_invalidate_phys_page_fast()` should invalidate the affected TBs.

If Option E fails, **combine C + D** — use `uc_ctl_remove_cache()` for known code ranges, with a fallback timer that only flushes pages that were written to.

Option A (restoring the full dirty bitmap) is the nuclear option — correct but high-risk. Save it for when the simpler approaches prove insufficient.

## Key Files

| File | What It Contains |
|---|---|
| `subprojects/unicorn/qemu/include/exec/ram_addr.h` | **THE SMOKING GUN** — all dirty bitmap functions stubbed |
| `subprojects/unicorn/qemu/exec.c:1531` | Empty `invalidate_and_set_dirty()` |
| `subprojects/unicorn/qemu/exec.c:1597` | Raw `memcpy()` in `flatview_write_continue()` |
| `subprojects/unicorn/qemu/accel/tcg/cputlb.c:877` | `TLB_NOTDIRTY` set based on `is_clean()` (always true) |
| `subprojects/unicorn/qemu/accel/tcg/cputlb.c:1189` | `notdirty_write()` — partial SMC detection |
| `subprojects/unicorn/qemu/accel/tcg/cputlb.c:2362` | `store_helper()` — checks `TLB_NOTDIRTY` |
| `subprojects/unicorn/qemu/accel/tcg/translate-all.c:644` | `page_collection_lock()` — `#if 0`'d out |
| `subprojects/unicorn/qemu/accel/tcg/translate-all.c:1841` | `tb_gen_code()` — `tlb_reset_dirty_by_vaddr()` |
| `subprojects/unicorn/qemu/accel/tcg/translate-all.c:2019` | `tb_invalidate_phys_page_fast()` — **WORKS** |
| `src/cpu/unicorn_wrapper.c` | Our `hook_block()` with 60Hz `uc_ctl_flush_tb()` workaround |

## See Also

- [NextSteps.md](../NextSteps.md) — Item 2: Fix TB Invalidation Properly
- [cpu/UnicornQuirks.md](cpu/UnicornQuirks.md) — JIT TB invalidation section
- Unicorn Issues: #1148, #820, #1561, #1344, #437
