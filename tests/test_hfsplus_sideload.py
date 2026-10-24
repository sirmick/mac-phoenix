#!/usr/bin/env python3
"""test_hfsplus_sideload.py -- Executor's System from an HFS+ image
(hfs_sideload.cpp through hfs/plus): mac-phoenix --executor-system on the
long-name fixture, whose blessed System Folder (made on Mac OS 9.0.4 by
MakeLongNames) holds stand-in System and Finder files and a Preferences
folder with a long-named file. The copy in <storage>/executor-systems
must have them, data forks as the files and resource forks and Finder
info in the AppleDouble "%NAME", byte for byte as hfsplus_dump reads them.

    test_hfsplus_sideload.py --emulator BUILD/mac-phoenix --dump BUILD/hfsplus_dump
"""
import argparse
import gzip
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus", "longnames.img.gz")


def appledouble(path):
    b = open(path, "rb").read()
    n = struct.unpack(">H", b[24:26])[0]
    entries = {}
    for i in range(n):
        eid, off, ln = struct.unpack(">III", b[26 + 12 * i:38 + 12 * i])
        entries[eid] = b[off:off + ln]
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--emulator", required=True)
    ap.add_argument("--dump", required=True)
    args = ap.parse_args()
    home_tmp = os.path.expanduser("~/storage/tmp")      # not /tmp: tmpfs
    storage = tempfile.mkdtemp(prefix="hfsplus-sideload-", dir=home_tmp if os.path.isdir(home_tmp) else None)
    try:
        os.makedirs(os.path.join(storage, "images"))
        image = os.path.join(storage, "images", "longnames.img")
        with gzip.open(FIXTURE) as src, open(image, "wb") as dst:
            shutil.copyfileobj(src, dst)
        # The System is copied out when the machine starts; the stand-in
        # System doesn't run, the copy is all this looks at.
        sysdir = os.path.join(storage, "executor-systems", "longnames.img.clean", "System Folder")
        port = 19600 + os.getpid() % 300
        proc = subprocess.Popen([args.emulator, "--config", "/dev/null", "--storage-dir", storage,
                                 "--backend", "executor", "--executor-system", "longnames.img",
                                 "--headless-http", "--port", str(port), "--timeout", "60"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(60):
                try:
                    urllib.request.urlopen(urllib.request.Request(
                        "http://127.0.0.1:%d/api/emulator/start" % port, data=b"{}"), timeout=5).read()
                    break
                except OSError:
                    time.sleep(0.5)
            for _ in range(60):
                if os.path.exists(os.path.join(sysdir, "Finder")):
                    time.sleep(1)               # the rest of the copy
                    break
                time.sleep(0.5)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
        tree = [json.loads(l) for l in subprocess.run([args.dump, image], capture_output=True,
                                                       check=True).stdout.decode().splitlines()]
        vol, items = tree[0], tree[1:]
        want = [it for it in items if it["path"].startswith("/System Folder/") and it["kind"] == "file"
                and it["path"].split("/")[2] in ("System", "Finder", "Preferences")]
        fails = []
        if not os.path.isdir(sysdir):
            fails.append("no %s" % sysdir)
        for it in want if not fails else []:
            name = bytes.fromhex(it["classic"]).decode("mac_roman")
            rel = it["path"].split("/")[2:-1] + [name]
            host = os.path.join(sysdir, *rel)
            data = subprocess.run([args.dump, image, "read", str(it["cnid"])], capture_output=True).stdout
            rsrc = subprocess.run([args.dump, image, "read", str(it["cnid"]), "rsrc"], capture_output=True).stdout
            if not os.path.exists(host):
                fails.append("%s: not copied (as %s)" % (it["path"], os.path.join(*rel)))
                continue
            if open(host, "rb").read() != data:
                fails.append("%s: data fork differs" % it["path"])
            ad = appledouble(os.path.join(os.path.dirname(host), "%" + name))
            if ad.get(2, b"") != rsrc:
                fails.append("%s: resource fork differs" % it["path"])
            if ad.get(9, b"")[:8] != bytes.fromhex(it["type"] + it["creator"]):
                fails.append("%s: type/creator %r" % (it["path"], ad.get(9, b"")[:8]))
        print("%s: blessed folder %d, %d files checked%s" % ("FAIL" if fails else "PASS", vol["blessed"],
                                                               len(want), "" if fails else " (System, Finder, Preferences)"))
        for f in fails:
            print("    " + f)
        return 1 if fails or len(want) < 3 else 0
    finally:
        shutil.rmtree(storage, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
