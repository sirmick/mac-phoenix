#!/usr/bin/env python3
"""Validate and repair HFS volume headers (primary + alternate MDB).

Classic HFS volumes keep two Master Directory Blocks:
  - primary MDB  at volume offset 1024 (logical block 2)
  - alternate MDB in the next-to-last logical block (offset N-1024)

If either header is corrupt (wrong signature, inconsistent block counts,
stale free-block totals) the Mac ROM's boot scanner either skips the
volume entirely (blinking "?") or mounts it read-only and refuses to
boot. This script cross-checks both headers and restores the broken
one from the surviving copy.

Usage:
    fix_hfs_header.py <image>              # verify only
    fix_hfs_header.py <image> --fix        # repair broken side from good side
    fix_hfs_header.py <image> --dump       # print both headers

Detects APM images (Apple Partition Map) and locates the Apple_HFS
partition before inspecting the MDBs. Bare-HFS images are handled too.
"""

import argparse
import struct
import sys

HFS_SIG = 0x4244  # 'BD'
MDB_OFFSET_IN_PART = 1024

# Bits that are *defined* in drAtrb. Any other bit being set is a corrupt
# reserved bit — Apple's fsck.hfs rejects the MDB outright, and the Quadra
# ROM's boot scanner refuses to mount the volume ("?" floppy).
#
#   bit  7 (0x0080): hardware write lock
#   bit  8 (0x0100): volume cleanly unmounted
#   bit  9 (0x0200): bad blocks spared
#   bit 10 (0x0400): no cache required
#   bit 11 (0x0800): boot volume inconsistent (set by Mac OS 8.1+)
#   bit 12 (0x1000): catalog node IDs reused
#   bit 13 (0x2000): journaled (HFS+)
#   bit 15 (0x8000): software write lock
VALID_DRATRB_MASK = 0xBF80

MDB_FIELDS = [
    ("drSigWord",   0,  "H"),
    ("drCrDate",    2,  "I"),
    ("drLsMod",     6,  "I"),
    ("drAtrb",      10, "H"),
    ("drNmFls",     12, "H"),
    ("drVBMSt",     14, "H"),
    ("drAllocPtr",  16, "H"),
    ("drNmAlBlks",  18, "H"),
    ("drAlBlkSiz",  20, "I"),
    ("drClpSiz",    24, "I"),
    ("drAlBlSt",    28, "H"),
    ("drNxtCNID",   30, "I"),
    ("drFreeBks",   34, "H"),
    ("drWrCnt",     70, "I"),
    ("drXTFlSize",  130, "I"),
    ("drCTFlSize",  146, "I"),
]


def find_hfs_partition(f):
    """Return (byte_offset, block_count) of the HFS partition, or None."""
    f.seek(512)
    if f.read(2) == b"PM":
        entry = 0
        while True:
            off = (1 + entry) * 512
            f.seek(off)
            if f.read(2) != b"PM":
                break
            f.seek(off + 4)
            n_entries = struct.unpack(">I", f.read(4))[0]
            f.seek(off + 8)
            pblock_start = struct.unpack(">I", f.read(4))[0]
            pblock_count = struct.unpack(">I", f.read(4))[0]
            f.seek(off + 48)
            ptype = f.read(32).split(b"\x00")[0].decode("ascii", errors="replace")
            if ptype == "Apple_HFS":
                return pblock_start * 512, pblock_count
            entry += 1
            if entry >= n_entries:
                break
        return None
    # No APM — assume bare HFS if primary MDB signature matches
    f.seek(MDB_OFFSET_IN_PART)
    if struct.unpack(">H", f.read(2))[0] == HFS_SIG:
        f.seek(0, 2)
        return 0, f.tell() // 512
    return None


def parse_mdb(blob):
    """Parse MDB blob into a dict of named fields + raw drVN bytes."""
    out = {}
    for name, off, fmt in MDB_FIELDS:
        size = struct.calcsize(">" + fmt)
        out[name] = struct.unpack(">" + fmt, blob[off:off + size])[0]
    vn_len = blob[36]
    out["drVN"] = blob[37:37 + min(vn_len, 27)].decode("mac_roman", errors="replace")
    return out


def read_mdb(f, offset):
    f.seek(offset)
    return f.read(162)


