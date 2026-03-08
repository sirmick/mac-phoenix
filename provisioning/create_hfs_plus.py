#!/usr/bin/env python3
"""Create an HFS+ disk image for use with MacPhoenix."""

import argparse
import os
import shutil
import subprocess
import sys


def parse_size(size_str):
    """Parse a size string like '120M' or '1G' into bytes."""
    suffixes = {"M": 1024 * 1024, "G": 1024 * 1024 * 1024}
    size_str = size_str.strip().upper()
    if size_str[-1] in suffixes:
        return int(size_str[:-1]) * suffixes[size_str[-1]]
    return int(size_str)


def main():
    parser = argparse.ArgumentParser(description="Create an HFS+ disk image.")
    parser.add_argument("-o", "--output", required=True, help="Output image path")
    parser.add_argument(
        "-s", "--size", default="120M", help="Image size, e.g. 120M, 1G (default: 120M)"
    )
    parser.add_argument(
        "-n", "--name", default="Macintosh HD", help='Volume name (default: "Macintosh HD")'
    )
    args = parser.parse_args()

    if not shutil.which("mkfs.hfsplus"):
        print("Error: mkfs.hfsplus not found. Install hfsprogs:", file=sys.stderr)
        print("  sudo apt install hfsprogs", file=sys.stderr)
        sys.exit(1)

    try:
        size_bytes = parse_size(args.size)
    except ValueError:
        print(f"Error: invalid size '{args.size}'", file=sys.stderr)
        sys.exit(1)

    if size_bytes < 4 * 1024 * 1024:
        print("Error: minimum size for HFS+ is 4MB", file=sys.stderr)
        sys.exit(1)

    output = os.path.abspath(args.output)
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)

    if os.path.exists(output):
        print(f"Error: {output} already exists", file=sys.stderr)
        sys.exit(1)

    # Create sparse image
    try:
        subprocess.run(["truncate", "-s", str(size_bytes), output], check=True)
    except subprocess.CalledProcessError:
        print("Error: failed to create image file", file=sys.stderr)
        sys.exit(1)

    # Format as HFS+
    try:
        subprocess.run(
            ["mkfs.hfsplus", "-v", args.name, output],
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as e:
        os.unlink(output)
        print(f"Error: mkfs.hfsplus failed: {e.stderr.strip()}", file=sys.stderr)
        sys.exit(1)

    size_mb = size_bytes / (1024 * 1024)
    print(f"Created HFS+ image: {output}")
    print(f"  Volume: {args.name}")
    print(f"  Size:   {size_mb:.0f}MB")


if __name__ == "__main__":
    main()
