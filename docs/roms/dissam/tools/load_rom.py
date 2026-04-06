# -*- coding: utf-8 -*-
# Ghidra headless postScript: load, disassemble and annotate a raw Mac ROM.
#
# Invocation: -postScript load_rom.py <machine>
#
# What it does:
#   1. Labels the Apple ROM header (checksum + initial PC words).
#   2. Creates a small RAM memory block at 0..$2000 so low-memory global
#      references from ROM resolve to labels.
#   3. Imports low-memory globals from vendor/classic-mac-rom-ghidra-tools
#      /lomem_globals.txt.
#   4. Brute-force disassembles every word-aligned address in the ROM range
#      whose bytes could be a valid M68K instruction. Ghidra's analyzer then
#      sorts out control flow, function boundaries, and false positives.
#   5. Creates ResetEntry function at ROM+$2A and follows flow.
#   6. Imports per-machine ROM symbol map from vendor/68k-mac-rom-maps/rom-maps/
#      (if available for this machine).
#   7. Kicks auto-analysis for another pass.
#
# @category Mac.ROM
# @runtime Jython

import sys
import os
from ghidra.program.model.symbol import SourceType
from ghidra.program.model.listing import Instruction
from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.plugin.core.analysis import AutoAnalysisManager
from ghidra.program.model.address import AddressSet
from ghidra.program.model.mem import MemoryConflictException

args = getScriptArgs()
if len(args) < 1:
    print("load_rom.py: expected <machine> argument")
    sys.exit(1)

machine = args[0]
script_dir = os.path.dirname(os.path.abspath(sourceFile.getAbsolutePath()))

# ---------------------------------------------------------------------------
# Per-machine config
# ---------------------------------------------------------------------------
ROM_BASES = {
    "se":     0x00400000,
    "macii":  0x40800000,
    "maciix": 0x40800000,
    "iici":   0x40800000,
}
ROM_SIZES = {
    "se":     0x40000,   # 256K
    "macii":  0x40000,
    "maciix": 0x40000,
    "iici":   0x80000,   # 512K
}
# cy384/68k-mac-rom-maps filenames. None = no map available for this ROM.
ROM_MAP_FILES = {
    "se":     "MacSEROM.lst.txt",
    "macii":  "MacIIROM.lst.txt",
    # IIx/SE30/IIcx shared ROM ($97221136) has no upstream map. Both ROMs
    # are version $0178 and the two images are ~95.7% byte-identical, so
    # we reuse MacII's map and verify-per-symbol at import time
    # (see REFERENCE_ROM_FOR_MAP below).
    "maciix": "MacIIROM.lst.txt",
    "iici":   "MacIIciROM.lst.txt",
}

# For machines whose map is borrowed from a different ROM, we need the
# reference ROM's bytes to verify each symbol address actually points to the
# same code sequence. Paths are relative to tools/vendor/ROM_STAGING/ or
# absolute, resolved by resolve_reference_rom() below.
REFERENCE_ROM_PATHS = {
    # maciix map is borrowed from MacII; verify against the MacII image.
    "maciix": [
        "~/storage/roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM",
        "~/storage/roms/256KB ROMs/1987-03 - 9779D2C4 - MacII (800k v2).ROM",
    ],
}
# How many bytes must match between the two ROMs at each symbol address
# before we trust the borrowed name. Covers at least 2-4 M68K instructions.
VERIFY_WINDOW = 16

if machine not in ROM_BASES:
    print("load_rom.py: unknown machine '%s'" % machine)
    sys.exit(1)

base  = ROM_BASES[machine]
size  = ROM_SIZES[machine]
af    = currentProgram.getAddressFactory()
space = af.getDefaultAddressSpace()
mem   = currentProgram.getMemory()

reset_entry   = space.getAddress(base + 0x2A)
checksum_addr = space.getAddress(base)
entry_word    = space.getAddress(base + 4)
rom_start     = space.getAddress(base)
rom_end       = space.getAddress(base + size - 1)

# ---------------------------------------------------------------------------
# 1. Label ROM header
# ---------------------------------------------------------------------------
try:
    createLabel(checksum_addr, "ROM_Checksum", True)
    setEOLComment(checksum_addr, "Apple ROM checksum (matches filename hex)")
    createLabel(entry_word, "ROM_InitialPC", True)
    setEOLComment(entry_word, "Initial PC word read by CPU reset vector")
except Exception as e:
    print("warn: header labels: %s" % e)

