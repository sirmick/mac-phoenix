#!/usr/bin/env python3
"""Rerun a ctest run's failures alone and sort them (docs/Testing.md "Flaky tests").

  tools/ci/rerun_flaky.py --test-dir BUILD [--log CTEST-OUTPUT] [--out DIR]
                          [--env K=V ...] [--flaky tests/flaky.txt] [--max N]

Takes the failures from the ctest run's output (--log: its "The following
tests FAILED:" list) or else BUILD/Testing/Temporary/LastTestsFailed.log
(ctest keeps the entries of earlier runs' failures it didn't run again
there: give --log, or remove the file before the run), reruns each failed
test on its own (ctest -R '^NAME$', one at a time, the environment given with
--env added to this one's) and classifies it against the known-flaky list:

  flake       passes alone, listed in tests/flaky.txt
  new-flake   passes alone, not listed: add it to tests/flaky.txt?
  real        fails alone (or skips alone, or was not rerun: past --max)

Writes DIR/rerun.md (a table: test, class, the flaky line's reason, log) and
DIR/rerun.json ({"tests": [{name, class, reason, log}], "verdict": ...}),
each rerun's output in DIR/rerun-NAME.log; DIR defaults to BUILD/rerun.

Exit status: 0 nothing failed, 3 flakes only (listed), 4 flakes only with
new ones among them, 1 a real failure, 2 a usage error.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def read_flaky(path):
    """[(regex, date, reason)] from the list."""
    out = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = line.split(None, 2)
        out.append((re.compile(f[0]), f[1] if len(f) > 1 else "", f[2] if len(f) > 2 else ""))
    return out


def listed(name, flaky):
    for rx, date, reason in flaky:
        if rx.fullmatch(name):
            return f"{reason} ({date})" if date else reason
    return None


def failed(test_dir):
    log = Path(test_dir) / "Testing" / "Temporary" / "LastTestsFailed.log"
    if not log.exists():
        return []
    names = []
    for line in log.read_text().splitlines():
        _, _, name = line.partition(":")
        if name:
            names.append(name.strip())
    return names


def failed_in_log(log):
    text = Path(log).read_text(errors="replace")
    if "The following tests FAILED:" not in text:
        return []
    tail = text.split("The following tests FAILED:", 1)[1]
    return re.findall(r"^\s+\d+ - (\S+) \(", tail, re.M)


def ctest_regex(name):
    """^NAME$ in the regex ctest -R reads (CMake's: backslash escapes)."""
    return "^" + re.sub(r"([.^$*+?()\[\]{}|\\])", r"\\\1", name) + "$"


def rerun(test_dir, name, log, env, timeout):
    cmd = ["ctest", "--test-dir", str(test_dir), "-R", ctest_regex(name), "--output-on-failure"]
    if timeout:
        cmd += ["--timeout", str(timeout)]
    with open(log, "w") as f:
        rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env=env).returncode
    text = Path(log).read_text(errors="replace")
    if "No tests were found" in text:
        return "missing"
    if re.search(r"\*\*\*Skipped|Not Run", text):
        return "skipped"
    return "pass" if rc == 0 and "100% tests passed" in text else "fail"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--test-dir", required=True)
    ap.add_argument("--log", help="the ctest run's output")
    ap.add_argument("--out")
    ap.add_argument("--env", action="append", default=[], metavar="K=V")
    ap.add_argument("--flaky", default=str(REPO / "tests" / "flaky.txt"))
    ap.add_argument("--max", type=int, default=12, help="rerun at most N failures (default 12)")
    ap.add_argument("--timeout", type=int, default=0, help="ctest --timeout for each rerun")
    o = ap.parse_args()

    test_dir = Path(o.test_dir).resolve()
    out = Path(o.out) if o.out else test_dir / "rerun"
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for kv in o.env:
        k, sep, v = kv.partition("=")
        if not sep:
            sys.exit(f"--env {kv}: K=V")
        env[k] = v
    flaky = read_flaky(o.flaky)
    names = failed_in_log(o.log) if o.log else failed(test_dir)

    rows = []
    for i, name in enumerate(names):
        reason = listed(name, flaky)
        log = out / ("rerun-" + re.sub(r"[^\w.-]", "_", name) + ".log")
        if i >= o.max:
            rows.append({"name": name, "class": "real", "alone": "not rerun", "reason": reason or "", "log": ""})
            continue
        print(f"rerun_flaky: {name} alone ...", file=sys.stderr, flush=True)
        alone = rerun(test_dir, name, log, env, o.timeout)
        cls = ("flake" if reason else "new-flake") if alone == "pass" else "real"
        rows.append({"name": name, "class": cls, "alone": alone, "reason": reason or "", "log": str(log)})
        print(f"rerun_flaky: {name}: {alone} alone -> {cls}", file=sys.stderr, flush=True)

    classes = {r["class"] for r in rows}
    verdict = ("none" if not rows else "real" if "real" in classes
               else "new-flake" if "new-flake" in classes else "flake")
    (out / "rerun.json").write_text(json.dumps({"tests": rows, "verdict": verdict}, indent=1) + "\n")
    with open(out / "rerun.md", "w") as f:
        f.write(f"Failures of {test_dir} rerun alone: {verdict}\n\n")
        if rows:
            f.write("| test | alone | class | flaky.txt | log |\n|---|---|---|---|---|\n")
            for r in rows:
                cls = "new flake: add it to tests/flaky.txt?" if r["class"] == "new-flake" else r["class"]
                f.write(f"| {r['name']} | {r['alone']} | {cls} | {r['reason']} | {r['log']} |\n")
    print((out / "rerun.md").read_text(), end="")
    sys.exit({"none": 0, "flake": 3, "new-flake": 4, "real": 1}[verdict])


if __name__ == "__main__":
    main()
