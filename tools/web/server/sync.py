"""Each user's copy of what the tab keeps in OPFS, for another browser.

users/ID/ in the data directory: sync.sqlite and objects/.

Overlays (overlay.js: a disk image's changed 4 KB blocks), by the
overlay's key (the image's name and content id, or for an upload its
name, size and time): every upload is a new version of the overlay and
its blocks carry it, so a browser that has version V asks for the blocks
newer than V. An upload names the version it is on (base): another one
since, from another browser, and it is refused (409, with the version)
and the browser chooses whose to keep. Large uploads are staged in
batches and committed as one version; a replace (the whole overlay, the
blocks not in it dropped) too. Deleting an overlay (Revert) is a
version, the reset: a browser older than it drops its own blocks first.

Volumes (worker.js: Executor's own volume, per System choice): a
manifest, { dirs, files: { path: { size, mtime, sha } } }, versioned and
based the same way; the files' contents are objects by SHA-256, shared
by every volume of the user, uploaded before the manifest that names
them (409 { missing } otherwise).
"""
import hashlib
import json
import os
import re
import sqlite3
import struct
import threading
import time

BLOCK = 4096
RECORD = 4 + BLOCK                      # block number (uint32 LE), the block
KEY = re.compile(r"[\w.-]{1,200}")
SHA = re.compile(r"[0-9a-f]{64}")
TOKEN = re.compile(r"[\w-]{8,64}")
# Objects no manifest names are deleted after this long (an upload in
# progress names them only when it ends); staged blocks never committed too.
ORPHAN_AGE = 3600
STAGE_AGE = 86400

SCHEMA = """
pragma journal_mode = wal;
create table if not exists overlays (
    key text primary key, name text, size integer,
    version integer not null default 0, reset integer not null default 0);
create table if not exists blocks (
    key text not null, block integer not null, version integer not null, data blob not null,
    primary key (key, block));
create table if not exists volumes (
    key text primary key, version integer not null default 0, manifest text);
create table if not exists staged (
    token text not null, key text not null, block integer not null, data blob not null,
    created integer not null, primary key (token, block));
"""


class SyncError(Exception):
    def __init__(self, status, message, **extra):
        super().__init__(message)
        self.status = status
        self.extra = extra


def check_key(key):
    if not KEY.fullmatch(key or ""):
        raise SyncError(400, "bad key")
    return key


def records(body):
    if len(body) % RECORD:
        raise SyncError(400, "records are 4 + 4096 bytes")
    return ((struct.unpack_from("<I", body, i)[0], body[i + 4:i + RECORD]) for i in range(0, len(body), RECORD))


