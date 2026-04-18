#!/usr/bin/env python3
"""Bless a System Folder on an HFS disk image.

Sets the blessed folder in the volume's Master Directory Block so the
Mac boots from the specified System Folder.

Requires: hfsutils (hmount/hls/humount)

Usage:
    hfs_bless.py <image> <folder>          # bless by name
    hfs_bless.py <image> --cnid <id>       # bless by CNID
    hfs_bless.py <image> --list            # list root folders with CNIDs
    hfs_bless.py <image> --show            # show currently blessed folder

Examples:
    hfs_bless.py MacPac.img "System 7.5.5"
    hfs_bless.py MacPac.img "System 6.0.8"
    hfs_bless.py MacPac.img --list
"""

import argparse
import re
import struct
import subprocess
import sys


def run_hfs(image, *cmds):
    """Run a sequence of hfsutils commands, return stdout."""
    script = f"hmount {image} 2>&1"
    for c in cmds:
        script += f" && {c} 2>&1"
    script += " && humount 2>&1"
    r = subprocess.run(["bash", "-c", script], capture_output=True, text=True)
    if r.returncode != 0:
        print(f"hfsutils error: {r.stdout}\n{r.stderr}", file=sys.stderr)
        sys.exit(1)
    return r.stdout


def list_root(image):
    """Return list of (cnid, name, is_dir) for root directory entries."""
    out = run_hfs(image, "hls -ila")
    entries = []
    for line in out.splitlines():
        # Format: "  CNID d|f  ... Mon DD  YYYY Name" or "... Mon DD HH:MM Name"
        m = re.match(
            r"^\s*(\d+)\s+(d[i ]?|f[i ]?)\s+.*"
            r"\w{3}\s+\d{1,2}\s+(?:\d{4}|\d{1,2}:\d{2})\s+(.+)$",
            line)
        if m:
            cnid = int(m.group(1))
            is_dir = m.group(2).startswith("d")
            name = m.group(3).strip()
            entries.append((cnid, name, is_dir))
    return entries


def find_hfs_partition(f):
    """Find the HFS partition offset and size (in 512-byte blocks).
    Returns (byte_offset, block_count) or None."""
    f.seek(512)
    sig = f.read(2)
    if sig == b"PM":
        # Apple Partition Map
        entry = 0
        while True:
            offset = (1 + entry) * 512
            f.seek(offset)
            if f.read(2) != b"PM":
                break
            f.seek(offset + 4)
            map_entries = struct.unpack(">I", f.read(4))[0]
            f.seek(offset + 8)
            pblock_start = struct.unpack(">I", f.read(4))[0]
            pblock_count = struct.unpack(">I", f.read(4))[0]
            f.seek(offset + 48)
            ptype = f.read(32).split(b"\x00")[0].decode()
            if ptype == "Apple_HFS":
                return pblock_start * 512, pblock_count
            entry += 1
            if entry >= map_entries:
                break
        return None
    else:
        # No partition map — check for MDB directly at offset 1024
        f.seek(1024)
        mdb_sig = struct.unpack(">H", f.read(2))[0]
        if mdb_sig == 0x4244:
            f.seek(0, 2)
            total = f.tell()
            return 0, total // 512
        return None


def read_mdb_blessed(f, hfs_offset):
    """Read the currently blessed CNID from the MDB."""
    mdb_offset = hfs_offset + 1024
    f.seek(mdb_offset + 92)
    return struct.unpack(">I", f.read(4))[0]


def write_mdb_blessed(f, hfs_offset, hfs_blocks, new_cnid):
    """Write the blessed CNID to both primary and alternate MDB."""
    # Primary MDB
    mdb_offset = hfs_offset + 1024
    f.seek(mdb_offset)
    sig = struct.unpack(">H", f.read(2))[0]
    if sig != 0x4244:
        print(f"Error: bad MDB signature 0x{sig:04X}", file=sys.stderr)
        sys.exit(1)

    f.seek(mdb_offset + 92)
    old_cnid = struct.unpack(">I", f.read(4))[0]
    f.seek(mdb_offset + 92)
    f.write(struct.pack(">I", new_cnid))

    # Alternate MDB (last 1024 bytes of partition)
    alt_mdb_offset = hfs_offset + (hfs_blocks * 512) - 1024
    f.seek(alt_mdb_offset)
    alt_sig = struct.unpack(">H", f.read(2))[0]
    if alt_sig == 0x4244:
        f.seek(alt_mdb_offset + 92)
        f.write(struct.pack(">I", new_cnid))
    else:
        print("Warning: alternate MDB not found, only primary updated",
              file=sys.stderr)

    return old_cnid


def main():
    parser = argparse.ArgumentParser(
        description="Bless a System Folder on an HFS disk image")
    parser.add_argument("image", help="Path to HFS disk image")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("folder", nargs="?", help="System Folder name to bless")
    group.add_argument("--cnid", type=int, help="Bless folder by CNID directly")
    group.add_argument("--list", action="store_true",
                       help="List root directories with CNIDs")
    group.add_argument("--show", action="store_true",
                       help="Show currently blessed folder")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would change without writing")
    args = parser.parse_args()

    if args.list:
        entries = list_root(args.image)
        print(f"{'CNID':>8}  {'Name'}")
        print(f"{'----':>8}  {'----'}")
        for cnid, name, is_dir in entries:
            marker = " *" if is_dir else ""
            print(f"{cnid:>8}  {name}{marker}")
        return

    entries = list_root(args.image)
    cnid_to_name = {cnid: name for cnid, name, is_dir in entries if is_dir}

    with open(args.image, "r+b" if not (args.show or args.dry_run) else "rb") as f:
        part = find_hfs_partition(f)
        if part is None:
            print("Error: no HFS partition found", file=sys.stderr)
            sys.exit(1)
        hfs_offset, hfs_blocks = part

        current_cnid = read_mdb_blessed(f, hfs_offset)
        current_name = cnid_to_name.get(current_cnid, "???")

        if args.show:
            print(f"Blessed: {current_name} (CNID {current_cnid})")
            return

        # Resolve target CNID
        if args.cnid:
            new_cnid = args.cnid
            new_name = cnid_to_name.get(new_cnid, "???")
        else:
            # Find folder by name
            matches = [(c, n) for c, n, d in entries if d and n == args.folder]
            if not matches:
                print(f"Error: folder \"{args.folder}\" not found in root",
                      file=sys.stderr)
                print("Available folders:", file=sys.stderr)
                for cnid, name, is_dir in entries:
                    if is_dir:
                        print(f"  {name} (CNID {cnid})", file=sys.stderr)
                sys.exit(1)
            new_cnid, new_name = matches[0]

        if new_cnid == current_cnid:
            print(f"Already blessed: {new_name} (CNID {new_cnid})")
            return

        if args.dry_run:
            print(f"Would change: {current_name} (CNID {current_cnid})"
                  f" -> {new_name} (CNID {new_cnid})")
            return

        old_cnid = write_mdb_blessed(f, hfs_offset, hfs_blocks, new_cnid)

    print(f"Blessed: {current_name} (CNID {old_cnid})"
          f" -> {new_name} (CNID {new_cnid})")


if __name__ == "__main__":
    main()
