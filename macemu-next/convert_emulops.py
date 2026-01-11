#!/usr/bin/env python3
"""
Convert all traditional EmulOp emissions to use the new abstraction layer.
This script replaces:
    *wp++ = htons(M68K_EMUL_OP_XXX);
with:
    emit_emulop(&wp, M68K_EMUL_OP_XXX);
"""

import re
import sys

def convert_file(filename):
    with open(filename, 'r') as f:
        content = f.read()

    # Pattern to match: *wp++ = htons(M68K_EMUL_OP_xxx);
    pattern = r'\*wp\+\+ = htons\((M68K_EMUL_OP_[A-Z0-9_]+)\);'

    # Count replacements
    matches = re.findall(pattern, content)
    print(f"Found {len(matches)} EmulOp emissions to convert:")
    for match in sorted(set(matches)):
        count = matches.count(match)
        print(f"  {match}: {count} occurrence(s)")

    # Replace with: emit_emulop(&wp, M68K_EMUL_OP_xxx);
    replacement = r'emit_emulop(&wp, \1);'
    new_content = re.sub(pattern, replacement, content)

    # Write back
    with open(filename, 'w') as f:
        f.write(new_content)

    print(f"\nConverted {len(matches)} EmulOp emissions to use abstraction layer")
    return len(matches)

if __name__ == '__main__':
    filename = '/home/mick/macemu-dual-cpu/macemu-next/src/core/rom_patches.cpp'
    count = convert_file(filename)
    sys.exit(0 if count > 0 else 1)