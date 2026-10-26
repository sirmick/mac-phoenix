#!/usr/bin/env python3
"""fat_image.py: small FAT12/FAT16 disk images for the File System Manager
tests (a DOS disk for PC Exchange to mount).

    fat_image.py make IMG LABEL [--fat12]   a 20 MB FAT16 (or 1.44 MB FAT12)
                                            image: README.TXT, DATA.BIN and a
                                            DOCS folder with NOTE.TXT
    fat_image.py ls IMG [DIR/...]           a directory's entries
    fat_image.py cat IMG PATH               a file's bytes to stdout
    fat_image.py check IMG                  consistency: FAT copies equal, no
                                            cross-linked, free or lost
                                            clusters, sizes within chains;
                                            exit 1 on a problem

The files' contents are fixed (README_TEXT, DATA_BYTES, NOTE_TEXT) so a
test can compare what the Mac read.
"""
import struct
import sys

README_TEXT = b'Hello from a DOS disk.\r\nPC Exchange mounted this FAT image.\r\n'
NOTE_TEXT = b'Inside a subdirectory.\r\n'
DATA_BYTES = bytes((i * 7) & 0xFF for i in range(5000))

SEC = 512


def make(path, label, fat12=False):
    label = label.upper()[:11].ljust(11).encode('ascii')
    if fat12:
        total, spc, rootent, fatsz, media = 2880, 1, 224, 9, 0xF0
    else:
        total, spc, rootent, fatsz, media = 40960, 4, 512, 40, 0xF8
    res, nfat, cl = 1, 2, SEC * spc
    eoc = 0xFFF if fat12 else 0xFFFF
    img = bytearray(total * SEC)
    bs = bytearray(SEC)
    bs[0:3] = b'\xEB\x3C\x90'
    bs[3:11] = b'MSDOS5.0'
    struct.pack_into('<HBHBHHBHHHII', bs, 11, SEC, spc, res, nfat, rootent, total, media,
                     fatsz, 18, 2, 0, 0)
    bs[38] = 0x29
    struct.pack_into('<I', bs, 39, 0x12345678)
    bs[43:54] = label
    bs[54:62] = b'FAT12   ' if fat12 else b'FAT16   '
    bs[510:512] = b'\x55\xAA'
    img[0:SEC] = bs
    fat = [0xFF0 if fat12 else 0xFFF8, eoc]
    rootoff = (res + nfat * fatsz) * SEC
    dataoff = rootoff + rootent * 32

    def put(c, data):
        img[dataoff + (c - 2) * cl:dataoff + (c - 1) * cl] = data.ljust(cl, b'\0')

    def alloc(data):
        n = max(1, (len(data) + cl - 1) // cl)
        first = len(fat)
        for i in range(n):
            fat.append(first + i + 1 if i < n - 1 else eoc)
            put(first + i, data[i * cl:(i + 1) * cl])
        return first

    def entry(name, ext, attr, clus, size):
        return (name.ljust(8).encode() + ext.ljust(3).encode() + bytes([attr]) + bytes(10)
                + struct.pack('<HHHI', 0x6000, 0x5A21, clus, size))

    root = [label + bytes([0x08]) + bytes(10) + struct.pack('<HHHI', 0x6000, 0x5A21, 0, 0),
            entry('README', 'TXT', 0x20, alloc(README_TEXT), len(README_TEXT)),
            entry('DATA', 'BIN', 0x20, alloc(DATA_BYTES), len(DATA_BYTES))]
    docs = len(fat)
    fat.append(eoc)
    sub = (entry('.', '', 0x10, docs, 0) + entry('..', '', 0x10, 0, 0)
           + entry('NOTE', 'TXT', 0x20, alloc(NOTE_TEXT), len(NOTE_TEXT)))
    put(docs, sub)
    root.append(entry('DOCS', '', 0x10, docs, 0))
    for i, e in enumerate(root):
        img[rootoff + i * 32:rootoff + (i + 1) * 32] = e
    fb = bytearray(fatsz * SEC)
    if fat12:
        for i in range(0, len(fat), 2):
            a, b = fat[i], fat[i + 1] if i + 1 < len(fat) else 0
            o = i * 3 // 2
            fb[o:o + 3] = bytes([a & 0xFF, ((a >> 8) & 0xF) | ((b & 0xF) << 4), b >> 4])
    else:
        for i, v in enumerate(fat):
            struct.pack_into('<H', fb, i * 2, v)
    for k in range(nfat):
        img[(res + k * fatsz) * SEC:(res + (k + 1) * fatsz) * SEC] = fb
    open(path, 'wb').write(img)


class Volume:
    def __init__(self, path):
        img = self.img = open(path, 'rb').read()
        (bps, self.spc, res, self.nfat, rootent, total, _media,
         self.fatsz) = struct.unpack_from('<HBHBHHBH', img, 11)
        self.fat12 = img[54:59] != b'FAT16'
        self.fats = [img[(res + k * self.fatsz) * bps:(res + (k + 1) * self.fatsz) * bps]
                     for k in range(self.nfat)]
        self.rootoff = (res + self.nfat * self.fatsz) * bps
        self.dataoff = self.rootoff + rootent * 32
        self.cl = bps * self.spc
        self.nclus = (total * bps - self.dataoff) // self.cl + 2
        self.eoc = 0xFF8 if self.fat12 else 0xFFF8

    def next(self, c):
        fat = self.fats[0]
        if not self.fat12:
            return struct.unpack_from('<H', fat, c * 2)[0]
        v = struct.unpack_from('<H', fat, c * 3 // 2)[0]
        return v >> 4 if c & 1 else v & 0xFFF

    def chain(self, c):
        out = []
        while 2 <= c < self.eoc and c < self.nclus and c not in out:
            out.append(c)
            c = self.next(c)
        return out

    def data(self, c, size=None):
        d = b''.join(self.img[self.dataoff + (x - 2) * self.cl:self.dataoff + (x - 1) * self.cl]
                     for x in self.chain(c))
        return d if size is None else d[:size]

    def root(self):
        return self.img[self.rootoff:self.dataoff]

    @staticmethod
    def entries(raw):
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            name = e[:8].decode('latin-1').rstrip()
            ext = e[8:11].decode('latin-1').rstrip()
            yield ((name + '.' + ext) if ext else name, e[11],
                   struct.unpack_from('<H', e, 26)[0], struct.unpack_from('<I', e, 28)[0])

    def find(self, path):
        raw = self.root()
        parts = [p for p in path.split('/') if p]
        for i, part in enumerate(parts):
            for name, attr, clus, size in self.entries(raw):
                if name.upper() == part.upper():
                    if i == len(parts) - 1:
                        return attr, clus, size
                    raw = self.data(clus)
                    break
            else:
                return None
        return 0x10, None, 0

    def check(self):
        problems = []
        if any(f != self.fats[0] for f in self.fats):
            problems.append('FAT copies differ')
        owner = {}

        def walk(raw, where):
            for name, attr, clus, size in self.entries(raw):
                if name in ('.', '..'):
                    continue
                c, n = clus, 0
                while 2 <= c < self.eoc:
                    if c >= self.nclus:
                        problems.append('%s%s: cluster %d out of range' % (where, name, c))
                        break
                    if c in owner:
                        problems.append('%s%s: cluster %d also %s\'s' % (where, name, c, owner[c]))
                        break
                    if self.next(c) == 0:
                        problems.append('%s%s: cluster %d free in the FAT' % (where, name, c))
                    owner[c] = where + name
                    n += 1
                    c = self.next(c)
                if not attr & 0x10 and n * self.cl < size:
                    problems.append('%s%s: %d bytes in %d clusters' % (where, name, size, n))
                if attr & 0x10:
                    walk(self.data(clus), where + name + '/')
        walk(self.root(), '')
        lost = [c for c in range(2, self.nclus) if self.next(c) != 0 and c not in owner]
        if lost:
            problems.append('lost clusters %s' % lost[:16])
        return problems


def main(argv):
    if len(argv) >= 4 and argv[1] == 'make':
        make(argv[2], argv[3], '--fat12' in argv[4:])
    elif len(argv) >= 3 and argv[1] == 'ls':
        v = Volume(argv[2])
        raw = v.root()
        if len(argv) > 3:
            found = v.find(argv[3])
            if not found or not found[0] & 0x10:
                print('not a folder: %s' % argv[3])
                return 1
            raw = v.data(found[1])
        for name, attr, clus, size in Volume.entries(raw):
            print('%-12s %s %d' % (name, 'dir ' if attr & 0x10 else 'file', size))
    elif len(argv) == 4 and argv[1] == 'cat':
        v = Volume(argv[2])
        found = v.find(argv[3])
        if not found or found[0] & 0x10:
            print('not a file: %s' % argv[3], file=sys.stderr)
            return 1
        sys.stdout.buffer.write(v.data(found[1], found[2]))
    elif len(argv) == 3 and argv[1] == 'check':
        problems = Volume(argv[2]).check()
        for p in problems:
            print(p)
        return 1 if problems else 0
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
