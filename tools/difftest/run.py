#!/usr/bin/env python3
"""run.py PROBE [options]: a differential probe (tests/guest/difftest/PROBE.c,
built as PROBEPPC.bin by build-retro68.sh) on Apple's code and on Executor's,
compared key by key.

  --target T        what to test (default executor-8.6); repeatable
  --refresh         run the reference targets again even if their logs are
                    current (they are rerun anyway when the probe changed)
  --no-refresh      never run a reference: compare with the committed ones
  --dump            pixel dumps too (Host:PROBE.dump): side-by-side PNGs of
                    the differing ones in the report directory
  --accept          add the unexpected differences to the expected file
                    (reason "TODO"), to start one off
  --out DIR         reports and kept files (default <storage>/difftest/PROBE;
                    a git worktree's <storage>/difftest/wt-NAME/PROBE)

Targets: g3-8.6, g3-9.0.4 (the G3 ROM, mame-ppc), executor-8.6. Each test
target is compared with its reference (executor-8.6 with g3-8.6).

References: tests/guest/difftest/ref/PROBE-TARGET.txt, committed, the
probe binary's SHA-256 on their first line: a reference made by another
build of the probe is stale and run again (on this machine, with the ROM).
Expected differences: tests/guest/difftest/expected/PROBE-TARGET.txt, one
"key-glob<TAB>reason" a line. The report lists what differs and isn't
expected (exit status 1), what's missing, and expected entries that now
match (to remove). A crash leaves a partial log: the run is resumed from
the section after the last one started (Host:PROBE.start), as often as it
takes; the crashed sections are reported.

Ports 18600, 18610, 18620 (by target), nominal: the checkout's range
(tests/lib/ports.py, MACPHOENIX_PORT_BASE); $DIFFTEST_PORT_BASE moves them.
A run holds the out directory's .lock: another waits for it, up to
$DIFFTEST_LOCK_TIMEOUT seconds (default 1800). $DIFFTEST_NO_SKIP=1 (the
integration run, tools/ci/integration.sh): a target that can't run (exit 77
otherwise) fails.
"""
import argparse
import fcntl
import fnmatch
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tests" / "lib"))
import ports  # noqa: E402
PROBES = REPO / "tests" / "guest" / "difftest"
STORAGE = Path(os.environ.get("MACEMU_STORAGE", Path.home() / "storage"))
G3 = STORAGE / "roms" / "g3.rom"

TARGETS = {
    "g3-8.6": (["--backend", "kpx", "--core", "mame-ppc", "--jit", "--image", "8.6"], {"MACEMU_ROM": str(G3)}),
    "g3-9.0.4": (["--backend", "kpx", "--core", "mame-ppc", "--jit", "--os-version", "9.0.4"], {"MACEMU_ROM": str(G3)}),
    "executor-8.6": (["--backend", "executor", "--executor-ppc", "--image", "8.6"], {}),
}
REFERENCE = {"executor-8.6": "g3-8.6"}
# nominal (tests/lib/ports.py maps them to this checkout's range);
# $DIFFTEST_PORT_BASE moves them (another run, or someone else's emulator,
# holding the defaults)
PORT_BASE = int(os.environ.get("DIFFTEST_PORT_BASE", "18600"))
PORTS = {t: ports.port(PORT_BASE + 10 * i) for i, t in enumerate(["g3-8.6", "g3-9.0.4", "executor-8.6"])}


def parse(text):
    """[(key, value)], sections in order, done?"""
    lines, sections, done = [], [], False
    for line in text.replace("\r", "\n").split("\n"):
        if line.startswith("== "):
            sections.append(line[3:].strip())
        elif line.startswith("DONE"):
            done = True
        elif ": " in line and not line.startswith(("#", "PASS", "FAIL", "SKIP")):
            k, v = line.split(": ", 1)
            if "~" not in k:      # "~" keys: information, not compared
                lines.append((k, v))
    return lines, sections, done


def port_free(port):
    import socket
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) != 0


