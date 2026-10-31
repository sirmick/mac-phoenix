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
(~/.cache/macphoenix/port-slots, "slot<TAB>path" lines, under flock): a git
main tree (/home/mick/src/mac-phoenix) prefers slot 0, base 18000, the
historical ports; a worktree takes the lowest free slot; entries for paths
that no longer exist are dropped. Slots are kept below the ephemeral range
(32768-60999) and away from the UI's 11000.

  ports.py base [ROOT]     the checkout's base
  ports.py port N [ROOT]   nominal port N mapped
  ports.py slots           the registry
"""
import fcntl
import os
import subprocess
import sys
from pathlib import Path

NOMINAL = 18000
WIDTH = 1000
# slot -> base: 18000 first (slot 0: the main tree), then up to the ephemeral
# range, then below 18000 (not 10000/11000), then above it
BASES = ([18000 + WIDTH * i for i in range(14)]          # 18000-31999
         + [12000 + WIDTH * i for i in range(6)]         # 12000-17999
         + [61000 + WIDTH * i for i in range(4)])        # 61000-64999
REGISTRY = Path.home() / ".cache" / "macphoenix" / "port-slots"
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


def _read(f):
    slots = {}
    for line in f.read().splitlines():
        slot, _, path = line.partition("\t")
        if slot.isdigit() and path:
            slots[int(slot)] = path
    return slots


def slot_for(root):
    root = str(Path(root).resolve())
    REGISTRY.parent.mkdir(parents=True, exist_ok=True)
    with open(REGISTRY, "a+") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        f.seek(0)
        slots = _read(f)
        for slot, path in slots.items():
            if path == root:
                return slot
        slots = {s: p for s, p in slots.items() if Path(p).is_dir()}
        free = [s for s in range(len(BASES)) if s not in slots]
        if not is_main_tree(root):
            free = [s for s in free if s != 0] or free
        if not free:
            sys.exit(f"ports.py: all {len(BASES)} port slots taken ({REGISTRY}); "
                     "remove stale lines or set MACPHOENIX_PORT_BASE")
        slot = free[0]
        slots[slot] = root
        f.seek(0)
        f.truncate()
        f.write("".join(f"{s}\t{p}\n" for s, p in sorted(slots.items())))
        return slot


def base(root=ROOT):
    env = os.environ.get("MACPHOENIX_PORT_BASE")
    if env:
        b = int(env)
        if b % WIDTH or not 1024 <= b <= 65535 - WIDTH:
            sys.exit(f"MACPHOENIX_PORT_BASE={env}: a multiple of {WIDTH}, 1024-{65535 - WIDTH}")
        return b
    return BASES[slot_for(root)]


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
            for slot, path in sorted(_read(open(REGISTRY)).items()):
                print(f"{slot}\t{BASES[slot]}\t{path}")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
