#!/usr/bin/env python3
"""run.py: the speed table (docs/executor/Speed.md). The Utilities disk's
benchmarks on Executor (68k, PowerPC; natively and in the tab) and on the
ROM machines (Quadra 650, G3), each configuration RUNS times; the scores
of each run in OUT/results.jsonl, the screens of each result in OUT/shots,
the table of medians in OUT/table.md.

  tools/bench/run.py [--only CONFIG,...] [--bench speedometer,macbench]
                     [--runs 3] [--out DIR] [--binary build/mac-phoenix]
                     [--wasm build-wasm/...] [--port 19420]
  tools/bench/run.py --table DIR          the table again from DIR's results

Speedometer 4.0 (68k and PowerPC code): its splash, Not Yet at its
registration screen, then Tests > Performance Rating, Benchmark Mix, FPU
Benchmarks and Color QuickDraw, one after another with every test of each
ticked, and its text report saved after each (File > Save Text Report,
into its own folder on the Utilities disk), so that a run that crashes in
one test keeps the others. The reports are read out of the disk image
afterwards (hfsutils; in the tab, the image as changed, 'download').
MacBench 5.0 (PowerPC only): its licence scrolled to the end and agreed
to, then its Processor test (Test Suites > Processor), its scores read
off the screen.

Each session: a scratch --storage-dir with copies of the System's and the
Utilities disk's images, --config /dev/null, --no-idlewait, 640x480,
--depth 8 where the machine takes one (68k ROM machine, Executor).
"""
import argparse
import json
import os
import re
import shutil
import signal
import statistics
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOME = os.path.expanduser("~")
IMAGES = os.path.join(HOME, "storage/images")
QUADRA_ROM = os.path.join(HOME, "roms/quadra.rom")
G3_ROM = os.path.join(HOME, "storage/roms/g3.rom")
UTILITIES = "utilities.img"
WEB_LIBRARY = os.path.join(HOME, "storage/web-library")              # the tab's boot disks
TAB_UTILITIES = os.path.join(HOME, "storage/tmp/utils-disk/web-library")  # make_library.py's Utilities
SPEEDOMETER = "Utilities:Speedometer 4.0 ƒ:Speedometer 4.0"
MACBENCH = "Utilities:MacBench 5.0 ƒ:MacBench® 5.0"

# name: (kind, System image, arguments, look). look: the System's
# appearance, which places menus and alerts (7.x: System 7's Chicago bar;
# 8.6: Appearance's platinum).
CONFIGS = {
    # The ROM machines: Quadra 650 on System 7.6.1, G3 on Mac OS 8.6.
    "quadra-uae":       ("rom", "7.6.1", ["--backend", "uae", "--core", "uae", "--no-jit", "--rom", QUADRA_ROM, "--depth", "8"], "7"),
    "quadra-uae-jit":   ("rom", "7.6.1", ["--backend", "uae", "--core", "uae", "--jit", "--rom", QUADRA_ROM, "--depth", "8"], "7"),
    "quadra-mame68k":   ("rom", "7.6.1", ["--backend", "uae", "--core", "mame-68k", "--rom", QUADRA_ROM, "--depth", "8"], "7"),
    "g3-kpx":           ("rom", "8.6", ["--backend", "kpx", "--core", "kpx", "--no-jit", "--rom", G3_ROM], "8"),
    "g3-kpx-jit":       ("rom", "8.6", ["--backend", "kpx", "--core", "kpx", "--jit", "--rom", G3_ROM], "8"),
    "g3-mameppc-jit":   ("rom", "8.6", ["--backend", "kpx", "--core", "mame-ppc", "--jit", "--rom", G3_ROM], "8"),
    "g3-mameppc":       ("rom", "8.6", ["--backend", "kpx", "--core", "mame-ppc", "--no-jit", "--rom", G3_ROM], "8"),
    # Executor, natively: 68k code on mame-68k; PowerPC code on mame-ppc.
    "exec-761-68k":     ("executor", "7.6.1", ["--depth", "8"], "7"),
    "exec-761-ppc":     ("executor", "7.6.1", ["--depth", "8", "--executor-ppc"], "7"),
    "exec-86-jit":      ("executor", "8.6", ["--depth", "8", "--executor-ppc"], "8"),
    "exec-86":          ("executor", "8.6", ["--depth", "8", "--executor-ppc", "--executor-debug", "ppcinterp"], "8"),
    # Executor in the tab (build-wasm, headless Chromium): tools/bench/tab.mjs.
    "tab-761-68k":      ("tab", "7.6.1", {"depth": 8}, "7"),
    "tab-761-ppc":      ("tab", "7.6.1", {"depth": 8, "ppc": 1}, "7"),
    "tab-86":           ("tab", "8.6", {"depth": 8, "ppc": 1}, "8"),
}
# Which benchmarks each configuration runs (MacBench needs a PowerPC).
PPC = {"g3-kpx", "g3-kpx-jit", "g3-mameppc-jit", "g3-mameppc", "exec-761-ppc", "exec-86-jit", "exec-86",
       "tab-761-ppc", "tab-86"}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ---- a session: one machine, driven through the HTTP API or the tab ----

