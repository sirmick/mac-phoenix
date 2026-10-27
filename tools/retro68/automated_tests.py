#!/usr/bin/env python3
"""Retro68's AutomatedTests on MacPhoenix's Executor.

Runs the tests a Retro68 build tree defines (its AutomatedTests ctest
entries: the application and the expected output) through LaunchAPPL's
Executor backend pointed at tools/retro68/launchappl-executor, for 68k
(build-target) and PowerPC (build-target-ppc). The build tree is only
read: its own tests are configured without a launch method.

    tools/retro68/automated_tests.py [--retro68 DIR] [-R REGEX] [-v]

Exit status 0 if every test passes, 1 if any fails, 77 (ctest SKIP)
without the Retro68 build.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
WRAPPER = HERE / "launchappl-executor"


def tests_of(build_dir):
    """(name, application, properties) for each test the tree defines."""
    out = subprocess.run(["ctest", "--show-only=json-v1"], cwd=build_dir,
                         capture_output=True, text=True, check=True).stdout
    for t in json.loads(out)["tests"]:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        yield t["name"], t["command"][-1], props


def passed(props, rc, output):
    """ctest's verdict: a pass regex decides alone; else the exit code."""
    if any(re.search(r, output) for r in props.get("FAIL_REGULAR_EXPRESSION", [])):
        ok = False
    elif "PASS_REGULAR_EXPRESSION" in props:
        ok = any(re.search(r, output) for r in props["PASS_REGULAR_EXPRESSION"])
    else:
        ok = rc == 0
    return ok != bool(props.get("WILL_FAIL"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--retro68", default=os.path.expanduser("~/opt/retro68/build-ppc"),
                    help="Retro68 build directory (with toolchain/ and build-target*/)")
    ap.add_argument("-R", dest="regex", help="only tests whose name matches")
    ap.add_argument("--timeout", type=int, default=30, help="seconds per test")
    ap.add_argument("-v", action="store_true", help="print each test's output")
    args = ap.parse_args()

    retro = Path(args.retro68)
    launch = retro / "toolchain/bin/LaunchAPPL"
    trees = [retro / d / "AutomatedTests" for d in ("build-target", "build-target-ppc")]
    trees = [t for t in trees if (t / "CTestTestfile.cmake").exists()]
    if not launch.exists() or not trees:
        print(f"SKIP: no Retro68 build with AutomatedTests in {retro}")
        return 77

    failures = []
    total = 0
    for tree in trees:
        for name, app, props in tests_of(tree):
            if args.regex and not re.search(args.regex, name):
                continue
            if props.get("DISABLED"):
                continue
            total += 1
            cmd = [str(launch), "-e", "executor", f"--executor-path={WRAPPER}",
                   f"--timeout={args.timeout}", str(tree / app)]
            try:
                # LaunchAPPL makes its temporary folder where it runs.
                with tempfile.TemporaryDirectory(prefix="retro68-launch-") as scratch:
                    r = subprocess.run(cmd, cwd=scratch, capture_output=True, text=True,
                                       timeout=args.timeout + 30)
                rc, output = r.returncode, r.stdout
            except subprocess.TimeoutExpired as e:
                rc, output = -1, (e.stdout or b"").decode(errors="replace")
            ok = passed(props, rc, output)
            print(f"{'PASS' if ok else 'FAIL'}: {name}" + ("" if ok else f" (exit {rc})"))
            if args.v or not ok:
                for line in output.splitlines():
                    print(f"    {line}")
            if not ok:
                failures.append(name)
    print(f"{total - len(failures)} of {total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
