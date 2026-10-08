#!/usr/bin/env python3
"""Ghidra for macdecode: a 68040 + A-line language, snapshot projects with
macdecode's labels, and one-shot disassembly/decompilation from the shell.

  ghidra.py install                 build + install the MacOS68k language
  ghidra.py import SNAPSHOT         Ghidra project for a snapshot (RAM at 0,
                                    ROM at its base), labelled from macdecode
  ghidra.py fn SNAPSHOT ADDR [-n N] disassemble + decompile the function at ADDR
  ghidra.py res IMAGE FILE TYPE ID [ADDR]
                                    one resource (decompressed) as its own
                                    program at 0; decompile from ADDR (0)

Projects live in ~/storage/ghidra/<snapshot>/ (open them in the Ghidra GUI).
Uses the snap's `ghidra --headless` (PyGhidra).
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GHIDRA = Path("/snap/ghidra/current/ghidra")
PROJECTS = Path.home() / "storage/ghidra"
LANG = "68000:BE:32:MacOS"


def ghidra_version():
    props = (GHIDRA / "Ghidra/application.properties").read_text()
    return re.search(r"application.version=(\S+)", props).group(1)


def extension_dir():
    return (Path.home() / f"snap/ghidra/current/.config/ghidra/ghidra_{ghidra_version()}_PUBLIC"
            / "Extensions/MacOS68k")


def install():
    src, dst = HERE / "ghidra/MacOS68k", extension_dir()
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    props = dst / "extension.properties"
    props.write_text(props.read_text().replace("@extversion@", ghidra_version()))
    langs = dst / "data/languages"
    stock = GHIDRA / "Ghidra/Processors/68000/data/languages"
    for f in ("68000.sinc", "68000.pspec", "68000.cspec"):
        shutil.copy(stock / f, langs / f)
    r = subprocess.run([str(GHIDRA / "support/sleigh"), str(langs / "macos68k.slaspec")],
                       capture_output=True, text=True)
    if r.returncode or not (langs / "macos68k.sla").exists():
        sys.exit(f"sleigh failed:\n{r.stdout}\n{r.stderr}")
    print(f"installed {LANG} -> {dst}")


def headless(project, *args):
    """Run `ghidra --headless` and return the lines our scripts print."""
    PROJECTS.mkdir(parents=True, exist_ok=True)
    cmd = ["ghidra", "--headless", str(PROJECTS), project, *args,
           "-scriptPath", str(HERE / "ghidra/scripts")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = [l[4:] for l in (r.stdout + r.stderr).splitlines() if l.startswith("MD| ")]
    if r.returncode or any("SCRIPT ERROR" in l for l in r.stdout.splitlines()):
        errs = [l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l or "Exception" in l]
        print("\n".join(errs[-12:]), file=sys.stderr)
    return out


def names_only():
    """Trap and dispatcher names without a snapshot (for resources)."""
    sys.path.insert(0, str(HERE))
    import macdecode as md
    from atraps import Names
    names = Names(md.load_db())
    traps = {}
    for word in list(range(0xA000, 0xA100)) + list(range(0xA800, 0xAC00)):
        traps[f"{'tool' if word & 0x800 else 'os'}:{word & (0x3FF if word & 0x800 else 0xFF):x}"] = \
            names.trap(word)
    dispatchers = {}
    for (table, index), d in names.ds.slots.items():
        if d["kind"]:
            dispatchers[f"{table}:{index:x}"] = {
                "kind": d["kind"], "mask": d["mask"],
                "selectors": {f"{v:x}": x["name"] for v, x in d["selectors"].items()}}
    return {"lowmem": [{"addr": g["address"], "name": g["name"], "size": names.db.size(g["type"]) or 0}
                       for g in names.db.lowmem if g["address"] < md.LOWMEM_END],
            "traps": traps, "dispatchers": dispatchers, "trap_entries": [], "origins": []}


def do_res(image, fname, rtype, rid, addr, lines):
    sys.path.insert(0, str(HERE))
    from rsrc import Disk
    out_dir = PROJECTS / "res"
    out_dir.mkdir(parents=True, exist_ok=True)
    d = Disk(image, out_dir / ".cache")
    try:
        mac_path = fname.encode("mac_roman") if ":" in fname else b"System Folder:" + fname.encode("mac_roman")
        res = d.resources(mac_path)
    finally:
        d.close()
    if (rtype, rid) not in res:
        sys.exit(f"{fname} has no '{rtype}' {rid}")
    data = res[(rtype, rid)][1]
    stem = re.sub(r"[^A-Za-z0-9_-]", "_", f"{Path(fname).name}_{rtype}_{rid}")
    binf = out_dir / f"{stem}.bin"
    binf.write_bytes(data)
    labels = out_dir / f"{stem}.json"
    labels.write_text(json.dumps(names_only()))
    project = f"res_{stem}"
    if not (PROJECTS / f"{project}.gpr").exists():
        headless(project, "-import", str(binf), "-processor", LANG, "-loader", "BinaryLoader",
                 "-loader-baseAddr", "0", "-noanalysis")
    print(f"{fname} '{rtype}' {rid}: {len(data)} bytes (decompressed); project {PROJECTS / project}.gpr")
    out = headless(project, "-process", binf.name, "-noanalysis",
                   "-postScript", "mac_func.py", f"{addr:x}", str(labels), str(lines))
    print("\n".join(out))


def export_labels(snapshot):
    """What macdecode knows about a snapshot, for the Ghidra scripts."""
    sys.path.insert(0, str(HERE))
    import macdecode as md
    from atraps import Names
    snap = md.Snapshot(snapshot)
    db = md.load_db()
    zones = md.find_zones(snap)
    origins = md.load_origins(snap, zones)
    world = md.World(snap, db, origins, zones)
    names = Names(db)
    lowmem = [{"addr": g["address"], "name": g["name"], "size": db.size(g["type"]) or 0}
              for g in db.lowmem if g["address"] < md.LOWMEM_END]
    traps = {}
    for word in list(range(0xA000, 0xA100)) + list(range(0xA800, 0xAC00)):
        traps[f"{'tool' if word & 0x800 else 'os'}:{word & (0x3FF if word & 0x800 else 0xFF):x}"] = \
            names.trap(word)
    dispatchers = {}
    for (table, index), d in names.ds.slots.items():
        if d["kind"]:
            dispatchers[f"{table}:{index:x}"] = {
                "kind": d["kind"], "mask": d["mask"],
                "selectors": {f"{v:x}": s["name"] for v, s in d["selectors"].items()}}
    entries, seen = [], set()
    for r in md.trap_entries(world):
        if r["unimplemented"] or r["addr"] in seen:
            continue
        seen.add(r["addr"])
        word = r["trap"]
        entries.append({"addr": r["addr"], "name": names.trap(word).lstrip("_")})
    runs = []
    if origins:
        for start, end, fname, rtype, rid, roff in origins.runs:
            runs.append({"start": start, "end": end, "label": f"{fname} '{rtype}' {rid} +{roff:x}"})
    out = Path(snapshot) / "ghidra-labels.json"
    out.write_text(json.dumps({"lowmem": lowmem, "traps": traps, "dispatchers": dispatchers,
                               "trap_entries": entries, "origins": runs}))
    return out, snap


def project_name(snapshot):
    return Path(snapshot).resolve().name


def do_import(snapshot):
    labels, snap = export_labels(snapshot)
    name = project_name(snapshot)
    if (PROJECTS / f"{name}.gpr").exists():
        sys.exit(f"{PROJECTS / name}.gpr exists; delete it to re-import")
    out = headless(name, "-import", str(Path(snapshot).resolve() / "ram.bin"), "-processor", LANG,
                   "-loader", "BinaryLoader", "-loader-baseAddr", "0", "-noanalysis",
                   "-postScript", "mac_setup.py", str(labels),
                   str(Path(snapshot).resolve() / "rom.bin"), f"{snap.rom_base:x}")
    print("\n".join(out) or "(no output: see errors above)")
    print(f"project: {PROJECTS / name}.gpr (program ram.bin)")


def do_fn(snapshot, addr, lines):
    labels = Path(snapshot) / "ghidra-labels.json"
    if not labels.exists():
        export_labels(snapshot)
    out = headless(project_name(snapshot), "-process", "ram.bin", "-noanalysis",
                   "-postScript", "mac_func.py", f"{addr:x}", str(labels.resolve()), str(lines))
    print("\n".join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("install")
    p = sub.add_parser("import")
    p.add_argument("snapshot")
    p = sub.add_parser("res")
    p.add_argument("image")
    p.add_argument("file", help="file in the System Folder, or a full Mac path with ':'")
    p.add_argument("type")
    p.add_argument("id", type=int)
    p.add_argument("addr", nargs="?", default=0, type=lambda x: int(x.replace("$", ""), 16))
    p.add_argument("-n", type=int, default=400)
    p = sub.add_parser("fn")
    p.add_argument("snapshot")
    p.add_argument("addr", type=lambda x: int(x.replace("$", ""), 16))
    p.add_argument("-n", type=int, default=400, help="max listing lines")
    a = ap.parse_args()
    if a.cmd == "install":
        install()
    elif a.cmd == "import":
        do_import(a.snapshot)
    elif a.cmd == "res":
        do_res(a.image, a.file, a.type, a.id, a.addr, a.n)
    elif a.cmd == "fn":
        do_fn(a.snapshot, a.addr, a.n)


if __name__ == "__main__":
    main()
