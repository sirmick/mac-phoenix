#!/usr/bin/env python3
"""gen_tables.py -- the Unicode tables Executor's HFS+ reader needs, made
from published data (docs/executor/HFSPlus.md):

  src/executor/romlib/hfs/plus/unicode_tables.h

1. The case-folding table of HFS+'s case-insensitive name order (TN1150,
   "Case-Insensitive String Comparison Algorithm": FastUnicodeCompare and
   its gLowerCaseTable, whose data TN1150 leaves to a download), in the
   form TN1150 describes: 256 high-byte entries, each 0 or the index of a
   256-entry subtable. Derived from Unicode 2.1.2's UnicodeData.txt (TN1150:
   HFS+ names are Unicode 2.0/2.1; Mac OS 8.1-10.2 decompose by 2.1):
     - U+0000 -> 0xFFFF (TN1150: so a NUL in a name isn't taken for
       "ignored");
     - Unicode 2.0's ignorable formatting characters -> 0 (ignored):
       U+200C-200F, U+202A-202E, U+206A-206F, U+FEFF;
     - a character with a canonical decomposition stays as it is (HFS+
       names are stored decomposed: such characters never occur);
     - an uppercase or titlecase letter or a letter number (categories Lu,
       Lt, Nl) -> its Unicode 2.1 lowercase mapping;
     - everything else stays as it is.
   --check-casefold FILE compares the result with a dump of TN1150's own
   table ("XXXX YYYY" lines: code point, folded), as hfsfuse carries it:
   identical for all 65536 code points when this was written.

2. MacRoman <-> HFS+ Unicode: each MacRoman byte's character (Apple's
   ROMAN.TXT, unicode.org/Public/MAPPINGS/VENDORS/APPLE/ROMAN.TXT; bytes
   0x00-0x1F and 0x7F as themselves) fully decomposed by Unicode 2.1's canonical
   decompositions, as Mac OS stores a MacRoman name on HFS+ (TN1150,
   "Unicode Subtleties"), but for U+2000-2FFF and U+F900-FAFF, which
   TN1150 says are not decomposed. --check-decomposition TN1150TABLE.html compares
   these with TN1150's decomposition table.

Usage:
  gen_tables.py UNICODEDATA-2.1.2.txt ROMAN.TXT [-o OUT]
      [--check-casefold DUMP] [--check-decomposition tn1150table.html]
UnicodeData-2.1.2.txt: https://www.unicode.org/Public/2.1-Update/UnicodeData-2.1.2.txt
(sha256 1d05f8d8912f16b1d30ec94df31ad8458541901593f1ae6971da3f9c096d9a7a).
"""
import argparse
import hashlib
import html
import os
import re
import sys

UNICODE_SHA256 = "1d05f8d8912f16b1d30ec94df31ad8458541901593f1ae6971da3f9c096d9a7a"
ROMAN_SHA256 = "18e571645be895e9553ed5c842ea8f65f9c5d3c9ccb43e66e0c33a132ed0d721"
IGNORABLE = set(range(0x200C, 0x2010)) | set(range(0x202A, 0x202F)) | set(range(0x206A, 0x2070)) | {0xFEFF}
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def read_unicode(path):
    lower, decomp, cat = {}, {}, {}
    for line in open(path, encoding="ascii"):
        f = line.rstrip("\n").split(";")
        c = int(f[0], 16)
        cat[c] = f[2]
        if f[5] and not f[5].startswith("<"):
            decomp[c] = [int(x, 16) for x in f[5].split()]
        if f[13]:
            lower[c] = int(f[13], 16)
    return lower, decomp, cat


def casefold(lower, decomp, cat):
    fold = list(range(0x10000))
    fold[0] = 0xFFFF
    for c in range(1, 0x10000):
        if c in IGNORABLE:
            fold[c] = 0
        elif c in decomp:
            pass
        elif c in lower and cat.get(c) in ("Lu", "Lt", "Nl"):
            fold[c] = lower[c]
    return fold


def full_decomposition(c, decomp):
    # TN1150: U+2000-2FFF (punctuation, symbols...) and U+F900-FAFF stay
    # whole; so does the MacRoman not-equal sign.
    if c not in decomp or 0x2000 <= c <= 0x2FFF or 0xF900 <= c <= 0xFAFF:
        return [c]
    out = []
    for d in decomp[c]:
        out += full_decomposition(d, decomp)
    return out


def macroman(path, decomp):
    table = {b: b for b in list(range(0x20)) + [0x7F]}     # controls: not in ROMAN.TXT
    for line in open(path, encoding="ascii"):
        m = re.match(r"0x([0-9A-F]{2})\s+0x([0-9A-F]{4})", line)
        if m:
            table[int(m.group(1), 16)] = int(m.group(2), 16)
    if len(table) != 256:
        sys.exit("ROMAN.TXT: %d bytes mapped, not 256" % len(table))
    return [table[b] for b in range(256)], [full_decomposition(table[b], decomp) for b in range(256)]


