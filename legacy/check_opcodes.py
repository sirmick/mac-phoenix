#!/usr/bin/env python3
import subprocess
import os

# Check MOVEQ encoding (0x70xx with bit 8=0)
print("MOVEQ instructions (0x70xx):")
for reg in range(8):
    opcode = 0x7000 | (reg << 9) | 0x03  # MOVEQ #3, Dn
    with open('/tmp/op.bin', 'wb') as f:
        f.write(bytes([opcode >> 8, opcode & 0xFF]))
    result = subprocess.run(['m68k-linux-gnu-objdump', '-D', '-b', 'binary', '-m', 'm68k', '/tmp/op.bin'],
                          capture_output=True, text=True)
    for line in result.stdout.split('\n'):
        if '0:' in line:
            print(f"  0x{opcode:04x}: {line.split()[2:]}")

print("\n0x71xx range (bit 8=1, should be invalid):")
for val in [0x00, 0x01, 0x02, 0x03, 0x04, 0x10, 0x20, 0x30]:
    opcode = 0x7100 | val
    with open('/tmp/op.bin', 'wb') as f:
        f.write(bytes([opcode >> 8, opcode & 0xFF]))
    result = subprocess.run(['m68k-linux-gnu-objdump', '-D', '-b', 'binary', '-m', 'm68k', '/tmp/op.bin'],
                          capture_output=True, text=True)
    for line in result.stdout.split('\n'):
        if '0:' in line:
            parts = line.split()
            if len(parts) >= 3:
                print(f"  0x{opcode:04x}: {' '.join(parts[2:])}")