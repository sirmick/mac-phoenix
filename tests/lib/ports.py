#!/usr/bin/env python3
"""Per-worktree test ports (docs/Testing.md "Ports").

Tests name their ports in one nominal range, 18000-18999 (tests/CMakeLists.txt,
the scripts' defaults, tools/difftest/run.py). Each checkout moves that range
to its own base, so two worktrees running ctest at once never bind the same
port:

  actual = MACPHOENIX_PORT_BASE + (nominal - 18000)

A port outside 18000-18999 is already an actual one (a caller mapped it, or
asked for it on purpose) and is left alone, so mapping twice is harmless.

MACPHOENIX_PORT_BASE (a multiple of 1000) overrides; by default each checkout
gets a slot from a registry shared by every checkout on this box
(~/.cache/macphoenix/port-bases, "base<TAB>path" lines, under flock): a git
main tree (/home/mick/src/mac-phoenix) prefers slot 0, base 18000, the
historical ports; a worktree takes the lowest free slot with nothing
listening in its range just then (someone's hand-started emulator); entries
for paths that no longer exist are dropped. Slots are kept below the
ephemeral range (32768-60999), away from the UI's 11000 and from 19000-19999,
where emulators started by hand tend to go.

  ports.py base [ROOT]     the checkout's base
  ports.py port N [ROOT]   nominal port N mapped
  ports.py slots           the registry (base, checkout)
"""
import fcntl
import os
import subprocess
import sys
from pathlib import Path

NOMINAL = 18000
WIDTH = 1000
# slot -> base: 18000 first (slot 0: the main tree), then up to the ephemeral
# range (not 19000: hand-started emulators), then below 18000 (not
# 10000/11000), then above it
BASES = ([18000]
         + [20000 + WIDTH * i for i in range(12)]        # 20000-31999
         + [12000 + WIDTH * i for i in range(6)]         # 12000-17999
         + [61000 + WIDTH * i for i in range(4)])        # 61000-64999
REGISTRY = Path.home() / ".cache" / "macphoenix" / "port-bases"
ROOT = Path(__file__).resolve().parents[2]


def _git(root, *args):
    try:
        return subprocess.run(["git", "-C", str(root), *args], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def is_main_tree(root):
    out = _git(root, "rev-parse", "--absolute-git-dir", "--git-common-dir")
    if not out:
        return True     # not a git checkout (a tarball): the historical ports
    git_dir, common = out.splitlines()
    return Path(git_dir).resolve() == (Path(root) / common).resolve()


def _listening():
    """Local TCP ports in LISTEN now."""
    ports = set()
    for name in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for line in open(name).read().splitlines()[1:]:
                f = line.split()
                if f[3] == "0A":
                    ports.add(int(f[1].rsplit(":", 1)[1], 16))
        except OSError:
            pass
    return ports


def _read(f):
    bases = {}
    for line in f.read().splitlines():
        b, _, path = line.partition("\t")
        if b.isdigit() and path:
            bases[int(b)] = path
    return bases


def _old_slots():
    """The first registry's lines (slot index, its first table)."""
    old = [18000 + WIDTH * i for i in range(14)] + [12000 + WIDTH * i for i in range(6)] \
        + [61000 + WIDTH * i for i in range(4)]
    try:
        with open(REGISTRY.with_name("port-slots")) as f:
            return {old[s]: p for s, p in _read(f).items() if s < len(old)}
    except OSError:
        return {}


def base_for(root):
    """The checkout's base from the registry, given one if it has none."""
    root = str(Path(root).resolve())
    REGISTRY.parent.mkdir(parents=True, exist_ok=True)
    with open(REGISTRY, "a+") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        f.seek(0)
        bases = _read(f)
        if not bases and _old_slots():
            bases = _old_slots()
            f.write("".join(f"{b}\t{p}\n" for b, p in sorted(bases.items())))
            f.flush()
        for b, path in bases.items():
            if path == root:
                return b
        bases = {b: p for b, p in bases.items() if Path(p).is_dir()}
        free = [b for b in BASES if b not in bases]
        if not is_main_tree(root):
            free = [b for b in free if b != NOMINAL] or free
        busy = _listening()
        free = [b for b in free if not any(b <= p < b + WIDTH for p in busy)] or free
        if not free:
            sys.exit(f"ports.py: all {len(BASES)} port ranges taken ({REGISTRY}); "
                     "remove stale lines or set MACPHOENIX_PORT_BASE")
        bases[free[0]] = root
        f.seek(0)
        f.truncate()
        f.write("".join(f"{b}\t{p}\n" for b, p in sorted(bases.items())))
        return free[0]


def base(root=ROOT):
    env = os.environ.get("MACPHOENIX_PORT_BASE")
    if env:
        b = int(env)
        if b % WIDTH or not 1024 <= b <= 65535 - WIDTH:
            sys.exit(f"MACPHOENIX_PORT_BASE={env}: a multiple of {WIDTH}, 1024-{65535 - WIDTH}")
        return b
    return base_for(root)


def port(nominal, root=ROOT):
    nominal = int(nominal)
    if not NOMINAL <= nominal < NOMINAL + WIDTH:
        return nominal
    return base(root) + nominal - NOMINAL


def main(argv):
    if argv[:1] == ["base"] and len(argv) <= 2:
        print(base(argv[1] if len(argv) > 1 else ROOT))
    elif argv[:1] == ["port"] and 2 <= len(argv) <= 3:
        print(port(argv[1], argv[2] if len(argv) > 2 else ROOT))
    elif argv == ["slots"]:
        if REGISTRY.exists():
            for b, path in sorted(_read(open(REGISTRY)).items()):
                print(f"{b}\t{path}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
