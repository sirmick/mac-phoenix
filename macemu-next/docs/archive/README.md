# Archived Documentation

This directory contains historical documentation that is no longer actively maintained but preserved for reference.

---

## Why Archive?

As macemu-next evolved through iterative development, we accumulated 79+ markdown files. Many became:
- **Obsolete** (approaches that were tried and abandoned, like MMIO)
- **Duplicates** (multiple investigation docs on the same topic)
- **Superseded** (replaced by newer, more complete documentation)

Archiving these docs keeps the main documentation clean and current while preserving historical context.

---

## Archive Structure

### `abandoned/`
**Approaches that were tried and abandoned**

#### `abandoned/mmio/` (3 docs)
MMIO (Memory-Mapped I/O) transport for EmulOps was implemented but later **completely removed** in commit `8260df5d` ("Major cleanup: Remove MMIO transport, unify ROM patching").

Replaced by: A-line EmulOps (0xAE00-0xAE3F) using `UC_HOOK_INTR`

---

### `investigations/`
**Historical bug investigations and research (all resolved)**

#### `investigations/timer/` (5 docs)
Multiple timer implementation iterations. Timer is now complete and stable.

Current docs: See `completed/TIMER_IMPLEMENTATION_FINAL.md`

#### `investigations/interrupts/` (3 docs)
Interrupt implementation research and planning documents.

Current docs: See `completed/INTERRUPT_COMPLETE.md`

#### `investigations/cpu/` (8 docs)
Resolved CPU bugs and investigations:
- VBR corruption (fixed in commit `006cc0f8`)
- Divergence root causes (resolved)
- Unicorn early crashes (resolved)
- RTE batch execution bug (resolved)
- Lazy flag bugs (resolved)

Current docs: See `deepdive/cpu/` for current CPU documentation

#### `investigations/misc/` (4 docs)
- Callback research
- QEMU extraction analysis
- Old platform architecture
- Other research documents

---

### `planning/`
**Planning documents for completed work (7 docs)**

- File migration planning (migration complete)
- Reorganization plans (reorganization complete)
- Phase 2 implementation tracking (Phase 2 complete)
- Dual architecture design (implementation complete)
- Initialization refactor analysis (refactor complete)
- Next session plans (outdated)

Current status: See `../STATUS_SUMMARY.md` and `../TodoStatus.md`

---

### `status-history/`
**Old status snapshots (4 docs)**

Historical phase completion docs and old status summaries.

Current status: See `../STATUS_SUMMARY.md`

---

### `webrtc/`
**Old WebRTC planning iterations (2 docs)**

Earlier WebRTC integration planning documents.

Current status: See `../WEBRTC_INTEGRATION_STATUS.md`

---

## Finding Current Documentation

### For New Developers
Start here:
1. [`../README.md`](../README.md) - Quick start guide
2. [`../Architecture.md`](../Architecture.md) - System overview
3. [`../Commands.md`](../Commands.md) - Build and test commands

### For Implementation Details
See [`../deepdive/`](../deepdive/) directory:
- `MemoryArchitecture.md` - Memory system
- `cpu/UnicornQuirks.md` - **CRITICAL** Unicorn limitations
- `cpu/ALineAndFLineStatus.md` - Current trap handling status
- `InterruptTimingAnalysis.md` - Timing analysis

### For Historical Context
This directory (`archive/`) - Browse by category above

---

## Archive Policy

**When to archive a document:**
1. Approach was abandoned (e.g., MMIO)
2. Bug/issue is resolved and documented elsewhere
3. Planning doc for completed work
4. Superseded by newer, better documentation
5. No longer actively referenced

**When NOT to archive:**
1. Active reference material
2. Current implementation documentation
3. Still-relevant design documents
4. Ongoing investigations

**How we archive:**
- Use `git mv` (preserves history)
- Organize by category (abandoned/investigations/planning/etc.)
- Update references in active docs
- Document in this README

---

## Archived: January 21, 2026

**Cleanup Summary**: Archived 45+ documents to reduce main docs from 79 files to ~30 essential ones.

See [`../COMPREHENSIVE_CLEANUP_PLAN.md`](../COMPREHENSIVE_CLEANUP_PLAN.md) for full details of what was archived and why.

---

**Note**: All archived documents remain in the git repository and retain their full commit history. Nothing is deleted, just better organized.
