# PPC Code Diff Audit: mac-phoenix vs Legacy SheepShaver

## Status: 45 files compared

### Identical (23 files) - No action needed
All core CPU interpreter, JIT, and infrastructure files are byte-for-byte identical.

### Modified (6 files) - Intentional, verified safe
- ppc-config.hpp: JIT disable, comments
- spcflags.hpp: volatile + raw_mask() accessor
- cpu_ppc_kpx.cpp: Platform API rewrite of sheepshaver_glue.cpp
- emul_op_ppc.cpp: boot_progress tracking added
- rom_patches_ppc.cpp: renamed PatchROM → PatchROM_PPC
- rsrc_patches_ppc.cpp: debug logging moved

---

## Likely Bug Causes

### Type 10 (Unimplemented A-line trap)
1. Name Registry init fails silently (returns instead of QuitEmulator)
2. PatchNativeResourceManager() is stubbed (resource manager not patched)

### Type 6768 (Driver/extension error)
1. **MoveDrivesFromDriverToFront() is stubbed** - boot drive ordering broken
2. **InstallExtFS() is stubbed** - ExtFS not available
3. Video driver stubs (cursor, dirty area)

### JIT execute() returns early
- Recursive execute() from HandleInterrupt → interrupt() sets SPCFLAG_JIT_EXEC_RETURN
- Outer JIT loop sees it and exits prematurely
- Legacy has same code but uses PPC_CHECK_INTERRUPTS=0 (no HandleInterrupt from spcflags)

---

## Stubbed Functions (ppc_stubs.cpp) - Priority Order

| Function | Stubbed | Legacy Has Real? | Impact |
|----------|---------|-------------------|--------|
| MoveDrivesFromDriverToFront | empty | YES (macos_util.cpp:163-190) | **HIGH - disk mount** |
| PatchNativeResourceManager | empty | YES (rsrc_patches.cpp) | **HIGH - resource mgr** |
| InstallExtFS | empty | YES (extfs.cpp) | MEDIUM - shared folders |
| Enqueue | missing | YES (macos_util.cpp:98) | MEDIUM - queue ops |
| FindFreeDriveNumber | missing | YES (macos_util.cpp:153) | MEDIUM - disk mount |
| MountVolume | missing | YES (macos_util.cpp:200) | MEDIUM - disk mount |
| FileDiskLayout | missing | YES (macos_util.cpp:210) | MEDIUM - disk layout |
| AddSifter/FindSifter | stub | YES (audio.cpp) | LOW - audio |
| EtherResetCachedAllocation | empty | YES (ether.cpp) | LOW - network |
| SerialInterrupt | empty | YES (serial.cpp) | LOW - serial |

---

## ROM Compatibility

**Question: Which ROM should we use?**
- Current: "Power Mac G3 desktop" (1997-11, 79D68D63)
- rom_patches_ppc.cpp supports both ROMTYPE_NEWWORLD and OldWorld
- Need to verify: does the ROM type match the interrupt dispatch addresses?
  - NewWorld: ROMBase + 0x312b1c
  - OldWorld: ROMBase + 0x312a3c

---

## Next Steps

1. [ ] Copy MoveDrivesFromDriverToFront from legacy macos_util.cpp
2. [ ] Copy PatchNativeResourceManager from legacy rsrc_patches.cpp
3. [ ] Copy Enqueue, FindFreeDriveNumber, MountVolume, FileDiskLayout from legacy
4. [ ] Verify ROM type detection (NewWorld vs OldWorld)
5. [ ] Fix JIT reentrant execute (PPC_CHECK_INTERRUPTS interaction)