class UserSync:
    def __init__(self, root):
        self.root = root
        os.makedirs(os.path.join(root, "objects"), exist_ok=True)
        self.path = os.path.join(root, "sync.sqlite")
        self.db = sqlite3.connect(self.path, check_same_thread=False)
        self.lock = threading.Lock()
        with self.lock:
            self.db.executescript(SCHEMA)

    # ---- overlays ----
    def _overlay(self, db, key):
        row = db.execute("select version, reset from overlays where key = ?", (key,)).fetchone()
        return row or (0, 0)

    def overlay_get(self, key, since):
        """(version, reset, count, rows): the blocks newer than since, in a
        read transaction of their own (a snapshot, while uploads go on)."""
        check_key(key)
        db = sqlite3.connect(self.path)
        db.execute("begin")
        version, reset = self._overlay(db, key)
        count = db.execute("select count(*) from blocks where key = ? and version > ?", (key, since)).fetchone()[0]

        def rows():
            try:
                for block, data in db.execute(
                        "select block, data from blocks where key = ? and version > ? order by block", (key, since)):
                    yield struct.pack("<I", block) + data
            finally:
                db.close()
        return version, reset > since, count, rows()

    def overlay_stage(self, key, token, body):
        """Blocks for a commit to come (overlay_put with the token)."""
        check_key(key)
        if not TOKEN.fullmatch(token or ""):
            raise SyncError(400, "bad stage token")
        now = int(time.time())
        with self.lock, self.db:
            self.db.executemany("insert or replace into staged values (?, ?, ?, ?, ?)",
                                ((token, key, b, data, now) for b, data in records(body)))

    def overlay_put(self, key, base, body, name=None, size=None, token=None, replace=False):
        """A new version from base: body's blocks and the staged ones
        (token); replace: they are all of it. 409 if base isn't current."""
        check_key(key)
        if token is not None and not TOKEN.fullmatch(token):
            raise SyncError(400, "bad stage token")
        recs = list(records(body))
        with self.lock, self.db:
            current = self._overlay(self.db, key)[0]
            if base != current:          # (its staged blocks age out)
                raise SyncError(409, "changed since", version=current)
            version = current + 1
            self.db.execute("insert into overlays (key, name, size, version) values (?, ?, ?, ?)"
                            " on conflict (key) do update set version = excluded.version,"
                            " name = coalesce(excluded.name, name), size = coalesce(excluded.size, size)",
                            (key, name, size, version))
            if replace:
                self.db.execute("delete from blocks where key = ?", (key,))
                self.db.execute("update overlays set reset = ? where key = ?", (version, key))
            if token:
                self.db.execute("insert or replace into blocks select key, block, ?, data from staged"
                                " where token = ? and key = ?", (version, token, key))
                self.db.execute("delete from staged where token = ?", (token,))
            self.db.executemany("insert or replace into blocks values (?, ?, ?, ?)",
                                ((key, b, version, data) for b, data in recs))
            self.db.execute("delete from staged where created < ?", (int(time.time()) - STAGE_AGE,))
        return version

    def overlay_delete(self, key):
        check_key(key)
        with self.lock, self.db:
            version = self._overlay(self.db, key)[0] + 1
            self.db.execute("insert into overlays (key, version, reset) values (?, ?, ?)"
                            " on conflict (key) do update set version = excluded.version, reset = excluded.reset",
                            (key, version, version))
            self.db.execute("delete from blocks where key = ?", (key,))
        return version

    # ---- volumes ----
    def volume_get(self, key):
        check_key(key)
        with self.lock:
            row = self.db.execute("select version, manifest from volumes where key = ?", (key,)).fetchone()
        if not row:
            return 0, None
        return row[0], json.loads(row[1]) if row[1] else None

    def volume_put(self, key, manifest, base):
        """A new version from base (409 { version } if it isn't current;
        409 { missing } for objects not uploaded)."""
        check_key(key)
        if (not isinstance(manifest, dict) or not isinstance(manifest.get("dirs"), list)
                or not isinstance(manifest.get("files"), dict)):
            raise SyncError(400, "a manifest is { dirs, files }")
        shas = set()
        for f in manifest["files"].values():
            if not isinstance(f, dict) or not SHA.fullmatch(str(f.get("sha", ""))):
                raise SyncError(400, "every file needs its sha")
            shas.add(f["sha"])
        # Under the lock: the collector can't delete what this names.
        with self.lock, self.db:
            row = self.db.execute("select version from volumes where key = ?", (key,)).fetchone()
            current = row[0] if row else 0
            if base != current:
                raise SyncError(409, "changed since", version=current)
            missing = self.objects_missing(shas)
            if missing:
                raise SyncError(409, "objects missing", missing=missing, version=current)
            version = current + 1
            self.db.execute("insert or replace into volumes values (?, ?, ?)", (key, version, json.dumps(manifest)))
        self._collect()
        return version

    def volume_delete(self, key):
        check_key(key)
        with self.lock, self.db:
            row = self.db.execute("select version from volumes where key = ?", (key,)).fetchone()
            version = (row[0] if row else 0) + 1
            self.db.execute("insert or replace into volumes values (?, ?, null)", (key, version))
        self._collect()
        return version

    # ---- objects ----
    def object_path(self, sha):
        if not SHA.fullmatch(sha or ""):
            raise SyncError(400, "bad sha")
        return os.path.join(self.root, "objects", sha[:2], sha)

    def objects_missing(self, shas):
        return sorted(s for s in shas if not os.path.isfile(self.object_path(s)))

    def object_put(self, sha, data):
        path = self.object_path(sha)
        if hashlib.sha256(data).hexdigest() != sha:
            raise SyncError(400, "the data is not that sha")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = f"{path}.{threading.get_ident()}.tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        with self.lock:                  # new (or again): young, not collected
            os.replace(tmp, path)

    def _collect(self):
        old = time.time() - ORPHAN_AGE
        top = os.path.join(self.root, "objects")
        with self.lock:
            named = set()
            for (m,) in self.db.execute("select manifest from volumes where manifest is not null"):
                named.update(f["sha"] for f in json.loads(m)["files"].values())
            for sub in os.listdir(top):
                for name in os.listdir(os.path.join(top, sub)):
                    p = os.path.join(top, sub, name)
                    if SHA.fullmatch(name) and name not in named and os.stat(p).st_mtime < old:
                        os.unlink(p)

    def summary(self):
        with self.lock:
            overlays = [{"key": k, "name": n, "size": s, "version": v, "blocks": c}
                        for k, n, s, v, c in self.db.execute(
                            "select key, name, size, version, (select count(*) from blocks b where b.key = o.key)"
                            " from overlays o order by key")]
            volumes = [{"key": k, "version": v, "files": len(json.loads(m)["files"]) if m else 0}
                       for k, v, m in self.db.execute("select key, version, manifest from volumes order by key")]
        return {"overlays": overlays, "volumes": volumes}


class SyncStore:
    def __init__(self, data_dir):
        self.root = os.path.join(data_dir, "users")
        self.users = {}
        self.lock = threading.Lock()

    def user(self, user_id):
        with self.lock:
            if user_id not in self.users:
                self.users[user_id] = UserSync(os.path.join(self.root, str(int(user_id))))
            return self.users[user_id]
