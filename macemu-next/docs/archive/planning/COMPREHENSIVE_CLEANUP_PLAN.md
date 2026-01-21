# Comprehensive Documentation Cleanup Plan
**Date**: January 21, 2026
**Current Doc Count**: 79 markdown files (way too many!)
**Goal**: Reduce to ~25-30 essential documents

---

## Executive Summary

The docs directory has **accumulated 79 markdown files** through iterative development. Many are:
- **Obsolete** (describe abandoned approaches like MMIO)
- **Duplicates** (multiple timer/interrupt investigation docs)
- **Superseded** (old status docs replaced by newer ones)
- **Orphaned** (orphaned planning docs for incomplete features)

**Recommendation**: Archive 40-50 docs, keep 25-30 essential ones, create clear directory structure.

---

## Analysis by Category

### 🗑️ CATEGORY 1: OBSOLETE - Remove or Archive (35 docs)

#### MMIO Implementation Docs - **OBSOLETE** ❌
MMIO approach was **abandoned** (commit `8260df5d` - "Major cleanup: Remove MMIO transport")
- ❌ `MMIO_IMPLEMENTATION_PLAN.md` (8.2K)
- ❌ `MMIO_IMPLEMENTATION_PLAN_V2.md` (7.6K)
- ❌ `MMIO_INTEGRATION_INSTRUCTIONS.md` (6.6K)

**Action**: Move to `docs/archive/abandoned/mmio/`

---

#### Timer Investigation Docs - **MOSTLY OBSOLETE** ⚠️
6 documents covering timer implementation iterations. Implementation is **complete**.

Keep ONE summary, archive the rest:
- ✅ **KEEP**: `TIMER_IMPLEMENTATION_FINAL.md` (9.0K) - Final implementation
- ❌ Archive: `TIMER_IMPLEMENTATION_COMPARISON.md` (6.1K)
- ❌ Archive: `TIMER_IMPLEMENTATION_COMPLETE.md` (8.0K)
- ❌ Archive: `TIMER_INVESTIGATION_RESULTS.md` (7.3K)
- ❌ Archive: `TIMER_INVESTIGATION_STATUS.md` (13K)
- ❌ Archive: `TIMER_REFACTOR_PLAN.md` (11K)

**Action**:
- Move 5 to `docs/archive/investigations/timer/`
- Keep `TIMER_IMPLEMENTATION_FINAL.md`, OR better yet, merge its content into `completed/` and delete

---

#### Interrupt Investigation Docs - **SUPERSEDED** ⚠️
Completed docs exist in `completed/INTERRUPT_*.md`

Root-level investigation docs are redundant:
- ❌ `INTERRUPT_FIX_PLAN.md` (8.7K) - Superseded by completed/INTERRUPT_COMPLETE.md
- ❌ `SIMPLIFIED_INTERRUPT_APPROACH.md` (7.9K) - Superseded by completed/INTERRUPT_DESIGN.md
- ❌ `QEMU_INTERRUPT_ANALYSIS.md` (9.1K) - Historical research, archive

**Action**: Move to `docs/archive/investigations/interrupts/`

---

#### Phase/Status Docs - **DUPLICATES** ⚠️
Multiple overlapping status documents:
- ✅ **KEEP**: `STATUS_SUMMARY.md` (7.1K) - Current authoritative status
- ✅ **KEEP**: `TodoStatus.md` - Active todo tracking
- ❌ Archive: `PHASE1_COMPLETE.md` (8.5K) - Superseded by STATUS_SUMMARY.md
- ❌ Archive: `PHASE2_COMPLETE.md` (9.4K) - Superseded by STATUS_SUMMARY.md
- ❌ Archive: `PHASE2_IN_PROGRESS.md` (6.2K) - Outdated (Phase 2 is complete now)
- ❌ Archive: `completed/STATUS.md` - Old status doc

**Action**: Move to `docs/archive/status-history/`

---

#### WebRTC Docs - **DUPLICATES/INCOMPLETE** ⚠️
3 documents, 2 appear to be duplicates:
- ⚠️ `WebRTCIntegrationPlan.md` (23K) - Original plan
- ⚠️ `WEBRTC_INTEGRATION_PLAN.md` (16K) - Looks like duplicate/variant
- ⚠️ `WEBRTC_INTEGRATION_STATUS.md` (20K) - Status doc

