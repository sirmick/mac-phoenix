#!/usr/bin/env python3
"""test_hfsplus_writer.py -- Executor's HFS+ writer (src/executor/romlib/hfs/plus/
writer.cpp, btree.cpp) driven through hfsplus_dump's script mode on blank
volumes (the fixtures: mkfs.hfsplus, bare and HFS-wrapped), checked by
tools that share nothing with it:

  - fsck.hfsplus -n -f (hfsprogs, in a container: docker image
    macphoenix-hfsprogs, built here when missing) must find the volume OK;
  - hfsfuse's hfsdump must list every folder as a model of the script
    predicts and read every fork's bytes as the model has them.

The scripts (random, seeded) fill folders with 1600 files (the catalog
B-tree splits to three levels and its file grows), write forks in turns (files past
their 8 extents: extents overflow records), rename, move, truncate and
delete (nodes freed, the tree shrinking).

    test_hfsplus_writer.py --dump BUILD/hfsplus_dump [--seeds N] [--hfsdump PATH]

Exit 77 (skip) without docker or hfsdump.
"""
import argparse
import gzip
import hashlib
import os
import random
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus")
RECON = os.path.expanduser("~/storage/hfsplus-recon")
IMAGE = "macphoenix-hfsprogs"


def pattern(off, n, seed):
    return bytes((seed + 7 * o + o // 251) & 0xFF for o in range(off, off + n))


class Model:
    """What the volume should hold: folders, and files with two forks."""

    def __init__(self):
        self.folders = {""}
        self.files = {}            # path -> {"data": bytearray, "rsrc": bytearray}
        self.lines = []

    def parent(self, p):
        return p.rsplit("/", 1)[0] if "/" in p else ""

    def children(self, d):
        return [p for p in list(self.folders) + list(self.files) if p and self.parent(p) == d]

    def q(self, p):
        return '"%s"' % p

    def mkdir(self, p):
        self.folders.add(p)
        self.lines.append("mkdir %s" % self.q(p))

    def create(self, p):
        self.files[p] = {"data": bytearray(), "rsrc": bytearray()}
        self.lines.append("create %s" % self.q(p))

    def write(self, p, fork, off, n, seed):
        b = self.files[p][fork]
        if len(b) < off + n:
            b.extend(bytes(off + n - len(b)))
        b[off:off + n] = pattern(off, n, seed)
        self.lines.append("write %s %s %d %d %d" % (self.q(p), fork, off, n, seed))

    def truncate(self, p, fork, n):
        b = self.files[p][fork]
        if n < len(b):
            del b[n:]
        else:
            b.extend(bytes(n - len(b)))
        self.lines.append("truncate %s %s %d" % (self.q(p), fork, n))

    def _repath(self, old, new):
        for coll in (self.files,):
            for p in [p for p in coll if p == old or p.startswith(old + "/")]:
                coll[new + p[len(old):]] = coll.pop(p)
        for p in [p for p in self.folders if p == old or p.startswith(old + "/")]:
            self.folders.remove(p)
            self.folders.add(new + p[len(old):])

    def rename(self, p, name):
        self.lines.append("rename %s %s" % (self.q(p), self.q(name)))
        self._repath(p, (self.parent(p) + "/" if self.parent(p) else "") + name)

    def move(self, p, d):
        self.lines.append("move %s %s" % (self.q(p), self.q(d)))
        self._repath(p, (d + "/" if d else "") + p.rsplit("/", 1)[-1])

    def delete(self, p):
        self.lines.append("delete %s" % self.q(p))
        self.files.pop(p, None)
        self.folders.discard(p)


def script(seed):
    rnd = random.Random(seed)
    m = Model()
    m.mkdir("Many")
    m.mkdir("Tree")
    m.mkdir("Tree/Deeper")
    # hundreds of files in one folder, names that sort apart
    for i in range(700):
        p = "Many/File %04d %s" % (rnd.randrange(10000), "x" * rnd.randrange(0, 40))
        if p in m.files:
            continue
        m.create(p)
        if rnd.random() < 0.3:
            m.write(p, "data", 0, rnd.randrange(1, 9000), rnd.randrange(256))
        if rnd.random() < 0.1:
            m.write(p, "rsrc", 0, rnd.randrange(1, 3000), rnd.randrange(256))
    # more than the catalog file has nodes for: the file grows
    m.mkdir("More")
    for i in range(900):
        m.create("More/%05d %s" % (i * 7919 % 100000, "y" * (i % 30)))
    # forks written in turns: many extents each
    frag = ["Tree/Frag %d" % i for i in range(3)]
    for p in frag:
        m.create(p)
    for r in range(45):
        for p in frag:
            n = len(m.files[p]["data"])
            m.write(p, "data", n, 4096 * rnd.randrange(1, 5) + rnd.randrange(100), rnd.randrange(256))
    m.write(frag[0], "data", 5000, 20000, 9)                 # over what's there
    m.write(frag[1], "rsrc", 0, 70000, 3)
    # renames, moves, folders
    many = sorted(p for p in m.files if p.startswith("Many/"))
    for p in rnd.sample(many, 60):
        new = "Renamed %d %s" % (rnd.randrange(100000), "e\u0301" if rnd.random() < 0.3 else "")
        if (m.parent(p) + "/" + new) not in m.files:
            m.rename(p, new)
    many = sorted(p for p in m.files if p.startswith("Many/"))
    for p in rnd.sample(many, 40):
        if "Tree/Deeper/" + p.rsplit("/", 1)[-1] not in m.files:
            m.move(p, "Tree/Deeper")
    m.mkdir("Tree/Deeper/Sub")
    m.move("Tree/Deeper", "")
    m.rename("Deeper", "Moved Deeper")
    # truncations, both ways
    m.truncate(frag[2], "data", 3000)
    m.truncate(frag[1], "data", len(m.files[frag[1]]["data"]) + 50000)
    m.truncate(frag[1], "rsrc", 0)
    # half the files out: nodes freed
    many = sorted(p for p in m.files if p.startswith("Many/"))
    for p in rnd.sample(many, len(many) * 2 // 3):
        m.delete(p)
    m.delete(frag[0])
    for p in sorted(p for p in m.files if p.startswith("More/"))[::2]:
        m.delete(p)
    return m


def hfsdump(exe, image, *args, timeout=30):
    try:
        r = subprocess.run([exe, image, *args], capture_output=True, timeout=timeout)
        return r.returncode, r.stdout
    except subprocess.TimeoutExpired:
        return None, b""


def check(m, image, hfsdump_exe):
    fails = []
    for d in sorted(m.folders):
        want = sorted(c.rsplit("/", 1)[-1] for c in m.children(d))
        rc, out = hfsdump(hfsdump_exe, image, "read", "/" + d, timeout=10)
        if rc is None and not want:
            continue                            # hfsdump loops listing an empty folder
        got = sorted(n for n in out.decode("utf-8").split("\n") if n and not n.startswith("\x00")
                     and n not in ("HFS+ Private Data",)) if rc == 0 else None
        import unicodedata
        if got is None or [unicodedata.normalize("NFC", n) for n in got] != \
                [unicodedata.normalize("NFC", n) for n in want]:
            fails.append("folder /%s: hfsdump %s, model %d items" % (d, "fails" if got is None else len(got), len(want)))
    for p, forks in sorted(m.files.items()):
        for fork in ("data", "rsrc"):
            rc, out = hfsdump(hfsdump_exe, image, "read", "/" + p + ("/rsrc" if fork == "rsrc" else ""))
            if rc != 0 and not forks[fork]:
                continue
            if rc != 0 or hashlib.sha256(out).digest() != hashlib.sha256(bytes(forks[fork])).digest():
                fails.append("/%s %s: %d bytes read, %d expected" % (p, fork, len(out), len(forks[fork])))
    return fails


def fsck(image):
    d, f = os.path.split(os.path.abspath(image))
    r = subprocess.run(["docker", "run", "--rm", "-v", d + ":/w", IMAGE, "fsck.hfsplus", "-n", "-f", "/w/" + f],
                       capture_output=True, text=True)
    return r.returncode, r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True)
    ap.add_argument("--hfsdump", default=os.environ.get("HFSDUMP", os.path.join(RECON, "src", "hfsfuse", "hfsdump")))
    ap.add_argument("--seeds", type=int, default=2)
    ap.add_argument("--keep", help="leave the images in this folder")
    args = ap.parse_args()
    if not os.access(args.hfsdump, os.X_OK) or not shutil.which("docker"):
        print("SKIP: no hfsdump or docker")
        return 77
    if subprocess.run(["docker", "image", "inspect", IMAGE], capture_output=True).returncode:
        df = "FROM ubuntu:24.04\nRUN apt-get update -qq && apt-get install -y -qq hfsprogs && rm -rf /var/lib/apt/lists/*\n"
        if subprocess.run(["docker", "build", "-q", "-t", IMAGE, "-"], input=df.encode(), capture_output=True).returncode:
            print("SKIP: can't build the hfsprogs image")
            return 77
    home_tmp = os.path.expanduser("~/storage/tmp")      # not /tmp: tmpfs
    work = args.keep or tempfile.mkdtemp(prefix="hfsplus-writer-", dir=home_tmp if os.path.isdir(home_tmp) else None)
    os.makedirs(work, exist_ok=True)
    failed = False
    try:
        for fixture in ("blank-64m", "blank-wrapped-64m"):
            for seed in range(1, args.seeds + 1):
                image = os.path.join(work, "%s-%d.img" % (fixture, seed))
                with gzip.open(os.path.join(FIXTURES, fixture + ".img.gz")) as src, open(image, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                m = script(seed)
                path = image + ".script"
                open(path, "w", encoding="utf-8").write("\n".join(m.lines) + "\n")
                r = subprocess.run([args.dump, image, "script", path], capture_output=True, text=True)
                fails = []
                if r.returncode or r.stdout.strip():
                    fails.append("script: rc %d %s" % (r.returncode, (r.stdout + r.stderr).strip()[:400]))
                rc, out = fsck(image)
                if rc:
                    fails.append("fsck.hfsplus %d: %s" % (rc, " / ".join(l.strip() for l in out.splitlines()
                                                                       if not l.startswith("**"))[:600]))
                fails += check(m, image, args.hfsdump)
                print("%s %s seed %d: %d lines, %d files, %d folders" % ("FAIL" if fails else "PASS", fixture, seed,
                      len(m.lines), len(m.files), len(m.folders) - 1))
                for f in fails[:20]:
                    print("    " + f)
                failed |= bool(fails)
        return 1 if failed else 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