def name_dumps(probe, keep):
    """The probe's pixel dumps (PROBE-<crc>.ppm, the key on the comment
    line) renamed PROBE-<section>.<key>.ppm."""
    for p in keep.glob(f"{probe}-????????.ppm"):
        with open(p, "rb") as f:
            f.readline()
            comment = f.readline()
        if comment.startswith(b"# "):
            p.replace(keep / f"{probe}-{comment[2:].strip().decode('mac_roman')}.ppm")


def run_target(probe, target, binary, keep, dump, start=None, timeout=400):
    """One run; the kept files' log text ('' if none)."""
    args, env = TARGETS[target]
    if not port_free(PORTS[target]):
        sys.exit(f"port {PORTS[target]} is taken (an earlier run still up?)")
    tmp = Path(tempfile.mkdtemp(prefix="difftest-", dir=STORAGE / "tmp"))
    extra = []
    if start:
        (tmp / f"{probe}.start").write_text(start + "\r")
        extra += ["--host-file", str(tmp / f"{probe}.start")]
    if dump:
        (tmp / f"{probe}.dump").write_text("\r")
        extra += ["--host-file", str(tmp / f"{probe}.dump")]
    if start and (keep / f"{probe}.txt").exists():
        shutil.copy(keep / f"{probe}.txt", tmp / f"{probe}.txt")
        extra += ["--host-file", str(tmp / f"{probe}.txt")]
    cmd = ["bash", str(REPO / "tests" / "test_guest_suite.sh"), *args, "--app", str(binary),
           "--results", f"{probe}.txt", "--network", "none", "--timeout", str(timeout),
           "--port", str(PORTS[target]), "--keep-dir", str(keep), *extra]
    log = keep / f"run-{target}{'-' + start if start else ''}.log"
    for attempt in range(3):
        with open(log, "w") as f:
            subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env={**os.environ, **env})
        # the session never got as far as launching the probe: again
        if "Could not launch" not in log.read_text(errors="replace"):
            break
        print(f"  {target}: launch failed, again", flush=True)
    shutil.rmtree(tmp, ignore_errors=True)
    if re.search(r"^SKIP", log.read_text(errors="replace"), re.M):
        print(f"  {target}: skipped ({log})")
        sys.exit(1 if os.environ.get("DIFFTEST_NO_SKIP") == "1" else 77)
    name_dumps(probe, keep)
    p = keep / f"{probe}.txt"
    return p.read_text(errors="replace") if p.exists() else ""


def run_full(probe, target, binary, out, dump, order=None):
    """Runs until DONE, resuming after crashed sections: (text, crashed)."""
    keep = out / target
    shutil.rmtree(keep, ignore_errors=True)
    keep.mkdir(parents=True)
    text = run_target(probe, target, binary, keep, dump)
    crashed = []
    for _ in range(40):
        _, sections, done = parse(text)
        if done:
            break
        if not sections:
            crashed.append("(start)")
            break
        last = sections[-1]
        crashed.append(last)
        known = order or sections
        if last not in known or known.index(last) + 1 >= len(known):
            break
        nxt = known[known.index(last) + 1]
        print(f"  {target}: crashed in {last}, resuming at {nxt}", flush=True)
        text = run_target(probe, target, binary, keep, dump, start=nxt)
    return text, crashed


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def reference(probe, target, binary, out, refresh, no_refresh, dump):
    ref = PROBES / "ref" / f"{probe}-{target}.txt"
    want = sha(binary)
    if ref.exists() and not refresh:
        first = ref.read_text().split("\n", 1)[0]
        if first == f"# probe {want}" or no_refresh:
            if first != f"# probe {want}":
                print(f"  {ref.name}: made by another build of the probe (--no-refresh)")
            return ref.read_text(), []
    if no_refresh:
        sys.exit(f"no current reference {ref}, and --no-refresh")
    if not G3.exists():
        sys.exit(f"reference {ref} needs the G3 ROM ({G3})")
    print(f"  running the reference {target}", flush=True)
    text, crashed = run_full(probe, target, binary, out, dump)
    lines, sections, _ = parse(text)
    if not sections:
        # the session never ran the probe (launch failed): no reference
        sys.exit(f"{target}: the probe didn't run, see {out / target}")
    ref.parent.mkdir(parents=True, exist_ok=True)
    # keep the sections (resume order) and the lines
    out_lines = [f"# probe {want}"]
    seen = set()
    for line in text.replace("\r", "\n").split("\n"):
        if line.startswith("== ") and line in seen:
            continue
        seen.add(line)
        if line.startswith("== ") or (": " in line and not line.startswith(("PASS", "FAIL", "SKIP"))):
            out_lines.append(line)
    if crashed:
        out_lines.append("# crashed in: " + ", ".join(crashed))
    ref.write_text("\n".join(out_lines) + "\n")
    return ref.read_text(), crashed


