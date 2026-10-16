#!/usr/bin/env python3
"""fat_cfm.py — merge a CFM-68K and a PowerPC build of a Code Fragment
Manager library or application into one fat file.

Installers often install only the build for the machine they run on (MacPython's
PythonCore, say). A fat file holds both: the data fork is the two code
fragments one after the other, and the 'cfrg' 0 resource lists each with its
architecture and its offset and length in the data fork. The Code Fragment
Manager on each machine picks its own.

  fat_cfm.py A.bin B.bin OUT.bin      (MacBinary in and out; A's other
                                       resources and Finder info are kept)
"""

import struct
import sys

ARCHS = (b"m68k", b"pwpc")


def macbinary(blob):
    """(header, data, rsrc) of a MacBinary file."""
    dlen, rlen = struct.unpack(">II", blob[83:91])
    data = blob[128:128 + dlen]
    rstart = 128 + (dlen + 127) // 128 * 128
    return blob[:128], data, blob[rstart:rstart + rlen]


def read_rsrc(fork):
    """[(type, id, attrs, name or None, data)] in fork order."""
    if len(fork) < 16:
        return []
    doff, moff, _, mlen = struct.unpack(">IIII", fork[:16])
    m = fork[moff:moff + mlen]
    tlo, nlo = struct.unpack(">HH", m[24:28])
    out = []
    for t in range(struct.unpack(">H", m[tlo:tlo + 2])[0] + 1):
        typ, count, ref = struct.unpack(">4sHH", m[tlo + 2 + t * 8:tlo + 10 + t * 8])
        for r in range(count + 1):
            e = tlo + ref + r * 12
            rid, name_off, attr_off = struct.unpack(">hHI", m[e:e + 8])
            off = doff + (attr_off & 0xFFFFFF)
            n = struct.unpack(">I", fork[off:off + 4])[0]
            name = None
            if name_off != 0xFFFF:
                p = nlo + name_off
                name = m[p + 1:p + 1 + m[p]]
            out.append((typ, rid, attr_off >> 24, name, fork[off + 4:off + 4 + n]))
    return out


def write_rsrc(resources):
    """A resource fork holding resources (as read_rsrc returns them)."""
    data, names = b"", b""
    by_type = {}
    for typ, rid, attrs, name, payload in resources:
        by_type.setdefault(typ, []).append((rid, attrs, name, len(data)))
        data += struct.pack(">I", len(payload)) + payload
    types = list(by_type)
    type_list = struct.pack(">H", len(types) - 1)
    refs = b""
    ref_base = 2 + 8 * len(types)
    for typ in types:
        entries = by_type[typ]
        type_list += struct.pack(">4sHH", typ, len(entries) - 1, ref_base + len(refs))
        for rid, attrs, name, off in entries:
            name_off = 0xFFFF
            if name is not None:
                name_off = len(names)
                names += bytes([len(name)]) + name
            refs += struct.pack(">hHII", rid, name_off, (attrs << 24) | off, 0)
    map_body = type_list + refs
    header_len = 16 + 4 + 2 + 2 + 4           # copy of header, handle, ref num, attrs, offsets
    tlo, nlo = header_len, header_len + len(map_body)
    doff = 256
    mlen = nlo + len(names)
    head = struct.pack(">IIII", doff, doff + len(data), len(data), mlen)
    rmap = head + bytes(4 + 2) + struct.pack(">HHH", 0, tlo, nlo) + map_body + names
    return head + bytes(doff - 16) + data + rmap


def members(cfrg):
    """The member entries of a 'cfrg' resource: (offset in cfrg, bytes)."""
    count = struct.unpack(">H", cfrg[30:32])[0]
    pos, out = 32, []
    for _ in range(count):
        size = struct.unpack(">H", cfrg[pos + 40:pos + 42])[0]
        out.append(bytearray(cfrg[pos:pos + size]))
        pos += size
    return out


def merge(a, b):
    """Fat MacBinary from two single-architecture ones."""
    ha, da, ra = macbinary(a)
    _, db, rb = macbinary(b)
    res_a, res_b = read_rsrc(ra), read_rsrc(rb)
    cfrg_a = next(r[4] for r in res_a if r[0] == b"cfrg" and r[1] == 0)
    cfrg_b = next(r[4] for r in res_b if r[0] == b"cfrg" and r[1] == 0)
    ma, mb = members(cfrg_a), members(cfrg_b)
    if {bytes(m[0:4]) for m in ma} & {bytes(m[0:4]) for m in mb}:
        raise SystemExit("both files have a fragment for the same architecture")
    gap = (-len(da)) % 16
    base_b = len(da) + gap
    for m, base, fork in [(m, 0, da) for m in ma] + [(m, base_b, db) for m in mb]:
        if m[23] != 1:                 # where: kDataForkCFragLocator
            raise SystemExit("only data-fork fragments can be merged")
        off, length = struct.unpack(">II", m[24:32])
        struct.pack_into(">II", m, 24, base + off, length or len(fork) - off)
    cfrg = bytearray(cfrg_a[:32])
    struct.pack_into(">H", cfrg, 30, len(ma) + len(mb))
    cfrg = bytes(cfrg) + b"".join(bytes(m) for m in ma + mb)
    resources = [(t, i, at, n, cfrg if (t, i) == (b"cfrg", 0) else d) for t, i, at, n, d in res_a]
    data = da + bytes(gap) + db
    rsrc = write_rsrc(resources)
    header = bytearray(ha)
    struct.pack_into(">II", header, 83, len(data), len(rsrc))
    header[124:126] = struct.pack(">H", crc(bytes(header[:124])))

    def pad(x):
        return x + bytes((-len(x)) % 128)
    return bytes(header) + pad(data) + pad(rsrc)


def crc(data):
    c = 0
    for byte in data:
        c ^= byte << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
            c &= 0xFFFF
    return c


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        a = f.read()
    with open(sys.argv[2], "rb") as f:
        b = f.read()
    with open(sys.argv[3], "wb") as f:
        f.write(merge(a, b))


if __name__ == "__main__":
    main()
