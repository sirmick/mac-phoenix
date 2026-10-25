#!/usr/bin/env python3
"""fsreftest.py --machine 9.0.4|executor --out DIR

FSRefTest (this folder, Mac OS 9.0's HFS Plus APIs) against three volumes,
one boot: the long-name fixture read-only ("LongNames": refs, catalog
info, iteration, forks read, search), a blank HFS+ volume and a blank HFS
one, both writable ("Blank", "Classic": creates, renames, moves, forks,
deletes in a folder of their own), and the 12 GB volume of forks past
2 GB (test_hfsplus_volume.py's big-12g, read-only: the fork calls there). On Mac OS 9.0.4 (the G3 machine on
mame-ppc) for the oracle, or on Executor (Apple's 7.5.5 System) to compare
(tests/test_hfsplus_fsref.py). DIR gets fsreftest.txt.

The HFS blank is made with mkfs.hfsplus -h (docker image macphoenix-hfsprogs).
MACPHOENIX_EMULATOR names the emulator (default build/mac-phoenix).
"""
import argparse
import gzip
import os
import re
import shutil
import stat
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
sys.path.insert(0, os.path.join(ROOT, "tests", "guest"))
import guest          # noqa: E402
import install_macbinary  # noqa: E402

STORAGE = os.environ.get("MACEMU_STORAGE", os.path.expanduser("~/storage"))
FIXTURES = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus")
CFG = os.environ.get("FSREFTEST_CFG", "read LongNames\rwrite Blank\rwrite untitled\rbig BigFork")   # mkfs.hfsplus -h names its HFS volume so


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--machine", choices=["9.0.4", "executor"], required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", type=int, default=19500 + os.getpid() % 400)
    ap.add_argument("--dump", default=os.path.join(ROOT, "build", "src", "executor", "romlib", "hfsplus_dump"))
    ap.add_argument("--bin", default=os.path.join(HERE, "FSRefTest.bin"),
                    help="the FSRefTest to run (FSRefTestPPC.bin: the PowerPC one)")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    work = os.path.join(out, "work-" + args.machine)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    disks = []
    for stem in ("longnames", "blank-64m"):
        path = os.path.join(work, stem + ".img")
        with gzip.open(os.path.join(FIXTURES, stem + ".img.gz")) as src, open(path, "wb") as dst:
            shutil.copyfileobj(src, dst)
        disks.append(path)
    os.chmod(disks[0], stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)     # LongNames: locked
    classic = os.path.join(work, "classic.img")
    with open(classic, "wb") as f:
        f.truncate(32 << 20)
    subprocess.run(["docker", "run", "--rm", "-v", work + ":/w", "macphoenix-hfsprogs", "mkfs.hfsplus", "-h",
                    "-v", "Classic", "/w/classic.img"], check=True, capture_output=True)
    disks.append(classic)
    # BigFork: forks past 2 GB, read-only (test_hfsplus_volume.py's big-12g)
    import json
    spec = json.load(open(os.path.join(FIXTURES, "generated.json")))["big-12g"]
    bigimg = os.path.join(work, "big-12g.img")
    with open(bigimg, "wb") as f:
        f.truncate(12 << 30)
    subprocess.run(["docker", "run", "--rm", "-v", work + ":/w", "macphoenix-hfsprogs", "mkfs.hfsplus", "-v",
                    spec["volume"]] + spec["mkfs"] + ["/w/big-12g.img"], check=True, capture_output=True)
    subprocess.run([args.dump, bigimg, "script", os.path.join(FIXTURES, "big-12g.script")], check=True)
    os.chmod(bigimg, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
    disks.append(bigimg)

    if args.machine == "9.0.4":
        rom = guest.roms(STORAGE)["kpx"]
        if not os.path.exists(rom):
            print("no G3 ROM at %s" % rom)
            return 77
        boot = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                        "tests-9.0.4", str(os.getpid())], text=True).strip()
        g = guest.Guest(work, "kpx", [boot] + disks, [], args.port, STORAGE, timeout=1800,
                        extra=("--core", "mame-ppc", "--jit"))
    else:
        system = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                          "tests-7.5.5", str(os.getpid())], text=True).strip()
        g = guest.Guest(work, "executor", disks, [], args.port, STORAGE, timeout=1800,
                        extra=("--executor-system", os.path.basename(system), "--executor-writable-images")
                        + tuple(os.environ.get("FSREFTEST_EXTRA", "").split()))
    try:
        install_macbinary.install(args.bin, g.host_dir(), "FSRefTest")
        print("booting %s..." % args.machine)
        if args.machine == "9.0.4":
            g.wait_finder(240, agent=True)
            g.close_energy_saver(5)
        else:
            deadline = time.time() + 120
            while not (g.get("/api/status") or {}).get("bridge_agent_connected"):
                if time.time() > deadline:
                    raise RuntimeError("no bridge agent")
                time.sleep(1)
        host = g.host_dir()
        report = os.path.join(host, "fsreftest.txt")
        with open(os.path.join(host, "fsreftest.cfg"), "wb") as f:
            f.write(CFG.encode("mac_roman") + b"\r")
        r = g.bridge("/api/launch", {"path": "Host:FSRefTest"})
        if not r.get("success"):
            raise RuntimeError("launch: %s" % r)
        for _ in range(600):
            if os.path.exists(report) and re.search(rb"[\r\n]---[\r\n]", open(report, "rb").read()):    # (CodeWarrior's C: LF)
                break
            g.pause(1)
        else:
            print("FSRefTest did not finish")
            g.shot("unfinished")
        text = open(report, "rb").read().decode("mac_roman").replace("\r\n", "\n").replace("\r", "\n") \
            if os.path.exists(report) else ""
        open(os.path.join(out, "fsreftest.txt"), "w").write(text)
        print("  FSRefTest: %d lines" % text.count("\n"))
        g.pause(2)
        if args.machine == "9.0.4":
            g.shutdown()
    finally:
        g.stop()
        for d in (disks[0], disks[-1]):
            os.chmod(d, stat.S_IRUSR | stat.S_IWUSR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
