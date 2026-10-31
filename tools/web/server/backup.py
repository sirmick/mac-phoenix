"""A copy of the data directory, made while the server runs.

    tools/web/server --config FILE backup DEST

The databases (accounts.sqlite, each user's users/ID/sync.sqlite) through
SQLite's backup API, so each copy is consistent though the server writes
on (they are in WAL mode: copying the files could catch half a
transaction); then the objects the manifests name (users/ID/objects,
named by their SHA-256 and never changed), only those DEST lacks, so a
backup into the same DEST again copies what is new. The databases are
copied first: an object a copied manifest names is there by then (it is
uploaded before the manifest), and is collected only an hour after no
manifest names it. image-ids.json is a cache and left out.

To restore: stop the server, put DEST's contents in the data directory.
"""
import os
import shutil
import sqlite3


def copy_db(src, dest):
    tmp = dest + ".tmp"
    if os.path.exists(tmp):
        os.remove(tmp)
    s = sqlite3.connect(f"file:{src}?mode=ro", uri=True)
    d = sqlite3.connect(tmp)
    try:
        s.backup(d)
    finally:
        d.close()
        s.close()
    os.replace(tmp, dest)


def backup(data, dest):
    """Copies data into dest; returns (databases, objects copied)."""
    os.makedirs(dest, exist_ok=True)
    dbs = objects = 0
    if os.path.isfile(os.path.join(data, "accounts.sqlite")):
        copy_db(os.path.join(data, "accounts.sqlite"), os.path.join(dest, "accounts.sqlite"))
        dbs += 1
    users = os.path.join(data, "users")
    ids = sorted(os.listdir(users)) if os.path.isdir(users) else []
    for uid in ids:
        src = os.path.join(users, uid)
        if not os.path.isfile(os.path.join(src, "sync.sqlite")):
            continue
        out = os.path.join(dest, "users", uid)
        os.makedirs(out, exist_ok=True)
        copy_db(os.path.join(src, "sync.sqlite"), os.path.join(out, "sync.sqlite"))
        dbs += 1
    for uid in ids:
        src = os.path.join(users, uid, "objects")
        for root, _, files in os.walk(src):
            for name in files:
                if name.endswith(".tmp"):        # an upload in progress
                    continue
                to = os.path.join(dest, "users", uid, "objects", os.path.relpath(root, src), name)
                if os.path.isfile(to):
                    continue
                os.makedirs(os.path.dirname(to), exist_ok=True)
                try:
                    shutil.copy2(os.path.join(root, name), to + ".tmp")
                except FileNotFoundError:        # collected meanwhile
                    continue
                os.replace(to + ".tmp", to)
                objects += 1
    return dbs, objects