**Need to review**: Which is current? Likely can consolidate to 1 doc.

**Action**: Review and consolidate, move old versions to `docs/archive/webrtc/`

---

#### Reorganization Docs - **HISTORICAL** 📚
- ❌ `REORGANIZATION_PLAN_v2.md` (15K)
- ❌ `REORGANIZATION_SUMMARY.md` (12K)

These were **planning docs** for past reorganizations. Now complete.

**Action**: Move to `docs/archive/planning/`

---

#### One-Off Investigation Docs - **HISTORICAL** 📚
- ❌ `DIVERGENCE_ROOT_CAUSE.md` (4.5K) - Historical bug investigation
- ❌ `vbr_corruption_analysis.md` (7.6K) - Historical bug (fixed in commit `006cc0f8`)
- ❌ `INITIALIZATION_REFACTOR_ANALYSIS.md` (20K) - Planning doc
- ❌ `NEXT_SESSION_PLAN.md` (12K) - Session planning (outdated)
- ❌ `FILE_MIGRATION_PLAN.md` (17K) - Migration planning (done?)
- ❌ `CallbackResearch.md` - Research doc
- ❌ `DUAL_ARCHITECTURE_DESIGN.md` (29K) - Design doc (implementation complete?)
- ❌ `Phase2Implementation.md` - Implementation tracking (complete?)

**Action**: Move to `docs/archive/investigations/` or `docs/archive/planning/`

---