def load_expected(path):
    exp = []
    if path.exists():
        for line in path.read_text().splitlines():
            if line.strip() and not line.startswith("#"):
                glob, _, reason = line.partition("\t")
                # keys may end in spaces (Gestalt 'mmu '): only a line
                # without a tab is trimmed
                exp.append((glob if reason else glob.strip(), reason.strip()))
    return exp


def side_by_side(a, b, dest):
    try:
        from PIL import Image
    except ImportError:
        return False
    ia, ib = Image.open(a), Image.open(b)
    w, h = ia.width + ib.width + 10, max(ia.height, ib.height)
    im = Image.new("RGB", (w, h), (255, 0, 255))
    im.paste(ia, (0, 0))
    im.paste(ib, (ia.width + 10, 0))
    im = im.resize((w * 3, h * 3), Image.NEAREST)
    im.save(dest)
    return True


def compare(probe, target, ref_target, ref_text, text, crashed, out, accept):
    rl, ref_sections, _ = parse(ref_text)
    tl, _, done = parse(text)
    ref, got = dict(rl), dict(tl)
    exp_path = PROBES / "expected" / f"{probe}-{target}.txt"
    expected = load_expected(exp_path)

    def why(key):
        for g, reason in expected:
            if fnmatch.fnmatchcase(key, g):
                return g, reason
        return None, None

    unexpected, known, missing, extra, used = [], [], [], [], set()
    for k, v in rl:
        g, reason = why(k)
        if k not in got:
            (known if g else missing).append((k, v, None, reason))
            if g: used.add(g)
        elif got[k] != v:
            if g:
                known.append((k, v, got[k], reason)); used.add(g)
            else:
                unexpected.append((k, v, got[k]))
        elif g:
            pass
    for k, v in tl:
        if k not in ref:
            extra.append((k, v))
    matching_now = [(g, r) for g, r in expected if g not in used]

    rep = [f"# {probe}: {target} against {ref_target}", ""]
    rep.append(f"{len(rl)} reference lines; {len(unexpected)} differ unexpectedly, "
               f"{len(missing)} missing, {len(extra)} extra, {len(known)} expected differences, "
               f"{len(matching_now)} expected entries that now match"
               + ("" if done else "; the run did not finish") + ".")
    if crashed:
        rep += ["", "Crashed in: " + ", ".join(crashed)]
    diffdir = out / "diff"
    shutil.rmtree(diffdir, ignore_errors=True)
    if unexpected:
        rep += ["", "## Unexpected differences", "", f"| key | {ref_target} | {target} |", "|---|---|---|"]
        for k, a, b in unexpected:
            pic = ""
            ka, kb = out / ref_target / f"{probe}-{k}.ppm", out / target / f"{probe}-{k}.ppm"
            if ka.exists() and kb.exists():
                diffdir.mkdir(exist_ok=True)
                if side_by_side(ka, kb, diffdir / f"{k}.png"):
                    pic = f" ([png](diff/{k}.png))"
            rep.append(f"| {k}{pic} | `{a}` | `{b}` |")
    if missing:
        rep += ["", "## Missing (not reached, or absent)", ""] + [f"- {k}: `{v}`" for k, v, _, _ in missing[:200]]
    if extra:
        rep += ["", "## Extra (not in the reference)", ""] + [f"- {k}: `{v}`" for k, v in extra[:200]]
    if matching_now:
        rep += ["", f"## Expected differences that now match (remove from {exp_path.name})", ""]
        rep += [f"- `{g}` ({r})" for g, r in matching_now]
    if known:
        rep += ["", "## Expected differences", ""]
        rep += [f"- {k}: `{a}` / `{b}` ({r})" for k, a, b, r in known[:300]]
    report = "\n".join(rep) + "\n"
    (out / f"report-{target}.md").write_text(report)
    if accept and (unexpected or missing):
        exp_path.parent.mkdir(parents=True, exist_ok=True)
        with open(exp_path, "a") as f:
            for k, *_ in unexpected + missing:
                f.write(f"{k}\tTODO\n")
        print(f"  {len(unexpected) + len(missing)} lines added to {exp_path}")
    return report, not unexpected and not missing


