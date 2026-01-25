#!/usr/bin/env python3
"""
Simple ROM disassembler using objdump
Usage: ./disassemble_rom.py <rom_path> <address> [num_instructions]
"""

import subprocess
import sys
import re

def disassemble_rom(rom_path, address, num_instructions=20, rom_base=0x02000000):
    """Disassemble ROM starting at address"""

    # Calculate file offset
    if isinstance(address, str):
        address = int(address, 16) if address.startswith('0x') else int(address)

    # Add some padding before/after for context
    start_addr = max(rom_base, address - 0x20)
    end_addr = address + (num_instructions * 4) + 0x20

    cmd = [
        'm68k-linux-gnu-objdump',
        '-D',
        '-b', 'binary',
        '-m', 'm68k:68040',
        f'--adjust-vma={hex(rom_base)}',
        f'--start-address={hex(start_addr)}',
        f'--stop-address={hex(end_addr)}',
        rom_path
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)

        # Parse and display
        print(f"\nDisassembly around {address:08x}:")
        print("=" * 80)

        for line in result.stdout.splitlines():
            # Match instruction lines: "  address: opcode  instruction"
            match = re.match(r'\s*([0-9a-f]+):\s+([0-9a-f\s]+)\s+(.+)$', line)
            if match:
                addr = int(match.group(1), 16)
                opcode = match.group(2).strip()
                instr = match.group(3).strip()

                # Highlight target address
                marker = ">>>" if addr == address else "   "
                print(f"{marker} {addr:08x}: {instr}")

        print("=" * 80)

    except FileNotFoundError:
        print("Error: m68k-linux-gnu-objdump not found. Install with:")
        print("  sudo apt-get install binutils-m68k-linux-gnu")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Error running objdump: {e}")
        print(f"stderr: {e.stderr}")
        sys.exit(1)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    rom_path = sys.argv[1]
    address = sys.argv[2]
    num_instructions = int(sys.argv[3]) if len(sys.argv) > 3 else 20

    disassemble_rom(rom_path, address, num_instructions)
