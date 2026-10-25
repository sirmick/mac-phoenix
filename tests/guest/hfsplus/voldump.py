#!/usr/bin/env python3
"""voldump.py --machine 9.0.4|executor --out DIR IMAGE...

Runs VolDump (this folder) against HFS+ disk images, one boot for all of
them, and writes DIR/voldump-<image>.txt for each: on Mac OS 9.0.4 (the G3
machine on mame-ppc; the images read-only on the host, so the Mac sees
locked disks, as Executor mounts HFS+) to make the oracle, or on Executor
to check it (tests/test_hfsplus_volume.py compares the two).

Each image's volume needs a name of its own; images are copied sparse
(the big ones of test_hfsplus_volume.py are mostly holes). MACPHOENIX_EMULATOR names the
emulator (default build/mac-phoenix); --dump the build's hfsplus_dump
(default build/src/executor/romlib/hfsplus_dump), which reads the names.
"""
import argparse
import json
import os
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


def volume_name(dump, image):
    out = subprocess.run([dump, image], capture_output=True, check=True).stdout
    return json.loads(out.splitlines()[0])["volume"]


def run_all(g, host, names, out_dir, stems):
    for name, stem in zip(names, stems):
        report = os.path.join(host, "voldump.txt")
        if os.path.exists(report):
            os.unlink(report)
        with open(os.path.join(host, "voldump.cfg"), "wb") as f:
            f.write(name.encode("mac_roman") + b"\r")
        r = g.bridge("/api/launch", {"path": "Host:VolDump"})
        if not r.get("success"):
            raise RuntimeError("launch: %s" % r)
        for _ in range(300):
            if os.path.exists(report) and b"\r---" in open(report, "rb").read():
                break
            g.pause(1)
        else:
            raise RuntimeError("VolDump did not finish on %s" % name)
        text = open(report, "rb").read().decode("mac_roman").replace("\r", "\n")
        with open(os.path.join(out_dir, "voldump-%s.txt" % stem), "w") as f:
            f.write(text)
        print("  %s: %d lines" % (name, text.count("\n")))
        g.pause(2)                              # VolDump quits; the Finder comes back


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--machine", choices=["9.0.4", "executor"], required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dump", default=os.path.join(ROOT, "build", "src", "executor", "romlib", "hfsplus_dump"))
    ap.add_argument("--port", type=int, default=19500 + os.getpid() % 400)
    ap.add_argument("--work")
    ap.add_argument("images", nargs="+")
    args = ap.parse_args()

    names = [volume_name(args.dump, i) for i in args.images]
    if len(set(names)) != len(names):
        sys.exit("volume names must differ: %s" % names)
    stems = [os.path.splitext(os.path.basename(i))[0] for i in args.images]
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    work = os.path.abspath(args.work or os.path.join(out_dir, "work-" + args.machine))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    # copies, read-only: the guest sees locked disks and can't change them
    disks = []
    for image in args.images:
        copy = os.path.join(work, os.path.basename(image))
        if subprocess.run(["cp", "--sparse=always", image, copy]).returncode:    # sparse: a 12 GB image stays small
            shutil.copyfile(image, copy)
        os.chmod(copy, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
        disks.append(copy)

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
        # Apple's 7.5.5 System (BridgeAgent runs there), as the guest suite does
        system = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                          "tests-7.5.5", str(os.getpid())], text=True).strip()
        g = guest.Guest(work, "executor", disks, [], args.port, STORAGE, timeout=1800,
                        extra=("--executor-system", os.path.basename(system)))
    try:
        install_macbinary.install(os.path.join(HERE, "VolDump.bin"), g.host_dir(), "VolDump")
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
        run_all(g, g.host_dir(), names, out_dir, stems)
        if args.machine == "9.0.4":
            g.shutdown()
    finally:
        g.stop()
        for d in disks:
            os.chmod(d, stat.S_IRUSR | stat.S_IWUSR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
