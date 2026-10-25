#!/usr/bin/env python3
"""test_hfsplus_reader.py -- Executor's HFS+ reader (src/executor/romlib/hfs/plus,
through build's hfsplus_dump) against hfsfuse's hfsdump, image by image:
every folder's listing, and each item's CNID, kind, type, creator, fork
sizes and fork bytes. For the long-name fixture also the short names
against what Mac OS 9.0.4 gave the same items
(tests/guest/hfsplus/oracle-9.0.4/longnames.txt).

    test_hfsplus_reader.py --dump BUILD/hfsplus_dump [--hfsdump PATH] [--sample N] [IMAGE ...]

Without IMAGEs: the fixtures in src/executor/tests/fixtures/hfsplus
(gunzipped to a scratch folder) and the images in ~/storage/hfsplus-recon/images
that are there. Exit 77 (skip) without hfsdump (github.com/0x09/hfsfuse;
HFSDUMP or --hfsdump; default ~/storage/hfsplus-recon/src/hfsfuse/hfsdump).
"""
import argparse
import glob
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus")
ORACLE = os.path.join(ROOT, "tests", "guest", "hfsplus", "oracle-9.0.4", "longnames.txt")
RECON = os.path.expanduser("~/storage/hfsplus-recon")


def nfc(s):
    return unicodedata.normalize("NFC", s)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class HfsDump:
    def __init__(self, exe, image):
        self.exe, self.image = exe, image

    def run(self, *args):
        try:
            r = subprocess.run([self.exe, self.image, *args], capture_output=True, timeout=10)
        except subprocess.TimeoutExpired:       # hfsfuse loops listing an empty folder
            return None, b""
        return r.returncode, r.stdout

    def stat(self, what):
        rc, out = self.run("stat", str(what))
        if rc != 0:
            return None
        d = {}
        for line in out.decode("latin-1").splitlines():     # bytes as they are: types, creators
            k, sep, v = line.partition(": ")
            if sep:
                d[k.strip()] = v
        return d

    def listing(self, path):
        rc, out = self.run("read", path)        # not by CNID: hfsfuse loops on "read 2"
        return None if rc != 0 else [nfc(n) for n in out.decode("utf-8").split("\n") if n]     # "Icon\r" is a name


def ours(dump, image):
    out = subprocess.run([dump, image], capture_output=True, check=True).stdout.decode("utf-8")
    lines = [json.loads(l) for l in out.splitlines()]
    return lines[0], lines[1:]


def check_image(dump, hfsdump, image, sample=1):
    fails = []
    checked = {"folders": 0, "items": 0, "forks": 0}
    vol, items = ours(dump, image)
    theirs = HfsDump(hfsdump, image)
    # hfsdump shows a name's '/' as ':'; it hides HFS+'s private folders
    shown = lambda name: nfc(name.replace("/", ":"))
    hidden = lambda name: name.startswith("\u0000") or name in (".journal", ".journal_info_block")
    by_parent = {}
    for it in items:
        by_parent.setdefault(it["parent"], []).append(it)
    by_cnid = {it["cnid"]: it for it in items}

    def hpath(cnid):                            # the path as hfsdump takes it
        parts = []
        while cnid in by_cnid:
            parts.insert(0, shown(by_cnid[cnid]["name"]))
            cnid = by_cnid[cnid]["parent"]
        return "/" + "/".join(parts)
    for parent in [2] + [it["cnid"] for it in items if it["kind"] == "folder" and not hidden(it["name"])]:
        mine = sorted(shown(it["name"]) for it in by_parent.get(parent, []) if not hidden(it["name"]))
        listed = theirs.listing(hpath(parent))
        checked["folders"] += 1
        if listed is None:                      # hfsdump hangs on an empty folder: its valence then
            st = theirs.stat(hpath(parent))
            if not st or st.get("valence") != str(len(by_parent.get(parent, []))):
                fails.append("folder %d: hfsdump can't list it (valence %s, we have %d)"
                             % (parent, st and st.get("valence"), len(by_parent.get(parent, []))))
        elif sorted(listed) != mine:
            fails.append("folder %d: hfsdump lists %s, we %s"
                         % (parent, sorted(set(listed) - set(mine)), sorted(set(mine) - set(listed))))
    for n, it in enumerate(items):
        if hidden(it["name"]) or n % sample:
            continue
        # a hard link: hfsdump follows it by path, not by CNID
        what = hpath(it["cnid"]) if it.get("link") else str(it["cnid"])
        st = theirs.stat(what)
        checked["items"] += 1
        where = "%s (%d)" % (it["path"], it["cnid"])
        if not st:
            fails.append("%s: hfsdump has no such CNID" % where)
            continue
        if st.get("type") != it["kind"]:
            fails.append("%s: kind %s, hfsdump %s" % (where, it["kind"], st.get("type")))
            continue
        if it["kind"] != "file":
            continue
        for key, want in (("data_fork.logical_size", it["data"]), ("rsrc_fork.logical_size", it["rsrc"]),
                          ("user_info.file_type", bytes.fromhex(it["type"]).decode("latin-1")),
                          ("user_info.file_creator", bytes.fromhex(it["creator"]).decode("latin-1"))):
            if str(st.get(key, "")).strip("\0 ") != str(want).strip("\0 "):    # hfsdump: a zero type as ""
                fails.append("%s: %s %r, hfsdump %r" % (where, key, want, st.get(key)))
        for fork, size in (("", it["data"]), ("rsrc", it["rsrc"])):
            mine = subprocess.run([dump, image, "read", str(it["cnid"])] + ([fork] if fork else []),
                                  capture_output=True).stdout
            if len(mine) != size:
                fails.append("%s: %s fork %d bytes read, %d long" % (where, fork or "data", len(mine), size))
            if fork and "\u0000" in hpath(it["cnid"]):
                continue                        # in the hard links' folder: no path to give hfsdump
            rc, theirs_bytes = theirs.run("read", hpath(it["cnid"]) + "/rsrc" if fork else what)
            if rc != 0 and not size:
                continue
            if rc != 0:
                fails.append("%s: hfsdump can't read its %s fork" % (where, fork or "data"))
            elif sha(mine) != sha(theirs_bytes):
                fails.append("%s: %s fork differs from hfsdump's" % (where, fork or "data"))
            else:
                checked["forks"] += 1
    return vol, items, fails, checked


