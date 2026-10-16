#!/usr/bin/env python3
"""guest_install.py — run a Mac installer in the emulator and capture what
it installed.

Most classic Mac software only installs through its own installer (Installer
VISE, Apple Installer, ...), which runs on the Mac. This runs one in a
scratch guest, then captures the result, so provisioning can copy the
installed files instead of driving a GUI each time:

  1. A copy of a clean base System boots with an empty volume named
     "Development" (the install target: paths an installer records are
     already right) and the installer's media as disks or CD-ROMs.
  2. A recipe drives the installer: launch, clicks, keys, waits.
  3. The guest shuts down; the capture is an HFS image holding
       :Development:    everything the installer put on the target
       :System Folder:  files it added or changed in the System Folder
     (the System file and Finder themselves are never taken: changes to
     them are reported, not captured).

The capture is pinned (macfetch pin --file) and installed by build_disk.py
like any other list entry. Recipes are only needed to make a capture again.

Recipe (JSON): {"backend": "uae", "base": "macos-7.6.1.img.orig",
  "cdroms": [...], "media": [...], "disk_images": [...], "steps": [step, ...]}
  expect       names that must be at the top of the captured Development (a
               click that picked the wrong volume fails here, not later)
  from_boot    true: new items at the top of the boot disk are captured into
               Development too (installers that only install on a System disk)
  cdroms       storage-relative paths, attached as CD-ROMs
  media        {"file": NAME, "take": SPEC, "as": NAME}: items out of the
               list's files (build_disk.py take syntax), put on a volume "Media";
               "dc42": true rewrites raw floppy images (DiskDup .dsk, ...) as
               Disk Copy 4.2 images, which the guest's Disk Copy mounts
  disk_images  {"file": NAME, "take": SPEC}: disk images out of the list's
               files (NDIF decoded), each attached as a disk: an installer's
               floppies, all mounted at once
  {"launch": "Volume:Folder:App"}        through the bridge
  {"open": "Volume:Folder:Document"}     open a document with its creator (a Disk
                                         Copy image: mounted, and ejectable)
  {"click": [x, y]}                      screen coordinates (640x480)
  {"dclick": [x, y]}                     double-click
  {"drag": [[x, y], [x2, y2]]}           press, move, release (popup menus)
  {"key": "return"}                      a /api/keypress name
  {"wait": seconds}
  {"wait_app": "Name", "timeout": s}     until the front app is Name
  {"wait_app_not": "Name", "timeout": s} until it isn't
  {"wait_finder": seconds}               after a restart: until the Finder is back
  {"shot": "name"}                       a screenshot into the work dir
  {"quit": true}                         quit the front app (through the bridge)
  Any step may carry "backend": "uae" or "kpx" to run only on that machine
  (menus of volumes list them in a different order on each).

  guest_install.py RECIPE -o CAPTURE.img [--keep]

Archives get the same treatment, so no app reaches a disk through a Linux
unarchiver: --expand puts one of the list's archives on Development, opens
it with the StuffIt Expander already on the base System, and captures what
it expanded (without the archive):

  guest_install.py --expand Frontier_5.0.1.sit -o frontier.img
  guest_install.py --expand CW4MACTOOLS.ISO --take ":Apple Development Tools:ResEdit 2.1.3.sit" -o resedit.img

A recipe with {"expand": {"file": NAME, "take": SPEC}} does the same, then
runs its own steps: a first launch that answers an app's one-time questions
(registration, personalisation), so the capture opens cleanly. New files in
:System Folder:Preferences: are captured, bar the System's and the bridge's.
"""

import argparse
import json
import struct
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "macfetch"))
import macfetch  # noqa: E402
from build_disk import Hfs, Volume, install, macbinary_to_files, mr, named, open_container, take  # noqa: E402
from populate_disk import build_macbinary  # noqa: E402

ROMS = {"uae": os.path.expanduser(os.environ.get("MACEMU_ROM", "~/roms/quadra.rom")),
        "kpx": os.path.join(macfetch.STORAGE, "roms", "g3.rom")}
