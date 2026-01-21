# Documentation Cleanup - January 21, 2026

## Summary

Comprehensive cleanup and accuracy audit of macemu-next documentation to reflect actual implementation status, especially regarding Unicorn backend limitations discovered in January 2026.

## Major Changes

### 1. ⚠️ Critical Unicorn Limitation Documented

**Problem**: Documentation claimed A-line/F-line trap handling worked in Unicorn, but it actually doesn't.

**Root Cause**: Unicorn cannot change PC from interrupt hooks (Unicorn GitHub issue #1027) - discovered in commits `9464afa4` and `32a6926b`.

**Documentation Updates**:
- Updated [deepdive/cpu/ALineAndFLineStatus.md](deepdive/cpu/ALineAndFLineStatus.md) to reflect broken status
- Updated [deepdive/cpu/UnicornQuirks.md](deepdive/cpu/UnicornQuirks.md) with PC change limitation as #1 critical issue
- Added warning to [deepdive/cpu/ALineAndFLineTrapHandling.md](deepdive/cpu/ALineAndFLineTrapHandling.md) that the design doesn't work
- Updated [completed/INTERRUPT_COMPLETE.md](completed/INTERRUPT_COMPLETE.md) with limitation notes
- Updated [README.md](README.md) with accurate status and limitations section
- Updated [STATUS_SUMMARY.md](STATUS_SUMMARY.md) to distinguish UAE (fully working) from Unicorn (limited)
- Updated [TodoStatus.md](TodoStatus.md) with accurate Unicorn status

### 2. 📁 CPU Documentation Reorganization

**Change**: Created `docs/deepdive/cpu/` subdirectory to organize all CPU-related documentation.

**Files Moved**:
- `ALineAndFLineStatus.md` → `cpu/`
- `ALineAndFLineTrapHandling.md` → `cpu/`
- `CpuBackendApi.md` → `cpu/`
- `CpuModelConfiguration.md` → `cpu/`
- `CpuTraceDebugging.md` → `cpu/`
- `DualCpuValidationInitialization.md` → `cpu/`
- `JIT_Block_Size_Analysis.md` → `cpu/`
- `UaeQuirks.md` → `cpu/`
- `UnicornBatchExecutionRTEBug.md` → `cpu/`
- `UnicornBugSrLazyFlags.md` → `cpu/`
- `UnicornEarlyCrashInvestigation.md` → `cpu/`
- `UnicornQuirks.md` → `cpu/`
- `UnicornRTEQemuResearch.md` → `cpu/`

**Total**: 13 CPU-related docs organized into subdirectory

**Updated**: [deepdive/README.md](deepdive/README.md) with new structure and accurate status indicators

### 3. ✅ Accurate Status Reporting

**What Works in Unicorn**:
- ✅ Normal M68K instruction execution (UAE and Unicorn both work)
- ✅ A-line EmulOps (0xAE00-0xAE3F) - BasiliskII-specific
- ✅ EmulOps (0x71xx illegal instructions)
- ✅ Interrupt detection via UC_HOOK_BLOCK
- ✅ RTE instruction (return from exception)
- ✅ JIT compilation (~14.56M instructions/sec)

**What Doesn't Work in Unicorn**:
- ❌ Mac OS A-line traps (0xA000-0xAFFF except EmulOps)
- ❌ Mac OS F-line traps (0xF000-0xFFFF)
- ❌ Full ROM boot on Unicorn standalone
- ❌ True dual-CPU exception handling validation

**What Works in UAE**:
- ✅ Everything! UAE backend is fully functional.

### 4. 🔧 Workarounds Documented

**DualCPU Mode**:
- Execute A-line/F-line traps on UAE only
- Sync full register state to Unicorn after execution
- Continue validation with Unicorn in synchronized state
- **Status**: Works but defeats purpose of independent CPU comparison

**Standalone Unicorn**:
- A-line EmulOps (0xAE00-0xAE3F) work (don't need PC changes)
- Other traps cause hangs
- **Status**: Limited but functional for some use cases

## Files Modified

### Documentation Updates
- [docs/README.md](README.md) - Added limitations section, updated achievements
- [docs/STATUS_SUMMARY.md](STATUS_SUMMARY.md) - Distinguished UAE vs Unicorn status
- [docs/TodoStatus.md](TodoStatus.md) - Updated Unicorn backend status
- [docs/completed/INTERRUPT_COMPLETE.md](completed/INTERRUPT_COMPLETE.md) - Added limitations section
- [docs/deepdive/README.md](deepdive/README.md) - Reorganized for cpu/ subdirectory
- [docs/deepdive/cpu/ALineAndFLineStatus.md](deepdive/cpu/ALineAndFLineStatus.md) - Complete rewrite reflecting broken status
- [docs/deepdive/cpu/ALineAndFLineTrapHandling.md](deepdive/cpu/ALineAndFLineTrapHandling.md) - Added warning that design doesn't work
- [docs/deepdive/cpu/UnicornQuirks.md](deepdive/cpu/UnicornQuirks.md) - Added PC change limitation as critical issue

### File Reorganization (git mv)
- Created `docs/deepdive/cpu/` directory
- Moved 13 CPU-related docs into subdirectory
- Updated all cross-references

## Impact on Project Goals

### Core Goals Still Valid ✅
1. **Dual-CPU validation of normal instructions** - Still works with workarounds
2. **UAE backend for full emulation** - Fully functional
3. **Modern architecture** - Platform API abstraction working well
4. **JIT performance** - Unicorn JIT works for normal instructions

### Adjusted Expectations ⚠️
1. **Unicorn standalone mode** - Limited to normal instructions + A-line EmulOps
2. **Full ROM boot on Unicorn** - Not currently possible due to trap limitation
3. **True dual-CPU exception validation** - Not possible without Unicorn modifications

### Recommended Path Forward
1. **Continue using UAE as primary backend** for full Mac emulation
2. **Use Unicorn for normal instruction validation** where it works
3. **Document limitations clearly** to set accurate expectations
4. **Consider Unicorn fork/patch** if full standalone Unicorn mode becomes critical

## Lessons Learned

### What Went Well ✅
- Thorough investigation of Unicorn limitations
- Proper documentation of workarounds
- Clean separation of concerns (UAE vs Unicorn capabilities)
- Comprehensive testing revealed the limitation early

### What Could Be Improved 🔧
- Earlier discovery of Unicorn PC change limitation
- More upfront research on Unicorn's hook capabilities
- Better distinction in docs between "designed" vs "implemented and working"

## Conclusion

The documentation now accurately reflects the state of the project:

**UAE Backend**: ✅ Fully functional, production-ready
**Unicorn Backend**: ⚠️ Functional for normal instructions, limited for exceptions
**DualCPU Backend**: ✅ Functional with UAE-execute workaround

The project remains viable and valuable - the core dual-CPU validation goal works, and UAE provides a fully functional Mac emulator. The Unicorn limitation is documented and understood, with clear workarounds in place.

---

**Date**: January 21, 2026
**Author**: Claude Code (Documentation Audit)
**Branch**: phoenix-mac-planning
**Commit**: (to be determined)
