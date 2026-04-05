#!/usr/bin/env python3
"""
macbin_to_extfs.py - Convert a MacBinary file to ExtFS (BasiliskII) layout.

ExtFS stores:
  - data fork at <dir>/<filename>
  - resource fork at <dir>/.rsrc/<filename>
  - Finder info at <dir>/.finf/<filename> (32 bytes: FInfo + FXInfo)

MacBinary II format:
  [0] header (128 bytes)
    [1] filename length
    [2..64] filename (Pascal string, padded)
    [65..68] file type (4 chars)
    [69..72] creator (4 chars)
    [73] Finder flags (high byte)
    [74] 0
    [75..76] vertical position (big-endian)
    [77..78] horizontal position
    [79..80] folder id
    [81] protected flag
    [82] 0
    [83..86] data fork length (big-endian)
    [87..90] resource fork length (big-endian)
    ...
  [128] data fork (padded to 128-byte boundary)
  [128+dlen] resource fork (padded to 128-byte boundary)

Usage:
  macbin_to_extfs.py <MacBinary file> <extfs dir> [output filename]
"""
import sys
import os
import struct


def convert(macbin_path, extfs_dir, out_name=None):
    with open(macbin_path, 'rb') as f:
        data = f.read()

    if len(data) < 128:
        print(f"ERROR: {macbin_path} is too small to be MacBinary")
        return 1

    # Parse header
    name_len = data[1]
    name = data[2:2 + name_len].decode('macroman', errors='replace')
    file_type = data[65:69]
    creator = data[69:73]
    flags_hi = data[73]
    flags_lo = data[101]  # MacBinary II lower flags
    data_len = struct.unpack('>I', data[83:87])[0]
    rsrc_len = struct.unpack('>I', data[87:91])[0]

    if out_name is None:
        out_name = name

    print(f"File: {name!r} type={file_type!r} creator={creator!r}")
    print(f"  data fork: {data_len} bytes, rsrc fork: {rsrc_len} bytes")

    # Data fork starts at 128, padded to 128-byte boundary
    data_start = 128
    data_end = data_start + data_len
    data_padded = (data_len + 127) & ~127
    rsrc_start = data_start + data_padded
    rsrc_end = rsrc_start + rsrc_len

    data_fork = data[data_start:data_end]
    rsrc_fork = data[rsrc_start:rsrc_end]

    # Write data fork
    os.makedirs(extfs_dir, exist_ok=True)
    data_path = os.path.join(extfs_dir, out_name)
    with open(data_path, 'wb') as f:
        f.write(data_fork)
    print(f"  -> {data_path} ({len(data_fork)} bytes)")

    # Write resource fork
    rsrc_dir = os.path.join(extfs_dir, '.rsrc')
    os.makedirs(rsrc_dir, exist_ok=True)
    rsrc_path = os.path.join(rsrc_dir, out_name)
    with open(rsrc_path, 'wb') as f:
        f.write(rsrc_fork)
    print(f"  -> {rsrc_path} ({len(rsrc_fork)} bytes)")

    # Write Finder info (32 bytes: 16 FInfo + 16 FXInfo)
    # FInfo layout: type(4) creator(4) flags(2) location(4) fldr(2)
    finfo = bytearray(32)
    finfo[0:4] = file_type
    finfo[4:8] = creator
    finfo[8] = flags_hi
    finfo[9] = flags_lo
    # location: data[75:77] vert, data[77:79] horiz -> FInfo location (4 bytes: vert, horiz)
    finfo[10:12] = data[75:77]
    finfo[12:14] = data[77:79]
    # fldr: data[79:81]
    finfo[14:16] = data[79:81]
    # FXInfo zeroed

    finf_dir = os.path.join(extfs_dir, '.finf')
    os.makedirs(finf_dir, exist_ok=True)
    finf_path = os.path.join(finf_dir, out_name)
    with open(finf_path, 'wb') as f:
        f.write(finfo)
    print(f"  -> {finf_path} (32 bytes FInfo)")

    return 0


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    macbin = sys.argv[1]
    extfs = sys.argv[2]
    out_name = sys.argv[3] if len(sys.argv) > 3 else None
    sys.exit(convert(macbin, extfs, out_name))