NEVER = {b"System", b"Finder", b"Desktop DB", b"Desktop DF", b"Desktop"}
# What changes in a System Folder just from the guest running: the bridge's
# agent, recent items, preferences, and files the System touches at boot.
# Preferences: only new files are taken (an app's first-launch answers live
# there), minus these, which the System or the bridge write on every boot.
PREFS_NOISE = {b"MacPhoenix.cfg", b"ASLM Preferences", b"Apple Menu Options Prefs",
               b"Mac OS Easy Open Preferences", b"WindowShade Preferences", b"Internet Preferences",
               b"Finder Preferences", b"Users & Groups Data File", b"File Sharing Folder",
               b"Launcher Preferences", b"Desktop Pictures Prefs", b"Extensions Manager Preferences",
               b"StuffIt Expander Preferences",
               b"System Preferences", b"Monitors & Sound Prefs"}
NOISE = (b":Recent Applications:", b":Recent Documents:", b":Recent Servers:",
         b":Startup Items:BridgeAgent", b":MacTCP DNR", b":Extensions:Printer Share",
         b":Control Panels:Apple Menu Options", b":Extensions:Apple CD-ROM")


def work_root():
    """Scratch space for images: the storage disk, not /tmp (often a small
    RAM disk) -- builds stage whole volumes."""
    root = os.path.join(macfetch.STORAGE, "work")
    os.makedirs(root, exist_ok=True)
    return root


