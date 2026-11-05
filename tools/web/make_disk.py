#!/usr/bin/env python3
"""A compact HFS disk of chosen folders from other images, for the browser.

    tools/web/make_disk.py OUT.img --label NAME --take IMAGE FOLDER [FOLDER...]
                           [--take IMAGE FOLDER...] [--skip PATH...]
                           [--system-from SNAPSHOT...] [--additions-record JSON]
                           [--with IMAGE...]

Copies the named top-level folders (recursively) of each IMAGE into a new
volume sized to what it holds, as MacBinary through hfsutils, so forks,
Finder info and dates stay as they were. --skip leaves out paths under
them (":Folder:Sub"). A games disk for Executor's tab, say:

    tools/web/make_disk.py games-web.img --label Games \\
        --take games.img "Maelstrom 1.4.2 ƒ" "Lemmings 1.5.2 ƒ" \\
        --skip ":Glider PRO 1.1.2 ƒ:User-designed"

Top-level folder names are matched as given, or by their start (so
"Maelstrom" finds "Maelstrom 1.4.2 ƒ").

--system-from names provisioning snapshots (~/storage/baked/NAME.img)
whose System Folder part (what the product's installer put in the System
Folder: preferences, registration, shared libraries, fonts) goes in an
invisible top-level folder, "System Folder Additions", laid out as the
System Folder is; later snapshots' files win over earlier ones', as
provisioning/build.py's "system_from". Executor's tab copies it into its
System when the disk goes in (docs/executor/Disks.md, "System Folder
additions"). Left out: the System's own files and what the tab's boot
disks leave out (make_boot_disk.py's DROP). --additions-record writes what
went in (the disk's record's "system_additions") as JSON. Startup code
(extensions, control panels, components: STARTUP_TYPES) is left out too:
the additions are what applications need to start.

Aliases whose target isn't there are left out, as make_boot_disk.py's cut
does: one into this disk whose target wasn't taken, one into a --with image
(the boot disks of the Systems the disk is for: their volumes are
"Macintosh HD") whose target that image doesn't have and the disk's
additions don't bring (Photoshop 5.5's Preview In aliases to browsers no
disk has). An alias to any other volume (a CD) stays. One into the
volume a folder was taken from, when --label names the disk otherwise, is
made one into this disk (its record's volume name and full path).
"""
import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_boot_disk import DROP, DROP_CREATORS, SYSTEM_SUBFOLDER_DROP, Disk, Writer, alias_target  # noqa: E402

ADDITIONS = b"System Folder Additions"
# The System's own files, and what a session leaves at the System Folder's
# top: never an addition.
NOT_ADDED = {b"System", b"Finder", b"System Resources", b"Clipboard", b"Scrapbook File",
             b"Note Pad File", b"Shutdown Check"}
# Startup code is not added: what applications need to start is their
# preferences, support files, fonts and shared libraries, and an
# installer's extensions, control panels and components, run at every
# boot on whatever System the tab runs, brought it down (Kodak's CMS
# components, translators registered through the Shared Library Manager,
# Type Reunion, ATM, OLE, ...). The System Folder's folders of startup
# items, and anywhere files of the types that run at startup or register
# themselves: INITs, control panels, background applications, components,
# Shared Library Manager libraries, network drivers, Finder extensions,
# debuggers.
STARTUP_FOLDERS = {b"Control Panels", b"Startup Items", b"Shutdown Items", b"Control Strip Modules",
                   b"Launcher Items", b"Apple Menu Items", b"Extensions (Disabled)",
                   b"Control Panels (Disabled)", b"System Extensions (Disabled)"}
STARTUP_TYPES = {b"INIT", b"cdev", b"appe", b"thng", b"libr", b"RDEV", b"fext", b"AINI", b"dbgr",
                 b"scri", b"ADBS"}
