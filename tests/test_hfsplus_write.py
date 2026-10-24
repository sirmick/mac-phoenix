#!/usr/bin/env python3
"""test_hfsplus_write.py -- Executor's File Manager writing to an HFS+ volume,
against Mac OS 9.0.4 doing the same: VolWrite (tests/guest/hfsplus) runs on
Executor (Apple's 7.5.5 System) on a writable copy of the blank fixture
(with long-named files made beforehand), then VolDump reads the result.
Both outputs must equal what 9.0.4 wrote for the same run
(tests/guest/hfsplus/oracle-9.0.4/volwrite.txt, voldump-written.txt;
remade with tests/guest/hfsplus/volwrite.py --machine 9.0.4), and the
volume Executor leaves must pass fsck.hfsplus (docker; not checked without).

Set aside, as 9.0.4's Finder makes them when the volume mounts and Executor
has no such Finder: its own files (Desktop DB and DF, Desktop Folder,
Trash, TheVolumeSettingsFolder) and what follows from them -- the volume's
counts and free blocks, catalog node IDs, the "files open" and
"ejectable" bits; dates (made now).

    test_hfsplus_write.py [--dump BUILD/hfsplus_dump]

Exit 77 (skip) without tests-7.5.5.
"""
import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORACLE = os.path.join(ROOT, "tests", "guest", "hfsplus", "oracle-9.0.4")
FINDERS_OWN = {b"Desktop DB", b"Desktop DF", b"Desktop Folder", b"Trash", b"TheVolumeSettingsFolder",
               b"DesktopPrinters DB", b"Temporary Items"}


def normalize_log(text):
    out = []
    for line in text.splitlines():
        if line.startswith("VOLUME") and "attrib" in line:
            line = re.sub(r" (attrib|nmfls|frblk|filcnt|dircnt) \S+", "", line)
        line = re.sub(r"(DIRCREATE Folder -> 0) id \d+", r"\1", line)
        out.append(line)
    return out


# Bytes neither machine defines, their checksums not compared:
#  - "Moved file"'s data past its first 1000 bytes up to the sector's end:
#    SetEOF exposed them; 9.0.4 left its cache buffer's leftovers there,
#    Executor zeros;
#  - "Resource file"'s resource fork: the 256-byte header area the Resource
#    Manager keeps for itself (bytes 16-255) differs between them.
UNDEFINED = {(b"Moved file", "data"), (b"Resource file", "rsrc")}


def normalize_dump(text):
    """VolDump's lines, the Finder's own items out, IDs and dates masked."""
    out, skip, name = [], False, b""
    for line in text.splitlines():
        m = re.match(r"^(FILE|DIR ) ([0-9A-F]+|-) ", line)
        if m:
            name = bytes.fromhex(m.group(2)) if m.group(2) != "-" else b""
            skip = name in FINDERS_OWN
        elif not line.startswith("  "):
            skip = False
        f = re.match(r"^  (data|rsrc) open", line)
        if f and (name, f.group(1)) in UNDEFINED:
            line = re.sub(r" adler [0-9A-F]+", " adler -", line)
        if skip:
            continue
        if line.startswith("FOLDER "):
            continue                            # per-folder counts: the root's has the Finder's items
        if line.startswith("VOLUME"):
            line = re.sub(r" (attrib|nmfls|frblk|filcnt|dircnt|nxtcnid|cr|md) \S+", "", line)
        line = re.sub(r"\b(id|par) \d+", r"\1 N", line)
        line = re.sub(r" (cr|md) \d+", r" \1 T", line)
        out.append(line)
    return out


def compare(name, want, got):
    if want == got:
        print("PASS %s: %d lines as 9.0.4's" % (name, len(want)))
        return True
    print("FAIL %s:" % name)
    for line in list(difflib.unified_diff(want, got, "9.0.4", "executor", lineterm="", n=0))[:40]:
        print("    " + line[:240])
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default=os.path.join(ROOT, "build", "src", "executor", "romlib", "hfsplus_dump"))
    ap.add_argument("--keep")
    args = ap.parse_args()
    if not os.path.exists(os.path.expanduser("~/storage/images/tests-7.5.5.img")):
        print("SKIP: no tests-7.5.5.img")
        return 77
    home_tmp = os.path.expanduser("~/storage/tmp")      # not /tmp: tmpfs
    work = args.keep or tempfile.mkdtemp(prefix="hfsplus-write-", dir=home_tmp if os.path.isdir(home_tmp) else None)
    try:
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tests", "guest", "hfsplus", "volwrite.py"),
                            "--machine", "executor", "--out", work, "--dump", args.dump])
        if r.returncode:
            print("FAIL: volwrite.py exited %d" % r.returncode)
            return 1
        ok = compare("VolWrite", normalize_log(open(os.path.join(ORACLE, "volwrite.txt")).read()),
                     normalize_log(open(os.path.join(work, "volwrite.txt")).read()))
        ok &= compare("VolDump after", normalize_dump(open(os.path.join(ORACLE, "voldump-written.txt")).read()),
                      normalize_dump(open(os.path.join(work, "voldump.txt")).read()))
        if shutil.which("docker") and not subprocess.run(["docker", "image", "inspect", "macphoenix-hfsprogs"],
                                                         capture_output=True).returncode:
            f = subprocess.run(["docker", "run", "--rm", "-v", work + ":/w", "macphoenix-hfsprogs", "fsck.hfsplus",
                                "-n", "-f", "/w/written.img"], capture_output=True, text=True)
            print("%s fsck.hfsplus on the volume Executor wrote%s" % ("PASS" if f.returncode == 0 else "FAIL",
                  "" if f.returncode == 0 else ": " + f.stdout.strip()[-400:]))
            ok &= f.returncode == 0
        else:
            print("(no fsck: docker or the macphoenix-hfsprogs image missing)")
        return 0 if ok else 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
