#!/usr/bin/env python3
"""qtmovie.py — QuickTime movie files with known content, for QTTest.

Written from the published QuickTime File Format (Apple's "QuickTime File
Format" specification: atoms, the movie/track/media headers, sample
tables, sample descriptions) and the Animation ('rle ') codec's
documented bitstream. No Apple code.

Every movie is flattened: one 'moov' atom then one 'mdat' atom in the data
fork, the media's data reference a self-reference ('alis' with flag 1).
QuickTime 2.5 and later open such a file from its data fork; it has no
resource fork. The Mac file type ('MooV') is set by QTTest itself (a file
copied to the Host volume arrives as TEXT).

Video ('vide'): 'raw ' at 32 bits (ARGB, rows width*4) or 16 bits
(1-5-5-5 big-endian, rows width*2), and 'rle ' (Animation) at the same
depths, every frame a key frame. Each frame has colour bars, a moving
square and the frame number in blocks, so every frame differs.
Sound ('soun'): 'twos' (16-bit signed big-endian) or 'raw ' (8-bit
offset binary), mono, a sine tone.

The frames' checksums: QTTest draws each frame into a 32-bit GWorld and
takes a CRC-32 of the R, G, B bytes of every pixel, row by row
(frame_crc here); 16-bit pixels widen to 8 bits by repeating the top bits,
as QuickDraw does.

    python3 tests/lib/qtmovie.py OUTDIR          # the movies + manifest.txt
    python3 tests/lib/qtmovie.py --list          # what they are
"""

import math
import os
import struct
import sys
import zlib

# --- atoms ------------------------------------------------------------------


def atom(kind, *payload):
    body = b"".join(payload)
    return struct.pack(">I4s", 8 + len(body), kind.encode("mac_roman")) + body


def full(version=0, flags=0):
    return struct.pack(">I", (version << 24) | flags)


def pstr(s, size=None):
    b = s.encode("mac_roman")
    out = bytes([len(b)]) + b
    if size is not None:
        out = out.ljust(size, b"\0")
    return out


def fixed(x):
    return int(round(x * 65536)) & 0xFFFFFFFF


UNITY = struct.pack(">9I", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)
MAC_TIME = 0xB4F3C700          # 1 Jan 2000 in Mac seconds: fixed, so files are reproducible


def mvhd(time_scale, duration, next_track):
    return atom("mvhd", full(), struct.pack(">IIII", MAC_TIME, MAC_TIME, time_scale, duration),
                struct.pack(">IH", fixed(1.0), 0x0100), b"\0" * 10, UNITY,
                struct.pack(">IIIIIII", 0, 0, 0, 0, 0, 0, next_track))


def tkhd(track_id, duration, width, height, volume):
    return atom("tkhd", full(0, 0x0F), struct.pack(">IIIII", MAC_TIME, MAC_TIME, track_id, 0, duration),
                b"\0" * 8, struct.pack(">hhhH", 0, 0, volume, 0), UNITY,
                struct.pack(">II", fixed(width), fixed(height)))


def mdhd(time_scale, duration):
    return atom("mdhd", full(), struct.pack(">IIII", MAC_TIME, MAC_TIME, time_scale, duration),
                struct.pack(">HH", 0, 0))


def hdlr(ctype, subtype, name):
    return atom("hdlr", full(), ctype.encode(), subtype.encode(), b"appl",
                struct.pack(">II", 0, 0), pstr(name))


def dinf():
    return atom("dinf", atom("dref", full(), struct.pack(">I", 1), atom("alis", full(0, 1))))


def stbl(stsd_entry, stts, stsc, sample_size, sizes, offsets):
    parts = [atom("stsd", full(), struct.pack(">I", 1), stsd_entry),
             atom("stts", full(), struct.pack(">I", len(stts)), *(struct.pack(">II", c, d) for c, d in stts)),
             atom("stsc", full(), struct.pack(">I", len(stsc)), *(struct.pack(">III", *e) for e in stsc)),
             atom("stsz", full(), struct.pack(">II", sample_size, len(sizes) if sample_size == 0 else sizes),
                  *(struct.pack(">I", s) for s in (sizes if sample_size == 0 else []))),
             atom("stco", full(), struct.pack(">I", len(offsets)), *(struct.pack(">I", o) for o in offsets))]
    return atom("stbl", *parts)


def video_desc(codec, width, height, depth, name):
    return atom(codec, b"\0" * 6, struct.pack(">H", 1),
                struct.pack(">hh4sII", 0, 0, b"appl", 0, 0x200),
                struct.pack(">HHIIIH", width, height, fixed(72), fixed(72), 0, 1),
                pstr(name, 32), struct.pack(">hh", depth, -1))


def sound_desc(codec, channels, bits, rate):
    return atom(codec, b"\0" * 6, struct.pack(">H", 1),
                struct.pack(">hh4sHHhHI", 0, 0, b"\0\0\0\0", channels, bits, 0, 0, fixed(rate) if rate < 65536 else 0))


