#!/usr/bin/env python3
"""scoreboard.py run|tab|table: the target applications on Executor's 7.6.1 and
8.6 against the ROM machines (docs/executor/SCOREBOARD.md).

  run [--only COLUMN/SET ...]   grid.py for every column x application disk,
                                into OUT/<column>/<set>/ (skips finished ones
                                unless --force)
  tab [--only COLUMN/DISK ...]  the site's cells (executor/tests/web/tab_site.mjs, ctest
                                executor.web.site.*, in --build, the wasm build)
                                for the tab columns, into OUT/<column>/<disk>.jsonl
                                and screenshots in OUT/<column>/<disk>/
  table                         OUT's grids and tab runs + scoreboard_notes.json
                                -> docs/executor/SCOREBOARD.md

Columns: e761 (Executor, Apple's 7.6.1 System, 68k code only), e761ppc (the
same with --executor-ppc, fat applications), q761 (Quadra 650 ROM, 7.6.1),
e86 (Executor, 8.6, --executor-ppc), g86 (G3 ROM, 8.6). Tab columns: w761
and w86, Executor in the browser tab (WebAssembly) on the site's library
disks (tools/web/make_library.py), each application's row found by its
source disk and path. Sets: the application disks, with the target
applications (all programs on the games disks). Each grid run uses --jobs 2 (run --jobs N).

  MACPHOENIX_EMULATOR=$PWD/build/mac-phoenix.score tools/executor/scoreboard.py run
  tools/executor/scoreboard.py tab --build build-wasm
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
# The tab's columns: the system and the site's library disks it is tried on.
TAB_COLUMNS = {
    "w761": ("7.6.1", ["productivity", "creative", "development", "games-68k"]),
    "w86": ("8.6", ["productivity", "creative", "creative-8", "development", "games-ppc"]),
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
    "prod2": ("productivity-system-7.img", [
        "Productivity:Microsoft Office 98 ƒ:Microsoft Word",
        "Productivity:Aldus Persuasion 3.0 ƒ:Aldus Persuasion 3.0"], "fix-prod"),
    "creative": ("creative-system-7.img", [
        "Creative:Kid Pix 2.5 ƒ:Kid Pix",
        "Creative:Adobe Photoshop 4.0 ƒ:Adobe Photoshop® 4.0",
        "Creative:Adobe Illustrator 6.0 ƒ:Adobe Illustrator® 6.0",
        "Creative:Macromedia FreeHand 5.5 ƒ:FreeHand 5.5",
        "Creative:Deneba Canvas 5.0.2 ƒ:Canvas™ 5"], "fix-prod"),
    "creative8": ("creative-macos-8.img", [
        "Creative:Adobe Photoshop 5.5 ƒ:Adobe® Photoshop® 5.5",
        "Creative:Adobe Illustrator 8.0 ƒ:Adobe Illustrator® 8.0",
        "Creative:Adobe PageMaker 6.5 ƒ:Adobe® PageMaker® 6.5"], "fix-prod"),
    "games": ("games.img", None, "fix-games"),
    "classic": ("games-classic.img", None, "fix-games"),
    "gppc": ("games-ppc.img", None, "fix-games"),
}
# Afterlife's optional installs are copies of the game
EXCLUDE = ["Optional Afterlife Installs"]
SYSTEM_SETS = {"7.6.1": ["prod", "prod2", "creative", "dev", "games", "classic", "gppc"],
               "8.6": ["prod", "prod2", "creative", "creative8", "dev", "dev89", "games", "classic",
                       "gppc"]}


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
               "--jobs", str(opts.jobs), "--wait", str(opts.wait), "--out", out, *args]
        for p in paths or []:
            cmd += ["--path", p]
        if paths is None:
            for c in NOT_PROGRAMS:
                cmd += ["--skip-creator", c]
            for x in EXCLUDE:
                cmd += ["--exclude", x]
        if col == "q761" and st in ("games", "classic", "gppc"):
            cmd += ["--depth", "8"]   # as on Executor: many games run only in 256 colours
        print(f"== {col}/{st}", flush=True)
        subprocess.run(cmd)


def cmd_tab(opts):
    for col, (system, disks) in TAB_COLUMNS.items():
        for disk in disks:
            if opts.only and f"{col}/{disk}" not in opts.only and col not in opts.only \
                    and disk not in opts.only:
                continue
            results = os.path.join(OUT, col, disk + ".jsonl")
            if os.path.exists(results) and not opts.force:
                continue
            os.makedirs(os.path.join(OUT, col, disk), exist_ok=True)
            partial = results + ".part"
            if os.path.exists(partial):
                os.remove(partial)
            env = dict(os.environ, SITE_RESULTS=partial, SITE_SHOTS=os.path.join(OUT, col, disk))
            name = re.escape(f"executor.web.site.{system}.{disk}")
            print(f"== {col}/{disk}", flush=True)
            subprocess.run(["ctest", "--test-dir", opts.build, "-R", f"^{name}$",
                            "--output-on-failure"], env=env)
            if os.path.exists(partial):
                os.replace(partial, results)


def load_tab(col):
    """(source disk, path on it) -> the tab run's record, with its
    screenshot ("shot", relative to OUT) when there is one."""
    system, disks = TAB_COLUMNS[col]
    found = {}
    for disk in disks:
        try:
            with open(os.path.join(OUT, col, disk + ".jsonl")) as f:
                lines = f.read().splitlines()
        except OSError:
            continue
        for line in lines:
            r = json.loads(line)
            if not r.get("source"):
                continue
            png = os.path.join(col, disk, "%s-%s.png" % (system, re.sub(r"[^\w.-]+", "_", r["app"])))
            r["shot"] = png if os.path.exists(os.path.join(OUT, png)) else ""
            found[(r["source"], r["source_app"])] = r
    return found


def tab_verdict(r):
    """(runs|fails, why) of one tab run; nothing for an application not tried."""
    if r is None:
        return "", ""
    return ("runs", "") if r.get("ok") else ("fails", r.get("why") or "fails")


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
        r"session stopped|host signal|guest exited|SysError\(|\] fatal: ", l)), None)
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
    if end and "fatal: " in end:   # Executor's fatal error dialog, the program in front
        return "fails", re.sub(r" \(([\w.]+:\d+), .*", r" (\1)", end)
    ppc = next((l for l in lines if l.startswith("[mame-ppc] exception")), None)
    if res == "runs":
        calls = r.get("unimplemented") or []
        if ppc:   # PowerPC code gone astray, the program still in front
            m = re.match(r"\[mame-ppc\] (exception \d+ at \w+).*?(lr \w+)", ppc)
            return "fails", "PowerPC %s (%s)" % m.groups() + (
                " after unimplemented " + calls[0] if calls else "")
        if calls:
            return "partial", "unimplemented " + calls[0]
        # imports never called don't stop anything: a note, not a fault
        return "runs", "latent: " + imports if missing else ""
    if res == "not in front" and r.get("front") is None:
        # /api/app had no answer (full-screen games): the screenshot decides
        return "runs?", "front process unknown"
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
    faults761 = {}   # path -> the 7.6.1 row's blocker, without the column
    owners = {}      # (system, owner package) -> rows
    for system in ("7.6.1", "8.6"):
        if system not in opts.systems:
            lines += [f"## {system}", "", "Pending.", ""]
            continue
        cols = [c for c, (s, _) in COLUMNS.items() if s == system]
        rcol = [c for c in cols if not c.startswith("e")][0]
        tcols = [c for c, (s, _) in TAB_COLUMNS.items() if s == system]
        cols = cols[:cols.index(rcol)] + tcols + [rcol]   # the reference last
        ecols = [c for c in cols if c != rcol]
        tabs = {c: load_tab(c) for c in tcols}
        lines += [f"## {system}", "",
                  "| Disk | App | Code | " + " | ".join(cols) + " | First blocker | Screenshot | Owner |",
                  "|---|---|---|" + "---|" * len(cols) + "---|---|---|"]
        for st in SYSTEM_SETS[system]:
            disk, paths, owner = SETS[st]
            grids = {c: load(c, st) for c in cols if c not in tcols}
            seen = []
            for c in grids:
                for p in (grids[c] or {}):
                    if p not in seen:
                        seen.append(p)
            if paths:
                seen = [p for p in paths if p in seen] + [p for p in seen if p not in paths]
            for p in seen:
                any_r = next(g[p] for g in grids.values() if g and p in g)
                code = any_r.get("code", "")
                cells, blocker, shot, worst = [], "", "", 0
                for c in cols:
                    if c in tcols:
                        r = tabs[c].get((disk, p.split(":", 1)[1]))
                        v, b = tab_verdict(r)
                        shot_c = r["shot"] if r else ""
                    else:
                        r = (grids[c] or {}).get(p)
                        shot_c = os.path.join(c, st, "%03d.png" % r["n"]) if r else ""
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
                    elif f"{c}/{st}" in notes.get("_reviewed", []) and r is not None:
                        # screenshots checked against the reference, nothing wrong
                        # seen: what the log says stays a note (crashes stay fails)
                        v = "runs" if v in ("partial", "runs?") else v
                    cells.append(v)
                    if c not in ecols or not v:
                        continue
                    total.setdefault((system, c), {}).setdefault(v, 0)
                    total[(system, c)][v] += 1
                    # the worst column's blocker; a note on a running one in brackets
                    rank = {"fails": 3, "partial": 2}.get(v, 1 if b else 0)
                    if rank > worst:
                        worst = rank
                        blocker = f"{c}: {b}" if rank > 1 else f"({c}: {b})"
                        if shot_c:
                            shot = shot_c
                    if shot_c and not shot:
                        shot = shot_c
                ref = cells[cols.index(rcol)]
                fault = blocker.split(": ", 1)[-1]
                if system == "7.6.1" and worst > 1:
                    faults761[p] = fault
                row_owner = notes.get(f"{system}|{p}", {}).get("owner")
                if not row_owner:
                    if worst < 2:
                        row_owner = ""
                    elif system == "8.6" and p not in faults761:   # fine on 7.6.1
                        row_owner = "fix-86"
                    else:
                        row_owner = owner
                    if ref == "fails" and worst > 1:
                        row_owner += " (ref fails too)"
                if row_owner:
                    pkg = row_owner.split()[0]
                    owners[(system, pkg)] = owners.get((system, pkg), 0) + 1
                leaf = p.split(":", 1)[1]
                lines.append(f"| {disk.removesuffix('.img')} | {leaf} | {code} | " + " | ".join(cells) +
                             f" | {blocker.replace('|', '/')} | {shot} | {row_owner} |")
        lines.append("")
    head = ["# Executor scoreboard", "",
            "The target applications on Executor (Apple's 7.6.1 and 8.6 System Folders)",
            "beside the ROM machines running the same System and application disk",
            "(`tools/executor/scoreboard.py run` / `table`; grids, screenshots and logs under",
            "`~/storage/executor-grid/m1/<column>/<disk set>/`, screenshot paths relative to",
            "it; `scoreboard.py sheets` puts the columns side by side in `m1/sheets/`).",
            "",
            "Columns: **e761** Executor on 7.6.1, 68k code (`--no-ppc`); **e761ppc** the same",
            "with `--executor-ppc`, fat applications only; **q761** Quadra 650 ROM + a fresh",
            "copy of 7.6.1 (games at `--depth 8`, as Executor's screen); **e86** Executor on",
            "8.6 (`--executor-ppc`); **g86** G3 ROM + 8.6. Every session gets a fresh, writable",
            "copy of the application disk. BridgeAgent launches the application from the",
            "Finder; 20 s later the grid takes a screenshot and reads the log.",
            "**w761**, **w86**: Executor in the browser tab (WebAssembly) on the site's",
            "library disks, `executor/tests/web/tab_site.mjs` (`scoreboard.py tab`, the",
            "`executor.web.site.*` cells): **runs** when the application came to the front,",
            "drew a window, drew its second menu and was still in front with no error alert",
            "after that; blank where the site doesn't offer it. The site's cells check",
            "against these columns (the native one where a row has none).",
            "",
            "**runs**: the screen matches the reference's (checked by eye, sheet by sheet).",
            "**partial**: up, but drawn wrong or a wrong behaviour seen (or, unchecked, an",
            "unimplemented call logged). **fails**: crash, fatal error, SysError, an alert the",
            "reference doesn't show, or never up. A note in brackets is what the log says about",
            "a program that runs (unimplemented calls, CFM imports left unresolved, exceptions).",
            "**First blocker**: the worst Executor column's. **Owner**: fix-prod, fix-dev,",
            "fix-games by disk; fix-86 for faults on 8.6 only; platinum for 8.x frames, menus",
            "and controls; \"(ref fails too)\" when the ROM machine doesn't run it either.",
            "Visual verdicts and owner overrides: `tools/executor/scoreboard_notes.json`.",
            "", "| System | Column | runs | partial | fails |",
            "|---|---|---|---|---|"]
    for (system, c), t in total.items():
        if c == "e761ppc":
            t = {k: v for k, v in t.items() if k}   # not fat: no cell
        head.append(f"| {system} | {c} | {t.get('runs', 0)} | {t.get('partial', 0)} | "
                    f"{t.get('fails', 0)} |")
    head += ["", "Gaps in the references and the method:", "",
             "- The G3 starts in millions of colours and has no `--depth` (the PPC video",
             "  driver publishes 32-bit modes only, `src/cpu/kpx/video_ppc.cpp`), so many",
             "  games refuse to start there (g86 *fails*, \"needs 256 colours\"); their e86",
             "  verdicts are judged on their own.",
             "- Silicon Beach's black-and-white games (Dark Castle & co.) want a 1-bit screen:",
             "  the Quadra at 256 colours shows their alert; Executor crashes or stays blank.",
             "- Executor 8.6's desktop (no desktop picture, sparse Finder icons, no Control",
             "  Strip) is not counted against any row.",
             "- Full-screen games often leave `/api/app` without an answer; the screenshot",
             "  decides (\"runs?\" where it wasn't checked)."]
    head += ["", "Rows with a fault, by owner:", "", "| System | Owner | rows |", "|---|---|---|"]
    for (system, pkg), k in sorted(owners.items()):
        head.append(f"| {system} | {pkg} | {k} |")
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
    r.add_argument("--jobs", type=int, default=2, help="grid.py's sessions at once")
    w = sub.add_parser("tab")
    w.add_argument("--only", nargs="*")
    w.add_argument("--force", action="store_true")
    w.add_argument("--build", default=os.path.join(REPO, "build-wasm"))
    t = sub.add_parser("table")
    t.add_argument("--doc", default=DOC)
    t.add_argument("--systems", nargs="*", default=["7.6.1", "8.6"])
    sh = sub.add_parser("sheets")
    sh.add_argument("--only", nargs="*")
    opts = ap.parse_args()
    {"run": cmd_run, "tab": cmd_tab, "table": cmd_table, "sheets": cmd_sheets}[opts.cmd](opts)


if __name__ == "__main__":
    main()
