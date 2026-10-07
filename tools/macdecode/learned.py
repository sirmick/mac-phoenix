"""The learned overlay (learned.yaml) applied on top of multiversal.

Every lowmem global and trap name ends up tagged with where it came
from — "multiversal", "learned" or "learned, replaces <name>" — so the
decoder can show both side by side.
"""
from pathlib import Path

import yaml

DEFAULT_LEARNED = Path(__file__).resolve().parent / "learned.yaml"


def mtime(path):
    p = Path(path)
    return p.stat().st_mtime if p.exists() else 0


def apply(db, path=DEFAULT_LEARNED):
    """Merge learned.yaml into db in place."""
    path = Path(path)
    data = yaml.safe_load(path.read_text()) if path.exists() else {}
    data = data or {}
    by_addr = {g["address"]: g for g in db.lowmem}
    for g in db.lowmem:
        g.setdefault("source", "multiversal")
    for e in data.get("lowmem") or []:
        e = dict(e, file=path.name, source="learned")
        old = by_addr.get(e["address"])
        if old is not None:
            db.lowmem.remove(old)
            e["multiversal"] = {"name": old["name"], "type": old["type"], "file": old["file"]}
        db.lowmem.append(e)
        by_addr[e["address"]] = e
    db.lowmem.sort(key=lambda g: g["address"])

    from mvdb import trap_slot
    for t in data.get("traps") or []:
        slot = trap_slot(t["trap"])
        db.traps.setdefault(slot, []).insert(0, t["name"])
        db.trap_info.setdefault(slot, []).insert(0, {
            "name": t["name"], "file": path.name, "kind": "learned", "trap": t["trap"],
            "status": t.get("status"), "comment": t.get("comment")})
    db.learned_path, db.learned_mtime = path, mtime(path)
    return db