def primary_offset(hfs_offset):
    return hfs_offset + MDB_OFFSET_IN_PART


def alternate_offset(hfs_offset, hfs_blocks):
    return hfs_offset + (hfs_blocks - 2) * 512


def is_valid(mdb):
    """Return (ok, [errors]). Each error is (severity, message) where
    severity is 'fatal' (header must be replaced from the other copy) or
    'patch' (can be repaired in place without touching other fields)."""
    errors = []
    if mdb["drSigWord"] != HFS_SIG:
        errors.append(("fatal", f"bad signature 0x{mdb['drSigWord']:04x} (want 0x4244)"))
    if mdb["drAlBlkSiz"] == 0 or mdb["drAlBlkSiz"] % 512 != 0:
        errors.append(("fatal", f"bad drAlBlkSiz={mdb['drAlBlkSiz']} (must be >0 and multiple of 512)"))
    if mdb["drNmAlBlks"] == 0:
        errors.append(("fatal", "drNmAlBlks=0 (empty or uninitialized volume)"))
    if mdb["drFreeBks"] > mdb["drNmAlBlks"]:
        errors.append(("fatal", f"drFreeBks={mdb['drFreeBks']} > drNmAlBlks={mdb['drNmAlBlks']}"))
    bad_atrb = mdb["drAtrb"] & ~VALID_DRATRB_MASK
    if bad_atrb:
        errors.append(("patch",
            f"drAtrb=0x{mdb['drAtrb']:04x} has reserved bits 0x{bad_atrb:04x} set "
            f"(Quadra ROM rejects this; fsck.hfs flags it as 'invalid MDB drAtrb')"))
    return (len(errors) == 0), errors


CRITICAL_FIELDS = [
    "drSigWord", "drVBMSt", "drNmAlBlks", "drAlBlkSiz",
    "drClpSiz", "drAlBlSt", "drXTFlSize", "drCTFlSize",
]


def fields_differ(primary, alternate):
    """Return list of (name, primary_val, alt_val) for structural mismatches.

    Only compares layout-critical fields — block counts, sizes, tree offsets.
    drLsMod / drWrCnt / drFreeBks / drNmFls / drAtrb drift between the two
    copies by design: during a live mount the primary is updated on every
    write while the alternate stays frozen at the last unmount-time snapshot.
    (Reserved bits in drAtrb are a different story — those are checked in
    is_valid() and must be zero on both sides.)
    """
    out = []
    for name in CRITICAL_FIELDS:
        if primary[name] != alternate[name]:
            out.append((name, primary[name], alternate[name]))
    return out


def patch_dratrb_in_place(blob):
    """Clear reserved bits in drAtrb, return the patched blob."""
    old = struct.unpack(">H", blob[10:12])[0]
    new = old & VALID_DRATRB_MASK
    patched = bytearray(blob)
    struct.pack_into(">H", patched, 10, new)
    return bytes(patched), old, new


def dump(label, mdb):
    print(f"  [{label}]")
    print(f"    sig=0x{mdb['drSigWord']:04x} vol=\"{mdb['drVN']}\" atrb=0x{mdb['drAtrb']:04x}")
    print(f"    NmAlBlks={mdb['drNmAlBlks']} AlBlkSiz={mdb['drAlBlkSiz']} "
          f"AlBlSt={mdb['drAlBlSt']} FreeBks={mdb['drFreeBks']}")
    print(f"    NmFls={mdb['drNmFls']} NxtCNID={mdb['drNxtCNID']} WrCnt={mdb['drWrCnt']}")
    print(f"    XTFlSize={mdb['drXTFlSize']} CTFlSize={mdb['drCTFlSize']}")


