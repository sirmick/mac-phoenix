#!/usr/bin/env python3
"""volwrite.py --machine 9.0.4|executor --out DIR

VolWrite (this folder) on a writable HFS+ volume, an application copied
onto it (/api/copy), then VolDump on the result: on Mac OS 9.0.4 (the G3 machine on mame-ppc) for the oracle, or on
Executor to compare (tests/test_hfsplus_write.py, web/tests/tab_hfsplus_write.mjs).
The volume: src/executor/tests/fixtures/hfsplus/blank-long-64m.img.gz, the
blank fixture (mkfs.hfsplus, "Blank") with a folder "Long" of long-named
files made by hfsplus_dump's script mode (names the classic calls can't
make).

DIR gets volwrite.txt, voldump.txt and the volume after (written.img).
MACPHOENIX_EMULATOR names the emulator (default build/mac-phoenix).
"""
import argparse
import gzip
import os
import shutil
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
# the blank fixture (mkfs.hfsplus, "Blank") with a folder "Long" of
# long-named files, made by hfsplus_dump's script mode:
#   mkdir Long
#   create "Long/A file whose name is longer than thirty-one.txt" TEXT ttxt  (1500 bytes)
#   create "Long/Another long name, to be deleted.txt" TEXT ttxt  (70000, 300 in the resource fork)
FIXTURE = os.path.join(ROOT, "src", "executor", "tests", "fixtures", "hfsplus", "blank-long-64m.img.gz")


def run(g, host, app, cfg_name, cfg, out_name):
    report = os.path.join(host, out_name)
    if os.path.exists(report):
        os.unlink(report)
    with open(os.path.join(host, cfg_name), "wb") as f:
        f.write(cfg.encode("mac_roman") + b"\r")
    r = g.bridge("/api/launch", {"path": "Host:" + app})
    if not r.get("success"):
        raise RuntimeError("launch %s: %s" % (app, r))
    for _ in range(600):
        if os.path.exists(report) and b"\r---" in open(report, "rb").read():
            break
        g.pause(1)
    else:
        raise RuntimeError("%s did not finish" % app)
    g.pause(2)
    return open(report, "rb").read().decode("mac_roman").replace("\r", "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--machine", choices=["9.0.4", "executor"], required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dump", default=os.path.join(ROOT, "build", "src", "executor", "romlib", "hfsplus_dump"))
    ap.add_argument("--port", type=int, default=19500 + os.getpid() % 400)
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    work = os.path.join(out, "work-" + args.machine)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    volume = os.path.join(work, "Blank.img")
    with gzip.open(FIXTURE) as src, open(volume, "wb") as dst:
        shutil.copyfileobj(src, dst)

    if args.machine == "9.0.4":
        rom = guest.roms(STORAGE)["kpx"]
        if not os.path.exists(rom):
            print("no G3 ROM at %s" % rom)
            return 77
        boot = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                        "tests-9.0.4", str(os.getpid())], text=True).strip()
        g = guest.Guest(work, "kpx", [boot, volume], [], args.port, STORAGE, timeout=1800,
                        extra=("--core", "mame-ppc", "--jit"))
    else:
        system = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                          "tests-7.5.5", str(os.getpid())], text=True).strip()
        g = guest.Guest(work, "executor", [volume], [], args.port, STORAGE, timeout=1800,
                        extra=("--executor-system", os.path.basename(system), "--executor-writable-images"))
    try:
        for app in ("VolWrite", "VolDump"):
            install_macbinary.install(os.path.join(HERE, app + ".bin"), g.host_dir(), app)
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
        log = run(g, g.host_dir(), "VolWrite", "volwrite.cfg", "Blank", "volwrite.txt")
        # an application copied in through the File Manager (BridgeAgent's
        # copy: both forks, Finder info), as the Finder would
        r = g.bridge("/api/copy", {"from": "Host:VolDump", "to": "Blank:Long:"})
        log += "COPY Host:VolDump -> %s\n" % ("ok" if r.get("success") else r.get("error_code", r))
        open(os.path.join(out, "volwrite.txt"), "w").write(log)
        dump = run(g, g.host_dir(), "VolDump", "voldump.cfg", "Blank", "voldump.txt")
        open(os.path.join(out, "voldump.txt"), "w").write(dump)
        print("  VolWrite %d lines, VolDump %d lines" % (log.count("\n"), dump.count("\n")))
        g.shutdown()                            # the volume unmounted cleanly
    finally:
        g.stop()
    shutil.copyfile(volume, os.path.join(out, "written.img"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