#### Completed/ Folder Review - **GOOD BUT NEEDS CURATION** ✅
Currently has 13 docs. Most are good historical records. Consider:
- ✅ Keep most (they're already archived)
- ⚠️ Review for duplicates with root-level docs

---

#### deepdive/cpu/ - **RECENTLY ORGANIZED** ✅
13 docs, recently organized. Mostly good!
- ✅ Keep structure
- ⚠️ Consider moving some historical bug investigations to archive

Candidates for archive:
- `UnicornEarlyCrashInvestigation.md` - Historical (resolved)
- `UnicornBatchExecutionRTEBug.md` - Historical (resolved)
- `UnicornBugSrLazyFlags.md` - Historical bug
- `UnicornRTEQemuResearch.md` - Research (historical)

---

### ✅ CATEGORY 2: KEEP - Essential Docs (25-30 docs)

#### Top-Level Essential (7 docs)
- ✅ `README.md` - Quick start
- ✅ `Architecture.md` - System overview
- ✅ `Commands.md` - Build/test commands
- ✅ `ProjectGoals.md` - Vision and philosophy
- ✅ `TodoStatus.md` - Active TODO tracking
- ✅ `STATUS_SUMMARY.md` - Current status
- ✅ `JSON_CONFIG.md` - Configuration docs

#### Top-Level Keep (But Review) (2 docs)
- ⚠️ `THREADING_ARCHITECTURE.md` - If WebRTC is active
- ⚠️ `DOCUMENTATION_CLEANUP_2026_01_21.md` - Recent cleanup summary (keep temporarily)

#### deepdive/ Essential (8 docs)
- ✅ `README.md` - Deepdive index
- ✅ `MemoryArchitecture.md` - Memory system
- ✅ `InterruptTimingAnalysis.md` - Important analysis
- ✅ `PlatformAdapterImplementation.md` - Platform API
- ✅ `PlatformAPIInterrupts.md` - Interrupt abstraction
- ✅ `RomPatchingRequired.md` - ROM patches
- ⚠️ `RTE_FIX.md` - RTE bug fix (might be superseded by cpu/UnicornBatchExecutionRTEBug.md?)
- ⚠️ `PlatformArchitectureOld.md` - Says "Old" in name, probably archive

#### deepdive/cpu/ Essential (6-8 docs)
- ✅ `ALineAndFLineStatus.md` - Current status
- ✅ `UnicornQuirks.md` - Critical limitations
- ✅ `UaeQuirks.md` - UAE specifics
- ✅ `CpuBackendApi.md` - Backend API
- ✅ `CpuTraceDebugging.md` - Debugging guide
- ✅ `JIT_Block_Size_Analysis.md` - Performance analysis
- ⚠️ `CpuModelConfiguration.md` - CPU model selection (keep?)
- ⚠️ `DualCpuValidationInitialization.md` - DualCPU init (keep?)

#### completed/ folder (10-13 docs)
- ✅ Keep most as historical record
- ✅ `INTERRUPT_COMPLETE.md` - Interrupt implementation
- ✅ `UNICORN_NATIVE_TRAP_EXECUTION.md` - Trap execution
- ✅ Others as historical reference

---

## Recommended Directory Structure

```
docs/
├── README.md                          # Quick start
├── Architecture.md                    # System overview
├── Commands.md                        # Build/test commands
├── ProjectGoals.md                    # Vision
├── TodoStatus.md                      # Active TODOs
├── STATUS_SUMMARY.md                  # Current status
├── JSON_CONFIG.md                     # Configuration
├── THREADING_ARCHITECTURE.md          # WebRTC threading (if active)
│
├── deepdive/                          # Technical deep-dives
│   ├── README.md                      # Index
│   ├── MemoryArchitecture.md
│   ├── InterruptTimingAnalysis.md
│   ├── PlatformAPIInterrupts.md
│   ├── RomPatchingRequired.md
│   │
│   └── cpu/                           # CPU-specific docs
│       ├── ALineAndFLineStatus.md
│       ├── UnicornQuirks.md           # CRITICAL - PC limitation
│       ├── UaeQuirks.md
│       ├── CpuBackendApi.md
│       ├── CpuTraceDebugging.md
│       └── JIT_Block_Size_Analysis.md
│
├── completed/                         # Historical: completed work
│   ├── README.md
│   ├── INTERRUPT_COMPLETE.md
│   ├── UNICORN_NATIVE_TRAP_EXECUTION.md
│   └── [~10 other completed docs]
│
└── archive/                           # ARCHIVED (40-50 docs)
    ├── abandoned/                     # Abandoned approaches
    │   └── mmio/
    │       ├── MMIO_IMPLEMENTATION_PLAN.md
    │       ├── MMIO_IMPLEMENTATION_PLAN_V2.md
    │       └── MMIO_INTEGRATION_INSTRUCTIONS.md
    │
    ├── investigations/                # Historical investigations
    │   ├── timer/
    │   │   ├── TIMER_INVESTIGATION_STATUS.md
    │   │   ├── TIMER_REFACTOR_PLAN.md
    │   │   └── [4 more timer docs]
    │   │
    │   ├── interrupts/
    │   │   ├── INTERRUPT_FIX_PLAN.md
    │   │   ├── SIMPLIFIED_INTERRUPT_APPROACH.md
    │   │   └── QEMU_INTERRUPT_ANALYSIS.md
    │   │
    │   ├── cpu/                       # Historical CPU bug investigations
    │   │   ├── vbr_corruption_analysis.md
    │   │   ├── DIVERGENCE_ROOT_CAUSE.md
    │   │   ├── UnicornEarlyCrashInvestigation.md
    │   │   └── [3 more resolved bugs]
    │   │
    │   └── misc/
    │       ├── CallbackResearch.md
    │       └── QemuExtractionAnalysis.md
    │
    ├── planning/                      # Historical planning docs
    │   ├── REORGANIZATION_PLAN_v2.md
    │   ├── REORGANIZATION_SUMMARY.md
    │   ├── INITIALIZATION_REFACTOR_ANALYSIS.md
    │   ├── FILE_MIGRATION_PLAN.md
    │   ├── NEXT_SESSION_PLAN.md
    │   └── Phase2Implementation.md
    │
    ├── status-history/                # Old status documents
    │   ├── PHASE1_COMPLETE.md
    │   ├── PHASE2_COMPLETE.md
    │   └── PHASE2_IN_PROGRESS.md
    │
    └── webrtc/                        # Old WebRTC planning docs
        └── [consolidated old versions]
```

**Result**:
- **Active docs**: ~25-30 (organized, essential)
- **Archive**: ~40-50 (preserved for history, but out of the way)

---

## Action Plan

### Phase 1: Create Archive Structure ✅
```bash
cd docs
mkdir -p archive/{abandoned/mmio,investigations/{timer,interrupts,cpu,misc},planning,status-history,webrtc}
```

### Phase 2: Move MMIO Docs (Obsolete) ✅
```bash
git mv MMIO_*.md archive/abandoned/mmio/
```

### Phase 3: Move Timer Investigation Docs ✅
```bash
git mv TIMER_IMPLEMENTATION_COMPARISON.md archive/investigations/timer/
git mv TIMER_IMPLEMENTATION_COMPLETE.md archive/investigations/timer/
git mv TIMER_INVESTIGATION_*.md archive/investigations/timer/
git mv TIMER_REFACTOR_PLAN.md archive/investigations/timer/
# Keep TIMER_IMPLEMENTATION_FINAL.md OR move to completed/ (decide)
```

### Phase 4: Move Interrupt Investigation Docs ✅
```bash
git mv INTERRUPT_FIX_PLAN.md archive/investigations/interrupts/
git mv SIMPLIFIED_INTERRUPT_APPROACH.md archive/investigations/interrupts/
git mv QEMU_INTERRUPT_ANALYSIS.md archive/investigations/interrupts/
```

### Phase 5: Move Historical Investigations ✅
```bash
git mv DIVERGENCE_ROOT_CAUSE.md archive/investigations/cpu/
git mv vbr_corruption_analysis.md archive/investigations/cpu/
git mv CallbackResearch.md archive/investigations/misc/
# Move resolved CPU bugs from deepdive/cpu/
```

### Phase 6: Move Planning Docs ✅
```bash
git mv REORGANIZATION_*.md archive/planning/
git mv INITIALIZATION_REFACTOR_ANALYSIS.md archive/planning/
git mv FILE_MIGRATION_PLAN.md archive/planning/
git mv NEXT_SESSION_PLAN.md archive/planning/
git mv Phase2Implementation.md archive/planning/
git mv DUAL_ARCHITECTURE_DESIGN.md archive/planning/  # If implementation complete
```

### Phase 7: Move Old Status Docs ✅
```bash
git mv PHASE1_COMPLETE.md archive/status-history/
git mv PHASE2_COMPLETE.md archive/status-history/
git mv PHASE2_IN_PROGRESS.md archive/status-history/
```

### Phase 8: Consolidate WebRTC Docs ✅
```bash
# Review and determine which is current
# Move old versions to archive/webrtc/
```

### Phase 9: Clean deepdive/cpu/ ✅
```bash
cd deepdive/cpu
# Move resolved bugs to archive
git mv UnicornEarlyCrashInvestigation.md ../../archive/investigations/cpu/
git mv UnicornBatchExecutionRTEBug.md ../../archive/investigations/cpu/
git mv UnicornBugSrLazyFlags.md ../../archive/investigations/cpu/
git mv UnicornRTEQemuResearch.md ../../archive/investigations/cpu/
```

### Phase 10: Update READMEs ✅
- Update `docs/README.md` with simplified structure
- Update `docs/deepdive/README.md` with reduced doc list
- Create `docs/archive/README.md` explaining archive purpose

### Phase 11: Add Archive README ✅
Create `docs/archive/README.md`:
```markdown
# Archived Documentation

This directory contains historical documentation that is no longer actively maintained
but preserved for reference.

## Categories

- **abandoned/** - Approaches that were tried and abandoned (e.g., MMIO)
- **investigations/** - Historical bug investigations (now resolved)
- **planning/** - Planning documents for completed work
- **status-history/** - Old status snapshots
- **webrtc/** - Old WebRTC planning iterations

## Why Archive?

These docs are valuable historical context but were cluttering the main docs.
If you're researching how something was implemented or debugged, this is where to look.

For current documentation, see the parent `docs/` directory.
```

---

## Expected Results

**Before**: 79 markdown files (confusing, duplicates, hard to navigate)
**After**:
- 25-30 essential docs in main structure (clear, current)
- 40-50 archived docs (preserved but separate)
- Clear directory structure (easy navigation)

**Benefits**:
1. ✅ New developers see only essential, current docs
2. ✅ Historical context preserved for reference
3. ✅ No duplicates or conflicting information
4. ✅ Clear "completed" vs "active" vs "archived" separation
5. ✅ Easier to maintain documentation going forward

---

## Timeline

**Estimated effort**: 2-3 hours
- 30 min: Review and categorize all docs
- 60 min: Execute git mv commands
- 30 min: Update READMEs and create archive README
- 30 min: Test links, commit changes

---

## Notes

- All moves use `git mv` to preserve history
- Archive is NOT .gitignored (preserved in repo)
- Archive README explains purpose clearly
- Original commit history preserved for all files

---

**Ready to execute?** This will dramatically improve documentation clarity!