class Guest:
    def __init__(self, work, backend, disks, cdroms, port):
        self.base = f"http://localhost:{port}"
        self.backend = backend
        args = [os.path.join(ROOT, "build", "mac-phoenix"), "--config", "/dev/null", "--network", "none",
                "--bridge", "--backend", backend, "--rom", ROMS[backend], "--port", str(port),
                "--timeout", "3600", "--dismiss-shutdown-dialog"]
        for d in disks:
            args += ["--disk", d]
        for c in cdroms:
            args += ["--cdrom", c]
        self.log = open(os.path.join(work, "emulator.log"), "w")
        self.proc = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT, cwd=ROOT)
        for _ in range(80):
            if self.get("/api/status") is not None:
                break
            time.sleep(0.5)
        self.post("/api/emulator/start", {})
        self.work = work

    def call(self, path, body=None, timeout=30):
        req = urllib.request.Request(self.base + path, method="POST" if body is not None else "GET",
                                     data=json.dumps(body).encode() if body is not None else None)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                data = r.read()
                if r.headers.get_content_type() != "application/json":
                    return data
                try:
                    return json.loads(data)
                except UnicodeDecodeError:          # app names come back in MacRoman
                    return json.loads(data.decode("mac_roman"))
        except OSError:
            return None

    def get(self, path, timeout=30):
        return self.call(path, None, timeout)

    def post(self, path, body, timeout=30):
        return self.call(path, body, timeout)

    def app(self):
        return (self.get("/api/app") or {}).get("app", "")

    def wait_finder(self, timeout):
        r = self.post("/api/wait", {"condition": "boot=Finder", "timeout": timeout}, timeout + 10)
        if not (r or {}).get("ok"):
            raise RuntimeError(f"no Finder within {timeout}s")

    def mouse(self, **kw):
        self.post("/api/mouse", kw)

    def click(self, x, y):
        self.mouse(x=x, y=y)
        time.sleep(0.4)
        self.mouse(button=0, down=True)
        time.sleep(0.2)
        self.mouse(button=0, down=False)
        time.sleep(1)

    def dclick(self, x, y):
        self.mouse(x=x, y=y)
        time.sleep(0.4)
        for _ in range(2):
            self.mouse(button=0, down=True)
            time.sleep(0.08)
            self.mouse(button=0, down=False)
            time.sleep(0.12)
        time.sleep(1)

    def cmd_key(self, keycode):
        """Command + an ADB keycode (0x0C Q, 0x2F period, 0x0D W)."""
        self.post("/api/keypress", {"key": 0x37, "down": True})
        time.sleep(0.1)
        self.post("/api/keypress", {"key": keycode})
        time.sleep(0.1)
        self.post("/api/keypress", {"key": 0x37, "down": False})
        time.sleep(1)

    def close_front(self, name, patience=10):
        """Quit app `name` and get the Finder back in front: the bridge's
        Quit, then (only while `name` itself is in front, never the bridge's
        own agent) Escape or Return for a modal dialog and Command-Q."""
        def settle():
            deadline = time.time() + patience
            while time.time() < deadline:
                front = self.app()
                if front == "Finder":
                    return True
                if name.lower() in front.lower():
                    return False            # still there, in front: escalate
                time.sleep(1)               # BridgeAgent mid-command, or switching
            return False

        self.post("/api/quit", {}, 60)
        if settle():
            return True
        for keys in ("escape", "return", "cmd-q", "escape", "return", "cmd-q"):
            if name.lower() not in self.app().lower():
                if settle():
                    return True
                continue
            if keys in ("escape", "return"):     # cancel, or take the default button
                self.post("/api/keypress", {"key": keys})
                time.sleep(2)
                if name.lower() in self.app().lower():
                    self.post("/api/quit", {}, 60)
            else:
                self.cmd_key(0x0C)
            if settle():
                return True
        return False

    def drag(self, a, b):
        self.mouse(x=a[0], y=a[1])
        time.sleep(0.4)
        self.mouse(button=0, down=True)
        time.sleep(1)
        self.mouse(x=b[0], y=b[1])
        time.sleep(0.6)
        self.mouse(button=0, down=False)
        time.sleep(1)

    def shot(self, name):
        data = self.get("/api/screenshot")
        if isinstance(data, bytes):
            with open(os.path.join(self.work, name + ".png"), "wb") as f:
                f.write(data)

    def shutdown(self):
        """A clean shutdown, so every volume is flushed and marked unmounted."""
        self.post("/api/shutdown", {}, 60)
        try:
            self.proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            self.proc.wait(timeout=30)

    def run(self, step):
        if "launch" in step or "open" in step:
            body = {"path": step["launch"]} if "launch" in step else {"path": step["open"], "open": True}
            for _ in range(4):           # the agent may still be starting
                r = self.post("/api/launch", body, 240) or {}
                if r.get("success") or "bridge agent" not in str(r.get("error", "")):
                    break
                time.sleep(20)
            if not r.get("success"):
                raise RuntimeError(f"{body}: {r}")
        elif "click" in step:
            self.click(*step["click"])
        elif "dclick" in step:
            self.dclick(*step["dclick"])
        elif "drag" in step:
            self.drag(*step["drag"])
        elif "key" in step:
            self.post("/api/keypress", {"key": step["key"]})
            time.sleep(1)
        elif "wait" in step:
            time.sleep(step["wait"])
        elif "wait_app" in step or "wait_app_not" in step:
            want, gone = step.get("wait_app"), step.get("wait_app_not")
            deadline = time.time() + step.get("timeout", 120)
            while time.time() < deadline:
                a = self.app()
                if (want and want in a) or (gone and gone not in a and a):
                    break
                time.sleep(2)
            else:
                raise RuntimeError(f"timeout: {step} (front app {self.app()!r})")
        elif "wait_finder" in step:
            time.sleep(15)
            self.wait_finder(step["wait_finder"])
        elif "shot" in step:
            self.shot(step["shot"])
        elif "quit" in step:
            self.post("/api/quit", {}, 60)
            time.sleep(3)
        else:
            raise ValueError(f"unknown step {step}")


def listing(hfs, image, folder):
    """{name: (is_dir, `hls -l` line)} of a folder, recursively, keyed by path."""
    out = {}
    hfs.mount(image)
    try:
        def walk(path):
            for line in hfs.run("hls", "-l", "-a", path + b":").splitlines():
                from build_disk import LS
                m = LS.match(line)
                if not m:
                    continue
                p = path + b":" + m.group(2)
                out[p] = (m.group(1) == b"d", line)
                if m.group(1) == b"d":
                    walk(p)
        walk(folder)
    finally:
        hfs.umount()
    return out


