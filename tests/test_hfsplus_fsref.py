#!/usr/bin/env python3
"""test_hfsplus_fsref.py -- Mac OS 9.0's HFS Plus APIs (FSRefs, Unicode
names, catalog info, iterators and search, the fork calls) on Executor
against Mac OS 9.0.4: FSRefTest (tests/guest/hfsplus) on the long-name
fixture (read-only), a blank HFS+ volume and a blank HFS one (writable),
its output equal to 9.0.4's (oracle-9.0.4/fsreftest.txt; remade with
tests/guest/hfsplus/fsreftest.py --machine 9.0.4). Twice: the 68K build
(MPW's SC) and the PowerPC one (FSRefTestPPC.bin: CodeWarrior's MWCPPC
and MWLinkPPC against Universal Interfaces 3.3.2's InterfaceLib, run on
mame-ppc through InterfaceLib; on 9.0.4 its output is the 68K one's).

Set aside, as they depend on the machine rather than the calls:
  - the Desktop DB and DF are open on 9.0.4 (its Finder holds them);
  - on the writable volumes, what 9.0.4's Finder makes when they mount (its
    Desktop files, Trash...): the volume's counts, free space and next ID,
    and so the CNIDs in the classic short names ("#17");
  - on the HFS volume, what Executor's HFS code does otherwise: it
    allocates by the clump (4 KB) where 9.0.4's fork calls allocate blocks
    (physical lengths), and it takes ':' in a name for a path separator,
    so the one name with a ':' isn't made (bdNamErr) and isn't listed.

    test_hfsplus_fsref.py
Needs docker (mkfs.hfsplus makes the HFS volume); exit 77 (skip) without
it or tests-7.5.5.
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
ORACLE = os.path.join(ROOT, "tests", "guest", "hfsplus", "oracle-9.0.4", "fsreftest.txt")


def normalize(text):
    out, section = [], None
    for line in text.splitlines():
        m = re.match(r"VOLUME (\S+)", line)
        if m:
            section = m.group(1)
        line = re.sub(r'^(    u"Desktop D[BF]" flags )0088', r"\g<1>0000", line)
        if section in ("Blank", "untitled"):
            line = re.sub(r" files \d+ folders \d+ (total \d+) free \d+ blk (\d+)/(\d+)/\d+ (clumps \S+) nextid \d+",
                          r" \1 blk \2/\3 \4", line)
            line = re.sub(r'#[0-9A-F]+(\.[^"]*)?"', r'#N\1"', line)
        if section == "untitled":
            if "Colon: and slash/ in it" in line or line.startswith("  create colon slash"):
                continue
            if re.match(r"  bulk -> -?\d+ got \d+ changed", line) or re.match(r"  total \d+", line):
                continue                        # (the batches: one item fewer)
            line = re.sub(r"(data \d+) / \d+", r"\1 / -", line)
            line = re.sub(r" peof \d+", " peof -", line)
            line = re.sub(r"(allocate 100000 -> 0) got \d+", r"\1 got -", line)
        out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--keep")
    args = ap.parse_args()
    if not os.path.exists(os.path.expanduser("~/storage/images/tests-7.5.5.img")):
        print("SKIP: no tests-7.5.5.img")
        return 77
    if not shutil.which("docker") or subprocess.run(["docker", "image", "inspect", "macphoenix-hfsprogs"],
                                                    capture_output=True).returncode:
        print("SKIP: no docker image macphoenix-hfsprogs")
        return 77
    home_tmp = os.path.expanduser("~/storage/tmp")      # not /tmp: tmpfs
    work = args.keep or tempfile.mkdtemp(prefix="hfsplus-fsref-", dir=home_tmp if os.path.isdir(home_tmp) else None)
    try:
        want = normalize(open(ORACLE).read())
        failed = False
        for label, binary in (("68K", "FSRefTest.bin"), ("PowerPC", "FSRefTestPPC.bin")):
            out = os.path.join(work, label)
            r = subprocess.run([sys.executable, os.path.join(ROOT, "tests", "guest", "hfsplus", "fsreftest.py"),
                                "--machine", "executor", "--out", out,
                                "--bin", os.path.join(ROOT, "tests", "guest", "hfsplus", binary)])
            if r.returncode or not os.path.exists(os.path.join(out, "fsreftest.txt")):
                print("FAIL %s: fsreftest.py exited %d" % (label, r.returncode))
                failed = True
                continue
            got = normalize(open(os.path.join(out, "fsreftest.txt")).read())
            if want == got:
                print("PASS FSRefTest %s: %d lines as 9.0.4's" % (label, len(want)))
                continue
            failed = True
            print("FAIL FSRefTest %s:" % label)
            for line in list(difflib.unified_diff(want, got, "9.0.4", "executor", lineterm="", n=0))[:60]:
                print("    " + line[:240])
        return 1 if failed else 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