# --- pictures ----------------------------------------------------------------

BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]

# 3x5 digits, rows top to bottom, bit 2 the left column.
DIGITS = [[0b111, 0b101, 0b101, 0b101, 0b111], [0b010, 0b110, 0b010, 0b010, 0b111],
          [0b111, 0b001, 0b111, 0b100, 0b111], [0b111, 0b001, 0b111, 0b001, 0b111],
          [0b101, 0b101, 0b111, 0b001, 0b001], [0b111, 0b100, 0b111, 0b001, 0b111],
          [0b111, 0b100, 0b111, 0b101, 0b111], [0b111, 0b001, 0b001, 0b001, 0b001],
          [0b111, 0b101, 0b111, 0b101, 0b111], [0b111, 0b101, 0b111, 0b001, 0b111]]


def frame_pixels(width, height, n, frames):
    """Frame n: colour bars, a 1/4-height square moving left to right, the
    frame number (two digits, blocks of 1/24 height) top left. RGB rows."""
    rows = []
    sq = max(2, height // 4)
    sx = (width - sq) * n // max(1, frames - 1)
    sy = (height - sq) // 2
    blk = max(1, height // 24)
    digits = "%02d" % n
    for y in range(height):
        row = []
        for x in range(width):
            c = BARS[x * len(BARS) // width]
            if y >= height * 3 // 4:                       # a grey ramp along the bottom
                g = x * 255 // max(1, width - 1)
                c = (g, g, g)
            if sx <= x < sx + sq and sy <= y < sy + sq:
                c = (128, 64, 32)
            dx, dy = x // blk - 1, y // blk - 1
            if 0 <= dy < 5 and 0 <= dx < 8 and dx % 4 < 3:
                d = DIGITS[int(digits[dx // 4])]
                if d[dy] & (4 >> (dx % 4)):
                    c = (16, 16, 16)
            row.append(c)
        rows.append(row)
    return rows


def to16(c):
    r, g, b = c
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)


def widen16(v):
    """A 1-5-5-5 pixel as QuickDraw widens it to 8 bits a channel."""
    out = []
    for shift in (10, 5, 0):
        f = (v >> shift) & 31
        out.append((f << 3) | (f >> 2))
    return tuple(out)


def frame_crc(rows, depth):
    data = bytearray()
    for row in rows:
        for c in row:
            data += bytes(widen16(to16(c)) if depth == 16 else c)
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def pixel_bytes(c, depth):
    if depth == 16:
        return struct.pack(">H", to16(c))
    return bytes((0,) + tuple(c))       # ARGB, alpha 0 as QuickDraw leaves it


def encode_raw(rows, depth):
    return b"".join(pixel_bytes(c, depth) for row in rows for c in row)


def encode_rle(rows, depth):
    """Animation codec, a key frame: size, header 8 (start line 0, every
    line), then per line a skip byte (1 = none) and codes: n > 0 literal
    pixels, -n a pixel repeated n times, -1 the line's end; 0 ends."""
    out = bytearray(struct.pack(">HHHHH", 0x0008, 0, 0, len(rows), 0))
    for row in rows:
        out.append(1)
        px = [pixel_bytes(c, depth) for c in row]
        i = 0
        while i < len(px):
            run = 1
            while i + run < len(px) and px[i + run] == px[i] and run < 127:
                run += 1
            if run >= 2:
                out += struct.pack(">b", -run) + px[i]
                i += run
                continue
            j = i
            while j < len(px) and j - i < 127 and not (j + 1 < len(px) and px[j + 1] == px[j]):
                j += 1
            if j == i:
                j = i + 1
            out += struct.pack(">b", j - i) + b"".join(px[i:j])
            i = j
        out += struct.pack(">b", -1)
    out.append(0)
    return struct.pack(">I", len(out) + 4) + bytes(out)


# --- movies ------------------------------------------------------------------


class Track:
    pass


def video_track(codec, depth, width, height, frames, time_scale, frame_dur):
    t = Track()
    t.kind = "vide"
    t.samples = []
    t.crcs = []
    for n in range(frames):
        rows = frame_pixels(width, height, n, frames)
        t.samples.append(encode_raw(rows, depth) if codec == "raw " else encode_rle(rows, depth))
        t.crcs.append(frame_crc(rows, depth))
    t.time_scale = time_scale
    t.duration = frames * frame_dur
    t.stts = [(frames, frame_dur)]
    t.desc = video_desc(codec, width, height, depth, "None" if codec == "raw " else "Animation")
    t.width, t.height = width, height
    t.sample_size = 0
    t.handler = ("vide", "Apple Video Media Handler")
    return t


def tone_track(codec, rate, freq, seconds, amplitude=0.5):
    t = Track()
    t.kind = "soun"
    n = int(rate * seconds)
    bits = 16 if codec == "twos" else 8
    if bits == 16:
        data = b"".join(struct.pack(">h", int(round(amplitude * 32767 * math.sin(2 * math.pi * freq * i / rate))))
                        for i in range(n))
    else:
        data = bytes(128 + int(round(amplitude * 127 * math.sin(2 * math.pi * freq * i / rate))) for i in range(n))
    chunk = rate // 2                  # half a second a chunk
    t.samples = [data[i * bits // 8:(i + chunk) * bits // 8] for i in range(0, n, chunk)]
    t.chunk_frames = [len(s) * 8 // bits for s in t.samples]
    t.time_scale = rate
    t.duration = n
    t.stts = [(n, 1)]
    t.desc = sound_desc(codec, 1, bits, rate)
    t.width = t.height = 0
    t.sample_size = bits // 8
    t.n_samples = n
    t.handler = ("soun", "Apple Sound Media Handler")
    t.crcs = []
    return t


def movie(tracks, time_scale):
    """moov then mdat; the chunk offsets are known once moov's size is."""
    def build(mdat_start):
        traks = []
        pos = mdat_start + 8
        for tid, t in enumerate(tracks, 1):
            offsets = []
            for s in t.samples:
                offsets.append(pos)
                pos += len(s)
            dur = t.duration * time_scale // t.time_scale
            if t.kind == "vide":
                stsc = [(1, 1, 1)]
                sizes = [len(s) for s in t.samples]
                stbl_atom = stbl(t.desc, t.stts, stsc, 0, sizes, offsets)
                mh = atom("vmhd", full(0, 1), struct.pack(">HHHH", 0x40, 0x8000, 0x8000, 0x8000))
            else:
                stsc = [(1, t.chunk_frames[0], 1)]
                if t.chunk_frames[-1] != t.chunk_frames[0]:
                    stsc.append((len(t.chunk_frames), t.chunk_frames[-1], 1))
                stbl_atom = stbl(t.desc, t.stts, stsc, t.sample_size, t.n_samples, offsets)
                mh = atom("smhd", full(), struct.pack(">hH", 0, 0))
            minf = atom("minf", mh, hdlr("dhlr", "alis", "Apple Alias Data Handler"), dinf(), stbl_atom)
            mdia = atom("mdia", mdhd(t.time_scale, t.duration), hdlr("mhlr", t.handler[0], t.handler[1]), minf)
            traks.append(atom("trak", tkhd(tid, dur, t.width, t.height, 0x100 if t.kind == "soun" else 0), mdia))
        dur = max(t.duration * time_scale // t.time_scale for t in tracks)
        return atom("moov", mvhd(time_scale, dur, len(tracks) + 1), *traks)

    moov = build(0)
    moov = build(len(moov))
    mdat = atom("mdat", *(s for t in tracks for s in t.samples))
    return moov + mdat


# name -> (description, builder); QTTest's own list must match (qttest.c).
def catalogue():
    return [
        ("raw32.mov", "'raw ' 32-bit 160x120, 10 frames at 1/10 s, time scale 600",
         lambda: [video_track("raw ", 32, 160, 120, 10, 600, 60)], 600),
        ("raw16.mov", "'raw ' 16-bit 80x60, 8 frames at 1/5 s, time scale 30",
         lambda: [video_track("raw ", 16, 80, 60, 8, 30, 6)], 30),
        ("rle32.mov", "'rle ' 32-bit 160x120, 10 frames at 1/10 s, time scale 600",
         lambda: [video_track("rle ", 32, 160, 120, 10, 600, 60)], 600),
        ("rle16.mov", "'rle ' 16-bit 64x48, 6 frames at 1/4 s, time scale 1000",
         lambda: [video_track("rle ", 16, 64, 48, 6, 1000, 250)], 1000),
        ("tone16.mov", "'twos' 16-bit mono 22050 Hz, 1000 Hz tone, 2 s",
         lambda: [tone_track("twos", 22050, 1000, 2.0)], 600),
        ("tone8.mov", "'raw ' 8-bit mono 11025 Hz, 440 Hz tone, 1 s",
         lambda: [tone_track("raw ", 11025, 440, 1.0)], 600),
        ("av.mov", "'raw ' 32-bit 80x60 20 frames at 1/10 s + 'twos' 22050 Hz 1000 Hz, 2 s",
         lambda: [video_track("raw ", 32, 80, 60, 20, 600, 60), tone_track("twos", 22050, 1000, 2.0)], 600),
    ]


def write_all(outdir):
    os.makedirs(outdir, exist_ok=True)
    manifest = []
    for name, desc, build, ts in catalogue():
        tracks = build()
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(movie(tracks, ts))
        for t in tracks:
            for n, crc in enumerate(t.crcs):
                manifest.append("%s %d %08X" % (name, n, crc))
    with open(os.path.join(outdir, "manifest.txt"), "w") as f:
        f.write("\n".join(manifest) + "\n")
    return manifest


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--list":
        for name, desc, _, _ in catalogue():
            print("%-12s %s" % (name, desc))
    elif len(sys.argv) == 2:
        write_all(sys.argv[1])
    else:
        sys.exit(__doc__)
