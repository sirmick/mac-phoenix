#!/usr/bin/env python3
"""grid_compare.py OLD NEW [--threshold N] [--out FILE.md]: two runs of
tools/executor/grid.py (directories with grid.json, NNN.png, NNN.log),
item by item (matched by path): result, front process, crash, the calls
answered as unimplemented, and whether the screenshot changed (pixels that
differ, outside the BridgeAgent window and the desktop icon column).
Markdown; with Pillow, a side-by-side PNG of each changed screenshot in
NEW/compare/."""
import argparse
import json
from pathlib import Path


def load(d):
    return {x["path"]: x for x in json.loads((Path(d) / "grid.json").read_text())}


def changed_pixels(a, b):
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return None
    if not (a.exists() and b.exists()):
        return None
    try:
        ia, ib = Image.open(a).convert("L"), Image.open(b).convert("L")
    except Exception:
        return None      # an empty screenshot (the session had died)
    if ia.size != ib.size:
        return -1
    w, h = ia.size
    box = (210, 30, w - 90, h - 10)   # not BridgeAgent's window, not the disk icons
    d = ImageChops.difference(ia.crop(box), ib.crop(box))
    return sum(d.point(lambda p: 255 if p > 40 else 0).histogram()[255:])


def side_by_side(a, b, dest):
    from PIL import Image
    ia, ib = Image.open(a), Image.open(b)
    im = Image.new("RGB", (ia.width + ib.width + 8, max(ia.height, ib.height)), (255, 0, 255))
    im.paste(ia, (0, 0))
    im.paste(ib, (ia.width + 8, 0))
    im.save(dest)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--threshold", type=int, default=500, help="changed pixels that count as a change")
    ap.add_argument("--out")
    o = ap.parse_args()
    old, new = load(o.old), load(o.new)
    cmp_dir = Path(o.new) / "compare"
    rows = []
    for path, y in sorted(new.items(), key=lambda kv: kv[1]["n"]):
        x = old.get(path)
        name = path.split(":")[-1]
        if not x:
            rows.append((y["n"], name, "new item", "", ""))
            continue
        notes = []
        if x.get("result") != y.get("result"):
            notes.append(f"{x.get('result')} -> {y.get('result')}")
        if x.get("front") != y.get("front"):
            notes.append(f"front {x.get('front')!r} -> {y.get('front')!r}")
        ux, uy = set(x.get("unimplemented") or []), set(y.get("unimplemented") or [])
        if ux != uy:
            notes.append(f"unimplemented -{len(ux - uy)} +{len(uy - ux)}")
        a, b = Path(o.old) / f"{x['n']:03d}.png", Path(o.new) / f"{y['n']:03d}.png"
        px = changed_pixels(a, b)
        pic = ""
        ok = lambda p: p.exists() and p.stat().st_size > 0
        if px is None and ok(a) != ok(b):
            notes.append("screenshot " + ("appeared" if ok(b) else "gone"))
        elif px is not None and (px < 0 or px > o.threshold):
            notes.append(f"screenshot changed ({px} px)" if px >= 0 else "screenshot size changed")
            cmp_dir.mkdir(exist_ok=True)
            side_by_side(a, b, cmp_dir / f"{y['n']:03d}.png")
            pic = f"[png](compare/{y['n']:03d}.png)"
        if notes:
            rows.append((y["n"], name, "; ".join(notes), pic, y.get("ended") or ""))
    for path, x in old.items():
        if path not in new:
            rows.append((x["n"], path.split(":")[-1], "gone from the new run", "", ""))
    lines = [f"# {Path(o.old).name} -> {Path(o.new).name}", "",
             f"{len(new)} items, {len(rows)} changed.", "",
             "| # | item | change | screenshots | ended |", "|---:|---|---|---|---|"]
    lines += [f"| {n} | {name} | {c} | {pic} | {e} |" for n, name, c, pic, e in rows]
    text = "\n".join(lines) + "\n"
    if o.out:
        Path(o.out).write_text(text)
        print(o.out)
    else:
        print(text)


if __name__ == "__main__":
    main()