def two_level(fold):
    """TN1150's layout: 256 high-byte entries, then subtables."""
    index, subtables = [0] * 256, []
    for hi in range(256):
        block = fold[hi << 8:(hi + 1) << 8]
        if block != list(range(hi << 8, (hi + 1) << 8)):
            index[hi] = 256 * (1 + len(subtables))
            subtables.append(block)
    return index + [v for s in subtables for v in s]


def check_casefold(fold, dump):
    theirs = {}
    for line in open(dump):
        a, b = line.split()
        theirs[int(a, 16)] = int(b, 16)
    diff = [c for c in range(0x10000) if theirs.get(c) != fold[c]]
    print("case folding vs %s: %d differences%s" % (dump, len(diff),
          "" if not diff else ": " + " ".join("%04X" % c for c in diff[:20])))
    return not diff


def check_decomposition(seqs, path):
    page = open(path, encoding="utf-8", errors="replace").read()
    cells = [html.unescape(re.sub(r"<[^>]+>", " ", c)).split()
             for c in re.findall(r"(?is)<td[^>]*>(.*?)</td>", page)]
    cells = [c for c in cells if c and all(re.fullmatch(r"0x[0-9A-F]{4}", x) for x in c)]
    pairs = {}                                  # "Illegal" cell, then its "Replace With"
    for illegal, replace in zip(cells[0::2], cells[1::2]):
        pairs[int(illegal[0], 16)] = [int(x, 16) for x in replace]
    bad = 0
    for b, seq in enumerate(seqs):
        if len(seq) > 1:
            src = None
            # the precomposed character ROMAN.TXT gives for b
            for c, rep in pairs.items():
                if rep == seq:
                    src = c
            if src is None:
                print("  0x%02X: %s not in TN1150's table" % (b, " ".join("%04X" % c for c in seq)))
                bad += 1
    print("decomposition vs TN1150's table: %d MacRoman characters decomposed, %d not found there"
          % (sum(len(s) > 1 for s in seqs), bad))
    return not bad


def emit(out, fold2, raw, seqs):
    lines = ["/* Generated by tools/hfsplus/gen_tables.py from Unicode 2.1.2's",
             " * UnicodeData.txt and Apple's ROMAN.TXT (published data); see the",
             " * script for how. Do not edit. */",
             "#pragma once",
             "#include <cstdint>",
             "",
             "namespace Executor::hfsplus::tables {",
             "",
             "/* TN1150's case-folding table: 256 high-byte entries (0, or the index",
             "   of the character's 256-entry subtable), then the subtables; 0 means",
             "   ignored. */",
             "inline constexpr uint16_t kLowerCase[%d] = {" % len(fold2)]
    for i in range(0, len(fold2), 8):
        lines.append("    " + " ".join("0x%04X," % v for v in fold2[i:i + 8]))
    lines += ["};", "",
              "/* Each MacRoman byte as HFS+ stores it: fully decomposed Unicode,",
              "   up to 3 code units, unused ones 0 (U+0000 is byte 0x00 alone). */",
              "inline constexpr uint16_t kMacRomanDecomposed[256][3] = {"]
    for b, seq in enumerate(seqs):
        if len(seq) > 3:
            sys.exit("0x%02X decomposes to %d code units" % (b, len(seq)))
        cells = seq + [0] * (3 - len(seq))
        lines.append("    { %s },  /* 0x%02X */" % (", ".join("0x%04X" % v for v in cells), b))
    lines += ["};", "",
              "/* Each MacRoman byte's character as ROMAN.TXT gives it (precomposed):",
              "   what a writer that doesn't decompose stores. */",
              "inline constexpr uint16_t kMacRomanUnicode[256] = {"]
    for i in range(0, 256, 8):
        lines.append("    " + " ".join("0x%04X," % v for v in raw[i:i + 8]))
    lines += ["};", "", "}  // namespace Executor::hfsplus::tables", ""]
    open(out, "w").write("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("unicodedata")
    ap.add_argument("roman")
    ap.add_argument("-o", default=os.path.join(ROOT, "src", "executor", "romlib", "hfs", "plus", "unicode_tables.h"))
    ap.add_argument("--check-casefold")
    ap.add_argument("--check-decomposition")
    args = ap.parse_args()
    for path, want in ((args.unicodedata, UNICODE_SHA256), (args.roman, ROMAN_SHA256)):
        if sha256(path) != want:
            sys.exit("%s: not the pinned file (sha256 %s)" % (path, want))
    lower, decomp, cat = read_unicode(args.unicodedata)
    fold = casefold(lower, decomp, cat)
    raw, seqs = macroman(args.roman, decomp)
    ok = True
    if args.check_casefold:
        ok &= check_casefold(fold, args.check_casefold)
    if args.check_decomposition:
        ok &= check_decomposition(seqs, args.check_decomposition)
    emit(args.o, two_level(fold), raw, seqs)
    print("wrote %s" % args.o)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
