"""The library: the disk images the site offers, from their records.

provisioning/build.py writes NAME.img.json beside each image it makes,
with a title, tags and a note; every image with a record that the
include/exclude patterns let through is in /library.json, the tab's list
(web/executor/main.js), and is served at disks/NAME.

Each disk's id is its content's (SHA-256, the first 24 digits): its ETag
and X-Image-Id, and the tab keys a disk's changes (its overlay) on it, so
they survive the image being touched or copied, and only a different
image starts afresh. Ids are kept in the data directory's image-ids.json
by path, size, time and inode, worked out on first use (Ids.warm: all of
the library's at start, in the background).
"""
import fnmatch
import hashlib
import json
import os
import threading
from urllib.parse import quote


class Ids:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.hashing = {}                # path -> Lock: one hash a file
        try:
            with open(path) as f:
                self.known = json.load(f)
        except (OSError, ValueError):
            self.known = {}

    def get(self, path):
        st = os.stat(path)
        stamp = [st.st_size, st.st_mtime_ns, st.st_ino]
        with self.lock:
            k = self.known.get(path)
            if k and k["stamp"] == stamp:
                return k["id"]
            busy = self.hashing.setdefault(path, threading.Lock())
        with busy:
            with self.lock:
                k = self.known.get(path)
                if k and k["stamp"] == stamp:
                    return k["id"]
            h = hashlib.sha256()
            with open(path, "rb") as f:
                while chunk := f.read(4 << 20):
                    h.update(chunk)
            ident = h.hexdigest()[:24]
            with self.lock:
                self.known[path] = {"stamp": stamp, "id": ident}
                tmp = self.path + ".tmp"
                with open(tmp, "w") as f:
                    json.dump(self.known, f)
                os.replace(tmp, self.path)
            return ident

    def warm(self, paths):
        def run():
            for p in paths:
                try:
                    self.get(p)
                except OSError:
                    pass
        threading.Thread(target=run, daemon=True).start()


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
