#!/usr/bin/env python3
"""CD images as they came: checked, and each given its record.

    python3 provisioning/cds.py [--images DIR] [--fetch] [NAME...]

For each CD in cds.json (magazine cover discs and the like, pinned with its
cover scan in media.json, dir "images"): the image and the cover are there
with their pinned size and digests (--fetch downloads missing ones, one at
a time), the image is an HFS volume (bare, or behind an Apple partition
map) with the volume name cds.json expects, and NAME.json beside it is the
record: the definition's title, tags, note, magazine, issue, date,
publisher, contents and cover, with the volume, its partition, the size
and the source. tools/web/make_library.py offers them on the site.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools", "macfetch"))
import macfetch  # noqa: E402

MEDIA = os.path.join(HERE, "media.json")
CDS = os.path.join(HERE, "cds.json")


def hfs_volume(path):
    """(volume name, partition offset in bytes, partition map entry or None)."""
    with open(path, "rb") as f:
        def read(off, n):
            f.seek(off)
            return f.read(n)
        if read(1024, 2) == b"BD":
            return read(1024 + 36, 28), 0, None
        if read(0, 2) != b"ER":
            raise ValueError("neither an HFS volume nor an Apple partition map")
        count = None
        i = 1
        while count is None or i <= count:
            pm = read(i * 512, 512)
            if pm[:2] != b"PM":
                break
            count, start, blocks = struct.unpack(">III", pm[4:16])
            kind = pm[48:80].split(b"\0")[0]
            if kind == b"Apple_HFS" and read(start * 512 + 1024, 2) == b"BD":
                part = {"index": i, "name": pm[16:48].split(b"\0")[0].decode("mac_roman"),
                        "start": start * 512, "size": blocks * 512}
                return read(start * 512 + 1024 + 36, 28), start * 512, part
            i += 1
        raise ValueError("no Apple_HFS partition with an HFS volume")


def pascal(b):
    return b[1:1 + b[0]].decode("mac_roman")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("names", nargs="*")
    ap.add_argument("--images", default=os.path.join(macfetch.STORAGE, "images"))
    ap.add_argument("--fetch", action="store_true", help="download missing images and covers")
    args = ap.parse_args()
    media = {p.get("as") or p["name"]: p for p in macfetch.load_manifest(MEDIA)["files"]}
    with open(CDS) as f:
        cds = json.load(f)["cds"]
    bad = 0
    for cd in cds:
        if args.names and cd["name"] not in args.names:
            continue
        for name in (cd["name"], cd["cover"]):
            pin = media[name]
            path = os.path.join(args.images, name)
            if macfetch.file_ok(path, pin):
                continue
            if not args.fetch:
                sys.exit(f"{path}: missing or not as pinned (--fetch downloads it)")
            print(f"{name}: downloading")
            macfetch.download(pin, os.path.dirname(args.images), log=print)
        path = os.path.join(args.images, cd["name"])
        try:
            raw, offset, part = hfs_volume(path)
        except ValueError as e:
            print(f"{path}: {e}")
            bad += 1
            continue
        volume = pascal(raw)
        if volume != cd["volume"]:
            print(f"{path}: volume {volume!r}, expected {cd['volume']!r}")
            bad += 1
            continue
        pin = media[cd["name"]]
        ident, file = pin["ia"].split("/", 1)
        rec = {k: cd[k] for k in ("title", "tags", "note", "magazine", "issue", "date", "publisher",
                                  "contents", "cover")}
        rec.update({"volume": volume, "partition": part, "size": pin["size"], "md5": pin["md5"],
                    "sha256": pin["sha256"],
                    "source": {"archive.org": ident, "file": file, "url": pin["page"],
                               "cover": media[cd["cover"]]["ia"]},
                    "made_by": "provisioning/cds.py"})
        out = path + ".json"
        new = json.dumps(rec, indent=1, ensure_ascii=False) + "\n"
        if not os.path.exists(out) or open(out).read() != new:
            with open(out, "w") as f:
                f.write(new)
        where = f"partition {part['index']} ({part['name']}) at {offset}" if part else "bare HFS"
        print(f"{cd['name']}: {volume!r}, {where}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
