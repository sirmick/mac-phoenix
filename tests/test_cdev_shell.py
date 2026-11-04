#!/usr/bin/env python3
"""test_cdev_shell.py -- the control panel shell on Mac OS 9.0.4: what a
classic control panel ('cdev') is sent and when (Inside Macintosh: More
Macintosh Toolbox, ch. 8; Technote 215's cursorDev), against what the G3's
9.0.4 Finder does (tests/guest/cdev/expected.txt, measured with --record).

CdevTest (tests/guest/cdev, nine variants) writes every message it gets to
Host:cdevtest.log, and at initDev where its window is. Each is opened
through the Finder (/api/launch), the cursor rests over it, its "Hit"
button is clicked and it is closed with Command-W; an alert is answered
with Return. The transcript: the panel's
messages, where its window opened (or no window, an alert), the process left
in front, the position the shell saved in the panel's 'wloc' -16474.

    test_cdev_shell.py [--backend executor|kpx] [--record] [--keep DIR]

kpx is the reference (the G3 ROM, the tests-9.0.4 image); executor runs on
9.0.4's own System (--executor-ppc). Exit 77 (skip) without the image/ROM.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import guest  # noqa: E402

STORAGE = os.environ.get("MACEMU_STORAGE", os.path.expanduser("~/storage"))
CDEV_DIR = os.path.join(ROOT, "tests", "guest", "cdev")
EXPECTED = os.path.join(CDEV_DIR, "expected.txt")
VARIANTS = ["CdevMac", "CdevAll", "CdevNoRun", "CdevInitRes", "CdevNulMem", "CdevHitGen",
            "CdevMachII", "CdevMachADB", "CdevMach0"]


def counts_coarse(line):
    """closeDev's "nul=N cursor=M": the numbers depend on timing; keep
    whether each is zero."""
    out = []
    for word in line.split():
        if word.startswith(("nul=", "cursor=")):
            k, v = word.split("=")
            word = "%s=%s" % (k, "0" if v == "0" else "N")
        out.append(word)
    return " ".join(out)


def new_lines(log_path, start):
    with open(log_path, "rb") as f:
        f.seek(start)
        return f.read().decode("mac_roman").replace("\r", "\n").splitlines()


def where(lines):
    """The panel's window and Hit button on the screen, from initDev's line
    (/api/ui doesn't answer on Executor), or None; and the lines without it."""
    found, out = None, []
    for l in lines:
        m = re.search(r" win=(-?\d+),(-?\d+) (-?\d+),(-?\d+) hit=(-?\d+),(-?\d+)", l)
        if m:
            found = [int(v) for v in m.groups()]
            l = l[:m.start()] + l[m.end():]
        out.append(l)
    return found, out


def run_variant(g, name, log_path):
    """Open the panel, rest the cursor over it, click Hit, close it with
    Command-W; an alert (the panel's process in front with no window) is
    answered with Return."""
    lines = []
    start = os.path.getsize(log_path) if os.path.exists(log_path) else 0
    r = g.post("/api/launch", {"path": "Host:" + name}, 60) or {}
    if not r.get("success"):
        lines.append("%s launch failed: %s" % (name, r.get("message")))
    time.sleep(4)
    w, _ = where(new_lines(log_path, start)) if os.path.exists(log_path) else (None, [])
    lines.append("%s window=%s" % (name, "%d,%d %d,%d" % tuple(w[:4]) if w else "no"))
    if w:
        left, top, right, bottom, hx, hy = w
        g.mouse(x=(left + right) // 2, y=bottom - 4)        # over the panel
        time.sleep(3)
        g.mouse(x=630, y=470)
        time.sleep(2)
        g.click(hx, hy)
        g.mouse(x=630, y=470)
        time.sleep(1)
        g.cmd_key(0x0D)                                     # Command-W: File > Close
        time.sleep(2)
    elif g.app() == name:
        lines.append(name + " alert")
        g.post("/api/keypress", {"key": "return"})
        time.sleep(2)
    time.sleep(1)
    lines.append("%s front=%s" % (name, "panel" if g.app() == name else "other"))
    got = where(new_lines(log_path, start) if os.path.exists(log_path) else [])[1]
    wloc = resource(os.path.join(os.path.dirname(log_path), ".rsrc", name), b"wloc", -16474)
    if wloc:
        lines.append("%s wloc=%d,%d" % ((name,) + struct.unpack(">hh", wloc[:4])[::-1]))
    return collapse([counts_coarse(l) for l in got]) + lines


def resource(path, rtype, rid):
    """A resource's data from a resource fork file, or None."""
    try:
        r = open(path, "rb").read()
        data_off, map_off, _, map_len = struct.unpack(">4I", r[:16])
        m = r[map_off:map_off + map_len]
        types = struct.unpack(">H", m[24:26])[0]
        for t in range(struct.unpack(">H", m[types:types + 2])[0] + 1):
            typ, count, off = struct.unpack(">4sHH", m[types + 2 + 8 * t:types + 10 + 8 * t])
            for q in range(count + 1 if typ == rtype else 0):
                i, _, attr_off = struct.unpack(">hhI", m[types + off + 12 * q:types + off + 12 * q + 8])
                if i == rid:
                    at = data_off + (attr_off & 0xFFFFFF)
                    return r[at + 4:at + 4 + struct.unpack(">I", r[at:at + 4])[0]]
    except (OSError, struct.error):
        pass
    return None


def collapse(lines):
    """A run of the same line, or of the same two lines (cursorDev and
    nulDev at each idle), once: how many depends on timing."""
    out = []
    for l in lines:
        out.append(l)
        while True:
            if len(out) >= 2 and out[-1] == out[-2]:
                out.pop()
            elif len(out) >= 4 and out[-2:] == out[-4:-2]:
                del out[-2:]
            else:
                break
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--backend", default="executor", choices=("executor", "kpx"))
    ap.add_argument("--record", action="store_true", help="write expected.txt from this run (kpx)")
    ap.add_argument("--port", type=int, default=guest.free_port())
    ap.add_argument("--keep")
    ap.add_argument("--only", nargs="*", help="these variants only")
    args = ap.parse_args()

    if args.backend == "kpx":
        if not os.path.exists(os.path.join(STORAGE, "roms", "g3.rom")):
            print("SKIP: no g3.rom")
            return 77
        if not os.path.exists(os.path.join(STORAGE, "images", "tests-9.0.4.img")):
            print("SKIP: no tests-9.0.4.img")
            return 77
    elif not os.path.exists(os.path.join(STORAGE, "images", "9.0.4.img")):
        print("SKIP: no 9.0.4.img")
        return 77
    home_tmp = os.path.join(STORAGE, "tmp")             # not /tmp: tmpfs
    work = os.path.abspath(args.keep or tempfile.mkdtemp(prefix="cdev-shell-",
                                                         dir=home_tmp if os.path.isdir(home_tmp) else None))
    os.makedirs(work, exist_ok=True)

    disk = None
    if args.backend == "kpx":
        disk = subprocess.check_output(["bash", os.path.join(ROOT, "tests", "lib", "refresh_test_disk.sh"),
                                        "tests-9.0.4", str(os.getpid())], text=True).strip()
        g = guest.Guest(work, "kpx", [disk], [], args.port, STORAGE, timeout=900)
    else:
        g = guest.Guest(work, "executor", [], [], args.port, STORAGE, timeout=900,
                        extra=("--executor-system", "9.0.4.img", "--executor-ppc"))
    for v in VARIANTS:
        subprocess.run([sys.executable, os.path.join(ROOT, "tests", "guest", "install_macbinary.py"),
                        os.path.join(CDEV_DIR, v + ".bin"), g.host_dir(), v], check=True,
                       stdout=subprocess.DEVNULL)
    log_path = os.path.join(g.host_dir(), "cdevtest.log")
    transcript = []
    try:
        deadline = time.time() + 180
        while not (g.get("/api/status") or {}).get("bridge_agent_connected"):
            if time.time() > deadline:
                print("FAIL: no bridge agent")
                return 1
            time.sleep(1)
        time.sleep(3)
        for v in args.only or VARIANTS:
            transcript += run_variant(g, v, log_path)
            g.shot(v)
    finally:
        g.stop()
        if disk and os.path.exists(disk):
            os.remove(disk)

    with open(os.path.join(work, "transcript.txt"), "w") as f:
        f.write("\n".join(transcript) + "\n")
    if args.record:
        with open(EXPECTED, "w") as f:
            f.write("\n".join(transcript) + "\n")
        print("recorded %s (%d lines)" % (EXPECTED, len(transcript)))
        return 0
    with open(EXPECTED) as f:
        want = f.read().splitlines()
    if args.only:
        want = [l for l in want if l.split()[0] in args.only]
    if transcript == want:
        print("PASS the control panel shell sent what the G3's 9.0.4 Finder sends (%d lines)" % len(want))
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        return 0
    import difflib
    print("FAIL the transcript differs from expected.txt (work dir %s):" % work)
    sys.stdout.writelines(l + "\n" for l in difflib.unified_diff(want, transcript, "G3", args.backend, lineterm=""))
    return 1


if __name__ == "__main__":
    sys.exit(main())