# ---------------------------------------------------------------------------
# 2. Create RAM memory block for low-memory globals (0..$2000)
# ---------------------------------------------------------------------------
try:
    lomem_start = space.getAddress(0x0)
    lomem_block = mem.getBlock(lomem_start)
    if lomem_block is None:
        lomem_block = mem.createUninitializedBlock(
            "RAM_LOMEM", lomem_start, 0x2000, False)
        lomem_block.setRead(True)
        lomem_block.setWrite(True)
        lomem_block.setExecute(False)
        print("  created RAM_LOMEM block 0x00000000..0x00002000")
except MemoryConflictException:
    pass
except Exception as e:
    print("warn: create lomem block: %s" % e)

# ---------------------------------------------------------------------------
# 3. Import low-memory globals
# ---------------------------------------------------------------------------
lomem_file = os.path.join(script_dir, "vendor",
                          "classic-mac-rom-ghidra-tools", "lomem_globals.txt")
lomem_count = 0
if os.path.exists(lomem_file):
    try:
        f = open(lomem_file, "r")
        for line in f:
            line = line.rstrip()
            if not line or line.startswith("#"):
                continue
            # Format: "Name  hex_addr  type  description..."
            parts = line.split(None, 3)
            if len(parts) < 2:
                continue
            name = parts[0]
            try:
                addr_val = int(parts[1], 16)
            except ValueError:
                continue
            if addr_val < 0 or addr_val >= 0x2000:
                continue
            try:
                addr = space.getAddress(addr_val)
                createLabel(addr, name, True, SourceType.IMPORTED)
                if len(parts) >= 4:
                    setEOLComment(addr, parts[3])
                lomem_count += 1
            except Exception:
                pass
        f.close()
        print("  imported %d low-memory globals" % lomem_count)
    except Exception as e:
        print("warn: lomem import: %s" % e)
else:
    print("warn: lomem file not found: %s" % lomem_file)

# ---------------------------------------------------------------------------
# 4. Brute-force disassemble the entire ROM range
# ---------------------------------------------------------------------------
try:
    rom_set = AddressSet(rom_start, rom_end)
    cmd = DisassembleCommand(rom_set, None, True)
    cmd.applyTo(currentProgram, monitor)
    print("  brute-force disassembled ROM range")
except Exception as e:
    print("warn: brute disassemble: %s" % e)

# ---------------------------------------------------------------------------
# 4b. Handle A-line traps ($Axxx opcodes)
# ---------------------------------------------------------------------------
# Ghidra's 68000 SLEIGH spec treats A-line opcodes as illegal instructions
# and stops disassembly.  On the Mac, these are Toolbox/OS trap calls that
# fall through to the next instruction.  We fix this by:
#   1. Scanning for 2-byte undefined regions whose value is $A000-$AFFF
#   2. Labeling them with the trap name
#   3. Forcing disassembly to resume at addr+2 (fall-through)
# This lets Ghidra's auto-analyzer follow control flow past A-traps.

try:
    # Import trap names from companion module (works in both Jython and CPython)
    import imp
    _traps_path = os.path.join(script_dir, "mac_traps.py")
    _traps_mod  = imp.load_source("mac_traps", _traps_path)
    _trap_name  = _traps_mod.trap_mnemonic

    _listing = currentProgram.getListing()
    atrap_count = 0
    addr = rom_start
    while addr is not None and addr.compareTo(rom_end) < 0:
        cu = _listing.getCodeUnitAt(addr)
        if cu is None:
            addr = addr.add(2)
            continue

        # Only process undefined/data units that are 2 bytes
        if isinstance(cu, Instruction):
            addr = cu.getAddress().add(cu.getLength())
            continue

        length = cu.getLength()
        if length == 2:
            try:
                w = (getByte(addr) & 0xFF) << 8 | (getByte(addr.add(1)) & 0xFF)
                if 0xA000 <= w <= 0xAFFF:
                    name = _trap_name(w)
                    if name:
                        try:
                            createLabel(addr, name, True, SourceType.USER_DEFINED)
                        except Exception:
                            pass  # label may already exist
                        setEOLComment(addr, "A-Trap $%04X %s" % (w, name))
                    # Force disassembly to continue past the A-trap
                    next_addr = addr.add(2)
                    try:
                        resume = DisassembleCommand(next_addr, None, True)
                        resume.applyTo(currentProgram, monitor)
                    except Exception:
                        pass
                    atrap_count += 1
            except Exception:
                pass

        addr = addr.add(length if length > 0 else 2)

    print("  labeled %d A-line traps" % atrap_count)
except Exception as e:
    print("warn: A-line trap pass: %s" % e)