def check_classic_names(items):
    """The long-name fixture: our short names against 9.0.4's."""
    want = {}
    for line in open(ORACLE, encoding="mac_roman"):
        p = line.split()
        if p and p[0] in ("FILE", "FOLDER") and p[3] == "0":
            want[int(p[-1])] = p[5]
    fails = []
    for it in items:
        if it["cnid"] in want and it["classic"] != want[it["cnid"]]:
            fails.append("%s: short name %r, 9.0.4 %r" % (
                it["path"], bytes.fromhex(it["classic"]).decode("mac_roman"),
                bytes.fromhex(want[it["cnid"]]).decode("mac_roman")))
    missing = set(want) - {it["cnid"] for it in items}
    fails += ["CNID %d: made on 9.0.4, not in our tree" % c for c in sorted(missing)]
    return len(want), fails


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True)
    ap.add_argument("--hfsdump", default=os.environ.get("HFSDUMP", os.path.join(RECON, "src", "hfsfuse", "hfsdump")))
    ap.add_argument("--sample", type=int, default=1, help="items: every Nth (folder listings: all)")
    ap.add_argument("images", nargs="*")
    args = ap.parse_args()
    if not os.access(args.hfsdump, os.X_OK):
        print("SKIP: no hfsdump at %s" % args.hfsdump)
        return 77
    home_tmp = os.path.expanduser("~/storage/tmp")      # not /tmp: tmpfs, in RAM
    scratch = tempfile.mkdtemp(prefix="hfsplus-", dir=home_tmp if os.path.isdir(home_tmp) else None)
    try:
        images = args.images
        if not images:
            for gz in sorted(glob.glob(os.path.join(FIXTURES, "*.img.gz"))):
                out = os.path.join(scratch, os.path.basename(gz)[:-3])
                with gzip.open(gz) as src, open(out, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                images.append(out)
            for name in ("carbonlib-1.6.img", "guest-written.img"):
                path = os.path.join(RECON, "images", name)
                if os.path.exists(path):
                    images.append(path)
        failed = False
        for image in images:
            vol, items, fails, checked = check_image(args.dump, args.hfsdump, image, args.sample)
            extra = ""
            if os.path.basename(image) == "longnames.img":
                n, more = check_classic_names(items)
                fails += more
                extra = ", %d short names checked against 9.0.4" % n
            print("%s %s: %s %s%s, %d items; %d folder listings, %d items, %d forks the same as hfsdump's%s"
                  % ("FAIL" if fails else "PASS", os.path.basename(image), vol["volume"], vol["signature"],
                     " (wrapped)" if vol["wrapped"] else "", len(items), checked["folders"], checked["items"],
                     checked["forks"], extra))
            for f in fails:
                print("    " + f)
            failed |= bool(fails)
        return 1 if failed else 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