# Startup code applications can't do without and that runs clean on Executor
# (allowed in src/executor/res/extension-policy.txt): kept by name.
# Microsoft OLE Extension: Works 4.0b won't start without it. Matched without
# case, as HFS names are (and hfs_sideload.cpp's startupCode).
STARTUP_KEPT = {b"microsoft ole extension"}
# Libraries Executor doesn't run: CarbonLib (no Carbon; an application that
# finds it takes the Carbon path).
NOT_FOR_EXECUTOR = {b"CarbonLib"}


def additions(snapshots, scratch):
    """{System Folder path: MacBinary} of the snapshots' System Folder parts,
    later ones winning, and the folders under it, parents first."""
    files, folders = {}, []
    for n, snap in enumerate(snapshots):
        disk = Disk(snap, Path(scratch) / f"snap{n}")
        try:
            def tree(path, rel):
                for name in disk.files(path):
                    if (not rel and name in NOT_ADDED) or name.decode("mac_roman") in DROP \
                            or name in NOT_FOR_EXECUTOR:
                        continue
                    blob = disk.macbinary(path + b":" + name)
                    if blob is None:
                        print(f"skipped (unreadable): {snap}: {(path + b':' + name).decode('mac_roman')}",
                              file=sys.stderr)
                    elif blob[69:73] not in DROP_CREATORS and (blob[65:69] not in STARTUP_TYPES
                                                               or name.lower() in STARTUP_KEPT):
                        files[rel + (b":" if rel else b"") + name] = blob
                for name in disk.folders(path):
                    if (not rel and (name in SYSTEM_SUBFOLDER_DROP or name in STARTUP_FOLDERS)) \
                            or name.decode("mac_roman") in DROP:
                        continue
                    sub = rel + (b":" if rel else b"") + name
                    if sub not in folders:
                        folders.append(sub)
                    tree(path + b":" + name, sub)
            if b"System Folder" in disk.folders(b""):
                tree(b"System Folder", b"")
        finally:
            disk.close()
    used = {f for p in files for f in folders if p.startswith(f + b":")}
    return files, [f for f in folders if f in used]