# ---------------------------------------------------------------------------
# 5. Create ResetEntry function
# ---------------------------------------------------------------------------
try:
    cmd = DisassembleCommand(reset_entry, None, True)
    cmd.applyTo(currentProgram, monitor)
    fn = getFunctionAt(reset_entry)
    if fn is None:
        createFunction(reset_entry, "ResetEntry")
    else:
        fn.setName("ResetEntry", SourceType.USER_DEFINED)
    setPlateComment(reset_entry,
        "========================================================\n"
        " ResetEntry - CPU reset lands here after ROM overlay.\n"
        " Called at cold boot (power-on) and warm reset.\n"
        " Hardware init -> RAM sizing -> ROM checksum -> trap\n"
        " dispatcher install -> driver install -> boot block read.\n"
        "========================================================")
    addEntryPoint(reset_entry)
except Exception as e:
    print("warn: ResetEntry: %s" % e)

# ---------------------------------------------------------------------------
# 6a. Load reference-ROM bytes if this machine borrows another ROM's map.
# ---------------------------------------------------------------------------
reference_bytes = None
verify_skipped = 0
if machine in REFERENCE_ROM_PATHS:
    for candidate in REFERENCE_ROM_PATHS[machine]:
        candidate = os.path.expanduser(candidate)
        if os.path.exists(candidate):
            try:
                rf = open(candidate, "rb")
                reference_bytes = rf.read()
                rf.close()
                print("  verifying borrowed map against %s (%d bytes)" %
                      (os.path.basename(candidate), len(reference_bytes)))
                break
            except Exception as e:
                print("warn: read reference rom %s: %s" % (candidate, e))
    if reference_bytes is None:
        print("warn: no reference ROM found for %s; applying map unverified" % machine)

# ---------------------------------------------------------------------------
# 6b. Import per-machine ROM symbol map (cy384), with optional verification.
# ---------------------------------------------------------------------------
map_name = ROM_MAP_FILES.get(machine)
map_count = 0
if map_name:
    map_file = os.path.join(script_dir, "vendor", "68k-mac-rom-maps",
                            "rom-maps", map_name)
    if os.path.exists(map_file):
        try:
            f = open(map_file, "r")
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split()
                if len(parts) < 2:
                    continue
                name = parts[0]
                # Address can be "0x123" or "123"
                addr_str = parts[1]
                try:
                    if addr_str.startswith("0x") or addr_str.startswith("0X"):
                        off = int(addr_str, 16)
                    else:
                        off = int(addr_str, 16)
                except ValueError:
                    continue
                kind = parts[2] if len(parts) >= 3 else "l"

                # Verification for borrowed maps: compare VERIFY_WINDOW bytes
                # between this ROM (at base+off) and the reference ROM (at
                # off). Skip the symbol if the bytes diverge.
                if reference_bytes is not None:
                    if off + VERIFY_WINDOW > len(reference_bytes):
                        verify_skipped += 1
                        continue
                    try:
                        here_bytes = bytearray(VERIFY_WINDOW)
                        for i in range(VERIFY_WINDOW):
                            here_bytes[i] = getByte(
                                space.getAddress(base + off + i)) & 0xFF
                        ref_slice = bytearray(reference_bytes[off:off + VERIFY_WINDOW])
                        if bytes(here_bytes) != bytes(ref_slice):
                            verify_skipped += 1
                            continue
                    except Exception:
                        verify_skipped += 1
                        continue

                try:
                    addr = space.getAddress(base + off)
                    if kind == "f":
                        fn = getFunctionAt(addr)
                        if fn is None:
                            try:
                                createFunction(addr, name)
                            except Exception:
                                createLabel(addr, name, True, SourceType.IMPORTED)
                        else:
                            fn.setName(name, SourceType.IMPORTED)
                    else:
                        createLabel(addr, name, True, SourceType.IMPORTED)
                    map_count += 1
                except Exception:
                    pass
            f.close()
            print("  imported %d symbols from %s" % (map_count, map_name))
        except Exception as e:
            print("warn: rom-map import: %s" % e)
    else:
        print("warn: rom-map not found: %s" % map_file)
else:
    print("  no rom-map available for %s (brute-force only)" % machine)

# ---------------------------------------------------------------------------
# 7. Re-analyze so new code/symbols propagate references
# ---------------------------------------------------------------------------
try:
    mgr = AutoAnalysisManager.getAnalysisManager(currentProgram)
    mgr.reAnalyzeAll(None)
    mgr.startAnalysis(monitor)
except Exception as e:
    print("warn: reAnalyzeAll: %s" % e)

if reference_bytes is not None:
    print("load_rom.py: %s base=0x%08x reset=0x%08x done "
          "(%d lomem, %d map, %d verify-skipped)" %
          (machine, base, base + 0x2A, lomem_count, map_count, verify_skipped))
else:
    print("load_rom.py: %s base=0x%08x reset=0x%08x done "
          "(%d lomem, %d map)" %
          (machine, base, base + 0x2A, lomem_count, map_count))
