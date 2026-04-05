#!/usr/bin/env python3
"""Populate an HFS disk image with PPC Mac installer files.

Target apps (PPC-native or fat): Classilla, Hotline, MR Browser, Toast,
StuffIt Expander, Disk Copy, Serial MReader.

Uses the same extraction handlers as populate_68k_installers.py.
"""

import argparse
import os
import sys

from populate_68k_installers import (
    handle_dsk,
    handle_macbinary_disk_image,
    handle_stuffit,
    hmount_image,
    humount_image,
    run_text,
)

# The HFS installer image is intentionally minimal: just enough to bootstrap
# everything else inside the Mac. Once StuffIt Expander and Disk Copy are
# installed, the rest of ~/storage/installers/ is reachable via --extfs.
SOURCE_FILES = [
    ("Bootstrap/StuffIt Expander/Stuffit_Expander_5.5.dsk", handle_dsk, "StuffIt Expander"),
    ("Bootstrap/Disk-Copy-633-smi.sit", handle_stuffit, "Disk Copy"),
]


def main():
    parser = argparse.ArgumentParser(
        description="Populate an HFS disk image with PPC Mac installer files."
    )
    parser.add_argument(
        "-i", "--image", required=True, help="Target HFS image to populate"
    )
    parser.add_argument(
        "-s",
        "--source-dir",
        default=os.path.expanduser("~/storage/installers"),
        help="Directory containing source files (default: ~/storage/installers)",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Show what would be done without doing it"
    )
    args = parser.parse_args()

    image = os.path.abspath(args.image)
    if not os.path.exists(image):
        print(f"Error: image {image} does not exist", file=sys.stderr)
        print(
            "Create it first with: python3 create_hfs.py -o IMAGE -s 120M",
            file=sys.stderr,
        )
        sys.exit(1)

    source_dir = args.source_dir
    if not os.path.isdir(source_dir):
        print(f"Error: source directory {source_dir} does not exist", file=sys.stderr)
        sys.exit(1)

    # Build processing list
    to_process = []
    for rel_path, handler, folder in SOURCE_FILES:
        filepath = os.path.join(source_dir, rel_path)
        if os.path.exists(filepath):
            to_process.append((rel_path, handler, folder, filepath))
        else:
            print(f"  SKIP (not found): {rel_path}")

    if not to_process:
        print("No files to process.")
        sys.exit(0)

    print(f"Target image: {image}")
    print(f"Source dir:   {source_dir}")
    print(f"Files to process: {len(to_process)}")
    print()

    if args.dry_run:
        for rel_path, handler, folder, filepath in to_process:
            size_mb = os.path.getsize(filepath) / (1024 * 1024)
            print(f"  {rel_path} ({size_mb:.1f}MB) -> :{folder}: [{handler.__name__}]")
        return

    # Process each file
    successes = 0
    failures = 0
    for rel_path, handler, folder, filepath in to_process:
        print(f"\nProcessing: {rel_path}")
        try:
            if handler(filepath, image, folder):
                successes += 1
            else:
                failures += 1
        except Exception as e:
            print(f"  ERROR: {e}", file=sys.stderr)
            failures += 1

    # Show final contents
    print(f"\n{'=' * 50}")
    print(f"Done: {successes} succeeded, {failures} failed")
    print(f"\nFinal image contents:")
    if hmount_image(image):
        rc, stdout, _ = run_text(["hls", "-la"])
        if rc == 0:
            print(stdout)
        humount_image()


if __name__ == "__main__":
    main()
