#!/usr/bin/env python3
"""test_hfsplus_volume.py -- Executor's File Manager on HFS+ volumes against
Mac OS 9.0.4's: VolDump (tests/guest/hfsplus) runs on Executor (Apple's
7.5.5 System, BridgeAgent) against HFS+ images and its dumps must equal
the ones 9.0.4 wrote for the same images (tests/guest/hfsplus/oracle-9.0.4/
voldump-*.txt; remade with tests/guest/hfsplus/voldump.py --machine 9.0.4).

Not compared, as they depend on the machine rather than on HFS+: the
volume's "ejectable" and "files open" bits (ROM-machine disks are
ejectable, Executor's fixed; 9.0.4's Finder holds the Desktop DB open), a
file's open bits, and writing to a fork 9.0.4's Finder holds open (opWrErr).

    test_hfsplus_volume.py [--dump BUILD/hfsplus_dump]

Images: the fixtures (src/executor/tests/fixtures/hfsplus) and, where
present, ~/storage/hfsplus-recon/images/{carbonlib-1.6,guest-written}.img.
Exit 77 (skip) without tests-7.5.5.
"""
import argparse
import difflib
import glob
import gzip
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus")
ORACLE = os.path.join(ROOT, "tests", "guest", "hfsplus", "oracle-9.0.4")
RECON = os.path.expanduser("~/storage/hfsplus-recon/images")


def normalize(text):
    out = []
    for line in text.splitlines():
        line = re.sub(r"^(VOLUME .* attrib )([0-9A-F]{4})",
                      lambda m: m.group(1) + "%04X" % (int(m.group(2), 16) & ~0x60), line)
        line = re.sub(r"^(FILE \S+ id \d+ par \d+ attrib )([0-9A-F]{2})",
                      lambda m: m.group(1) + "%02X" % (int(m.group(2), 16) & ~0x8C), line)
        # a failed FSWrite leaves its count as it was (9.0.4: stack garbage)
        line = re.sub(r" write (-?\d+)/-?\d+", r" write \1", line)
        out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default=os.path.join(ROOT, "build", "src", "executor", "romlib", "hfsplus_dump"))
    args = ap.parse_args()
    if not os.path.exists(os.path.join(os.path.expanduser("~/storage/images"), "tests-7.5.5.img")):
        print("SKIP: no tests-7.5.5.img")
        return 77
    home_tmp = os.path.expanduser("~/storage/tmp")      # not /tmp: tmpfs
    work = tempfile.mkdtemp(prefix="hfsplus-volume-", dir=home_tmp if os.path.isdir(home_tmp) else None)
    try:
        images = []
        for oracle in sorted(glob.glob(os.path.join(ORACLE, "voldump-*.txt"))):
            stem = os.path.basename(oracle)[len("voldump-"):-len(".txt")]
            gz = os.path.join(FIXTURES, stem + ".img.gz")
            if os.path.exists(gz):
                path = os.path.join(work, stem + ".img")
                with gzip.open(gz) as src, open(path, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                images.append(path)
            elif os.path.exists(os.path.join(RECON, stem + ".img")):
                images.append(os.path.join(RECON, stem + ".img"))
            else:
                print("(no %s.img: not compared)" % stem)
        out = os.path.join(work, "dumps")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tests", "guest", "hfsplus", "voldump.py"),
                            "--machine", "executor", "--out", out, "--dump", args.dump] + images)
        if r.returncode:
            print("FAIL: voldump.py exited %d" % r.returncode)
            return 1
        failed = False
        for image in images:
            stem = os.path.splitext(os.path.basename(image))[0]
            want = normalize(open(os.path.join(ORACLE, "voldump-%s.txt" % stem)).read())
            got = normalize(open(os.path.join(out, "voldump-%s.txt" % stem)).read())
            # a fork 9.0.4's Finder had open for writing (the Desktop DB):
            # opWrErr there; the rest of the line isn't comparable
            for i, line in enumerate(want):
                if " rdwr -49" in line and i < len(got):
                    want[i] = line.split(" rdwr ")[0]
                    got[i] = got[i].split(" rdwr ")[0]
            if want == got:
                print("PASS %s: %d lines as 9.0.4's" % (stem, len(want)))
                continue
            failed = True
            print("FAIL %s:" % stem)
            for line in list(difflib.unified_diff(want, got, "9.0.4", "executor", lineterm="", n=0))[:40]:
                print("    " + line[:240])
        return 1 if failed else 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
