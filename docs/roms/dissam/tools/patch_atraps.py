#!/usr/bin/env python3
"""Post-process a Ghidra .lst file to replace 2-byte DATA segments that
contain A-line trap opcodes ($A000-$AFFF) with proper trap mnemonics.

Usage:
    python3 patch_atraps.py <rom_file> <lst_file> <rom_base_hex>

Example:
    python3 patch_atraps.py ~/roms/MacSE.ROM se/rom.lst 0x400000
"""
import sys
import os
import re
import struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mac_traps import trap_mnemonic

def main():
    if len(sys.argv) != 4:
        print("Usage: patch_atraps.py <rom_file> <lst_file> <rom_base_hex>")
        sys.exit(1)

    rom_path = sys.argv[1]
    lst_path = sys.argv[2]
    rom_base = int(sys.argv[3], 16)

    with open(rom_path, "rb") as f:
        rom = f.read()

    with open(lst_path, "r") as f:
        lines = f.readlines()

    # Pattern: "00401a0..00401a1  DATA  (2 bytes)" or similar
    data2_re = re.compile(
        r'^([0-9a-f]{8})\.\.([0-9a-f]{8})\s+DATA\s+\(2 bytes\)(.*)')

    patched = 0
    out = []
    for line in lines:
        m = data2_re.match(line)
        if m:
            addr_start = int(m.group(1), 16)
            rom_offset = addr_start - rom_base
            if 0 <= rom_offset < len(rom) - 1:
                w = struct.unpack(">H", rom[rom_offset:rom_offset+2])[0]
                name = trap_mnemonic(w)
                if name:
                    rest = m.group(3).strip()
                    new_line = "%08x  %s" % (addr_start, name)
                    if rest:
                        new_line += "   %s" % rest
                    out.append(new_line + "\n")
                    patched += 1
                    continue
        out.append(line)

    with open(lst_path, "w") as f:
        f.writelines(out)

    print("patch_atraps: patched %d A-line traps in %s" % (patched, lst_path))

if __name__ == "__main__":
    main()
