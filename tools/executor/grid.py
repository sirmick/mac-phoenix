#!/usr/bin/env python3
"""grid.py IMAGE [--jobs N] [--only NAME ...] [--out DIR]: every control
panel and application on a System image, run one by one on Executor with
that System (docs/executor/MacOS8-9.md, the 8.6 grid).

The items come from the image itself (hfsutils): applications ('APPL',
'APPC'), control panels ('cdev', and the 8.x ones that are 'APPC') and desk
accessories ('dfil') in the System Folder's Control Panels and Apple Menu
Items and anywhere outside the System Folder. Each runs in a fresh session
(--executor-system on a copy of the image per worker, --executor-ppc,
--bridge): the Finder starts, BridgeAgent launches the item by path (a
control panel or desk accessory is opened by the Finder, as a double-click),
and after a while the grid records the front process, a screenshot, a crash
(the session ending) and the calls Executor answered as unimplemented while
the item ran. Results: DIR/grid.json, DIR/grid.md, DIR/<n>.png, DIR/<n>.log.

  tools/executor/grid.py 8.6.img --jobs 3 --out ~/storage/executor-grid/8.6

With --rom ROM (g3: ~/storage/roms/g3.rom) the same items run on Apple's
code instead, the ROM machine (kpx on mame-ppc) booting a fresh copy of the
image for each: the reference screenshots (default DIR
executor-grid/<image>-g3). grid_compare.py --html EXECUTOR-DIR REF-DIR
shows the two side by side.

  tools/executor/grid.py 8.6.img --rom g3 --jobs 2
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STORAGE = os.path.expanduser("~/storage")
IMAGES = os.path.join(STORAGE, "images")
KINDS = {b"APPL": "application", b"APPC": "control panel", b"cdev": "control panel",
         b"dfil": "desk accessory"}
LS = re.compile(rb"^([fFd])\s+(?:(\S{4})/(\S{4})\s+\d+\s+\d+|\d+ items?)\s+\w{3}\s+\d+\s+[\d:]+ (.*)$")


def inventory(image):
    """[(path, kind, type, creator)] of what the image holds to run."""
    home = tempfile.mkdtemp(prefix="grid-hfs.")
    env = dict(os.environ, HOME=home)

    def run(*args):
        return subprocess.run(args, env=env, capture_output=True, check=True).stdout

    try:
        run("hmount", image)
        label = re.search(rb'Volume name is "(.*)"', run("hvol")).group(1)
        out = []

        def walk(folder, inside_system):
            for line in run("hls", "-l", "-a", folder).split(b"\n"):
                m = LS.match(line)
                if not m:
                    continue
                name = m.group(4)
                path = folder + name
                if m.group(1) == b"d":
                    if folder == b":" and name == b"System Folder":
                        for sub in (b"Control Panels", b"Apple Menu Items"):
                            walk(path + b":" + sub + b":", True)
                    elif not inside_system:
                        walk(path + b":", False)
                    continue
                kind = KINDS.get(m.group(2))
                if kind:
                    out.append((label + path, kind, m.group(2), m.group(3)))
        walk(b":", False)
        return [(p.decode("mac_roman"), k, t.decode("mac_roman"), c.decode("mac_roman"))
                for p, k, t, c in out]
    finally:
        subprocess.run(["humount"], env=env, capture_output=True)
        shutil.rmtree(home, ignore_errors=True)


def api(port, path, body=None, timeout=10):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"http://localhost:{port}{path}", data=data,
                                 headers={"Content-Type": "application/json"} if data else {},
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read()
    try:
        return json.loads(raw)
    except ValueError:
        return raw


def unimplemented(text):
    calls = []
    for line in text.splitlines():
        m = re.search(r"\[unimplemented\] (.*)", line)
        if m and m.group(1) not in calls:
            calls.append(m.group(1))
    return calls


def ended(text):
    m = re.search(r"(guest exited.*|host signal.*|session stopped.*)", text)
    return m.group(1) if m else None


def run_item(n, item, image, port, out, wait, binary, rom=None, source=None):
    path, kind, ftype, creator = item
    log = os.path.join(out, f"{n:03d}.log")
    result = {"n": n, "path": path, "kind": kind, "type": ftype, "creator": creator}
    if rom:
        # the ROM machine writes its disk: a fresh copy each item
        subprocess.run(["cp", "--reflink=auto", source, image], check=True)
        # 640x480 as Executor's screen (8.6 takes the largest mode otherwise)
        args = ["--backend", "kpx", "--core", "mame-ppc", "--jit", "--ram", "128",
                "--max-resolution", "640x480", "--disk", image,
                "--dismiss-shutdown-dialog", "--bridge", rom]
    else:
        args = ["--backend", "executor", "--executor-system", os.path.basename(image),
                "--executor-ppc", "--bridge"]
    with open(log, "wb") as f:
        proc = subprocess.Popen([binary, "--config", "/dev/null", *args,
                                 "--port", str(port), "--timeout", str(wait + 150)],
                                stdout=f, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        for _ in range(30):
            try:
                api(port, "/api/status")
                break
            except OSError:
                time.sleep(1)
        api(port, "/api/emulator/start", {})
        start = time.time()
        while time.time() - start < 120:
            st = api(port, "/api/status")
            if isinstance(st, dict) and st.get("bridge_agent_connected"):
                break
            if proc.poll() is not None:
                break
            time.sleep(1)
        else:
            result["result"] = "no Finder/bridge"
        if "result" not in result and proc.poll() is None:
            mark = os.path.getsize(log)
            time.sleep(3)
            try:
                r = api(port, "/api/launch", {"path": path}, timeout=40)
                result["launch"] = r.get("message") or r.get("error") if isinstance(r, dict) else str(r)
            except OSError as e:
                result["launch"] = f"no answer ({e})"
            time.sleep(wait)
            try:
                result["front"] = api(port, "/api/app").get("app")
            except (OSError, AttributeError):
                result["front"] = None
            try:
                with open(os.path.join(out, f"{n:03d}.png"), "wb") as f:
                    f.write(api(port, "/api/screenshot", timeout=10))
            except OSError:
                pass
            with open(log, "rb") as f:
                f.seek(mark)
                tail = f.read().decode("latin-1")
            result["unimplemented"] = unimplemented(tail)
            result["ended"] = ended(tail)
    except OSError as e:
        result["result"] = f"api error: {e}"
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(5)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    if "result" not in result:
        leaf = path.rsplit(":", 1)[-1]
        if result.get("ended"):
            result["result"] = "crashed"
        elif result.get("launch") != "launched":
            result["result"] = "launch failed"
        elif kind == "application" and (result.get("front") or "Finder").startswith(("Finder", "BridgeAgent")):
            result["result"] = "not in front"
        else:
            result["result"] = "runs"
        result["leaf"] = leaf
    return result


def markdown(results, image, machine="Executor"):
    lines = [f"# {image} on {machine}: the grid", "",
             "| # | Item | Kind | Result | Front | Unimplemented calls |",
             "|---|------|------|--------|-------|---------------------|"]
    for r in sorted(results, key=lambda r: r["n"]):
        calls = "; ".join(r.get("unimplemented", []))[:300]
        lines.append(f"| {r['n']} | {r['path']} | {r['kind']} | {r['result']} | "
                     f"{r.get('front') or ''} | {calls} |")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--out")
    ap.add_argument("--wait", type=int, default=10, help="seconds an item runs before it is looked at")
    ap.add_argument("--port", type=int, default=19600)
    ap.add_argument("--rom", help="run on a ROM machine instead (g3: the G3 ROM)")
    ap.add_argument("--binary", default=os.environ.get("MACPHOENIX_EMULATOR",
                                                       os.path.join(REPO, "build", "mac-phoenix")))
    opts = ap.parse_args()
    image = opts.image if os.path.isabs(opts.image) else os.path.join(IMAGES, opts.image)
    name = os.path.basename(image).removesuffix(".img")
    rom = opts.rom
    if rom == "g3":
        rom = os.path.join(STORAGE, "roms", "g3.rom")
    out = opts.out or os.path.join(STORAGE, "executor-grid", name + ("-g3" if rom else ""))
    os.makedirs(out, exist_ok=True)

    items = inventory(image)
    if opts.only:
        items = [i for i in items if any(o.lower() in i[0].lower() for o in opts.only)]
    print(f"{len(items)} items", flush=True)

    # One copy of the image per worker: Executor extracts each image's
    # System Folder to its own place and restores it every run.
    copies = []
    for w in range(opts.jobs):
        if rom:   # made fresh for each item (run_item)
            copies.append(os.path.join(STORAGE, "tmp", f"grid-{name}-rom-{w}.img"))
            continue
        copy = os.path.join(IMAGES, f"grid-{name}-{w}.img")
        if not os.path.exists(copy) or os.path.getmtime(copy) < os.path.getmtime(image):
            shutil.copyfile(image, copy)
        copies.append(copy)

    results = []
    free = list(range(opts.jobs))
    with concurrent.futures.ThreadPoolExecutor(opts.jobs) as pool:
        def job(n, item):
            w = free.pop()
            try:
                return run_item(n, item, copies[w], opts.port + w, out, opts.wait, opts.binary,
                                rom, image)
            finally:
                free.append(w)
        futures = [pool.submit(job, n, item) for n, item in enumerate(items, 1)]
        for f in concurrent.futures.as_completed(futures):
            r = f.result()
            results.append(r)
            print(f"{r['n']:3d} {r['result']:14s} {r['path']}  "
                  f"[{len(r.get('unimplemented', []))} unimplemented]", flush=True)
            with open(os.path.join(out, "grid.json"), "w") as fj:
                json.dump(sorted(results, key=lambda r: r["n"]), fj, indent=1)
    with open(os.path.join(out, "grid.md"), "w") as f:
        f.write(markdown(results, name, "the ROM machine" if rom else "Executor"))
    for c in copies if rom else []:
        if os.path.exists(c):
            os.remove(c)
    print(os.path.join(out, "grid.md"))


if __name__ == "__main__":
    main()