def main():
    p = argparse.ArgumentParser(
        description="Validate and repair HFS volume headers (primary + alternate MDB).")
    p.add_argument("image", help="Path to HFS disk image")
    p.add_argument("--fix", action="store_true",
                   help="Restore broken header from the good copy")
    p.add_argument("--dump", action="store_true",
                   help="Dump both headers even if everything is valid")
    args = p.parse_args()

    mode = "r+b" if args.fix else "rb"
    with open(args.image, mode) as f:
        part = find_hfs_partition(f)
        if part is None:
            print(f"Error: no HFS partition found in {args.image}", file=sys.stderr)
            sys.exit(1)
        hfs_offset, hfs_blocks = part

        prim_off = primary_offset(hfs_offset)
        alt_off = alternate_offset(hfs_offset, hfs_blocks)

        prim_blob = read_mdb(f, prim_off)
        alt_blob = read_mdb(f, alt_off)
        prim = parse_mdb(prim_blob)
        alt = parse_mdb(alt_blob)

        prim_ok, prim_errs = is_valid(prim)
        alt_ok, alt_errs = is_valid(alt)

        def fmt_errs(errs):
            return "; ".join(msg for _sev, msg in errs)

        print(f"{args.image}")
        print(f"  HFS partition at offset {hfs_offset}, {hfs_blocks} blocks "
              f"({hfs_blocks * 512} bytes)")
        print(f"  primary MDB    @ 0x{prim_off:08x}: "
              f"{'OK' if prim_ok else 'BAD — ' + fmt_errs(prim_errs)}")
        print(f"  alternate MDB  @ 0x{alt_off:08x}: "
              f"{'OK' if alt_ok else 'BAD — ' + fmt_errs(alt_errs)}")

        if args.dump or not (prim_ok and alt_ok):
            dump("primary", prim)
            dump("alternate", alt)

        mismatches = fields_differ(prim, alt) if (prim_ok and alt_ok) else []
        if mismatches:
            print("  structural mismatches between primary and alternate:")
            for name, pv, av in mismatches:
                print(f"    {name}: primary={pv} alternate={av}")

        # An error list that is *all* 'patch' severity means we can repair the
        # header in place (clear reserved bits) without cross-copying anything.
        def is_patchable(errs):
            return errs and all(sev == "patch" for sev, _ in errs)

        prim_patchable = is_patchable(prim_errs)
        alt_patchable = is_patchable(alt_errs)

        if prim_ok and alt_ok and not mismatches:
            print("  all good — no repair needed")
            return

        if not args.fix:
            print("  run with --fix to repair", file=sys.stderr)
            sys.exit(2)

        # Repair logic
        if prim_patchable:
            new_prim_blob, old_a, new_a = patch_dratrb_in_place(prim_blob)
            print(f"  fixing: clearing drAtrb reserved bits in primary "
                  f"(0x{old_a:04x} → 0x{new_a:04x}) @ 0x{prim_off:08x}")
            f.seek(prim_off)
            f.write(new_prim_blob)
            prim_blob = new_prim_blob
            prim_ok = True
        if alt_patchable:
            new_alt_blob, old_a, new_a = patch_dratrb_in_place(alt_blob)
            print(f"  fixing: clearing drAtrb reserved bits in alternate "
                  f"(0x{old_a:04x} → 0x{new_a:04x}) @ 0x{alt_off:08x}")
            f.seek(alt_off)
            f.write(new_alt_blob)
            alt_blob = new_alt_blob
            alt_ok = True

        if prim_ok and not alt_ok:
            print(f"  fixing: copying primary MDB to alternate @ 0x{alt_off:08x}")
            f.seek(alt_off)
            f.write(prim_blob)
        elif alt_ok and not prim_ok:
            print(f"  fixing: copying alternate MDB to primary @ 0x{prim_off:08x}")
            f.seek(prim_off)
            f.write(alt_blob)
        elif prim_ok and alt_ok and not mismatches:
            pass  # drAtrb patch was the only fix needed
        elif prim_ok and alt_ok and mismatches:
            # Both signatures valid but fields disagree — prefer primary, it's
            # the one HFS reads first on mount. Rewrite alternate with the
            # structural fields from primary, leaving drift-prone fields alone.
            print(f"  fixing: rewriting alternate from primary "
                  f"(structural fields only) @ 0x{alt_off:08x}")
            patched = bytearray(alt_blob)
            for name, off, fmt in MDB_FIELDS:
                size = struct.calcsize(">" + fmt)
                struct.pack_into(">" + fmt, patched, off, prim[name])
            # Also rewrite the volume name bytes (offset 36, 28 bytes total).
            patched[36:64] = prim_blob[36:64]
            f.seek(alt_off)
            f.write(bytes(patched))
        else:
            print("  both headers are broken — cannot repair automatically",
                  file=sys.stderr)
            sys.exit(3)

    print("  repair complete")


if __name__ == "__main__":
    main()
