#!/usr/bin/env python3
"""Test ports, claimed as they are needed (docs/Testing.md "Ports").

Tests name their ports in one nominal range, 18000-18999 (tests/CMakeLists.txt,
the scripts' defaults, tools/difftest/run.py). Each nominal port a test asks
for is given a free port of its own from a pool shared by every checkout on
this box, 32000-32767 (below the ephemeral range, 32768-60999, and clear of
the 12000-31999 and 61000-64999 the old per-checkout bases used), claimed
with an exclusive flock on ~/.cache/macphoenix/ports/<port> and skipped while
anything has it open in /proc/net/tcp (a session a killed run left behind).
The claim lasts as long as its owner: a Python caller's process (port()), or
for the command line the process named by --pid (default: the caller), which
a small holder process watches. Two checkouts, or ctest -j in one, never get
the same port, and no checkout needs a slot of its own.

A port outside 18000-18999 is already an actual one (a caller mapped it, or
asked for it on purpose) and is left alone, so mapping twice is harmless.

MACPHOENIX_PORT_BASE (a multiple of 1000) still overrides, as before: the
fixed mapping actual = MACPHOENIX_PORT_BASE + (nominal - 18000), no claim.
MACPHOENIX_PORT_POOL=LO-HI moves the pool.

  ports.py port N [--pid P]   a port for nominal N, held until P exits
  ports.py base               MACPHOENIX_PORT_BASE, or "auto"
  ports.py held               the claims held now (port, pid, nominal, cwd)
"""
import fcntl
import os
import random
import select
import subprocess
import sys
import time
from pathlib import Path

NOMINAL = 18000
WIDTH = 1000
POOL = (32000, 32767)
LOCKS = Path.home() / ".cache" / "macphoenix" / "ports"
ROOT = Path(__file__).resolve().parents[2]

_held = {}      # nominal -> (port, fd): this process's claims


def _git(root, *args):
    try:
        return subprocess.run(["git", "-C", str(root), *args], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def is_main_tree(root):
    out = _git(root, "rev-parse", "--absolute-git-dir", "--git-common-dir")
    if not out:
        return False    # a copy of a tree (a reviewer's, a tarball)
    git_dir, common = out.splitlines()
    return Path(git_dir).resolve() == (Path(root) / common).resolve()


def _in_use():
    """Local TCP ports in any state now (LISTEN, TIME_WAIT, ...)."""
    ports = set()
    for name in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for line in open(name).read().splitlines()[1:]:
                ports.add(int(line.split()[1].rsplit(":", 1)[1], 16))
        except OSError:
            pass
    return ports


def _pool():
    env = os.environ.get("MACPHOENIX_PORT_POOL")
    if not env:
        return POOL
    lo, _, hi = env.partition("-")
    if not (lo.isdigit() and hi.isdigit() and 1024 <= int(lo) <= int(hi) <= 65535):
        sys.exit(f"MACPHOENIX_PORT_POOL={env}: LO-HI, 1024-65535")
    return int(lo), int(hi)


def base():
    """MACPHOENIX_PORT_BASE, checked, or None (ports claimed from the pool)."""
    env = os.environ.get("MACPHOENIX_PORT_BASE")
    if not env:
        return None
    b = int(env)
    if b % WIDTH or not 1024 <= b <= 65535 - WIDTH:
        sys.exit(f"MACPHOENIX_PORT_BASE={env}: a multiple of {WIDTH}, 1024-{65535 - WIDTH}")
    return b


def claim(nominal, owner=None):
    """A free port from the pool, locked: (port, fd). The lock goes with fd."""
    lo, hi = _pool()
    LOCKS.mkdir(parents=True, exist_ok=True)
    busy = _in_use()
    span = hi - lo + 1
    start = random.randrange(span)  # not the port the last test just let go
    for i in range(span):
        p = lo + (start + i) % span
        if p in busy:
            continue
        fd = os.open(LOCKS / str(p), os.O_RDWR | os.O_CREAT, 0o644)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(fd)
            continue
        if p in _in_use():      # bound between the look and the lock
            os.close(fd)
            continue
        os.ftruncate(fd, 0)
        os.write(fd, f"{owner or os.getpid()}\t{nominal}\t{os.getcwd()}\n".encode())
        return p, fd
    sys.exit(f"ports.py: no free port in {lo}-{hi} ({LOCKS}; ports.py held)")


def port(nominal, root=ROOT):
    """Nominal port -> actual, claimed for the life of this process."""
    nominal = int(nominal)
    if not NOMINAL <= nominal < NOMINAL + WIDTH:
        return nominal
    b = base()
    if b is not None:
        return b + nominal - NOMINAL
    if nominal not in _held:
        _held[nominal] = claim(nominal)
    return _held[nominal][0]


def _wait_for(pid):
    try:
        fd = os.pidfd_open(pid)
    except ProcessLookupError:
        return
    except OSError:     # no pidfd: look now and then
        while os.path.exists(f"/proc/{pid}"):
            time.sleep(2)
        return
    select.select([fd], [], [])


def hold(nominal, pid):
    """Command line: claim, print the port, then keep the lock (in a child
    process, off the caller's stdout so $(...) returns) until pid exits."""
    p, fd = claim(nominal, pid)
    print(p, flush=True)
    if os.fork():
        os._exit(0)
    os.setsid()
    null = os.open(os.devnull, os.O_RDWR)
    for i in (0, 1, 2):
        os.dup2(null, i)
    os.closerange(3, fd)
    os.closerange(fd + 1, 1024)
    _wait_for(pid)
    os._exit(0)


def held():
    """[(port, pid, nominal, cwd)] for the claims held now."""
    out = []
    for f in sorted(LOCKS.glob("*")) if LOCKS.is_dir() else []:
        if not f.name.isdigit():
            continue
        fd = os.open(f, os.O_RDONLY)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            pid, nominal, cwd = (f.read_text().rstrip("\n").split("\t") + ["?"] * 3)[:3]
            out.append((int(f.name), pid, nominal, cwd))
        finally:
            os.close(fd)
    return out


def main(argv):
    if argv[:1] == ["port"] and len(argv) in (2, 4) and (len(argv) == 2 or argv[2] == "--pid"):
        nominal = int(argv[1])
        if base() is not None or not NOMINAL <= nominal < NOMINAL + WIDTH:
            print(port(nominal))
        else:
            hold(nominal, int(argv[3]) if len(argv) == 4 else os.getppid())
    elif argv[:1] == ["base"] and len(argv) <= 2:     # (a ROOT argument: ignored)
        b = base()
        print("auto" if b is None else b)
    elif argv == ["held"]:
        for row in held():
            print("\t".join(map(str, row)))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
