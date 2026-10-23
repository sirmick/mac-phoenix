"""installers.py — the installers a bake used, staged for an Installers disk.

An Installers image definition (provisioning/images/installers-*.json, one
per category) lists products: {"as": "<Product> <version> ƒ", "bakes": [...]}.
Each product's folder holds what its bakes gave the guest, in the form the
guest got it:

  "media"   as on the bake's Media volume (bake.media_items)
  "archive" the archive, typed for StuffIt Expander (bake.archive_item)
  "disks", "first_boot_disks", "cdroms"
            a floppy-sized volume as a Disk Copy 4.2 image Disk Copy mounts;
            a bigger one (a CD) as a folder named after it holding only what
            the steps used: the folder of each installer launched or file
            opened there, each item copied from it (the whole volume when an
            installer sits at its top)

Bakes in one product share items: the first copy is kept.
"""

import os
import shutil
import struct
import tempfile

import bake
from containers import MAC_EPOCH, Volume, macbinary_to_files, mr, open_container, take
from populate_disk import build_macbinary

FLOPPY = 1474560


def volume_of(spec, work):
    """A bake's disk or CD entry ({"file", "take"} or a media name) as a Volume."""
    name, inner = (spec, None) if isinstance(spec, str) else (spec["file"], spec.get("take"))
    src = bake.media_path(name)
    if inner:
        stage = tempfile.mkdtemp(dir=work)
        items = take(src, inner, stage, work)
        src = os.path.join(tempfile.mkdtemp(dir=work), "item")
        macbinary_to_files(items[0][1], src)
    c = open_container(src, work)
    if not isinstance(c, Volume):
        raise SystemExit(f"{name}{'!' + inner if inner else ''}: not a disk image")
    return c, (inner or name).replace("/", ":").rsplit(":", 1)[-1]


def label(vol):
    vol.hfs.mount(vol.image, vol.partition)
    try:
        out = vol.hfs.run("hvol").decode("mac_roman", "replace")
    finally:
        vol.hfs.umount()
    for line in out.splitlines():
        if line.startswith("Volume name is"):
            return line.split('"', 1)[1].rsplit('"', 1)[0]
    raise RuntimeError(f"{vol.image}: no volume name")


def used(steps, volume):
    """Paths on `volume` the steps used, outermost first ("" = all of it)."""
    found = set()

    def walk(o, key=None):
        if isinstance(o, dict):
            for k, v in o.items():
                walk(v, k)
        elif isinstance(o, list):
            for v in o:
                walk(v, key)
        elif isinstance(o, str) and o.startswith(volume + ":") and key in ("launch", "open", "from"):
            path = o[len(volume) + 1:].rstrip(":")
            if key != "from":
                path = path.rpartition(":")[0]          # an installer: its folder
            found.add(path)
    walk(steps)
    if not found or "" in found:
        return [""]
    out = []
    for p in sorted(found):
        if not any(p.startswith(q + ":") for q in out):
            out.append(p)
    return out


def image_size(vol):
    return os.path.getsize(os.path.realpath(vol.image))


def disk_items(spec, b, work, volumes=None):
    """A disk's items; the name of a volume staged as a folder is added to
    `volumes` (aliases into it are rewritten to the folder)."""
    vol, file_name = volume_of(spec, work)
    stage = tempfile.mkdtemp(dir=work)
    name = label(vol)
    if image_size(vol) <= FLOPPY and vol.partition is None:
        local = os.path.join(stage, "disk.bin")
        with open(os.path.realpath(vol.image), "rb") as f:
            data = f.read()
        src = bake.media_path(spec if isinstance(spec, str) else spec["file"])
        stamp = int(os.path.getmtime(src)) + MAC_EPOCH
        with open(local, "wb") as f:
            f.write(build_macbinary(mr(file_name)[:31], b"????", b"????", data, b""))
        bake.to_dc42(local, name, stamp)
        return [(mr(file_name)[:31], local)]
    steps = [b.get("steps", []), b.get("first_boot", [])]
    items = [(mr(name), None)]
    if volumes is not None:
        volumes.add(name)
    for path in used(steps, name):
        got = vol.export(":" + path + ":*" if not path else ":" + path, stage)
        if path and ":" in path:                  # the folders above it
            parts = path.split(":")[:-1]
            for i in range(len(parts)):
                items.append((mr(name + ":" + ":".join(parts[:i + 1])), None))
            prefix = mr(name + ":" + ":".join(parts)) + b":"
        else:
            prefix = mr(name) + b":"
        items += [(prefix + r, l) for r, l in got]
    return items


def bake_items(b, work, volumes=None):
    """All a bake gave the guest, as (path in the product folder, MacBinary
    file or None for a folder); volumes: as disk_items."""
    items = []
    if b.get("archive"):
        name, local, _ = bake.archive_item(b, work)
        items.append((mr(name), local))
    for m in b.get("media", []):
        items += bake.media_items(m, work)
    for spec in b.get("disks", []) + b.get("first_boot_disks", []) + b.get("cdroms", []):
        items += disk_items(spec, b, work, volumes)
    return items


def has_media(b):
    return bool(bake.media_names(b))


def product_items(product, work, volumes=None):
    """A product's items, its bakes' merged: paths already there are kept.
    volumes: a set given the names of volumes staged as folders."""
    seen, out = set(), []
    for n in product["bakes"]:
        for rel, local in bake_items(bake.load_bake(n), work, volumes):
            if rel in seen:
                continue
            seen.add(rel)
            out.append((rel, local))
    return out


def staged_size(items):
    return sum(os.path.getsize(l) for _, l in items if l)


def forks(local):
    with open(local, "rb") as f:
        head = f.read(128)
    return struct.unpack(">II", head[83:91])


def volume_mb(items):
    """An HFS volume size (MB) that holds the items: its allocation block
    grows with it (at most 65535 blocks), so many small files need room."""
    sizes = [n for _, l in items if l for n in forks(l)]
    nodes = len(items)
    mb = 64
    while True:
        block = max(512, -(-(mb << 20) // 65535 // 512) * 512)
        need = sum(-(-n // block) * block for n in sizes) + nodes * 600     # + the catalog
        want = int(need * 1.08 / (1 << 20)) + 32
        if want <= mb:
            return mb
        mb = want


def unlisted(defs):
    """Bakes with media on no Installers definition."""
    listed = {n for d in defs for p in d.get("installers", []) for n in p["bakes"]}
    return [n for n in bake.all_bakes() if n not in listed and has_media(bake.load_bake(n))]


def cleanup(path):
    shutil.rmtree(path, ignore_errors=True)
