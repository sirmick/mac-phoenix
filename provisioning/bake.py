#!/usr/bin/env python3
"""bake.py — make snapshots: software installed by a Mac (docs/provisioning/PLAN.md).

A bake (provisioning/bakes/<name>.json) runs in a scratch guest booted from
a base System (provisioning/bases.json) with a blank volume as the install
target, then captures what it installed as a snapshot (snapshot.py):
baked/<name>.img, one per product. It runs on the 68k machine; with
"machines": ["68k", "ppc"] it runs on both (baked/parts/<name>@<machine>.img)
and the runs are merged: files only one has are all kept, a file both have
in different builds becomes one fat file (CFM code), other differences fail
the bake. Kinds:

  install  the product's own installer, driven by "steps"
  expand   "archive": a media file StuffIt Expander expands in the guest
           (then any "steps": a first launch, say)
  copy     items copied by the guest from attached media: "steps" with
           {"copy": {"from": ..., "to": "{target}:"}}
  stage    no snapshot: media only, put on an Installers disk (installers.py)
           for a product whose installer stops at a serial number
  derive   a snapshot made from other snapshots on the host: "from"
           (their names), merged as machine runs are
  os       a System: its installer (and updaters) run by "steps" onto the
           blank target ("Macintosh HD"); then the guest restarts from it
           alone (plus "first_boot_disks") for "first_boot" steps (an updater
           that only updates the startup disk); the snapshot is the whole volume,
           bootable ("version": what its System file must say). "os" names
           the System that runs the installer.

Bake fields: "kind", "os" (the base System, default 7.6.1), "machines"
(default ["68k"]), "target"
(install volume name, default Install), "archive"/"take" (expand),
"cdroms" (media names), "media" (items put on a volume "Media":
{"file", "take", "as", "dc42"}; "as_is": the MacBinary file itself, not opened:
a self-mounting HFS+ image the guest mounts), "disks" (disk images out of media,
attached: {"file", "take"}), "steps", "expect" (names that must be at
the top of the result; [] : at least one item, whatever its name), "from_boot" (new items at the top of the boot
disk count as installed), "launch" (the product's apps, relative to the
snapshot's :Items:, each a path or {"app", facts...}: "no_quit",
"needs_serial", "machines"), "settle" (seconds after boot), "system_remove"
(System Folder files the install supersedes but leaves behind: the build
deletes them where the snapshot's System Folder part goes), "system_take"
(System Folder files to take even if only changed: a control panel's
settings), "network" (the guest's --network: "socket" for net-bridge),
"exclude" (names at the top of the target left out of the capture: a self-extracting
archive copied there to expand in place), "volatile" (snapshot paths a run writes differently every time, compared by
name only: a first launch's database or preferences). "wip": a bake still being
worked out, left out of --all. "args": emulator arguments for every guest
the bake starts ("--model-id", "5": Systems before 7.5). "first_boot_agent":
false: the new System has no BridgeAgent (7.1: no File System Manager, so no
Host volume): its first boot goes through the Finder and the menus.

A re-bake is compared with the snapshot it replaces: "same install" when
only run noise differs (aliases, installer logs, "volatile" paths; for os
bakes also the System's own preferences, and fonts and control panels by
size: an installer picks fonts' resource IDs afresh, a control panel keeps
its settings in itself), else the paths that changed.

Strings in steps may use {target}, {media}, {boot} (volume names) and
{system} (the boot disk's System Folder path). A step with "backend"
runs on that machine only.

  bake.py NAME [--force]                   one bake
  bake.py --all [--jobs N]                 every bake not up to date
  bake.py --list                           bakes and their snapshots
  bake.py NAME --explore [--record]        boot it, leave the guest up;
  bake.py --do '{"click": [100, 200]}'       ... drive it (recorded with --record; a click
                                               on a named control is recorded by name);
                                               each step answers with the front window's
                                               controls and text
  bake.py --do '{"look": "front"}'           ... the UI, and a half-size screenshot of the
                                               front window ("screen": all of it; null: none)
  bake.py --finish | --abort                 ... capture (and save the steps), or not
  bake.py NAME ... --port 18000            the guest's web UI on a fixed port; every run
                                             prints a "watch:" URL (view only) and the web
                                             UI shows the step being run

A snapshot is up to date when its key (the bake definition, its media's
pins and what its base is: base_identity; or for a derive its parents'
content hashes) matches; so is a machine run.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import importlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools", "macfetch"))
import fat_cfm  # noqa: E402
import macfetch  # noqa: E402
import snapshot  # noqa: E402
from containers import MAC_EPOCH, Volume, macbinary_to_files, mr, named, open_container, take  # noqa: E402
import guest as guest_mod  # noqa: E402
from guest import Guest, watch_url  # noqa: E402
from populate_disk import build_macbinary  # noqa: E402
from snapshot import Hfs  # noqa: E402

BAKES = os.path.join(HERE, "bakes")
MEDIA = os.path.join(HERE, "media.json")
BASES = os.path.join(HERE, "bases.json")
STORAGE = macfetch.STORAGE
BAKED = os.path.join(STORAGE, "baked")
IMAGES = os.path.join(STORAGE, "images")
SESSION = os.path.join(STORAGE, "work", "bake-session.json")
DEFAULT_TARGET = "Install"
DEFAULT_OS = "7.6.1"
BACKENDS = {"68k": "uae", "ppc": "kpx", "se": "se", "iici": "iici"}    # machine -> Guest backend (guest.py)
BOTH = ["68k", "ppc"]                   # what "both machines" means (7.5.5, 7.6.1)

ARCHIVE_TYPES = {".sit": b"SIT!", ".hqx": b"TEXT", ".bin": b"BINA", ".zip": b"ZIP ", ".sea": b"APPL",
                 ".cpt": b"PACT", ".sitx": b"SITX"}
# Top-level items a guest makes on any volume it uses.
TOP_NOISE = {b"Desktop Folder", b"Trash", b"Temporary Items", b"Desktop DB", b"Desktop DF",
             b"Desktop", b"Installer Logs", b"TheVolumeSettingsFolder", b"Network Trash Folder"}
# System Folder files never taken, and what changes there just from a guest
# running: the bridge's agent, recent items, files the System touches at boot.
NEVER = {b"System", b"Finder", b"Desktop DB", b"Desktop DF", b"Desktop"}
NOISE = (b":Recent Applications:", b":Recent Documents:", b":Recent Servers:",
         b":Startup Items:BridgeAgent", b":MacTCP DNR", b":Extensions:Printer Share",
         b":Control Panels:Apple Menu Options", b":Extensions:Apple CD-ROM",
         b":Extensions:Apple CD/DVD Driver",                 # 8.6 and later
         b":Startup Items:\rEnergy Saver")             # the PPC machine's Energy Saver (invisible)
# Preferences: only new files are taken (an app's first-launch answers live
# there), minus these, which the System or the bridge write.
PREFS_NOISE = {b"MacPhoenix.cfg", b"Control Strip Preferences", b"CarbonLib Preferences (US)", b"ASLM Preferences", b"Apple Menu Options Prefs",
               b"Mac OS Easy Open Preferences", b"Macintosh Easy Open Preferences", b"WindowShade Preferences", b"Internet Preferences",
               b"Finder Preferences", b"Users & Groups Data File", b"File Sharing Folder",
               b"Launcher Preferences", b"Desktop Pictures Prefs", b"Extensions Manager Preferences",
               b"System Preferences", b"Monitors & Sound Prefs", b"StuffIt Expander Preferences",
               b"Sound Preferences", b"Expansion Manager Prefs", b"QuickTime Preferences",
               b"Disk Copy Log", b"Auto Power On/Off Preferences", b"Apple Video Player Prefs"}


def work_root():
    root = os.path.join(STORAGE, "work")
    os.makedirs(root, exist_ok=True)
    return root


# ---------------------------------------------------------------- inputs

def load_json(path):
    with open(path) as f:
        return json.load(f)


def load_bake(name):
    path = os.path.join(BAKES, name + ".json")
    if not os.path.exists(path):
        sys.exit(f"no bake {name} ({path})")
    bake = load_json(path)
    bake["name"] = name
    return bake


def all_bakes():
    return sorted(f[:-5] for f in os.listdir(BAKES) if f.endswith(".json"))


def bases():
    return {k: v for k, v in load_json(BASES).items() if not k.startswith("_")}


def base_image(os_name):
    """The image a base names: hand-made in images/, an os snapshot, or a
    build.py image (made first if it isn't there)."""
    b = bases().get(os_name)
    if not b:
        sys.exit(f"no base System {os_name} in {BASES}")
    if "snapshot" in b:
        path = snapshot_path(b["snapshot"])
        if not os.path.exists(path):
            sys.exit(f"base {os_name}: {path} not baked yet (bake.py {b['snapshot']})")
        return path, b
    if "built" in b:                        # built (or rebuilt) if it isn't up to date
        path = os.path.join(IMAGES, b["built"] + ".img")
        subprocess.run([sys.executable, os.path.join(HERE, "build.py"), b["built"]], check=True,
                       stdout=subprocess.DEVNULL)
        return path, b
    return os.path.join(IMAGES, b["image"]), b


def base_identity(os_name):
    """What a base System is, for keys: an os snapshot's content hash, a
    built image's content (build.content_identity: its definition and its
    snapshots' contents), a hand-made image's MD5. Not a built file's bytes: rebuilt, the same
    content differs in its dates."""
    path, b = base_image(os_name)
    if "snapshot" in b:
        return snapshot.manifest(path)["content_hash"]
    if "built" in b:
        import build                        # (build imports this module)
        return build.content_identity(build.load_def(b["built"]))
    return file_md5(path)


_hash_cache = {}


def file_md5(path):
    st = os.stat(path)
    k = (path, st.st_size, st.st_mtime)
    if k not in _hash_cache:
        h = hashlib.md5()
        with open(path, "rb") as f:
            while chunk := f.read(1 << 20):
                h.update(chunk)
        _hash_cache[k] = h.hexdigest()
    return _hash_cache[k]


def media_entry(name):
    m = next((p for p in macfetch.load_manifest(MEDIA)["files"] if p["name"] == name), None)
    if m is None:
        sys.exit(f"{name}: not in {MEDIA}")
    return m


def media_path(name):
    """The pinned media file NAME, fetched and verified if needed."""
    m = media_entry(name)
    path = os.path.join(macfetch.manifest_out(MEDIA), macfetch.pin_path(m))
    if not macfetch.file_ok(path, {"src": "pin", **m}):
        if not any(k in m for k in macfetch.LOCATORS):
            sys.exit(f"{name}: local-only media missing; put it at {path}")
        macfetch.download({"src": "pin", **m}, macfetch.manifest_out(MEDIA), log=print)
    return path


def media_names(bake):
    names = [bake["archive"]] if bake.get("archive") else []
    names += bake.get("cdroms", [])
    names += [m["file"] for m in bake.get("media", []) + bake.get("disks", []) + bake.get("first_boot_disks", [])]
    return names


def snapshot_path(name):
    return os.path.join(BAKED, f"{name}.img")


def part_path(name, machine):
    return os.path.join(BAKED, "parts", f"{name}@{machine}.img")


def machines(bake):
    """The bake's "machines"; else its base's (8.6 and 9.0.4: PowerPC only);
    else the 68k one."""
    if "machines" in bake:
        return bake["machines"]
    return bases().get(bake.get("os", DEFAULT_OS), {}).get("machines", ["68k"])


def bake_key(bake):
    """What a snapshot depends on: the definition, its media, its base (or
    for a derive, its parents' contents)."""
    h = hashlib.md5(json.dumps({k: v for k, v in bake.items() if k != "prefer"},   # merge-only
                               sort_keys=True).encode())
    if bake.get("kind") == "derive":
        for p in bake["from"]:
            h.update(snapshot.manifest(snapshot_path(p))["content_hash"].encode())
    else:
        h.update(base_identity(bake.get("os", DEFAULT_OS)).encode())
        for n in media_names(bake):
            h.update(media_entry(n).get("md5", "").encode())
    return h.hexdigest()


def current(path, key):
    if not os.path.exists(path):
        return False
    try:
        return snapshot.manifest(path).get("key") == key
    except (RuntimeError, ValueError, KeyError, FileNotFoundError):
        return False


def up_to_date(bake):
    return current(snapshot_path(bake["name"]), bake_key(bake))


# ------------------------------------------------------------ preparing

def put(hfs, image, items, to=b""):
    """Copy staged items into folder `to` of an unmounted image."""
    hfs.mount(image)
    try:
        for rel, local in items:
            target = (to + b":" if to else b":") + rel
            if local is None:
                hfs.run("hmkdir", target, check=False)
            else:
                hfs.run("hcopy", "-m", local, target)
    finally:
        hfs.umount()


def dc42_checksum(data):
    s = 0
    for (w,) in struct.iter_unpack(">H", data):
        s = (s + w) & 0xFFFFFFFF
        s = ((s >> 1) | (s << 31)) & 0xFFFFFFFF
    return s


def to_dc42(macbinary, name, stamp=0):
    """A raw floppy image in a MacBinary file, rewritten as a Disk Copy 4.2
    image (type dImg, creator ddsk: Disk Copy 6 in the guest mounts it); one
    that is a Disk Copy 4.2 image already is only typed. stamp: its dates."""
    with open(macbinary, "rb") as f:
        blob = f.read()
    dlen = struct.unpack(">I", blob[83:87])[0]
    data = blob[128:128 + dlen]
    if (len(data) > 84 and data[0] < 64 and data[82:84] == b"\x01\x00"
            and 84 + struct.unpack(">I", data[64:68])[0] <= len(data)):    # already Disk Copy 4.2:
        with open(macbinary, "wb") as f:                                    # only typed for Disk Copy
            f.write(build_macbinary(mr(name)[:31], b"dImg", b"ddsk", data, b"", stamp, stamp))
        return
    size, fmt = next(((s, f) for s, f in ((409600, 0), (819200, 1), (1474560, 3)) if len(data) <= s),
                     ((len(data) + 511) // 512 * 512, 3))
    data = data.ljust(size, b"\0")
    label = mr(name)[:63]
    header = bytes([len(label)]) + label.ljust(63, b"\0") + struct.pack(
        ">IIIIBBH", size, 0, dc42_checksum(data), 0, fmt, 0x22 if fmt else 0x02, 0x0100)
    with open(macbinary, "wb") as f:
        f.write(build_macbinary(mr(name)[:31], b"dImg", b"ddsk", header + data, b"", stamp, stamp))


RECENT = (b"Recent Applications", b"Recent Documents", b"Recent Servers")


def clear_recent(hfs):
    """Empty the Apple menu's Recent folders on the mounted volume: aliases to
    what the install ran (its installer on a floppy, the agent), not the
    System's. Their paths, removed."""
    removed = []
    for folder in RECENT:
        path = b":System Folder:Apple Menu Items:" + folder
        if hfs.exists(path):
            for name, is_dir in hfs.ls(path + b":"):
                if not is_dir:
                    hfs.run("hdel", path + b":" + name)
                    removed.append((path + b":" + name).decode("mac_roman"))
    return removed


def media_items(m, work):
    """A bake's "media" entry as the items put on its Media volume."""
    if m.get("as_is"):                      # a MacBinary file put there whole, not opened
        with open(media_path(m["file"]), "rb") as f:    # (a self-mounting HFS+ image)
            head = f.read(128)
        name = m.get("as") or head[2:2 + head[1]].decode("mac_roman")
        return [(mr(name), media_path(m["file"]))]
    stage = tempfile.mkdtemp(dir=work)
    items = take(media_path(m["file"]), m.get("take", ""), stage, work)
    if m.get("dc42"):
        stamp = int(os.path.getmtime(media_path(m["file"]))) + MAC_EPOCH
        for rel, local in items:
            if local:
                to_dc42(local, rel.rsplit(b":", 1)[-1].decode("mac_roman"), stamp)
    items, rename = named(items, m.get("take", ""), m.get("as"))
    if rename:
        items = [(mr(rename) + r[r.find(b":"):] if b":" in r else mr(rename), l) for r, l in items]
    return items


def archive_item(bake, work):
    """An expand bake's "archive" as a MacBinary file typed so StuffIt
    Expander knows it, creator Expander's: (name, file, data size)."""
    stage = tempfile.mkdtemp(dir=work)
    src = media_path(bake["archive"])
    if bake.get("take"):
        rel, local = take(src, bake["take"], stage, work)[0]
        name = rel.rsplit(b":", 1)[-1].decode("mac_roman")
        plain = os.path.join(stage, "archive")
        macbinary_to_files(local, plain)
        with open(local, "rb") as f:
            ftype = f.read(128)[65:69]
        with open(plain, "rb") as f:
            data = f.read()
    else:
        name = bake["archive"]
        ftype = ARCHIVE_TYPES.get(os.path.splitext(name)[1].lower(), b"????")
        with open(src, "rb") as f:
            data = f.read()
    name = name[:31]
    local = os.path.join(stage, "archive.bin")
    with open(local, "wb") as f:
        stamp = int(os.path.getmtime(src)) + MAC_EPOCH
        f.write(build_macbinary(mr(name), ftype, b"SITx", data, b"", stamp, stamp))
    return name, local, len(data)


def volume_name(image):
    hfs = Hfs(work_root())
    hfs.mount(image)
    try:
        out = hfs.run("hvol").decode("mac_roman", "replace")
    finally:
        hfs.umount()
    for line in out.splitlines():
        if line.startswith("Volume name is"):
            return line.split('"', 1)[1].rsplit('"', 1)[0]
    raise RuntimeError(f"{image}: no volume name")


def subst(obj, names):
    if isinstance(obj, str):
        for k, v in names.items():
            obj = obj.replace("{" + k + "}", v)
        return obj
    if isinstance(obj, list):
        return [subst(x, names) for x in obj]
    if isinstance(obj, dict):
        return {k: subst(v, names) for k, v in obj.items()}
    return obj


class Session:
    """A prepared guest for one bake on one base: images, media, steps."""

    def __init__(self, bake, machine, port, log=print, extra=()):
        self.bake, self.machine, self.log = bake, machine, log
        self.extra = list(extra) + bake.get("args", [])     # more emulator arguments (--no-text-feed,
                                                            # the bake's "args": --model-id 5 for 7.1)
        if bake.get("network"):                 # the guest networked (e.g. "socket": net-bridge, DHCP)
            self.extra += ["--network", bake["network"]]
        self.backend = BACKENDS[machine]
        self.os = bake.get("os", DEFAULT_OS)
        self.work = tempfile.mkdtemp(prefix=f"bake.{bake['name']}@{machine}.", dir=work_root())
        self.base_path, self.base = base_image(self.os)
        self.extra += [a for a in self.base.get("args", []) if a not in self.extra]   # (--model-id 5: 7.1)
        self.boot = os.path.join(self.work, "boot.img")
        self.target = os.path.join(self.work, "target.img")
        is_os = bake.get("kind") == "os"
        self.target_name = bake.get("target", "Macintosh HD" if is_os else DEFAULT_TARGET)
        shutil.copyfile(self.base_path, self.boot)
        hfs = Hfs(self.work)
        self.drop_old_agent(hfs)
        snapshot.blank(hfs, self.target, self.target_name, bake.get("target_mb", 250 if is_os else 1024))
        self.exclude = {mr(n) for n in bake.get("exclude", [])}   # e.g. an archive copied in to expand
        self.disks = [self.boot, self.target]
        self.cdroms = [media_path(n) for n in bake.get("cdroms", [])]
        self.pre_steps = []
        self.media_volume(hfs)
        self.disk_images()
        if bake.get("archive"):
            self.place_archive(hfs)
        boot_name = volume_name(self.boot)
        if boot_name == self.target_name:     # an os bake run from a System on "Macintosh HD"
            hfs.mount(self.boot)
            try:
                hfs.run("hrename", mr(boot_name) + b":", b"Bootstrap:")
            finally:
                hfs.umount()
            boot_name = "Bootstrap"
        self.names = {"target": self.target_name, "media": "Media", "boot": boot_name,
                      "system": boot_name + ":System Folder:"}
        self.steps = [s for s in subst(self.pre_steps + bake.get("steps", []), self.names)
                      if s.get("backend", self.backend) == self.backend]
        self.port = port
        self.guest = None

    def drop_old_agent(self, hfs):
        """The scratch boot disk without a BridgeAgent of its own: an older
        install's in Startup Items (the hand-made bases have one, with the
        _WaitNextEvent patch that bombs under Apple's Installer) would run
        beside the one the emulator launches."""
        hfs.mount(self.boot)
        try:
            for p in (b":System Folder:Startup Items:BridgeAgent", b":System Folder:Preferences:MacPhoenix.cfg"):
                if hfs.exists(p):
                    hfs.run("hdel", p)
        finally:
            hfs.umount()

    def media_volume(self, hfs):
        if not self.bake.get("media"):
            return
        vol = os.path.join(self.work, "media.img")
        snapshot.blank(hfs, vol, "Media", 512)
        for m in self.bake["media"]:
            put(hfs, vol, media_items(m, self.work))
        self.disks.append(vol)

    def disk_images(self):
        for d in self.bake.get("disks", []):
            if not d.get("take"):                  # the media file is the disk image:
                local = os.path.join(tempfile.mkdtemp(dir=self.work), d["file"])
                shutil.copyfile(media_path(d["file"]), local)   # a copy; the guest writes to disks
                self.disks.append(open_container(local, self.work).image)
                continue
            stage = tempfile.mkdtemp(dir=self.work)
            items = take(media_path(d["file"]), d["take"], stage, self.work)
            item = os.path.join(tempfile.mkdtemp(dir=self.work), "item")
            macbinary_to_files(items[0][1], item)
            self.disks.append(open_container(item, self.work).image)

    def place_archive(self, hfs):
        """The archive on the target, typed so StuffIt Expander knows it,
        creator Expander's; opened, expanded, left out of the capture."""
        name, local, size = archive_item(self.bake, self.work)
        put(hfs, self.target, [(mr(name), local)])
        self.exclude.add(mr(name))
        self.pre_steps = [{"open": "{target}:" + name}, {"wait_app": "StuffIt Expander", "timeout": 30, "optional": True},
                          {"wait": 5 + size // 200_000}, {"shot": "expanded"}, {"quit": True}]

    def boot_guest(self):
        self.log(f"  watch: {watch_url(self.port)}")
        self.guest = Guest(self.work, self.backend, self.disks, self.cdroms, self.port, STORAGE, extra=self.extra)
        self.guest.annotate(f"{self.bake['name']}@{self.machine}: booting")
        self.guest.wait_finder(240)
        time.sleep(self.bake.get("settle", 3))

    def run_steps(self):
        for i, step in enumerate(self.steps):
            line = f"step {i}: {json.dumps(step, ensure_ascii=False)}"
            self.log("  " + line)
            self.guest.annotate(f"{self.bake['name']} {line}")
            self.run_step({k: v for k, v in step.items() if k != "backend"})

    def run_step(self, step):
        if "reboot" in step:
            self.reboot()
        else:
            self.guest.run(step)

    def reboot(self):
        """{"reboot": true}: the guest restarted fresh on the same disks: a
        clean shutdown if the bridge's agent is up, else the emulator
        stopped. After Apple's Installer has installed a System and quit,
        BridgeAgent dies (system error 3: PLAN.md, P2 status), so an os
        bake reboots before it needs the bridge again."""
        if (not self.guest.agent
                or (self.guest.get("/api/status") or {}).get("bridge_agent_connected")):
            self.guest.shutdown()
        self.guest.stop()
        self.guest = Guest(self.work, self.backend, self.disks, self.cdroms, self.port, STORAGE, extra=self.extra)
        self.guest.annotate(f"{self.bake['name']}@{self.machine}: rebooting")
        self.guest.wait_finder(240, agent=self.guest.agent)
        time.sleep(self.bake.get("settle", 3))

    def first_boot_disks(self):
        """"first_boot_disks": disk images out of media attached at the first
        boot ({"file", "take"}, decoded on the host: a floppy set an
        updater wants while the new System is the startup disk)."""
        out = []
        for d in self.bake.get("first_boot_disks", []):
            stage = tempfile.mkdtemp(dir=self.work)
            items = take(media_path(d["file"]), d["take"], stage, self.work)
            item = os.path.join(tempfile.mkdtemp(dir=self.work), "item")
            macbinary_to_files(items[0][1], item)
            out.append(open_container(item, self.work).image)
        return out

    def first_boot(self):
        """An os bake: the guest restarted from the new System (and any
        "first_boot_disks"), for the "first_boot" steps."""
        self.disks = [self.target] + self.first_boot_disks()     # what a reboot boots from now
        self.cdroms = []
        self.guest = Guest(self.work, self.backend, self.disks, [], self.port, STORAGE, extra=self.extra)
        self.guest.agent = self.guest.agent and self.bake.get("first_boot_agent", True)
        self.guest.annotate(f"{self.bake['name']}: first boot from {self.target_name}")
        self.guest.wait_finder(300, agent=self.guest.agent)
        time.sleep(self.bake.get("settle", 3))
        self.guest.shot("first-boot")
        names = dict(self.names, boot=self.target_name, system=self.target_name + ":System Folder:")
        for i, step in enumerate(subst(self.bake.get("first_boot", []), names)):
            if step.get("backend", self.backend) != self.backend:
                continue
            self.log(f"  first boot {i}: {json.dumps(step, ensure_ascii=False)}")
            self.guest.annotate(f"{self.bake['name']} first boot {i}: {json.dumps(step, ensure_ascii=False)}")
            self.run_step({k: v for k, v in step.items() if k != "backend"})
        self.guest.shot("first-boot-done")
        self.guest.shutdown()

    def capture_os(self, out):
        """The whole target as the snapshot, minus what the bridge put there."""
        part = out + ".part"
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(self.target, part)
        hfs = Hfs(self.work)
        hfs.mount(part)
        removed = []
        try:
            for p in (b":System Folder:Startup Items:BridgeAgent", b":System Folder:Preferences:MacPhoenix.cfg"):
                if hfs.exists(p):
                    hfs.run("hdel", p)
                    removed.append(p.decode("mac_roman"))
            removed += clear_recent(hfs)
            if hfs.exists(b":Trash"):           # agents install_bridge_agent.sh moved aside
                for name, _ in hfs.ls(b":Trash:"):
                    if name.startswith(b"Old-BA-"):
                        hfs.run("hdel", b":Trash:" + name)
                        removed.append(":Trash:" + name.decode("mac_roman"))
            for need in (b":System Folder:System", b":System Folder:Finder"):
                if not hfs.exists(need):
                    raise RuntimeError(f"no {need.decode()} on the new volume")
            local = os.path.join(tempfile.mkdtemp(dir=self.work), "System.bin")
            hfs.run("hcopy", "-m", b":System Folder:System", local)
            enablers = []                       # a System Enabler carries an update's version:
            for line in hfs.run("hls", "-l", b":System Folder:").split(b"\n"):   # 9.0.4's System says 9.0
                if line[3:8] == b"gbly/":
                    e = os.path.join(tempfile.mkdtemp(dir=self.work), "enabler.bin")
                    hfs.run("hcopy", "-m", b":System Folder:" + enabler_name(line), e)
                    enablers.append(system_version(e))
        finally:
            hfs.umount()
        version = system_version(local)
        newer = [v for v in enablers if v and vers_key(v) > vers_key(version or "0")]
        if newer:
            version = max(newer, key=vers_key)
        want = self.bake.get("version")
        if want and version != want:
            raise RuntimeError(f"the System file is version {version}, not {want}")
        bake = self.bake
        meta = {"name": bake["name"], "kind": "os", "os": version, "bootstrap": self.os,
                "machines": [self.machine], "target": self.target_name, "key": bake_key(bake),
                "recipe_md5": hashlib.md5(json.dumps(bake, sort_keys=True).encode()).hexdigest(),
                "media": {n: media_entry(n).get("md5") for n in media_names(bake)},
                "made": time.strftime("%Y-%m-%d %H:%M:%S"), "bridge_removed": removed}
        meta = snapshot.seal_volume(self.work, part, meta)
        os.replace(part, out)
        self.log(f"{out}: System {version}, {len(meta['items'])} items, {os.path.getsize(out) >> 20} MB")
        return out

    # ---- capture

    def capture(self, out):
        """The snapshot at `out`: the target's top level and the boot disk's
        new or changed System Folder files."""
        work = self.work
        hfs = Hfs(work)
        before = sysfolder_content(hfs, self.base_path, work)
        take = {mr(t) for t in self.bake.get("system_take", [])}
        after = sysfolder_content(hfs, self.boot, work)
        added, changed, ignored = [], [], []
        for p, line in sorted(after.items()):
            if line is None or before.get(p) == line:
                continue
            leaf = p.rsplit(b":", 1)[1]
            prefs = p.startswith(b":System Folder:Preferences:")
            if p[len(b":System Folder:"):] in take:          # named by the bake: always taken
                (added if p not in before else changed).append(p)
            elif leaf in NEVER or any(n in p for n in NOISE) or (prefs and (p in before or leaf in PREFS_NOISE)):
                ignored.append(p)
            else:
                (added if p not in before else changed).append(p)
        # What the install took out of the System Folder (an installer moving
        # what it replaces aside: OT's the classic networking): the build
        # removes it too.
        removed = sorted(p for p in before
                         if p not in after and not any(n in p for n in NOISE) and p.rsplit(b":", 1)[1] not in NEVER)
        items = Volume(self.target, work).export(":*", tempfile.mkdtemp(dir=work))
        dropped = TOP_NOISE | self.exclude
        if self.bake.get("archive"):            # an .hqx expands to a .sit and then a folder
            dropped |= {r.split(b":")[0] for r, l in items if b":" not in r and l
                        and os.path.splitext(r.decode("mac_roman"))[1].lower() in ARCHIVE_TYPES}
        excluded = sorted({r.split(b":")[0] for r, _ in items if r.split(b":")[0] in dropped})
        items = [(r, l) for r, l in items if r.split(b":")[0] not in dropped]
        if self.bake.get("from_boot"):
            hfs.mount(self.base_path)
            had = {n for n, _ in hfs.ls(b":")}
            hfs.umount()
            hfs.mount(self.boot)
            new = [n for n, _ in hfs.ls(b":") if n not in had and n not in TOP_NOISE]
            hfs.umount()
            boot = Volume(self.boot, work)
            for n in new:
                items += boot.export(b":" + n, tempfile.mkdtemp(dir=work))
        system = []
        boot = Volume(self.boot, work)
        seen = set()
        for p in added + changed:
            rel = p[len(b":System Folder:"):]
            parts = rel.split(b":")
            for i in range(1, len(parts)):
                folder = b":".join(parts[:i])
                if folder not in seen:
                    seen.add(folder)
                    system.append((folder, None))
            (leaf, local), = boot.export(p.decode("mac_roman"), tempfile.mkdtemp(dir=work))
            system.append((rel, local))
        top = {r.decode("mac_roman") for r, _ in items if b":" not in r}
        missing = [n for n in self.bake.get("expect", []) if n not in top]
        if missing:
            raise RuntimeError(f"the bake didn't produce {missing} (it made {sorted(top)})")
        if self.bake.get("expect") == [] and not top:     # "expect": []: something, named or not
            raise RuntimeError("nothing was installed on the target")
        if not items and not system:
            raise RuntimeError("nothing was installed")
        bake = self.bake
        meta = {"name": bake["name"], "kind": bake.get("kind", "install"), "os": self.os,
                "machines": [self.machine], "target": self.target_name, "key": bake_key(bake),
                "recipe_md5": hashlib.md5(json.dumps(bake, sort_keys=True).encode()).hexdigest(),
                "media": {n: media_entry(n).get("md5") for n in media_names(bake)},
                "launch": bake.get("launch", []), "made": time.strftime("%Y-%m-%d %H:%M:%S"),
                "system_remove": bake.get("system_remove", []) + [
                    r.decode("mac_roman")[len(":System Folder:"):] for r in removed
                    if r.decode("mac_roman")[len(":System Folder:"):] not in bake.get("system_remove", [])],
                "excluded": [e.decode("mac_roman") for e in excluded],
                "system_ignored": [p.decode("mac_roman") for p in ignored]}
        os.makedirs(os.path.dirname(out), exist_ok=True)
        meta = snapshot.write(work, out, bake["name"], items, system, meta)
        for p in removed:
            self.log(f"  System Folder removed: {p.decode('mac_roman')}")
        for p in added:
            self.log(f"  System Folder added: {p.decode('mac_roman')}")
        for p in changed:
            self.log(f"  System Folder changed: {p.decode('mac_roman')}")
        self.log(f"{out}: {len(meta['items'])} items, {len(meta['system'])} System Folder entries, "
                 f"{os.path.getsize(out) >> 20} MB" + (f"; aliases: {meta['aliases']}" if meta["aliases"] else ""))
        return out


def vers_key(v):
    return tuple(int(x) if x.isdigit() else 0 for x in v.split("."))


def enabler_name(line):
    """The file name in an `hls -l` line (after the date's time or year)."""
    import re
    m = re.match(rb"^\S\s+\S+\s+\d+\s+\d+\s+\w{3}\s+\d+\s+[\d:]+\s(.*)$", line)
    return m.group(1) if m else line.split(b" ")[-1]


def system_version(macbinary):
    """The short version string of a System file's 'vers' 1."""
    with open(macbinary, "rb") as f:
        _, _, rsrc = fat_cfm.macbinary(f.read())
    for t, i, _, _, payload in fat_cfm.read_rsrc(rsrc):
        if t == b"vers" and i == 1:
            return payload[7:7 + payload[6]].decode("mac_roman")
    return None


def sysfolder_content(hfs, image, work):
    """{path: (type, creator, flags, data MD5, rsrc MD5) or None for a folder}
    for :System Folder, recursively. By content, not the listing: an installer
    copies files with their original dates, so a replaced file of the same
    size (Open Transport's AppleTalk Preferences) looks unchanged there."""
    hfs.mount(image)
    try:
        entries = snapshot.listing(hfs, b":System Folder", work)
    finally:
        hfs.umount()
    out = {}
    for e in entries:
        p = b":System Folder:" + e["path"].encode("mac_roman")
        out[p] = None if e.get("folder") else (e["type"], e["creator"], e["flags"], e["data"], e["rsrc"])
    return out


def sysfolder(hfs, image):
    """{path: hls -l line or None for a folder} for :System Folder, recursively."""
    out = {}
    hfs.mount(image)
    try:
        def walk(path):
            for line in hfs.run("hls", "-l", "-a", path + b":").split(b"\n"):    # not \r: names can hold one
                m = snapshot.LS.match(line)
                if not m:
                    continue
                p = path + b":" + m.group(2)
                if m.group(1) == b"d":
                    out[p] = None
                    walk(p)
                else:
                    out[p] = line
        walk(b":System Folder")
    finally:
        hfs.umount()
    return out


# ---------------------------------------------------------------- baking

def bake_run(bake, machine, out, keep=False, log=print, port=None, extra=()):
    """One run of a bake on one machine, captured to `out`."""
    s = Session(bake, machine, port or free_port(), log, extra)
    log(f"{bake['name']}@{machine}: work {s.work}")
    try:
        s.boot_guest()
        s.run_steps()
        s.guest.shot("final")
        s.guest.annotate(f"{bake['name']}@{machine}: steps done; shutting down and capturing")
        s.guest.shutdown()
        if bake.get("kind") == "os":
            s.first_boot()
            s.capture_os(out)
        else:
            s.capture(out)
    except BaseException as e:
        if s.guest:
            s.guest.annotate(f"{bake['name']}@{machine}: FAILED: {e}")
            s.guest.shot("failed")
            s.guest.stop()
        log(f"  failed; work directory kept: {s.work}")
        raise
    if not keep:
        shutil.rmtree(s.work, ignore_errors=True)
    return out


def volatile(bake):
    """Paths in the bake's manifest listings compared by name only."""
    v = set(bake.get("volatile", []))
    if bake.get("kind") == "os":                # the whole volume: the System's own churn
        v |= {"System Folder:Preferences:" + n.decode("mac_roman") for n in PREFS_NOISE}
    return v


def bake_one(bake, force=False, keep=False, log=print, port=None, extra=()):
    """The product's snapshot, compared with the one it replaces."""
    out = snapshot_path(bake["name"])
    try:
        old = snapshot.manifest(out) if os.path.exists(out) else None
    except (RuntimeError, ValueError, KeyError):
        old = None
    result = bake_one_(bake, force, keep, log, port, extra)
    if old:
        sized = ["System Folder:Fonts:", "System Folder:Control Panels:"] if bake.get("kind") == "os" else []
        diff = snapshot.install_diff(old, snapshot.manifest(out), volatile(bake), sized)
        log(f"{bake['name']}: " + (f"install changed: {', '.join(diff)}" if diff
                                   else "same install as the snapshot it replaces"))
    return result


def bake_one_(bake, force=False, keep=False, log=print, port=None, extra=()):
    """The product's snapshot: one run, or a run per machine merged."""
    if bake.get("kind") == "stage":
        sys.exit(f"{bake['name']}: a stage-only product (no snapshot); build.py puts it on an Installers disk")
    out = snapshot_path(bake["name"])
    key = bake_key(bake) if bake.get("kind") != "derive" else None
    if bake.get("kind") == "derive":
        return merge(bake, [snapshot_path(p) for p in bake["from"]], out, bake_key(bake), log)
    ms = machines(bake)
    if len(ms) == 1:
        return bake_run(bake, ms[0], out, keep, log, port, extra)
    parts = []
    for m in ms:
        part = part_path(bake["name"], m)
        if force or not current(part, key):
            bake_run(bake, m, part, keep, log, port, extra)
        else:
            log(f"  {os.path.basename(part)} up to date")
        parts.append(part)
    return merge(bake, parts, out, key, log)


def forks_of(local):
    with open(local, "rb") as f:
        return snapshot.macbinary_forks(f.read())


def same_file(a, b):
    ta, ca, fa, da, ra = forks_of(a)
    tb, cb, fb, db, rb = forks_of(b)
    return (ta, ca, da, snapshot.fork_md5(ra, True)) == (tb, cb, db, snapshot.fork_md5(rb, True))


def is_cfm(local):
    rsrc = forks_of(local)[4]
    return any(t == b"cfrg" for t, *_ in fat_cfm.read_rsrc(rsrc)) if rsrc else False


def is_classic(local):
    """A classic 68k build (CODE resources, no data fork): fat with a PowerPC one."""
    _, _, _, data, rsrc = forks_of(local)
    return fat_cfm.is_classic(fat_cfm.read_rsrc(rsrc), data) if rsrc else False


def merge(bake, parts, out, key, log=print):
    """One snapshot from several (machine runs, or a derive's parents): every
    file any has; a file they have in different builds made fat when it is
    CFM code (or a classic 68k build and a PowerPC one). Anything else that differs fails, unless the bake says which
    to keep ("prefer": {path: index into parts})."""
    work = tempfile.mkdtemp(prefix=f"merge.{bake['name']}.", dir=work_root())
    trees = [{f: dict(Volume(p, work).export(f":{f}:*", tempfile.mkdtemp(dir=work)))
              for f in ("Items", "System Folder")} for p in parts]
    prefer = {mr(k): v for k, v in bake.get("prefer", {}).items()}
    result, fat, conflicts = {}, [], []
    for folder in ("Items", "System Folder"):
        merged = {}
        for t in trees:
            for rel, local in t[folder].items():
                if folder == "System Folder" and rel.startswith(b"Preferences:") and rel[12:] in PREFS_NOISE:
                    continue                    # parts made before the noise list knew it
                if rel not in merged or merged[rel] is None or local is None:
                    merged.setdefault(rel, local)
                    continue
                if same_file(merged[rel], local):
                    continue
                if forks_of(merged[rel])[2] & snapshot.ALIAS_FLAG and forks_of(local)[2] & snapshot.ALIAS_FLAG:
                    continue                    # an alias records IDs: two installs never match byte for byte
                path = (folder + ":").encode() + rel
                if rel in prefer:
                    merged[rel] = trees[prefer[rel]][folder][rel]
                elif (is_cfm(merged[rel]) or is_classic(merged[rel])) and (is_cfm(local) or is_classic(local)) \
                        and not (is_classic(merged[rel]) and is_classic(local)):
                    with open(merged[rel], "rb") as a, open(local, "rb") as b:
                        data = fat_cfm.merge(a.read(), b.read())
                    fat_local = os.path.join(tempfile.mkdtemp(dir=work), "fat.bin")
                    with open(fat_local, "wb") as f:
                        f.write(data)
                    merged[rel] = fat_local
                    fat.append(path.decode("mac_roman"))
                else:
                    conflicts.append(path.decode("mac_roman"))
        result[folder] = merged
    if conflicts:
        raise RuntimeError(f"{bake['name']}: the runs differ in {conflicts}; add \"prefer\" to the bake")
    for f in fat:
        log(f"  fat: {f}")
    mans = [snapshot.manifest(p, work) for p in parts]
    meta = {"name": bake["name"], "kind": bake.get("kind", "install"), "os": mans[0]["os"],
            "machines": sorted({m for x in mans for m in x.get("machines", [])}), "target": mans[0]["target"],
            "key": key, "parts": [os.path.basename(p) for p in parts], "fat": fat,
            "launch": bake.get("launch", mans[0].get("launch", [])), "made": time.strftime("%Y-%m-%d %H:%M:%S")}
    meta = snapshot.write(work, out, bake["name"], sorted(result["Items"].items()),
                          sorted(result["System Folder"].items()), meta)
    log(f"{out}: {len(meta['items'])} items, {len(meta['system'])} System Folder entries, machines {meta['machines']}")
    shutil.rmtree(work, ignore_errors=True)
    return out


def free_port():
    import socket
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def base_deps(os_name, seen=None):
    """The snapshots a base System is made of: an os bake's, or a built
    image's (its base's, its System Folder parts', what it places)."""
    seen = set() if seen is None else seen
    b = bases().get(os_name, {})
    if "snapshot" in b:
        return {b["snapshot"]}
    if "built" not in b or b["built"] in seen:
        return set()
    seen.add(b["built"])
    try:
        d = load_json(os.path.join(HERE, "images", b["built"] + ".json"))
    except (OSError, ValueError):
        return set()
    deps = set(d.get("system_from", [])) | {p["snapshot"] for p in d.get("place", [])}
    if d.get("base"):
        deps |= base_deps(d["base"], seen)
    return deps


def bake_all(names, jobs, force):
    """Every bake not up to date, in waves: a bake runs once every snapshot
    its base System is made of is done (an os bake before what's baked on
    it); derives after the rest. "wip" bakes are left out."""
    todo = [b for b in (load_bake(n) for n in names) if not b.get("wip") and b.get("kind") != "stage"]
    failed = []

    def run(b):
        if not force and up_to_date(b):
            return f"up to date  {b['name']}"
        logf = os.path.join(work_root(), f"bake-{b['name']}.log")
        r = subprocess.run([sys.executable, __file__, b["name"]] + (["--force"] if force else []),
                           stdout=open(logf, "w"), stderr=subprocess.STDOUT)
        if r.returncode:
            failed.append(b["name"])
            return f"FAILED      {b['name']} (log {logf})"
        return f"baked       {b['name']}"

    rest = [b for b in todo if b.get("kind") != "derive"]
    deps = {b["name"]: base_deps(b.get("os", DEFAULT_OS)) - {b["name"]} for b in rest}
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        while rest:
            waiting = {b["name"] for b in rest}
            wave = [b for b in rest if not deps[b["name"]] & waiting] or rest
            rest = [b for b in rest if b not in wave]
            for line in pool.map(run, wave):
                print(line, flush=True)
    for b in todo:
        if b.get("kind") == "derive":
            print(run(b), flush=True)
    return 1 if failed else 0


def list_bakes():
    for n in all_bakes():
        b = load_bake(n)
        path = snapshot_path(n)
        if b.get("kind") == "stage":
            print(f"{'staged':11} {n}  [stage: on an Installers disk only]")
            continue
        try:
            state = "up to date" if up_to_date(b) else ("stale" if os.path.exists(path) else "missing")
        except SystemExit as e:
            state = f"? ({e})"
        print(f"{state:11} {n}  [{b.get('kind', 'install')}, {'+'.join(machines(b))}]")


# ------------------------------------------------------- explore / record

class Explore:
    """A bake's guest left up to be driven step by step (bake.py --do), with
    the steps optionally recorded into the bake; --finish captures."""

    def __init__(self, bake, machine, record, port=None, extra=()):
        self.port = port or free_port()
        self.lock = threading.Lock()            # one step at a time
        self.session = Session(bake, machine, self.port, extra=extra)
        self.record, self.recorded = record, []
        self.done = threading.Event()
        self.result = None

    def serve(self):
        """The control server first (so --abort works from the start), then
        the guest booted and the bake's existing steps replayed under the
        step lock. A replay that fails leaves the guest up to look at."""
        s = self.session
        control = free_port()
        explore = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
                try:
                    reply = explore.handle(self.path, body)
                except Exception as e:  # report to the driver, keep the session
                    reply = {"error": f"{type(e).__name__}: {e}"}
                data = json.dumps(reply, ensure_ascii=False).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(data)

        server = ThreadingHTTPServer(("127.0.0.1", control), Handler)
        with open(SESSION, "w") as f:
            json.dump({"control": control, "pid": os.getpid(), "bake": s.bake["name"], "machine": s.machine,
                       "work": s.work, "guest": f"http://localhost:{s.port}"}, f)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        print(f"explore {s.bake['name']}@{s.machine}: work {s.work}\nwatch: {watch_url(s.port)}", flush=True)
        with self.lock:
            try:
                s.boot_guest()
                s.run_steps()
                print(f"guest up at {s.guest.base}; drive it with: bake.py --do '<step json>' ... "
                      f"then --finish or --abort", flush=True)
            except Exception as e:
                print(f"replay stopped: {type(e).__name__}: {e}\n(the guest is left up: --do to go on, "
                      f"or --abort)", flush=True)
        self.done.wait()
        server.shutdown()
        if os.path.exists(SESSION):
            os.unlink(SESSION)
        return self.result

    def handle(self, path, body):
        s = self.session
        if path == "/abort":                    # not behind the lock: stops a step's waits
            if s.guest:
                s.guest.cancelled = True
            with self.lock:
                if s.guest:
                    s.guest.stop()
            self.done.set()
            return {"ok": True, "work": s.work}
        with self.lock:
            return self.handle_locked(path, body)

    def handle_locked(self, path, body):
        s = self.session
        if path == "/step":
            importlib.reload(guest_mod)         # guest.py edits apply to the running session
            s.guest.__class__ = guest_mod.Guest
            s.guest.cancelled = False
            if "look" in body:                  # just look: the UI, and a screenshot if asked
                return {"ui": s.guest.summary(),
                        "shot": s.guest.shot(f"look-{len(self.recorded):02d}", front=body["look"] != "screen",
                                             scale=0.5) if body["look"] else None}
            if isinstance(body.get("click"), list):   # a pixel click on something named: record the name
                named = s.guest.at(*body["click"])
                if named:
                    body = {"click": named}
            step = subst(body, s.names)
            s.guest.annotate(f"{s.bake['name']} explore: {json.dumps(body, ensure_ascii=False)}")
            s.run_step(step)
            self.recorded.append(body)
            return {"ok": True, "recorded": body, "ui": s.guest.summary()}
        if path == "/finish":
            s.guest.shutdown()
            if self.record and self.recorded:
                path_ = os.path.join(BAKES, s.bake["name"] + ".json")
                d = load_json(path_)
                d["steps"] = d.get("steps", []) + self.recorded
                with open(path_, "w") as f:
                    f.write(json.dumps(d, indent=1, ensure_ascii=False) + "\n")
                s.bake["steps"] = d["steps"]
            out = snapshot_path(s.bake["name"]) if len(machines(s.bake)) == 1 else part_path(s.bake["name"], s.machine)
            if s.bake.get("kind") == "os":
                s.first_boot()
                self.result = s.capture_os(out)
            else:
                self.result = s.capture(out)
            self.done.set()
            return {"ok": True, "snapshot": self.result, "recorded": len(self.recorded)}
        return {"error": f"unknown {path}"}


def control(path, body=None):
    import urllib.request
    if not os.path.exists(SESSION):
        sys.exit("no explore session running")
    sess = load_json(SESSION)
    req = urllib.request.Request(f"http://127.0.0.1:{sess['control']}{path}",
                                 data=json.dumps(body or {}).encode(), method="POST")
    with urllib.request.urlopen(req, timeout=900) as r:
        print(r.read().decode())


# ------------------------------------------------------------------- CLI

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", nargs="?")
    ap.add_argument("--machine", choices=sorted(BACKENDS), help="with --explore: which machine (default the first)")
    ap.add_argument("--force", action="store_true", help="bake even if up to date")
    ap.add_argument("--keep", action="store_true", help="keep the work directory")
    ap.add_argument("--all", action="store_true", help="every bake that isn't up to date")
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--explore", action="store_true")
    ap.add_argument("--record", action="store_true", help="with --explore: append driven steps to the bake")
    ap.add_argument("--do", metavar="STEP")
    ap.add_argument("--finish", action="store_true")
    ap.add_argument("--abort", action="store_true")
    ap.add_argument("--emulator-arg", action="append", default=[], metavar="ARG",
                    help="pass ARG to the emulator (repeatable; e.g. --emulator-arg=--no-text-feed)")
    ap.add_argument("--port", type=int, help="the guest's web UI port (watch it there; default: any free one)")
    args = ap.parse_args()
    if args.do:
        return control("/step", json.loads(args.do))
    if args.finish:
        return control("/finish")
    if args.abort:
        return control("/abort")
    if args.list:
        return list_bakes()
    if args.all:
        return bake_all([args.name] if args.name else all_bakes(), args.jobs, args.force)
    if not args.name:
        ap.error("name a bake, or --all / --list")
    bake = load_bake(args.name)
    if args.explore:
        out = Explore(bake, args.machine or machines(bake)[0], args.record, args.port, args.emulator_arg).serve()
        print(out or "aborted")
        return 0
    if not args.force and up_to_date(bake):
        print(f"up to date: {snapshot_path(bake['name'])}")
        return 0
    try:
        bake_one(bake, args.force, args.keep, port=args.port, extra=args.emulator_arg)
    except Exception:
        traceback.print_exc()
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