BOOT_TOP_NOISE = {b"Desktop Folder", b"Trash", b"System Folder", b"Temporary Items", b"Desktop DB",
                  b"Desktop DF", b"Installer Logs"}


def capture(work, boot_before, boot_after, target, out, ignore=(), from_boot=False, expect=(), exclude=()):
    hfs = Hfs(work)
    before = listing(hfs, boot_before, b":System Folder")
    after = listing(hfs, boot_after, b":System Folder")
    added, changed, ignored = [], [], []
    noise = NOISE + tuple(mr(i) for i in ignore)
    for p, (is_dir, line) in sorted(after.items()):
        if is_dir:
            continue
        if p in before and before[p][1] == line:
            continue
        leaf = p.rsplit(b":", 1)[1]
        prefs = p.startswith(b":System Folder:Preferences:")
        if leaf in NEVER or any(n in p for n in noise) or \
                (prefs and (p in before or leaf in PREFS_NOISE)):
            ignored.append(p)
        else:
            (added if p not in before else changed).append(p)
    with open(out + ".part", "wb") as f:
        f.truncate(256 << 20)
    hfs.run("hformat", "-l", "Capture", out + ".part")
    hfs.mount(out + ".part")
    hfs.run("hmkdir", ":System Folder")
    hfs.umount()
    # The installed tree, whole.
    stage = tempfile.mkdtemp(dir=work)
    items = Volume(target, work).export(":*", stage)
    # The volume's own Desktop Folder, Trash etc., if the guest made them.
    drop = BOOT_TOP_NOISE | {mr(x) for x in exclude}
    items = [(r, l) for r, l in items if r.split(b":")[0] not in drop]
    if exclude and not items:
        os.unlink(out + ".part")
        raise SystemExit("nothing was expanded")
    install(hfs, out + ".part", items, "Development", None, [], True)
    if from_boot:
        hfs.mount(boot_before)
        had = {n for n, _ in hfs.ls(b":")}
        hfs.umount()
        hfs.mount(boot_after)
        new = [n for n, _ in hfs.ls(b":") if n not in had and n not in BOOT_TOP_NOISE]
        hfs.umount()
        boot = Volume(boot_after, work)
        for n in new:
            moved = boot.export(b":" + n, tempfile.mkdtemp(dir=work))
            install(hfs, out + ".part", moved, "Development", None, [], True)
            items += moved
            print(f"  boot disk added: :{n.decode('mac_roman')} (captured into Development)")
    hfs.mount(out + ".part")
    top = {n.decode("mac_roman") for n, _ in hfs.ls(b":Development:")}
    hfs.umount()
    missing = [n for n in expect if n not in top]
    if missing:
        os.unlink(out + ".part")
        raise SystemExit(f"install didn't produce {missing} (Development has {sorted(top)})")
    # System Folder files that are new or changed, with their folders.
    stage = tempfile.mkdtemp(dir=work)
    src = Volume(boot_after, work)
    for p in added + changed:
        rel = p[len(b":System Folder:"):]
        folder = rel.rpartition(b":")[0]
        for leaf in src.export(p.decode("mac_roman"), stage):
            install(hfs, out + ".part", [leaf], ("System Folder:" + folder.decode("mac_roman")).rstrip(":"),
                    None, [], True)
    os.replace(out + ".part", out)
    for kind, paths in (("added", added), ("changed", changed), ("not captured (noise)", ignored)):
        for p in paths:
            print(f"  System Folder {kind}: {p.decode('mac_roman')}")
    print(f"{out}: {len(items)} items on Development, {len(added) + len(changed)} System Folder files")


def dc42_checksum(data):
    s = 0
    for (w,) in struct.iter_unpack(">H", data):
        s = (s + w) & 0xFFFFFFFF
        s = ((s >> 1) | (s << 31)) & 0xFFFFFFFF
    return s


