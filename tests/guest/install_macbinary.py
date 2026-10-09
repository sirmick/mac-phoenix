#!/usr/bin/env python3
"""
Unpack a MacBinary file into a shared (ExtFS) folder as a Mac file.

The data fork becomes <dir>/<name>, the resource fork <dir>/.rsrc/<name>
and the Finder info <dir>/.finf/<name> (FInfo + FXInfo, 32 bytes): the
layout Basilisk II's ExtFS keeps, which Executor's host folders read too.

Usage:
    install_macbinary.py <file.bin> <extfs_dir> <name>
"""
import os
import struct
import sys


def install(path, extfs_dir, name):
    blob = open(path, 'rb').read()
    if len(blob) < 128 or blob[0] != 0 or blob[74] != 0:
        sys.exit(f"{path}: not MacBinary")
    dlen, rlen = struct.unpack_from('>II', blob, 83)
    data_at = 128
    rsrc_at = data_at + ((dlen + 127) & ~127)
    data = blob[data_at:data_at + dlen]
    rsrc = blob[rsrc_at:rsrc_at + rlen]

    finfo = bytearray(32)
    finfo[0:8] = blob[65:73]            # type, creator
    finfo[8] = blob[73]                 # Finder flags, high byte
    finfo[9] = blob[101]                # low byte (MacBinary II)
    finfo[10:16] = blob[75:81]          # location, folder

    for sub, content in (('', data), ('.rsrc', rsrc), ('.finf', bytes(finfo))):
        d = os.path.join(extfs_dir, sub)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, name), 'wb') as f:
            f.write(content)
    print(f"  -> {extfs_dir}/{name}: {len(data)} data, {len(rsrc)} resource bytes, "
          f"{bytes(finfo[0:4]).decode('mac_roman')}/{bytes(finfo[4:8]).decode('mac_roman')}")


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    install(*sys.argv[1:])
