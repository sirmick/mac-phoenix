#!/usr/bin/env python3
"""Look up a PC address in the IIci ROM disassembly."""

import sys
import os
import re

DISASM = os.path.join(os.path.dirname(__file__), "..", "docs", "iici_rom_disasm.txt")
ROM_BASE = 0x02000000
CONTEXT = 5  # lines before/after

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <address> [context_lines]")
        print("  address: hex PC value (e.g. 0x0206B500, 6B500, $6B500)")
        sys.exit(1)

    raw = sys.argv[1].lstrip("$").replace("0x", "")
    addr = int(raw, 16)
    ctx = int(sys.argv[2]) if len(sys.argv) > 2 else CONTEXT

    # Normalize to ROM offset
    if addr >= ROM_BASE:
        addr -= ROM_BASE

    offset_re = re.compile(r"^([0-9a-f]{8})\s*:")
    stripped_re = re.compile(r"^\; \[(\d+) disassembly lines stripped")
    section_re = re.compile(r"^; =+ (.+?) =+")

    lines = open(DISASM).readlines()

    best_idx = None
    best_off = None
    current_section = None
    section_at_best = None
    in_stripped = None  # (section_name, count, start_off, end_off)

    for i, line in enumerate(lines):
        sm = section_re.match(line)
        if sm:
            current_section = sm.group(1).strip()

        m = offset_re.match(line)
        if m:
            off = int(m.group(1), 16)
            if off <= addr:
                best_idx = i
                best_off = off
                section_at_best = current_section
                in_stripped = None
            if off > addr:
                break

        st = stripped_re.match(line)
        if st and best_off is not None and best_off < addr:
            # The address falls in a stripped region
            # Find the next real offset after the stripped marker
            for j in range(i + 1, min(i + 10, len(lines))):
                m2 = offset_re.match(lines[j])
                if m2:
                    next_off = int(m2.group(1), 16)
                    if addr < next_off:
                        in_stripped = (current_section, int(st.group(1)))
                    break

    if best_idx is None:
        print(f"Address ${addr:05X} not found in disassembly.")
        sys.exit(1)

    if in_stripped:
        name, count = in_stripped
        print(f"${addr:05X}  — inside stripped region")
        print(f"  Section: {name}")
        print(f"  ({count} lines were stripped — not relevant to emulator boot)")
        sys.exit(0)

    # Print context
    start = max(0, best_idx - ctx)
    end = min(len(lines), best_idx + ctx + 1)

    if section_at_best:
        print(f"Section: {section_at_best}")
        print()

    for i in range(start, end):
        marker = ">>>" if i == best_idx else "   "
        print(f"{marker} {lines[i].rstrip()}")


if __name__ == "__main__":
    main()
