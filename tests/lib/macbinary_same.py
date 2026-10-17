#!/usr/bin/env python3
"""macbinary_same.py A.bin B.bin — are two MacBinary files the same build?

Compares what a build makes, not when: type and creator, the data fork
(a PEF container's timestamp, bytes 16-19, left out) and the resources by
type, ID, attributes, name and contents (the resource map's handle fields
and the header's reserved bytes are scratch the Resource Manager writes).
Exit 0 if the same, 1 with what differs.
"""
import struct
import sys


def resources(rsrc):
    if len(rsrc) < 16:
        return {}
    data_off, map_off = struct.unpack(">II", rsrc[:8])
    m = rsrc[map_off:]
    type_list, name_list = struct.unpack(">HH", m[24:28])
    out = {}
    for t in range(struct.unpack(">H", m[28:30])[0] + 1):
        e = type_list + 2 + t * 8
        rtype = m[e:e + 4]
        count, ref_list = struct.unpack(">HH", m[e + 4:e + 8])
        for k in range(count + 1):
            r = type_list + ref_list + k * 12
            rid, name_off = struct.unpack(">hH", m[r:r + 4])
            attrs = m[r + 4]
            off = data_off + int.from_bytes(m[r + 5:r + 8], "big")
            length = struct.unpack(">I", rsrc[off:off + 4])[0]
            name = b"" if name_off == 0xFFFF else m[name_list + name_off + 1:][:m[name_list + name_off]]
            out[(rtype, rid)] = (attrs, name, rsrc[off + 4:off + 4 + length])
    return out


def build(path):
    b = open(path, "rb").read()
    dlen, rlen = struct.unpack(">II", b[83:91])
    data = bytearray(b[128:128 + dlen])
    if data[:8] == b"Joy!peff":
        data[16:20] = bytes(4)
    rsrc = b[128 + ((dlen + 127) & ~127):][:rlen]
    return b[65:73], bytes(data), resources(rsrc)


def main():
    a, b = build(sys.argv[1]), build(sys.argv[2])
    diffs = []
    if a[0] != b[0]:
        diffs.append(f"type/creator {a[0]!r} {b[0]!r}")
    if a[1] != b[1]:
        diffs.append(f"data fork ({len(a[1])} / {len(b[1])} bytes)")
    for key in sorted(set(a[2]) | set(b[2])):
        if a[2].get(key) != b[2].get(key):
            diffs.append(f"resource {key[0].decode('mac_roman')} {key[1]}")
    if diffs:
        print("differ: " + ", ".join(diffs))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
