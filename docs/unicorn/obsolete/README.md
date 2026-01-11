# Obsolete Documentation

This folder contains documentation for approaches that were considered or partially implemented but are now obsolete.

## Why These Documents Are Obsolete

### MMIO_IMPLEMENTATION_PLAN.md
- **Status**: Partially implemented but fundamentally flawed
- **Problem**: MMIO transport requires 10 bytes vs 2 bytes for EmulOps
- **Result**: Breaks in-place ROM patching, can't be used

### MMIO_TRAP_APPROACH.md
- **Status**: Proposed but never implemented
- **Problem**: Complex state management, requires two uc_emu_start() calls
- **Superseded by**: Native instruction extension approach

## Current Approach

See [EMULOPS_FIXES.md](/home/mick/macemu-dual-cpu/macemu-next/EMULOPS_FIXES.md) for the current plan to properly handle EmulOps by extending the M68K instruction set in a Unicorn fork.

## Historical Value

These documents are preserved for historical reference and to understand the evolution of the EmulOps handling approach. They document what was tried and why it didn't work, which is valuable for understanding the project's development.

---
*Moved to obsolete: January 2025*