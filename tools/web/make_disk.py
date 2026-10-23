#!/usr/bin/env python3
"""A compact HFS disk of chosen folders from other images, for the browser.

    tools/web/make_disk.py OUT.img --label NAME --take IMAGE FOLDER [FOLDER...]
                           [--take IMAGE FOLDER...] [--skip PATH...]

Copies the named top-level folders (recursively) of each IMAGE into a new
volume sized to what it holds, as MacBinary through hfsutils, so forks,
Finder info and dates stay as they were. --skip leaves out paths under
them (":Folder:Sub"). A games disk for Executor's tab, say:

    tools/web/make_disk.py games-web.img --label Games \\
        --take games.img "Maelstrom 1.4.2 ƒ" "Lemmings 1.5.2 ƒ" \\
        --skip ":Glider PRO 1.1.2 ƒ:User-designed"

Top-level folder names are matched as given, or by their start (so
"Maelstrom" finds "Maelstrom 1.4.2 ƒ").
"""
import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_boot_disk import Disk, Writer  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--label", required=True)
    ap.add_argument("--take", nargs="+", action="append", metavar=("IMAGE", "FOLDER"),
                    required=True, help="an image and folders at its top level")
    ap.add_argument("--skip", nargs="+", default=[], help="paths to leave out")
    args = ap.parse_args()
    skip = [s.encode("mac_roman").lstrip(b":") for s in args.skip]

    folders, blobs = [], {}
    with tempfile.TemporaryDirectory(prefix="make-disk-") as scratch:
        for n, (image, *wanted) in enumerate(args.take):
            disk = Disk(image, Path(scratch) / f"cache{n}")
            try:
                top = disk.folders(b"")
                for want in wanted:
                    w = want.encode("mac_roman")
                    match = [f for f in top if f == w] or [f for f in top if f.startswith(w)]
                    if not match:
                        sys.exit(f"{want}: not at the top of {image}")

                    def tree(path):
                        if any(path == s or path.startswith(s + b":") for s in skip):
                            return
                        folders.append(path)
                        for name in disk.files(path):
                            p = path + b":" + name
                            if any(p == s for s in skip):
                                continue
                            blob = disk.macbinary(p)
                            if blob is None:
                                print(f"skipped (unreadable): {p.decode('mac_roman')}", file=sys.stderr)
                            else:
                                blobs[p] = blob
                        for name in disk.folders(path):
                            tree(path + b":" + name)

                    tree(match[0])
            finally:
                disk.close()

        content = sum(len(b) for b in blobs.values())
        size = (int(content * 1.1) + 2 * 1024 * 1024 + 511) // 512 * 512
        w = Writer(Path(args.out))
        try:
            w.format(size, args.label)
            for path in folders:
                w.mkdir(path)
            for path, blob in blobs.items():
                w.put(path, blob, scratch)
        finally:
            w.close()
    print(f"{args.out}: {size / 1e6:.1f} MB, {len(blobs)} files in {len(folders)} folders, \"{args.label}\"")


if __name__ == "__main__":
    main()
