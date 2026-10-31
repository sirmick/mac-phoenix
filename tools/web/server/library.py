"""The library: the disk images the site offers, from their records.

provisioning/build.py writes NAME.img.json beside each image it makes,
with a title, tags and a note; every image with a record that the
include/exclude patterns let through is in /library.json, the tab's list
(web/executor/main.js), and is served at disks/NAME.
"""
import fnmatch
import json
import os
from urllib.parse import quote


def catalog(images, include=("*.img",), exclude=()):
    out = []
    if not images or not os.path.isdir(images):
        return out
    for rec in sorted(os.listdir(images)):
        if not rec.endswith(".json"):
            continue
        name = rec[:-len(".json")]
        path = os.path.join(images, name)
        if (not os.path.isfile(path) or not any(fnmatch.fnmatch(name, p) for p in include)
                or any(fnmatch.fnmatch(name, p) for p in exclude)):
            continue
        try:
            with open(os.path.join(images, rec)) as f:
                r = json.load(f)
        except (OSError, ValueError):
            continue
        out.append({"name": name, "title": r.get("title") or name, "note": r.get("note", ""),
                    "tags": r.get("tags", []), "size": os.path.getsize(path),
                    "url": "disks/" + quote(name)})
    return out


def find(images, include, exclude, name):
    """The path of a disk in the catalog, or None."""
    for d in catalog(images, include, exclude):
        if d["name"] == name:
            return os.path.join(images, name)
    return None
