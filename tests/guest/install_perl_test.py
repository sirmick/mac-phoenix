#!/usr/bin/env python3
"""
Install a Perl script into an ExtFS shared folder with MacPerl type/creator.

Sets Finder info: type='TEXT', creator='McPL' so Finder launches it with MacPerl.

Usage:
    install_perl_test.py <script.pl> <extfs_dir> [output_name]
"""
import os
import sys
import struct


def install(script_path, extfs_dir, out_name=None):
    if out_name is None:
        out_name = os.path.basename(script_path)

    os.makedirs(extfs_dir, exist_ok=True)

    # Copy script as data fork — convert Unix LF to Mac CR line endings
    with open(script_path, 'rb') as f:
        data = f.read()
    data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r')

    dst = os.path.join(extfs_dir, out_name)
    with open(dst, 'wb') as f:
        f.write(data)
    print(f"  -> {dst} ({len(data)} bytes, CR line endings)")

    # Finder info: type=TEXT creator=McPL
    finfo = bytearray(32)
    finfo[0:4] = b'TEXT'
    finfo[4:8] = b'McPL'
    finf_dir = os.path.join(extfs_dir, '.finf')
    os.makedirs(finf_dir, exist_ok=True)
    finf_path = os.path.join(finf_dir, out_name)
    with open(finf_path, 'wb') as f:
        f.write(finfo)
    print(f"  -> {finf_path} (32 bytes FInfo TEXT/McPL)")

    return 0


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    sys.exit(install(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None))
