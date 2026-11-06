#!/usr/bin/env python3
"""The library's boot disks hold nothing Executor's policy denies.

    tools/web/audit_boot_disks.py LIBRARY [NAME.img ...]

Every boot-*.img in LIBRARY (tools/web/make_library.py's output; or the
ones named): no file in the System Folder, at any depth, has a name the
policy denies ("-NAME" in executor/core/res/extension-policy.txt: the
Monitors control panels and their Control Strip modules), and no alias
there (Apple Menu Items, Launcher Items, Startup Items, ...) points at one.
Prints what it found per disk; exit 1 if anything, 77 without a boot disk.
docs/executor/APPLE-DISKS.md, "Boot disks for the browser".
"""
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from make_boot_disk import POLICY, Disk, alias_target, policy_names, system_version  # noqa: E402


def audit(image):
    """(denied names, what was found): Mac paths as text."""
    found = []
    with tempfile.TemporaryDirectory(prefix="bootaudit-") as scratch:
        disk = Disk(str(image), Path(scratch) / "cache")
        try:
            denied = policy_names(POLICY, system_version(disk), denied=True)

            def walk(path):
                for name in disk.files(path):
                    full = path + b":" + name
                    if name.strip(b" ") in denied:
                        found.append(f"{full.decode('mac_roman')}")
                        continue
                    blob = disk.macbinary(full)
                    target = blob and alias_target(blob)
                    if target and target[1].rsplit(b":", 1)[-1].strip(b" ") in denied:
                        found.append(f"{full.decode('mac_roman')} (alias to {target[1].decode('mac_roman')})")
                for name in disk.folders(path):
                    walk(path + b":" + name)
            walk(b"System Folder")
        finally:
            disk.close()
    return sorted(n.decode("mac_roman") for n in denied), found


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    lib = Path(sys.argv[1]).expanduser()
    images = [lib / n for n in sys.argv[2:]] or sorted(lib.glob("boot-*.img"))
    images = [i for i in images if i.exists()]
    if not images:
        print(f"SKIP: no boot disk in {lib}")
        sys.exit(77)
    failed = 0
    for image in images:
        denied, found = audit(image)
        ok = not found and denied
        print(f"{'PASS' if ok else 'FAIL'} {image.name}: none of {', '.join(denied) or '(no denied names)'}"
              + "".join(f"\n    {f}" for f in found))
        failed += not ok
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