class Native:
    """mac-phoenix with the web server on PORT (the ROM machines, Executor)."""

    def __init__(self, binary, work, port, kind, system, args, disks):
        """DISKS: (name, source image) mounted after the System's, copies."""
        self.port, self.work, self.kind = port, work, kind
        st = os.path.join(work, "st")
        os.makedirs(os.path.join(st, "images"), exist_ok=True)
        extra = []
        for name, src in disks:
            extra.append(os.path.join(st, "images", name))
            shutil.copyfile(src, extra[-1])
        sysimg = os.path.join(st, "images", f"{system}.img")
        shutil.copyfile(os.path.join(IMAGES, f"{system}.img"), sysimg)
        cmd = [binary, "--config", "/dev/null", "--storage-dir", st, "--port", str(port),
               "--timeout", "3600", "--bridge", "--no-idlewait", "--network", "none",
               "--screen", "640x480"]
        if kind == "executor":
            cmd += ["--backend", "executor", "--executor-system", f"{system}.img",
                    "--executor-writable-images"]
        else:
            cmd += ["--disk", sysimg]
            if "kpx" in args:
                cmd += ["--max-resolution", "640x480"]
        for e in extra:
            cmd += ["--disk", e]
        cmd += args
        self.disks = extra + ([sysimg] if kind != "executor" else [])
        self.log = open(os.path.join(work, "emulator.log"), "w")
        self.proc = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
        for _ in range(60):
            if self.get("status", timeout=2):
                break
            time.sleep(0.5)
        self.post("emulator/start", {})

    def url(self, path):
        return f"http://localhost:{self.port}/api/{path}"

    def get(self, path, timeout=10, raw=False):
        try:
            with urllib.request.urlopen(self.url(path), timeout=timeout) as r:
                data = r.read()
                return data if raw else json.loads(data)
        except (urllib.error.URLError, OSError, ValueError):
            return None

    def post(self, path, body, timeout=30):
        req = urllib.request.Request(self.url(path), data=json.dumps(body).encode(), method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return json.loads(r.read() or b"{}")
        except (urllib.error.URLError, OSError, ValueError):
            return None

    def alive(self):
        return self.proc.poll() is None and "crashed" not in open(self.log.name, errors="replace").read()

    def ready(self, limit=180):
        """Until BridgeAgent answers (the Finder up)."""
        end = time.time() + limit
        while time.time() < end:
            s = self.get("status", timeout=3)
            if s and s.get("bridge_agent_connected"):
                return True
            if self.proc.poll() is not None:
                return False
            time.sleep(1)
        return False

    def app(self):
        # (/api/app passes the name's MacRoman bytes through: "MacBench\xa8 5.0")
        raw = self.get("app", timeout=5, raw=True)
        if not raw:
            return None
        try:
            return json.loads(raw.decode("mac_roman")).get("app")
        except ValueError:
            return None

    def launch(self, path):
        r = self.post("launch", {"path": path}, timeout=60)
        return bool(r and r.get("success"))

    def screenshot(self):
        data = self.get("screenshot", raw=True)
        return data if data and data[:4] == b"\x89PNG" else None

    def mouse(self, x, y, down=None):
        b = {"x": x, "y": y}
        if down is not None:
            b.update(button=0, down=down)
        self.post("mouse", b)

    def key(self, code):
        self.post("keypress", {"key": code})

    def disk_images(self):
        """The disks after the session (it is stopped): paths of the images."""
        return self.disks

    def stop(self):
        if self.proc.poll() is None:
            if self.kind == "rom":           # the guest's caches written out
                self.post("shutdown", {}, timeout=10)
                for _ in range(60):
                    if self.proc.poll() is not None:
                        break
                    time.sleep(0.5)
            if self.proc.poll() is None:
                os.killpg(self.proc.pid, signal.SIGTERM)
                try:
                    self.proc.wait(10)
                except subprocess.TimeoutExpired:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                    self.proc.wait()
        self.log.close()


class Tab:
    """Executor in headless Chromium (build-wasm's page on tools/standalone),
    through tools/bench/tab.mjs: one JSON command a line."""

    def __init__(self, wasm, work, system, query, disks):
        """DISKS: (name, source image) mounted after the System's; a
        record beside the source (NAME.json), or a plain one."""
        self.work = work
        lib = os.path.join(work, "library")
        os.makedirs(lib, exist_ok=True)
        boot = f"boot-{system}.img"
        for f in (boot, boot + ".json"):
            if not os.path.exists(os.path.join(lib, f)):
                os.symlink(os.path.join(WEB_LIBRARY, f), os.path.join(lib, f))
        for name, src in disks:                         # read lazily, changed in an overlay
            shutil.copyfile(src, os.path.join(lib, name))
            if os.path.exists(src + ".json"):
                shutil.copyfile(src + ".json", os.path.join(lib, name + ".json"))
            else:
                with open(os.path.join(lib, name + ".json"), "w") as f:
                    json.dump({"title": name, "label": os.path.splitext(name)[0]}, f)
        self.names = [name for name, _ in disks]
        self.outs = [os.path.join(work, name) for name in self.names]
        self.log = open(os.path.join(work, "tab.log"), "w")
        env = dict(os.environ, TMPDIR=work)
        env.setdefault("NODE_PATH", os.path.join(HOME, "storage/tmp/wasm-spike/node_modules"))
        env.setdefault("CHROMIUM", os.path.join(
            HOME, ".cache/ms-playwright/chromium_headless_shell-1243/chrome-headless-shell-linux64/chrome-headless-shell"))
        self.proc = subprocess.Popen(
            ["node", os.path.join(REPO, "tools/bench/tab.mjs"), wasm, lib, system, json.dumps(query),
             ",".join(self.names)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, env=env, text=True,
            start_new_session=True)
        self.dead = False

    def call(self, op, *args, timeout=60):
        if self.dead:
            return None
        try:
            self.proc.stdin.write(json.dumps([op, *args]) + "\n")
            self.proc.stdin.flush()
            line = self.proc.stdout.readline()
        except (BrokenPipeError, OSError):
            self.dead = True
            return None
        if not line:
            self.dead = True
            return None
        r = json.loads(line)
        return r.get("result")

    def alive(self):
        return not self.dead and self.call("alive") is True

    def get(self, path, **kw):
        return None

    def ready(self, limit=240):
        return self.call("ready", limit * 1000) is True

    def app(self):
        return self.call("app")

    def launch(self, path):
        r = self.call("launch", path)
        return r in (0, "0")

    def screenshot(self):
        import base64
        r = self.call("screenshot")
        return base64.b64decode(r) if r else None

    def mouse(self, x, y, down=None):
        self.call("mouse", x, y, down)

    def key(self, code):
        self.call("key", code)

    def disk_images(self):
        return [out for name, out in zip(self.names, self.outs)
                if self.call("download", name, out, timeout=180)]

    def stop(self):
        if self.proc.poll() is None:
            try:
                self.call("quit")
                self.proc.wait(20)
            except Exception:
                pass
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGKILL)
            self.proc.wait()
        self.log.close()


# ---- the screen ----

def image(png):
    from PIL import Image
    import io
    return Image.open(io.BytesIO(png)).convert("RGB")


def click(s, x, y):
    s.mouse(x, y)
    time.sleep(0.3)
    s.mouse(x, y, True)
    time.sleep(0.2)
    s.mouse(x, y, False)
    time.sleep(0.3)


def menu(s, mx, my, ix, iy):
    """Press on a menu title, drag to the item, release."""
    s.mouse(mx, my)
    time.sleep(0.3)
    s.mouse(mx, my, True)
    time.sleep(0.6)
    s.mouse(ix, (my + iy) // 2)
    time.sleep(0.3)
    s.mouse(ix, iy)
    time.sleep(0.5)
    s.mouse(ix, iy, False)
    time.sleep(0.5)


RETURN = 36


def key(s, code, wait=0.4):
    s.key(code)
    time.sleep(wait)


def save(s, shots, name):
    """The screen as it is, in shots/NAME.png."""
    png = s.screenshot()
    if png:
        with open(os.path.join(shots, f"{name}.png"), "wb") as f:
            f.write(png)
    return png


# Speedometer's "The tests are done!" alert, the inside of it (the same
# place on 7.x and 8.6; tools/bench/ref/done-LOOK*.png: Executor's drawing
# and Apple's)
DONE_BOX = (225, 108, 410, 195)
# Alerts on the way that only want OK (dismiss-LOOK*.png: the G3 has no
# monochrome, "GrafTest will be skipped")
DISMISS_BOX = (190, 95, 460, 180)
STILL = 180     # seconds of an unchanged screen that count as stuck


def matches(png, ref, box):
    from PIL import ImageChops, ImageStat
    if not png:
        return False
    crop = image(png).convert("L").crop(box)
    return ImageStat.Stat(ImageChops.difference(crop, ref)).mean[0] < 3


def wait_done(s, look, limit):
    """Until the "tests are done" alert: (seconds, png), or (None, last png)
    if the session died or limit passed."""
    from PIL import Image
    import glob
    refs = [Image.open(f).convert("L") for f in glob.glob(os.path.join(REPO, "tools/bench/ref", f"done-{look}*.png"))]
    dismiss = [Image.open(f).convert("L") for f in glob.glob(os.path.join(REPO, "tools/bench/ref", f"dismiss-{look}*.png"))]
    t0 = changed = time.time()
    png = last = None
    while time.time() - t0 < limit:
        png = s.screenshot()
        if any(matches(png, ref, DONE_BOX) for ref in refs):
            return time.time() - t0, png
        if any(matches(png, ref, DISMISS_BOX) for ref in dismiss):
            s.key(RETURN)             # "... GrafTest will be skipped": OK
            time.sleep(1)
        if not png and not s.alive():
            return None, None
        body = image(png).crop((0, 22, 640, 480)).tobytes() if png else None   # (not the menu bar's clock)
        if body != last:
            last, changed = body, time.time()
        elif time.time() - changed > STILL:      # nothing drawn for long: stuck
            break
        time.sleep(0.25)
    return None, png


# The Tests menu (both looks): title, then each item's row
TESTS_MENU = (110, 9)
FILE_MENU = (53, 9)
ROW = {"pr": 28, "mix": 44, "fpu": 60, "color": 76}
SAVE_REPORT = 44                     # File > Save Text Report…
COLOR_BOXES = [142, 163, 184, 205, 226]   # Monochrome, 2, 4, 8, 16 bits: x 247
DIGIT = {1: 18, 2: 19, 3: 20, 4: 21, 5: 23, 6: 22}


def front(s, name, limit=90):
    """Until NAME is the front application."""
    t0 = time.time()
    while time.time() - t0 < limit:
        if s.app() == name:
            return True
        time.sleep(1)
    return False


def settle(s, before, quiet=0.6, limit=20):
    """Until the screen differs from before and stays the same QUIET s."""
    t0 = time.time()
    last, since = None, time.time()
    while time.time() - t0 < limit:
        png = s.screenshot()
        if png != last:
            last, since = png, time.time()
        elif png and png != before and time.time() - since >= quiet:
            return png
        time.sleep(0.2)
    return last


def ticked(png, y):
    im = image(png).convert("L")
    return min(im.getpixel((x, yy)) for x in range(244, 251) for yy in range(y - 3, y + 4)) < 100


def speedometer(s, look, shots, tag, limit, tests=("pr", "mix", "fpu"), colors=range(5)):
    """One session: TESTS, then the colour depths COLORS; {test: wall
    seconds}, which reports were saved (1: after FPU; 2-6: after each
    colour depth) and "crashed": the depth that crashed the session."""
    out = {"wall": {}, "reports": [], "notes": []}
    if not s.launch(SPEEDOMETER) or not front(s, "Speedometer 4.0"):
        out["notes"].append("launch failed")
        return out
    before = settle(s, None, quiet=2)
    click(s, 320, 200)                # the splash waits for a click
    before = settle(s, before, quiet=1)
    click(s, 376, 291)                # registration: Not Yet
    settle(s, before, quiet=1)
    save(s, shots, f"{tag}-hardware")

    def report(n):
        # each step once the screen has changed and settled: keys sent
        # while a dialog is still coming up go astray
        before = s.screenshot()
        menu(s, *FILE_MENU, FILE_MENU[0] + 30, SAVE_REPORT)
        before = settle(s, before)
        if not out["reports"]:        # the comment: asked the first time only
            s.key(RETURN)
            before = settle(s, before, quiet=2.5)
        s.key(DIGIT[n])               # the report's name: N (it is selected)
        before = settle(s, before)
        s.key(RETURN)
        settle(s, before)
        # Escape: a Replace? alert cancelled, then the Save dialog, should
        # either still be up (nothing otherwise)
        key(s, 53, 1)
        key(s, 53, 1)
        out["reports"].append(n)

    for test in tests:
        menu(s, *TESTS_MENU, TESTS_MENU[0] + 30, ROW[test])
        time.sleep(1.5)
        key(s, RETURN, 0.1)           # every test ticked: run
        if test == "pr":
            time.sleep(1.5)
            key(s, RETURN, 0.1)       # the drive to test: the Utilities disk
        secs, png = wait_done(s, look, limit)
        if secs is None:
            out["notes"].append(f"{test}: {'crashed' if not s.alive() else 'no result'}")
            if png:
                with open(os.path.join(shots, f"{tag}-{test}-stuck.png"), "wb") as f:
                    f.write(png)
            return out
        out["wall"][test] = round(secs, 2)
        key(s, RETURN, 1)
        save(s, shots, f"{tag}-{test}")
    if tests:
        report(1)
    # Color QuickDraw, a depth at a time (all five in one run crash
    # Executor now and then: docs/executor/Speed.md)
    for i in colors:
        menu(s, *TESTS_MENU, TESTS_MENU[0] + 30, ROW["color"])
        time.sleep(1.5)
        png = s.screenshot()
        if not png:
            break
        for j, yy in enumerate(COLOR_BOXES):
            if ticked(png, yy) != (i == j):
                click(s, 247, yy)
        key(s, RETURN, 0.1)
        secs, png = wait_done(s, look, limit)
        if secs is None:
            dead = not s.alive()
            out["notes"].append(f"color {i}: {'crashed' if dead else 'no result'}")
            if dead:
                out["crashed"] = i
            if png:
                with open(os.path.join(shots, f"{tag}-color{i}-stuck.png"), "wb") as f:
                    f.write(png)
            break
        out["wall"][f"color{i}"] = round(secs, 2)
        key(s, RETURN, 1)
        save(s, shots, f"{tag}-color{i}")
        report(2 + i)
    return out


def hfs_reports(images, work):
    """Speedometer's reports (TEXT/sPd3) on the images: {name: text}."""
    found = {}
    env = dict(os.environ, HOME=work)          # hmount's state (~/.hcwd)
    for img in images:
        if not os.path.exists(img):
            continue
        if subprocess.run(["hmount", img], env=env, capture_output=True).returncode:
            continue
        try:
            ls = subprocess.run(["hls", "-lR", ":"], env=env, capture_output=True).stdout
            folder = b""
            for line in ls.split(b"\n"):
                if line.startswith(b":") and line.endswith(b":"):
                    folder = line[:-1]
                m = re.match(rb"^f\s+TEXT/sPd3\s+\d+\s+\d+\s+\w{3}\s+\d+\s+[\d:]+ (.*)$", line)
                if m:
                    name = m.group(1)
                    dst = os.path.join(work, "report.bin")
                    subprocess.run([b"hcopy", b"-r", folder + b":" + name, dst.encode()],
                                   env=env, capture_output=True)
                    if os.path.exists(dst):
                        found[name.decode("mac_roman")] = open(dst, "rb").read().decode("mac_roman").replace("\r", "\n")
                        os.remove(dst)
        finally:
            subprocess.run(["humount"], env=env, capture_output=True)
    return found


NUM = r"(-?\d+\.\d+)"
REPORT_LINES = {
    "cpu": r"^CPU: " + NUM, "graphics": r"^Graphics: " + NUM, "disk": r"^Disk: " + NUM,
    "math": r"^Math: " + NUM, "pr": r"^Performance Rating:.*?" + NUM + r"\s*$",
    "mix": r"^Benchmark Average: " + NUM, "fpu": r"^FPU Test Average: " + NUM,
    "dhrystones": r"^Dhrystones: " + NUM, "whetstones": r"^KWhetstones: " + NUM,
    "fpu_whetstones": r"^FPU KWhetstones: " + NUM,
}
COLOR_LINES = ["Black & White", "4 Colors", "16 Colors", "256 Colors", "32,767 Colors"]


def parse_reports(reports):
    """Scores (relative to a Quadra 605, FPU a Quadra 650) from the
    reports: each saved later has what the earlier ones had (the windows
    keep their results), the largest value of each line counts."""
    scores = {}
    for text in reports.values():
        for k, pat in REPORT_LINES.items():
            m = re.search(pat, text, re.M)
            if m and float(m.group(1)) > scores.get(k, 0):
                scores[k] = float(m.group(1))
        for i, label in enumerate(COLOR_LINES):
            m = re.search(r"^" + re.escape(label) + r": " + NUM, text, re.M)
            if m and float(m.group(1)) > scores.get(f"color{i}", 0):
                scores[f"color{i}"] = float(m.group(1))
    colors = [scores[f"color{i}"] for i in range(5) if f"color{i}" in scores]
    if colors:                        # (Speedometer's own average: of the five)
        scores["color"] = round(sum(colors) / len(colors), 3)
        scores["color_n"] = len(colors)
    return scores


# ---- MacBench 5.0 (ROM machines: its scores in /api/text) ----

def macbench(s, look, shots, tag, limit):
    out = {"wall": {}, "notes": [], "scores": {}}
    if not s.launch(MACBENCH) or not front(s, "MacBench\u00ae 5.0"):
        out["notes"].append("launch failed")
        return out
    time.sleep(10)
    # its licence: the scroll box to the end, then Agree (the default)
    for x, y, d in ((550, 120, None), (550, 120, True), (550, 200, None), (550, 310, None), (550, 310, False)):
        s.mouse(x, y, d)
        time.sleep(0.4)
    time.sleep(1)
    key(s, RETURN, 3)
    for c in (11, 14, 45, 8, 4):      # registered to "bench"
        key(s, c, 0.2)
    key(s, RETURN, 1)
    t0 = time.time()
    while time.time() - t0 < 120:     # its window, after a splash
        if any(t["text"] == "Processor Tests" for t in text(s)):
            break
        time.sleep(2)
    else:
        save(s, shots, f"{tag}-stuck")
        out["notes"].append("no main window (stays on its splash)")
        return out
    for test, row, y in (("processor", 49, 106), ("fp", 65, 152)):
        menu(s, 130, 9, 160, row)
        time.sleep(2)
        key(s, RETURN, 0.1)           # "this test may take a while": OK
        t0 = time.time()
        score = None
        while time.time() - t0 < limit:
            for t in text(s):
                r = t["rect"]
                if 240 <= r[0] <= 300 and y <= r[1] <= y + 12 and re.fullmatch(r"\d+", t["text"]):
                    score = int(t["text"])
            if score is not None:
                break
            time.sleep(0.5)
        if score is None:
            out["notes"].append(f"{test}: no score")
            save(s, shots, f"{tag}-{test}-stuck")
            return out
        out["wall"][test] = round(time.time() - t0, 1)
        out["scores"][test] = score
        save(s, shots, f"{tag}-{test}")
    return out


def text(s):
    t = s.get("text") if isinstance(s, Native) else None
    return (t or {}).get("text", [])


# ---- MacPhoenixBench (tests/guest/bench): our own, unattended ----

BENCH_APP = os.path.join(REPO, "tests/guest/bench/MacPhoenixBench.bin")
BENCH_IMG = "bench.img"
BENCH_PATH = "Bench:MacPhoenixBench"


def hfs(env, *cmd, **kw):
    return subprocess.run(list(cmd), env=env, capture_output=True, **kw)


def bench_image(path, work, runs, tests=""):
    """A 16 MB HFS disk "Bench": the application and its bench.cfg."""
    env = dict(os.environ, HOME=work)
    with open(path, "wb") as f:
        f.truncate(16 * 1024 * 1024)
    hfs(env, "hformat", "-l", "Bench", path, check=True)
    hfs(env, "hmount", path, check=True)
    try:
        hfs(env, "hcopy", "-m", BENCH_APP, ":MacPhoenixBench", check=True)
        cfg = os.path.join(work, "bench.cfg")
        with open(cfg, "w") as f:
            f.write(f"{runs}\n{tests}\n")
        hfs(env, "hcopy", "-t", cfg, ":bench.cfg", check=True)
    finally:
        hfs(env, "humount")


def bench_results(images, work):
    """bench-results.txt off the Bench disk (the first image that has it)."""
    env = dict(os.environ, HOME=work)
    for img in images:
        if not os.path.exists(img) or hfs(env, "hmount", img).returncode:
            continue
        try:
            dst = os.path.join(work, "bench-results.txt")
            if hfs(env, "hcopy", "-t", ":bench-results.txt", dst).returncode == 0:
                return open(dst, "rb").read().decode("mac_roman")
        finally:
            hfs(env, "humount")
    return None


def parse_bench(text):
    """{"tests": {name: {min, med, hmin, hmed, work}}, "skipped": {name:
    why}, "clock": {...}, "code", "done", "notes"} from the results."""
    out = {"tests": {}, "skipped": {}, "notes": [], "done": False}
    num = lambda v: None if v == "-" else int(v)
    for line in (text or "").splitlines():
        w = line.split()
        if not w:
            continue
        if w[0] == "TEST" and len(w) >= 8:
            out["tests"][w[1]] = {"runs": int(w[2]), "min": num(w[3]), "med": num(w[4]),
                                  "hmin": num(w[5]), "hmed": num(w[6]), "work": w[7]}
        elif w[0] == "SKIP":
            out["skipped"][w[1]] = " ".join(w[2:])
        elif w[0] == "CLOCK" and len(w) >= 4:
            out["clock"] = {"ticks": num(w[1]), "micro": num(w[2]), "host": num(w[3])}
        elif w[0] == "CODE":
            out["code"] = w[1]
        elif w[0] == "DONE":
            out["done"] = True
        elif w[0] == "#":
            out["notes"].append(line[2:])
    return out


def own(s, shots, tag, limit):
    """MacPhoenixBench launched, until it quits (or LIMIT s)."""
    out = {"wall": {}, "notes": []}
    if not s.launch(BENCH_PATH) or not front(s, "MacPhoenixBench"):
        out["notes"].append("launch failed")
        save(s, shots, f"{tag}-launch")
        return out
    t0 = time.time()
    shot = t0
    while time.time() - t0 < limit:
        if not s.alive():
            out["notes"].append("crashed")
            break
        if s.app() not in ("MacPhoenixBench", None):
            out["wall"]["all"] = round(time.time() - t0, 1)
            return out
        if time.time() - shot > 60:           # what it is doing, now and then
            save(s, shots, f"{tag}-running")
            shot = time.time()
        time.sleep(1)
    else:
        out["notes"].append("no end")
    save(s, shots, f"{tag}-stuck")
    return out


# ---- the runs and the table ----

def run_one(args, name, bench, n, out):
    kind, system, cargs, look = CONFIGS[name]
    tag = f"{name}-{bench}-{n}"
    work = os.path.join(out, "work", tag)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shots = os.path.join(out, "shots")
    load = os.getloadavg()[0]
    t0 = time.time()
    row = {"config": name, "bench": bench, "run": n, "load": round(load, 1), "time": time.strftime("%Y-%m-%d %H:%M"),
           "wall": {}, "notes": [], "sessions": 0}
    reports = {}
    disks = [(UTILITIES, os.path.join(TAB_UTILITIES if kind == "tab" else IMAGES, UTILITIES))]
    if bench == "own":
        disks = [(BENCH_IMG, os.path.join(work, BENCH_IMG))]
        bench_image(disks[0][1], work, args.own_runs, args.tests or "")
    colors = range(5)
    tests = ("pr", "mix", "fpu")
    # a colour depth that crashes the session: the others in a new one
    while True:
        row["sessions"] += 1
        sub = os.path.join(work, str(row["sessions"]))
        os.makedirs(sub)
        s = Tab(args.wasm, sub, system, cargs, disks) if kind == "tab" else \
            Native(args.binary, sub, args.port, kind, system, cargs, disks)
        r = {}
        try:
            if not s.ready():
                row["notes"].append("the Finder did not come up")
                save(s, shots, f"{tag}-boot")
            else:
                row.setdefault("boot", round(time.time() - t0, 1))
                if bench == "speedometer":
                    r = speedometer(s, look, shots, tag, args.limit, tests, colors)
                elif bench == "own":
                    r = own(s, shots, tag, args.own_limit)
                else:
                    r = macbench(s, look, shots, tag, args.limit)
                row["wall"].update(r.pop("wall", {}))
                row["notes"] += r.pop("notes", [])
                row.update(r)
        finally:
            if kind == "tab":
                images = s.disk_images() if bench in ("speedometer", "own") else []
                s.stop()
            else:
                s.stop()
                images = s.disk_images()
        if bench == "speedometer":
            for k, v in hfs_reports(images, sub).items():
                reports[f"{row['sessions']}-{k}"] = v
        if bench == "own":
            text = bench_results(images, sub)
            if text:
                with open(os.path.join(shots, f"{tag}-results.txt"), "w") as f:
                    f.write(text)
            row["own"] = parse_bench(text)
            if not row["own"]["done"]:
                row["notes"].append("unfinished" if text else "no results")
        if not args.keep:
            shutil.rmtree(os.path.join(sub, "st"), ignore_errors=True)
            shutil.rmtree(os.path.join(sub, "library"), ignore_errors=True)
        crashed = r.get("crashed") if bench == "speedometer" else None
        if crashed is None or crashed >= 4 or row["sessions"] >= 4:
            break
        colors, tests = range(crashed + 1, 5), ()
    if bench == "speedometer":
        for k, v in reports.items():
            with open(os.path.join(shots, f"{tag}-report{k}.txt"), "w") as f:
                f.write(v)
        row["scores"] = parse_reports(reports)
    row["load_after"] = round(os.getloadavg()[0], 1)
    return row


# The table's columns: (key, heading); Speedometer's scores are relative
# to a Quadra 605 (FPU: a Quadra 650); MacBench's to a Power Mac G3/300 = 1000.
# (no PR: the report garbles its line, "Performance Rating:  KWhetsto15.359")
SPEEDO_COLS = [("cpu", "CPU"), ("graphics", "Graphics"), ("math", "Math"), ("mix", "Mix"),
               ("fpu", "FPU"), ("color", "Color")]
MACBENCH_COLS = [("processor", "Processor"), ("fp", "Floating Point")]


def median(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def fmt(v, digits=1):
    return "—" if v is None else f"{v:.{digits}f}" if isinstance(v, float) else str(v)


def table(out):
    rows = [json.loads(l) for l in open(os.path.join(out, "results.jsonl"))]
    lines = own_table(rows)
    if not any(r["bench"] == "speedometer" for r in rows):
        text = "\n".join(lines) + "\n"
        with open(os.path.join(out, "table.md"), "w") as f:
            f.write(text)
        return text
    lines += ["## Speedometer 4.0", "",
             "Medians of the runs. Scores: a Quadra 605 = 1 (FPU: a Quadra 650 = 1); Color: the mean of the "
             "depths that ran (how many of the five: depths).", "",
             "| Configuration | runs | " + " | ".join(h for _, h in SPEEDO_COLS) + " | depths | load | notes |",
             "|---|---|" + "---|" * (len(SPEEDO_COLS) + 3)]
    for name in CONFIGS:
        rs = [r for r in rows if r["config"] == name and r["bench"] == "speedometer"]
        if not rs:
            continue
        sc = [median([r.get("scores", {}).get(k) for r in rs]) for k, _ in SPEEDO_COLS]
        depths = "/".join(str(r.get("scores", {}).get("color_n", 0)) for r in rs)
        notes = sorted({n for r in rs for n in r.get("notes", [])})
        lines.append(f"| {name} | {len(rs)} | " + " | ".join(fmt(v) for v in sc) + f" | {depths} | "
                     f"{fmt(median([r.get('load') for r in rs]))} | " + "; ".join(notes) + " |")
    mb = [r for r in rows if r["bench"] == "macbench"]
    if mb:
        lines += ["", "## MacBench 5.0", "", "Medians. Scores: a Power Macintosh G3/300 = 1000.", "",
                  "| Configuration | runs | " + " | ".join(h for _, h in MACBENCH_COLS) + " | " +
                  " | ".join(f"{h} s" for _, h in MACBENCH_COLS) + " | notes |",
                  "|---|---|" + "---|" * (2 * len(MACBENCH_COLS) + 1)]
        for name in CONFIGS:
            rs = [r for r in mb if r["config"] == name]
            if not rs:
                continue
            sc = [median([r.get("scores", {}).get(k) for r in rs]) for k, _ in MACBENCH_COLS]
            wl = [median([r.get("wall", {}).get(k) for r in rs]) for k, _ in MACBENCH_COLS]
            notes = sorted({n for r in rs for n in r.get("notes", [])})
            lines.append(f"| {name} | {len(rs)} | " + " | ".join(fmt(v, 0) for v in sc) + " | " +
                         " | ".join(fmt(v) for v in wl) + " | " + "; ".join(notes) + " |")
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "table.md"), "w") as f:
        f.write(text)
    return text


# MacPhoenixBench's groups of tests (the summary's columns): name prefixes
OWN_GROUPS = [
    ("Integer", ("dhry", "sieve", "sort", "crc32")),
    ("FP", ("whet", "fft", "matmul")),
    ("Memory", ("blockmove", "memcpy", "stride")),
    ("QuickDraw", ("copybits", "paintrect", "frameoval", "drawstring", "rgn_")),
    ("Traps", ("tickcount", "getsetport", "gestalt", "mixedmode")),
    ("Files", ("file_",)),
    ("Heap", ("newhandle", "lockmove", "compact")),
]
OWN_BASE = "quadra-uae"           # the summary's 1.0


def own_times(rows, name, key="med"):
    """{test: µs} for a configuration: the median over its sessions of each
    test's KEY (med: the run median; hmed: the same by the host's clock)."""
    rs = [r["own"] for r in rows if r["config"] == name and r["bench"] == "own" and r.get("own")]
    tests = {t for r in rs for t in r["tests"]}
    return {t: median([r["tests"][t].get(key) for r in rs if t in r["tests"]]) for t in tests}


def geomean(xs):
    import math
    xs = [x for x in xs if x]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else None


def own_table(rows):
    """MacPhoenixBench: a summary (speed against quadra-uae by group, the
    geometric mean of the tests' ratios; by the guest's clock and, in
    brackets where they differ by more than 10%, the host's) and every
    test's median µs."""
    names = [n for n in CONFIGS if any(r["config"] == n and r["bench"] == "own" for r in rows)]
    if not names:
        return []
    base = own_times(rows, OWN_BASE)
    lines = ["## MacPhoenixBench", "",
             f"Speed against {OWN_BASE} (= 1; higher is faster): per group, the geometric mean of "
             "the tests' ratios of median times, by Microseconds; [the host's clock] where it says "
             "more than 10% otherwise. clock: Microseconds over 2 s of the host's.", "",
             "| Configuration | code | " + " | ".join(g for g, _ in OWN_GROUPS) + " | clock | notes |",
             "|---|---|" + "---|" * (len(OWN_GROUPS) + 2)]
    for name in names:
        guest, host = own_times(rows, name), own_times(rows, name, "hmed")
        rs = [r for r in rows if r["config"] == name and r["bench"] == "own"]
        code = "/".join(sorted({(r.get("own") or {}).get("code", "?") for r in rs}))
        cells = []
        for _, prefixes in OWN_GROUPS:
            ts = [t for t in guest if t.startswith(prefixes) and base.get(t) and guest[t]]
            g = geomean([base[t] / guest[t] for t in ts])
            hb = own_times(rows, OWN_BASE, "hmed")
            h = geomean([hb[t] / host[t] for t in ts if hb.get(t) and host.get(t)])
            cell = fmt(g, 2 if g is not None and g < 10 else 1)
            if g and h and abs(h / g - 1) > 0.1:
                cell += f" [{fmt(h, 2 if h < 10 else 1)}]"
            cells.append(cell)
        clocks = [(r.get("own") or {}).get("clock") for r in rs]
        ratio = median([c["micro"] / c["host"] for c in clocks if c and c.get("host")])
        notes = sorted({n for r in rs for n in r.get("notes", [])})
        lines.append(f"| {name} | {code} | " + " | ".join(cells) + f" | {fmt(ratio, 2)} | " + "; ".join(notes) + " |")
    tests = []
    for r in rows:
        for t in (r.get("own") or {}).get("tests", {}):
            if t not in tests:
                tests.append(t)
    lines += ["", "Median times, ms (Microseconds; the median over the sessions of each test's median of "
              "its runs). —: skipped or not in that build.", "",
              "| Test | work | " + " | ".join(names) + " |", "|---|---|" + "---|" * len(names)]
    times = {n: own_times(rows, n) for n in names}
    work = {t: w for r in rows for t, v in (r.get("own") or {}).get("tests", {}).items() for w in [v["work"]]}
    for t in tests:
        lines.append(f"| {t} | {work.get(t, '')} | " + " | ".join(
            fmt(times[n].get(t) / 1000 if times[n].get(t) is not None else None,
                2 if (times[n].get(t) or 0) < 10000 else 1) for n in names) + " |")
    return lines + [""]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="configurations, comma-separated (default: all)")
    ap.add_argument("--bench", default="speedometer,macbench",
                    help="speedometer, macbench, own (MacPhoenixBench)")
    ap.add_argument("--own-runs", type=int, default=5, help="MacPhoenixBench: runs of each test in a session")
    ap.add_argument("--own-limit", type=float, default=3600, help="MacPhoenixBench: seconds a session may take")
    ap.add_argument("--tests", help="MacPhoenixBench: only these tests (names or their starts, spaces)")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--first", type=int, default=1, help="number of the first run (resuming a run)")
    ap.add_argument("--out", default=os.path.join(HOME, "storage/tmp/bench/out"))
    ap.add_argument("--binary", default=os.path.join(REPO, "build/mac-phoenix"))
    ap.add_argument("--wasm", default=os.path.join(REPO, "build-wasm/src/executor/romlib/config/front-ends/wasm"))
    ap.add_argument("--port", type=int, default=19420)
    ap.add_argument("--limit", type=float, default=600, help="seconds a test may take")
    ap.add_argument("--keep", action="store_true", help="keep each session's disk images")
    ap.add_argument("--table", metavar="DIR", help="only the table, from DIR's results")
    args = ap.parse_args()
    if args.table:
        print(table(args.table))
        return
    os.makedirs(os.path.join(args.out, "shots"), exist_ok=True)
    # a copy of the binary (a rebuild during the runs doesn't change it),
    # BridgeAgent where it looks for it (../BridgeAgent)
    bindir = os.path.join(args.out, "bin")
    os.makedirs(os.path.join(args.out, "BridgeAgent"), exist_ok=True)
    os.makedirs(bindir, exist_ok=True)
    shutil.copyfile(os.path.join(REPO, "BridgeAgent/BridgeAgent.bin"), os.path.join(args.out, "BridgeAgent/BridgeAgent.bin"))
    binary = os.path.join(bindir, "mac-phoenix")
    if os.path.abspath(args.binary) != binary:
        shutil.copyfile(args.binary, binary + ".new")
        os.chmod(binary + ".new", 0o755)
        os.replace(binary + ".new", binary)
    args.binary = binary
    names = args.only.split(",") if args.only else list(CONFIGS)
    for n in names:
        if n not in CONFIGS:
            sys.exit(f"unknown configuration {n}: {', '.join(CONFIGS)}")
    for bench in args.bench.split(","):
        for name in names:
            if bench == "macbench" and (name not in PPC or CONFIGS[name][0] == "tab"):
                continue                  # (the tab: no /api/text to read its scores)
            for n in range(args.first, args.first + args.runs):
                log(f"{name} {bench} run {n}")
                row = run_one(args, name, bench, n, args.out)
                log("  ", json.dumps({k: row.get(k) for k in ("scores", "wall", "notes", "boot")}))
                with open(os.path.join(args.out, "results.jsonl"), "a") as f:
                    f.write(json.dumps(row) + "\n")
                # MacBench that never gets past its splash: once is enough
                if bench == "macbench" and any("splash" in x for x in row.get("notes", [])):
                    break
    print(table(args.out))


if __name__ == "__main__":
    main()
