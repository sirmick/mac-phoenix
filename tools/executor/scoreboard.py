#!/usr/bin/env python3
"""scoreboard.py run|table: the target applications on Executor's 7.6.1 and
8.6 against the ROM machines (docs/executor/SCOREBOARD.md).

  run [--only COLUMN/SET ...]   grid.py for every column x application disk,
                                into OUT/<column>/<set>/ (skips finished ones
                                unless --force)
  table                         OUT's grids + scoreboard_notes.json ->
                                docs/executor/SCOREBOARD.md

Columns: e761 (Executor, Apple's 7.6.1 System, 68k code only), e761ppc (the
same with --executor-ppc, fat applications), q761 (Quadra 650 ROM, 7.6.1),
e86 (Executor, 8.6, --executor-ppc), g86 (G3 ROM, 8.6). Sets: the
application disks, with the target applications (all programs on the games
disks). Each grid run uses --jobs 2.

  MACPHOENIX_EMULATOR=$PWD/build/mac-phoenix.score tools/executor/scoreboard.py run
  tools/executor/scoreboard.py table
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.expanduser("~/storage/executor-grid/m1")
NOTES = os.path.join(HERE, "scoreboard_notes.json")
DOC = os.path.join(REPO, "docs", "executor", "SCOREBOARD.md")

COLUMNS = {
    "e761": ("7.6.1", ["--no-ppc"]),
    "e761ppc": ("7.6.1", ["--code", "fat"]),
    "q761": ("7.6.1", ["--rom", "quadra", "--code", "68k,fat"]),
    "e86": ("8.6", []),
    "g86": ("8.6", ["--rom", "g3"]),
}
# not programs: DOCMaker documents, registration, installers, patchers, help
NOT_PROGRAMS = ["Dk@P", "Areg", "ercR", "kajr", "ZAPS", "EDOC", "eSRD", "MSH1", "MSH2",
                "ttxt", "aucd", "arPD"]
SETS = {
    "prod": ("productivity-system-7.img", [
        "Productivity:AppleWorks 5.0.3 ƒ:AppleWorks",
        "Productivity:ClarisWorks 4.0v4 ƒ:ClarisWorks"], "fix-prod"),
    "dev": ("dev-tools-system-7.img", [
        "Development:MPW:MPW Shell",
        "Development:CodeWarrior Pro 4 ƒ:Metrowerks CodeWarrior:CodeWarrior IDE 3.2",
        "Development:THINK C 6.0 ƒ:THINK C:THINK Project Manager",
        "Development:Languages ƒ:THINK Pascal 4.5d4 ƒ:Think Pascal:THINK Pascal 4.5d4:THINK Pascal 4.5",
        "Development:Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard",
        "Development:Languages ƒ:MacPerl 5.2.0r4 ƒ:MacPerl",
        "Development:Tools ƒ:ResEdit 2.1.3 ƒ:ResEdit 2.1.3:ResEdit",
        "Development:Tools ƒ:BBEdit Lite 4.1 ƒ:BBEdit Lite 4.1"], "fix-dev"),
    "dev89": ("dev-tools-macos-8-9.img", [
        "Development:MPW:MPW Shell",
        "Development:Languages ƒ:MacPerl 5.6.1r2 ƒ:MacPerl",
        "Development:Tools ƒ:BBEdit Lite 6.1.2 ƒ:BBEdit Lite 6.1"], "fix-dev"),
    "games": ("games.img", None, "fix-games"),
    "classic": ("games-classic.img", None, "fix-games"),
    "gppc": ("games-ppc.img", None, "fix-games"),
}
# Afterlife's optional installs are copies of the game
EXCLUDE = ["Optional Afterlife Installs"]
SYSTEM_SETS = {"7.6.1": ["prod", "dev", "games", "classic", "gppc"],
               "8.6": ["prod", "dev", "dev89", "games", "classic", "gppc"]}


def runs():
    for col, (system, _) in COLUMNS.items():
        for st in SYSTEM_SETS[system]:
            yield col, st


def cmd_run(opts):
    for col, st in runs():
        if opts.only and f"{col}/{st}" not in opts.only and col not in opts.only \
                and st not in opts.only:
            continue
        out = os.path.join(OUT, col, st)
        if os.path.exists(os.path.join(out, "grid.md")) and not opts.force:
            continue
        system, args = COLUMNS[col]
        disk, paths, _ = SETS[st]
        cmd = [sys.executable, os.path.join(HERE, "grid.py"), system + ".img", "--disk", disk,
               "--jobs", "2", "--wait", str(opts.wait), "--out", out, *args]
        for p in paths or []:
            cmd += ["--path", p]
        if paths is None:
            for c in NOT_PROGRAMS:
                cmd += ["--skip-creator", c]
            for x in EXCLUDE:
                cmd += ["--exclude", x]
        print(f"== {col}/{st}", flush=True)
        subprocess.run(cmd)


def load(col, st):
    try:
        with open(os.path.join(OUT, col, st, "grid.json")) as f:
            return {r["path"]: r for r in json.load(f)}
    except OSError:
        return None


def session_log(col, st, r):
    """(the session's log lines after the launch, the line that ended it)."""
    try:
        with open(os.path.join(OUT, col, st, "%03d.log" % r["n"]), encoding="latin-1") as f:
            lines = f.read().splitlines()
    except OSError:
        return [], None
    for i, line in enumerate(lines):
        if "POST /api/launch" in line:
            lines = lines[i + 1:]
            break
    end = next((l for l in lines if re.search(
        r"session stopped|host signal|guest exited|SysError\(", l)), None)
    if end:
        end = re.sub(r"^\[\w+\] |: snapshotting$", "", end)
    return lines, end


def unresolved(lines):
    """The CFM imports (not weak) Executor could not resolve after the launch."""
    names = []
    for line in lines:
        m = re.match(r"CFM: unresolved import (\S+)", line)
        if m and m.group(1) not in names:
            names.append(m.group(1))
    return names


def verdict(r, col, st):
    """(runs|partial|fails|n/a, blocker) of one grid result."""
    if r is None:
        return "—", ""
    res = r["result"]
    lines, end = session_log(col, st, r) if col.startswith("e") else ([], None)
    missing = unresolved(lines)
    if missing:
        imports = "unresolved imports %s%s" % (", ".join(missing[:3]),
                                               " (+%d)" % (len(missing) - 3) if len(missing) > 3 else "")
    if res == "runs":
        calls = r.get("unimplemented") or []
        if calls:
            return "partial", "unimplemented " + calls[0]
        # imports never called don't stop anything: a note, not a fault
        return "runs", "latent: " + imports if missing else ""
    if res == "not in front" and missing:
        return "fails", "not in front; " + imports
    if res == "crashed":
        return "fails", end or r.get("syserror") or r.get("ended") or "crashed"
    if res == "not in front":
        return "fails", "not in front (front: %s)" % (r.get("front") or "?")
    if res == "launch failed":
        return "fails", "launch: %s" % r.get("launch")
    return "fails", res


def cmd_table(opts):
    notes = {}
    if os.path.exists(NOTES):
        with open(NOTES) as f:
            notes = json.load(f)
    lines = []
    total = {}
    for system in ("7.6.1", "8.6"):
        cols = [c for c, (s, _) in COLUMNS.items() if s == system]
        ecols = [c for c in cols if c.startswith("e")]
        rcol = [c for c in cols if not c.startswith("e")][0]
        lines += [f"## {system}", "",
                  "| App | Code | " + " | ".join(cols) + " | First blocker | Screenshot | Owner |",
                  "|---|---|" + "---|" * len(cols) + "---|---|---|"]
        for st in SYSTEM_SETS[system]:
            disk, paths, owner = SETS[st]
            grids = {c: load(c, st) for c in cols}
            seen = []
            for c in cols:
                for p in (grids[c] or {}):
                    if p not in seen:
                        seen.append(p)
            if paths:
                seen = [p for p in paths if p in seen] + [p for p in seen if p not in paths]
            for p in seen:
                any_r = next(g[p] for g in grids.values() if g and p in g)
                code = any_r.get("code", "")
                cells, blocker, shot = [], "", ""
                for c in cols:
                    r = (grids[c] or {}).get(p)
                    if r is None and c == "q761" and code == "ppc":
                        v, b = "n/a", ""
                    elif r is None and c == "e761ppc" and code != "fat":
                        v, b = "", ""
                    else:
                        v, b = verdict(r, c, st)
                    note = notes.get(f"{c}|{p}")
                    if note:
                        v = note.get("status", v)
                        b = note.get("blocker", b)
                    cells.append(v)
                    if c in ecols:
                        total.setdefault((system, c), {}).setdefault(v, 0)
                        total[(system, c)][v] += 1
                    if c in ecols and b and (not blocker or blocker.startswith("(")) \
                            and (v in ("partial", "fails") or not blocker):
                        blocker = f"{c}: {b}" if v in ("partial", "fails") else f"({c}: {b})"
                    if c in ecols and r and not shot:
                        shot = os.path.join(c, st, "%03d.png" % r["n"])
                ref = cells[cols.index(rcol)]
                row_owner = notes.get(f"{system}|{p}", {}).get("owner")
                if not row_owner:
                    if not blocker:
                        row_owner = ""
                    elif system == "8.6" and owner != "fix-games":
                        row_owner = "fix-86"
                    else:
                        row_owner = owner
                    if ref == "fails" and blocker:
                        row_owner += " (ref fails too)"
                leaf = p.split(":", 1)[1]
                lines.append(f"| {leaf} | {code} | " + " | ".join(cells) +
                             f" | {blocker.replace('|', '/')} | {shot} | {row_owner} |")
        lines.append("")
    head = ["# Executor scoreboard", "",
            "The target applications on Executor (Apple's 7.6.1 and 8.6 System Folders)",
            "beside the ROM machines running the same System and application disk",
            "(`tools/executor/scoreboard.py`; grids, screenshots and logs under",
            "`~/storage/executor-grid/m1/<column>/<set>/`, screenshot paths relative to it).",
            "",
            "Columns: **e761** Executor on 7.6.1, 68k code (`--no-ppc`); **e761ppc** the same",
            "with `--executor-ppc`, fat applications only; **q761** Quadra 650 ROM + boot copy",
            "of 7.6.1; **e86** Executor on 8.6 (`--executor-ppc`); **g86** G3 ROM + 8.6.",
            "Each application is launched by BridgeAgent from the Finder and looked at after",
            "20 s: **runs** (in front, no unimplemented calls, screen checked against the",
            "reference), **partial** (in front, but unimplemented calls or a drawing fault),",
            "**fails** (crash, SysError, launch error or never in front).",
            "Owners: fix-prod, fix-dev, fix-games, fix-86 (8.x-only), platinum (8.x frames",
            "and controls).", "", "| System | Column | runs | partial | fails |",
            "|---|---|---|---|---|"]
    for (system, c), t in total.items():
        head.append(f"| {system} | {c} | {t.get('runs', 0)} | {t.get('partial', 0)} | "
                    f"{t.get('fails', 0)} |")
    with open(opts.doc, "w") as f:
        f.write("\n".join(head + [""] + lines))
    print(opts.doc)


def cmd_sheets(opts):
    """Contact sheets: per application, every column's screenshot side by side
    (half size), 4 applications to a sheet, in OUT/sheets/."""
    from PIL import Image, ImageDraw
    os.makedirs(os.path.join(OUT, "sheets"), exist_ok=True)
    for system, sets in SYSTEM_SETS.items():
        cols = [c for c, (s_, _) in COLUMNS.items() if s_ == system]
        for st in sets:
            if opts.only and st not in opts.only:
                continue
            grids = {c: load(c, st) or {} for c in cols}
            paths = []
            for c in cols:
                paths += [p for p in grids[c] if p not in paths]
            for k in range(0, len(paths), 4):
                sheet = Image.new("RGB", (320 * len(cols), 260 * len(paths[k:k + 4])), "white")
                d = ImageDraw.Draw(sheet)
                for row, p in enumerate(paths[k:k + 4]):
                    for i, c in enumerate(cols):
                        r = grids[c].get(p)
                        y = row * 260
                        label = f"{c}: {r['result'] if r else '-'}"
                        if r:
                            png = os.path.join(OUT, c, st, "%03d.png" % r["n"])
                            try:
                                sheet.paste(Image.open(png).convert("RGB").resize((320, 240)),
                                            (320 * i, y + 20))
                            except OSError:   # none, or the session died writing it
                                pass
                        d.text((320 * i + 4, y + 4), label + "  " + p.rsplit(":", 1)[-1][:28],
                               fill="black")
                name = f"{system}-{st}-{k // 4 + 1:02d}.png"
                sheet.save(os.path.join(OUT, "sheets", name))
                print(name)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--only", nargs="*")
    r.add_argument("--force", action="store_true")
    r.add_argument("--wait", type=int, default=20)
    t = sub.add_parser("table")
    t.add_argument("--doc", default=DOC)
    sh = sub.add_parser("sheets")
    sh.add_argument("--only", nargs="*")
    opts = ap.parse_args()
    {"run": cmd_run, "table": cmd_table, "sheets": cmd_sheets}[opts.cmd](opts)


if __name__ == "__main__":
    main()