def to_dc42(macbinary, name):
    """A raw floppy image in a MacBinary file, rewritten as a Disk Copy 4.2
    image (84-byte header + sectors), type dImg, creator ddsk (Disk Copy 6,
    which the guest has; 4.2's own dCpy usually isn't registered)."""
    with open(macbinary, "rb") as f:
        blob = f.read()
    dlen = struct.unpack(">I", blob[83:87])[0]
    data = blob[128:128 + dlen]
    size, fmt = next(((s, f) for s, f in ((409600, 0), (819200, 1), (1474560, 3)) if len(data) <= s),
                     ((len(data) + 511) // 512 * 512, 3))
    data = data.ljust(size, b"\0")
    label = mr(name)[:63]
    header = bytes([len(label)]) + label.ljust(63, b"\0") + struct.pack(
        ">IIIIBBH", size, 0, dc42_checksum(data), 0, fmt, 0x22 if fmt else 0x02, 0x0100)
    with open(macbinary, "wb") as f:
        f.write(build_macbinary(mr(name)[:31], b"dImg", b"ddsk", header + data, b""))


def list_file(list_path, name):
    """Where the list's file NAME was provisioned."""
    p = next((p for p in macfetch.load_manifest(list_path)["files"] if p["name"] == name), None)
    if p is None:
        raise SystemExit(f"{name}: not in {list_path}")
    return os.path.join(macfetch.manifest_out(list_path), macfetch.pin_path(p))


# Types StuffIt Expander knows an archive by, from its name.
ARCHIVE_TYPES = {".sit": b"SIT!", ".hqx": b"TEXT", ".bin": b"BINA", ".zip": b"ZIP ", ".sea": b"APPL",
                 ".cpt": b"PACT"}


def expand_recipe(list_path, name, spec, work, target):
    """A recipe that expands one archive with StuffIt Expander: the archive
    goes on Development (as a file with a type Expander knows), and is
    left out of the capture."""
    src = list_file(list_path, name)
    stage = tempfile.mkdtemp(dir=work)
    if spec:
        rel, local = take(src, spec, stage, work)[0]
        arc = rel.rsplit(b":", 1)[-1].decode("mac_roman")
        # Expander's creator, so opening it finds Expander (not, say, StuffIt Deluxe).
        plain = os.path.join(stage, "archive")
        macbinary_to_files(local, plain)
        with open(local, "rb") as f:
            head = f.read(128)
        with open(plain, "rb") as f:
            data = f.read()
        local = os.path.join(stage, "archive.bin")
        with open(local, "wb") as f:
            f.write(build_macbinary(mr(arc)[:31], head[65:69], b"SITx", data, b""))
    else:
        arc = name
        local = os.path.join(stage, "archive.bin")
        with open(src, "rb") as f:
            data = f.read()
        ftype = ARCHIVE_TYPES.get(os.path.splitext(name)[1].lower(), b"????")
        with open(local, "wb") as f:
            f.write(build_macbinary(mr(arc)[:31], ftype, b"SITx", data, b""))
    hfs = Hfs(work)
    hfs.mount(target)
    hfs.run("hcopy", "-m", local, ":")
    hfs.umount()
    arc = arc[:31]
    size = os.path.getsize(local)
    return {"base": "macos-7.6.1.img.orig", "backend": "uae", "exclude": [arc],
            "steps": [{"open": "Development:" + arc}, {"wait_app": "StuffIt Expander", "timeout": 120},
                      {"wait": 20 + size // 100_000}, {"shot": "expanded"}, {"quit": True}]}


def media_disks(work, recipe, list_path):
    """The recipe's "media" volume and "disk_images", as image paths."""
    def path_of(name):
        return list_file(list_path, name)

    disks = []
    if recipe.get("media"):
        vol = os.path.join(work, "Media.img")
        with open(vol, "wb") as f:
            f.truncate(256 << 20)
        hfs = Hfs(work)
        hfs.run("hformat", "-l", "Media", vol)
        for m in recipe["media"]:
            stage = tempfile.mkdtemp(dir=work)
            items = take(path_of(m["file"]), m.get("take", ""), stage, work)
            if m.get("dc42"):
                for rel, local in items:
                    if local:
                        to_dc42(local, rel.rsplit(b":", 1)[-1].decode("mac_roman"))
            items, rename = named(items, m.get("take", ""), m.get("as"))
            install(hfs, vol, items, "", rename, [], True)
        disks.append(vol)
    for d in recipe.get("disk_images", []):
        stage = tempfile.mkdtemp(dir=work)
        items = take(path_of(d["file"]), d["take"], stage, work)
        item = os.path.join(tempfile.mkdtemp(dir=work), "item")
        macbinary_to_files(items[0][1], item)
        disks.append(open_container(item, work).image)
    return disks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recipe", nargs="?")
    ap.add_argument("--expand", metavar="FILE", help="expand this archive from the list (no recipe needed)")
    ap.add_argument("--take", help="with --expand: the archive inside FILE (build_disk.py take syntax)")
    ap.add_argument("-o", "--out", required=True, help="capture image to write")
    ap.add_argument("--port", type=int, default=19480)
    ap.add_argument("--backend", help="override the recipe's backend (uae | kpx)")
    ap.add_argument("--list", default=os.path.join(HERE, "lists", "dev.json"),
                    help="the list whose files media and disk_images name")
    ap.add_argument("--keep", action="store_true", help="keep the work directory (images, screenshots)")
    ap.add_argument("--explore", action="store_true",
                    help="boot with the recipe's media, run no steps, leave the guest up (to work out a recipe)")
    args = ap.parse_args()
    if bool(args.recipe) == bool(args.expand):
        ap.error("give a recipe or --expand FILE")
    recipe = {"base": "macos-7.6.1.img.orig"}
    if args.recipe:
        with open(args.recipe) as f:
            recipe = json.load(f)
    images = os.path.join(macfetch.STORAGE, "images")
    downloads = macfetch.STORAGE

    def media(p):
        return os.path.join(downloads, os.path.expanduser(p)) if not os.path.isabs(p) else p

    work = tempfile.mkdtemp(prefix="guest_install.", dir=work_root())
    boot0 = os.path.join(images, recipe["base"])
    boot = os.path.join(work, "boot.img")
    target = os.path.join(work, "Development.img")
    shutil.copyfile(boot0, boot)
    with open(target, "wb") as f:
        f.truncate(recipe.get("target_mb", 1024) << 20)
    Hfs(work).run("hformat", "-l", "Development", target)
    if args.expand or recipe.get("expand"):
        # An archive expanded by StuffIt Expander, then the recipe's own
        # steps (a first launch, say).
        e = recipe.get("expand") or {}
        generated = expand_recipe(args.list, args.expand or e["file"], args.take or e.get("take"), work, target)
        generated["steps"] += recipe.get("steps", [])
        for key in ("expect", "settle"):
            if key in recipe:
                generated[key] = recipe[key]
        recipe = generated
    extra = media_disks(work, recipe, args.list)
    guest = Guest(work, args.backend or recipe.get("backend", "uae"), [boot, target] + extra,
                  [media(c) for c in recipe.get("cdroms", [])], args.port)
    try:
        guest.wait_finder(240)
        if args.explore:
            print(f"guest up: {guest.base}  work: {work}", flush=True)
            guest.proc.wait()
            return
        time.sleep(recipe.get("settle", 20))
        for i, step in enumerate(recipe["steps"]):
            if step.get("backend", guest.backend) != guest.backend:
                continue
            print(f"  step {i}: {json.dumps(step)}")
            guest.run({k: v for k, v in step.items() if k != "backend"})
        guest.shot("final")
        guest.shutdown()
    except BaseException:
        guest.shot("failed")
        guest.proc.terminate()
        print(f"work directory kept: {work}")
        raise
    try:
        capture(work, boot0, boot, target, args.out, recipe.get("ignore", []), recipe.get("from_boot", False),
                recipe.get("expect", []), recipe.get("exclude", []))
    except SystemExit:
        print(f"work directory kept: {work}")
        raise
    if args.keep:
        print(f"work directory: {work}")
    else:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