def lock_wait(out, probe):
    """Holds out/.lock for the run (kept open); waits for another run."""
    lock = open(out / ".lock", "a+")
    limit = int(os.environ.get("DIFFTEST_LOCK_TIMEOUT", "1800"))
    start, said = time.monotonic(), None
    while True:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except OSError:
            pass
        waited = time.monotonic() - start
        lock.seek(0)
        holder = lock.read().strip() or "?"
        if said is None or waited - said >= 60:
            print(f"  waiting for another run of {probe} ({holder}) to finish with {out}", flush=True)
            said = waited
        if waited >= limit:
            sys.exit(f"another run of {probe} still holds {out / '.lock'} after {limit} s "
                     f"({holder}); DIFFTEST_LOCK_TIMEOUT waits longer, --out runs elsewhere")
        time.sleep(1)
    lock.seek(0)
    lock.truncate()
    lock.write(f"pid {os.getpid()}, {REPO}\n")
    lock.flush()
    if said is not None:
        print(f"  {out / '.lock'} free after {time.monotonic() - start:.0f} s", flush=True)
    return lock


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("probe")
    ap.add_argument("--target", action="append")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--no-refresh", action="store_true")
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--accept", action="store_true")
    ap.add_argument("--out")
    o = ap.parse_args()
    binary = PROBES / f"{o.probe}PPC.bin"
    if not binary.exists():
        sys.exit(f"no {binary}: tests/guest/difftest/build-retro68.sh {o.probe}")
    if o.out:
        out = Path(o.out)
    elif ports.is_main_tree(REPO):
        out = STORAGE / "difftest" / o.probe
    else:
        out = STORAGE / "difftest" / f"wt-{REPO.name}" / o.probe
    out.mkdir(parents=True, exist_ok=True)
    lock = lock_wait(out, o.probe)  # noqa: F841 (held to the end)
    ok = True
    for target in o.target or ["executor-8.6"]:
        ref_target = REFERENCE.get(target)
        if not ref_target:
            # a reference target alone: refresh it
            reference(o.probe, target, binary, out, True, False, o.dump)
            print(f"{target}: reference written")
            continue
        ref_text, _ = reference(o.probe, ref_target, binary, out, o.refresh, o.no_refresh, o.dump)
        if o.dump and not (out / ref_target).exists():
            print(f"  running {ref_target} for its pixel dumps", flush=True)
            run_full(o.probe, ref_target, binary, out, True)
        print(f"  running {target}", flush=True)
        order = parse(ref_text)[1]
        text, crashed = run_full(o.probe, target, binary, out, o.dump, order)
        report, good = compare(o.probe, target, ref_target, ref_text, text, crashed, out, o.accept)
        ok &= good
        print(report.split("\n\n")[1] if "\n\n" in report else report)
        print(f"report: {out / f'report-{target}.md'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
