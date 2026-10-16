#!/usr/bin/env python3
"""ndif.py — Disk Copy 6 (NDIF) image to a raw disk image.

Disk Copy 4.2 images are a header plus raw sectors; Disk Copy 6's NDIF
images ("rohd"/"dImg" files, often named .img or inside .smi self-mounting
images) keep the sectors in the data fork in chunks, described by the
resource fork's 'bcem' 128: zero runs left out, the rest stored raw or
ADC-compressed. hfsutils can't read them; this writes the raw image it can.

bcem 128: a 128-byte header (version, Str63 name, sector count at 68, chunk
count at 124), then one 12-byte entry per chunk: start sector (3 bytes),
type (1), offset in the data fork (4), length (4). A chunk ends where the
next starts. Types: 0x00 zeros, 0x02 raw, 0x83 ADC, 0xF0 comment, 0xFF end.

Usage: ndif.py IMAGE OUT.img       (IMAGE: MacBinary, or a data fork with
                                    IMAGE.rsrc / ._IMAGE AppleDouble beside it)
"""

import os
import struct
import sys

SECTOR = 512


def adc_decompress(src, out_len):
    """Apple Data Compression: literal runs and back references."""
    out = bytearray()
    i = 0
    while i < len(src) and len(out) < out_len:
        b = src[i]
        if b & 0x80:                      # 1-128 literal bytes
            n = (b & 0x7F) + 1
            out += src[i + 1:i + 1 + n]
            i += 1 + n
            continue
        if b & 0x40:                      # 4-67 bytes, 16-bit offset
            n = (b & 0x3F) + 4
            off = (src[i + 1] << 8 | src[i + 2]) + 1
            i += 3
        else:                             # 3-18 bytes, 10-bit offset
            n = ((b & 0x3F) >> 2) + 3
            off = ((b & 0x03) << 8 | src[i + 1]) + 1
            i += 2
        start = len(out) - off
        if off >= n:
            out += out[start:start + n]
        else:                             # overlapping: a repeating pattern
            for k in range(n):
                out.append(out[start + k])
    return bytes(out[:out_len])


def resource(rsrc, rtype, rid):
    """Resource rtype/rid from a raw resource fork, or None."""
    if len(rsrc) < 16:
        return None
    doff, moff, _, mlen = struct.unpack(">IIII", rsrc[:16])
    m = rsrc[moff:moff + mlen]
    tlo = struct.unpack(">H", m[24:26])[0]
    ntypes = struct.unpack(">H", m[tlo:tlo + 2])[0] + 1
    for t in range(ntypes):
        typ, count, ref = struct.unpack(">4sHH", m[tlo + 2 + t * 8:tlo + 10 + t * 8])
        if typ != rtype:
            continue
        for r in range(count + 1):
            e = tlo + ref + r * 12
            if struct.unpack(">h", m[e:e + 2])[0] == rid:
                off = doff + (struct.unpack(">I", m[e + 4:e + 8])[0] & 0xFFFFFF)
                n = struct.unpack(">I", rsrc[off:off + 4])[0]
                return rsrc[off + 4:off + 4 + n]
    return None


def to_raw(data, rsrc):
    """The raw disk image held by an NDIF data fork + resource fork; a data
    fork without a 'bcem' is taken to be raw already."""
    bcem = resource(rsrc, b"bcem", 128) if rsrc else None
    if bcem is None:
        return data
    sectors = struct.unpack(">I", bcem[68:72])[0]
    nchunks = struct.unpack(">I", bcem[124:128])[0]
    entries = []
    for k in range(nchunks):
        e = bcem[128 + k * 12:140 + k * 12]
        entries.append((int.from_bytes(e[0:3], "big"), e[3],
                        struct.unpack(">I", e[4:8])[0], struct.unpack(">I", e[8:12])[0]))
    out = bytearray(sectors * SECTOR)
    for (start, kind, off, length), nxt in zip(entries, entries[1:] + [(sectors, 0xFF, 0, 0)]):
        span = (nxt[0] - start) * SECTOR
        if kind in (0x00, 0xF0, 0xFF):
            continue
        if kind == 0x02:
            chunk = data[off:off + length]
        elif kind == 0x83:
            chunk = adc_decompress(data[off:off + length], span)
        else:
            raise ValueError(f"NDIF chunk type {kind:#x} at sector {start} not supported")
        out[start * SECTOR:start * SECTOR + len(chunk)] = chunk
    return bytes(out)


def forks(path):
    """(data, rsrc) of path: MacBinary, or data fork + IMAGE.rsrc / ._IMAGE."""
    with open(path, "rb") as f:
        blob = f.read()
    if len(blob) >= 128 and blob[0] == 0 and 0 < blob[1] < 64 and blob[74] == 0:
        dlen, rlen = struct.unpack(">II", blob[83:91])
        if 128 + dlen + rlen <= len(blob) + 256:
            dstart = 128
            rstart = dstart + (dlen + 127) // 128 * 128
            return blob[dstart:dstart + dlen], blob[rstart:rstart + rlen]
    head, tail = os.path.split(path)
    for side in (path + ".rsrc", os.path.join(head, "._" + tail)):
        if os.path.exists(side):
            with open(side, "rb") as f:
                return blob, appledouble_rsrc(f.read())
    return blob, b""


def appledouble_rsrc(ad):
    if ad[:4] not in (b"\x00\x05\x16\x07", b"\x00\x05\x16\x00"):
        return ad                          # a bare resource fork
    for k in range(struct.unpack(">H", ad[24:26])[0]):
        eid, off, n = struct.unpack(">III", ad[26 + k * 12:38 + k * 12])
        if eid == 2:
            return ad[off:off + n]
    return b""


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    raw = to_raw(*forks(sys.argv[1]))
    with open(sys.argv[2], "wb") as f:
        f.write(raw)
    print(f"{sys.argv[2]}: {len(raw) // SECTOR} sectors")


if __name__ == "__main__":
    main()