def dangling(blobs, folders, label, companions):
    """The aliases among blobs whose target is on this disk, or on a
    companion image's volume, and not there (paths, bytes)."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "diskaudit"))
    import disk_audit
    vols = {}
    for c in companions:
        v = disk_audit.Volume(c)
        vols.setdefault(v.name.encode("mac_roman"), set()).update(
            v.path_of(n).encode("mac_roman").lower() for n in v.nodes.values())
    have = {p.lower() for p in folders} | {p.lower() for p in blobs}
    out = []
    for path, blob in blobs.items():
        target = alias_target(blob)
        if not target:
            continue
        vol, where = target
        if vol == label.encode("mac_roman"):
            ok = where.lower() in have
        elif vol in vols:
            ok = (vol + b":" + where).lower() in vols[vol] or (
                where.startswith(b"System Folder:")
                and (ADDITIONS + where[len(b"System Folder"):]).lower() in have)
        else:
            continue                                    # another volume: a CD
        if not ok:
            out.append(path)
    return out


def macbinary_crc(header):
    """MacBinary II's CRC of the header's first 124 bytes (CRC-16/XMODEM)."""
    crc = 0
    for b in header[:124]:
        crc ^= b << 8
        for _ in range(8):
            crc = (crc << 1 ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc


def revolume_alias(blob, old, new):
    """An alias file into volume old (a source image's) made one into volume
    new (the disk's label): the record's volume name and its full path (tag
    2), so the Alias Manager finds the target on this disk. None if the blob
    is no such alias."""
    target = alias_target(blob)
    if not target or target[0] != old:
        return None
    from rsrc import macbinary_forks
    data, fork = macbinary_forks(blob)
    data_off, map_off, data_len, map_len = struct.unpack_from(">IIII", fork, 0)
    m = bytearray(fork[map_off:map_off + map_len])
    tl_off = struct.unpack_from(">H", m, 24)[0]
    ref = None
    for i in range((struct.unpack_from(">H", m, tl_off)[0] + 1) & 0xFFFF):
        te = tl_off + 2 + 8 * i
        count, ref_off = struct.unpack_from(">HH", m, te + 4)
        for j in range(count + 1):
            r = tl_off + ref_off + 12 * j
            if m[te:te + 4] == b"alis" and struct.unpack_from(">h", m, r)[0] == 0:
                ref = r
    doff = struct.unpack_from(">I", m, ref + 4)[0] & 0xFFFFFF
    p = data_off + doff
    ln = struct.unpack_from(">I", fork, p)[0]
    rec = bytearray(fork[p + 4:p + 4 + ln])
    rec[10:38] = bytes([len(new)]) + new.ljust(27, b"\0")    # the volume name, a Str27
    out, q = rec[:150], 150
    while q + 4 <= len(rec):
        tag, tl = struct.unpack_from(">hH", rec, q)
        value = rec[q + 4:q + 4 + tl]
        if tag == -1:
            out += rec[q:]
            break
        if tag == 2:
            value = new + value[len(old):]
        elif tag == 15:                                     # the volume name in Unicode
            u = new.decode("mac_roman").encode("utf-16-be")
            value = struct.pack(">H", len(u) // 2) + u
        out += struct.pack(">hH", tag, len(value)) + value + b"\0" * (len(value) & 1)
        q += 4 + tl + (tl & 1)
    struct.pack_into(">H", out, 4, len(out))                # the record's size
    grow = len(out) - ln
    resources = fork[data_off:data_off + data_len]
    resources = resources[:doff] + struct.pack(">I", len(out)) + out + resources[doff + 4 + ln:]
    for i in range((struct.unpack_from(">H", m, tl_off)[0] + 1) & 0xFFFF):  # later resources move
        te = tl_off + 2 + 8 * i
        count, ref_off = struct.unpack_from(">HH", m, te + 4)
        for j in range(count + 1):
            r = tl_off + ref_off + 12 * j
            word = struct.unpack_from(">I", m, r + 4)[0]
            if word & 0xFFFFFF > doff:
                struct.pack_into(">I", m, r + 4, (word & 0xFF000000) | (word & 0xFFFFFF) + grow)
    struct.pack_into(">IIII", m, 0, data_off, data_off + len(resources), len(resources), len(m))
    fork = bytes(m[:16]) + fork[16:data_off] + resources + bytes(m)
    header = bytearray(blob[:128])
    struct.pack_into(">I", header, 87, len(fork))
    struct.pack_into(">H", header, 124, macbinary_crc(header))
    pad = lambda b: b + b"\0" * (-len(b) % 128)  # noqa: E731
    return bytes(header) + pad(data) + pad(fork)


def additions_record(files):
    """What the disk adds to the System Folder, for its record: the items
    one level into the System Folder's folders (a folder of fonts is one
    item), and those that take a restart."""
    items, restart = [], []
    for p, blob in files.items():
        parts = p.split(b":")
        item = b":".join(parts[:2]).decode("mac_roman")
        if item not in items:
            items.append(item)
        # (the fonts as one: "Fonts"; the Font Manager lists the folder at startup)
        later = "Fonts" if parts[0] == b"Fonts" else None
        if later and later not in restart:
            restart.append(later)
    size = sum(int.from_bytes(b[83:87], "big") + int.from_bytes(b[87:91], "big") for b in files.values())
    return {"folder": ADDITIONS.decode("mac_roman"), "files": len(files), "bytes": size,
            "items": items, "restart": restart}


def volume_size(blobs, folders):
    """Bytes for a volume of these MacBinary files: every fork takes whole
    allocation blocks, and HFS's grow with the volume (at most 65535 of
    them), so a disk of many small files needs more than their sum."""
    forks = []
    for b in blobs:
        forks += [int.from_bytes(b[83:87], "big"), int.from_bytes(b[87:91], "big")]
    # the catalog and extents trees, the bitmap, the boot blocks: some slack
    overhead = 2 * 1024 * 1024 + (len(forks) // 2 + folders) * 512
    size = sum(forks) + overhead
    for _ in range(8):
        block = 512 * -(-size // (512 * 65535))
        want = int((sum(-(-f // block) * block for f in forks) + overhead) * 1.05)
        if want <= size:
            break
        size = want
    return (size + 511) // 512 * 512


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--label", required=True)
    ap.add_argument("--take", nargs="+", action="append", metavar=("IMAGE", "FOLDER"),
                    required=True, help="an image and folders at its top level")
    ap.add_argument("--skip", nargs="+", default=[], help="paths to leave out")
    ap.add_argument("--system-from", nargs="+", default=[], metavar="SNAPSHOT",
                    help="snapshot images whose System Folder part the disk carries")
    ap.add_argument("--additions-record", help="write what the disk adds to the System Folder here (JSON)")
    ap.add_argument("--with", dest="companions", nargs="+", default=[], metavar="IMAGE",
                    help="images aliases on the disk may point into (the boot disks of its Systems)")
    args = ap.parse_args()
    skip = [s.encode("mac_roman").lstrip(b":") for s in args.skip]

    folders, blobs = [], {}
    with tempfile.TemporaryDirectory(prefix="make-disk-") as scratch:
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "diskaudit"))
        import disk_audit
        label = args.label.encode("mac_roman")
        for n, (image, *wanted) in enumerate(args.take):
            source = disk_audit.Volume(image).name.encode("mac_roman")
            disk = Disk(image, Path(scratch) / f"cache{n}")
            try:
                top = disk.folders(b"")
                for want in wanted:
                    w = want.encode("mac_roman")
                    match = [f for f in top if f == w] or [f for f in top if f.startswith(w)]
                    if not match:
                        sys.exit(f"{want}: not at the top of {image}")

                    def tree(path):
                        if any(path == s or path.startswith(s + b":") for s in skip):
                            return
                        folders.append(path)
                        for name in disk.files(path):
                            p = path + b":" + name
                            if any(p == s for s in skip):
                                continue
                            blob = disk.macbinary(p)
                            if blob is None:
                                print(f"skipped (unreadable): {p.decode('mac_roman')}", file=sys.stderr)
                            else:
                                moved = revolume_alias(blob, source, label) if source != label else None
                                if moved:
                                    print(f"alias into {source.decode('mac_roman')}: now into "
                                          f"{args.label}: {p.decode('mac_roman')}")
                                blobs[p] = moved or blob
                        for name in disk.folders(path):
                            tree(path + b":" + name)

                    tree(match[0])
            finally:
                disk.close()

        extra, extra_folders = additions(args.system_from, scratch)
        if extra:
            folders += [ADDITIONS] + [ADDITIONS + b":" + f for f in extra_folders]
            blobs.update({ADDITIONS + b":" + p: b for p, b in extra.items()})
        if args.additions_record:
            with open(args.additions_record, "w") as f:
                json.dump(additions_record(extra) if extra else None, f, ensure_ascii=False)
        for path in dangling(blobs, folders, args.label, args.companions):
            print(f"left out (an alias to nothing here): {path.decode('mac_roman')}")
            del blobs[path]
        size = volume_size(blobs.values(), len(folders))
        w = Writer(Path(args.out))
        try:
            w.format(size, args.label)
            for path in folders:
                w.mkdir(path)
            for path, blob in blobs.items():
                w.put(path, blob, scratch)
            if extra:
                w.hide(ADDITIONS)       # the tab's, not the user's: out of the Finder's windows
        finally:
            w.close()
    print(f"{args.out}: {size / 1e6:.1f} MB, {len(blobs)} files in {len(folders)} folders, \"{args.label}\"")


if __name__ == "__main__":
    main()
