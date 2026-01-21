# Documentation Cleanup Proposal

## Current State Assessment (January 2026)

The docs folder contains a mix of:
- **Active documentation** (architecture, design)
- **Completed work summaries** (VBR fix, interrupt implementation, etc.)
- **Investigation notes** (unmapped memory, hybrid execution)
- **Outdated information** (status marked as December 2025, incomplete features now complete)

## Proposed Structure

### 1. Archive Completed Work
Move these files to `docs/archive/` or `docs/completed/`:
- `INVESTIGATION_COMPLETE.md` - VBR investigation summary
- `VBR_FIX_SUMMARY.md` - VBR fix details
- `CPU_TYPE_FIX_SUMMARY.md` - CPU type selection fix
- `INTERRUPT_DESIGN.md` - Design document (now implemented)
- `INTERRUPT_IMPL_STATUS.md` - Implementation status (now complete)
- `HOOK_FREE_DESIGN.md` - Design document (now implemented)
- `INTERRUPT_COMPLETE.md` - Completion summary
- `LEGACY_API_REMOVAL.md` - Removal summary
- `UNMAPPED_MEMORY_ISSUE.md` - Investigation (resolved)
- `UAE_HYBRID_EXECUTION_ISSUE.md` - Investigation (resolved)
- `UNICORN_NATIVE_TRAP_EXECUTION.md` - Implementation summary

### 2. Keep Active Documentation
These remain in `docs/` as they describe current architecture:
- `README.md` - **NEEDS UPDATE** (status section)
- `PROJECT-OVERVIEW.md` - **NEEDS UPDATE** (status, roadmap)
- `Platform-Architecture.md`
- `Platform-Adapter-Implementation.md`
- `CPU-Backend-API.md`
- `Memory.md`
- `UAE-Quirks.md`
- `Unicorn-Quirks.md`
- `CPU-Model-Configuration.md`
- `Full-Monty-Plan.md`
- `QEMU_EXTRACTION_ANALYSIS.md`
- `ROM-Patching-Required.md`
- `A-line-F-line-Exception-Design.md`
- `A-line-F-line-Status.md`

### 3. Current Status Documentation
Create `docs/STATUS.md` with:
- **What Works**: ROM boot, EmulOps, traps, interrupts, dual-CPU validation (514k instructions)
- **Recent Achievements**: VBR fix, native trap execution, hook removal
- **Known Issues**: Timer interrupt timing non-determinism, Unicorn stops at ~200k vs UAE 250k
- **Current Focus**: Understanding interrupt timing divergence (see INTERRUPT_TIMING_ANALYSIS.md)

### 4. Keep Important Analysis
- `INTERRUPT_TIMING_ANALYSIS.md` - **CRITICAL** - explains current blocker (timer interrupt non-determinism)
- This should stay in main docs as it's the key to understanding why traces diverge

## Recommended Actions

1. **Create archive folder**:
   ```bash
   mkdir -p macemu-next/docs/completed
   ```

2. **Move completed work**:
   ```bash
   mv macemu-next/docs/{INVESTIGATION_COMPLETE,VBR_FIX_SUMMARY,CPU_TYPE_FIX_SUMMARY}.md docs/completed/
   mv macemu-next/docs/{INTERRUPT_*,HOOK_FREE_DESIGN,LEGACY_API_REMOVAL}.md docs/completed/
   mv macemu-next/docs/{UNMAPPED_MEMORY_ISSUE,UAE_HYBRID_EXECUTION_ISSUE,UNICORN_NATIVE_TRAP_EXECUTION}.md docs/completed/
   ```

3. **Update active docs**:
   - `README.md` - Update "Project Status" section with current state
   - `PROJECT-OVERVIEW.md` - Update status, date, roadmap

4. **Create STATUS.md** - Current snapshot of project progress

5. **Keep INTERRUPT_TIMING_ANALYSIS.md** in main docs - it's the active investigation

## Benefits

- **Clearer for new contributors** - Easy to find current vs. historical info
- **Preserves history** - Completed work still documented, just organized
- **Accurate status** - No more "currently working on A-line/F-line" when it's done
- **Better navigation** - Active docs vs. completed work clearly separated
