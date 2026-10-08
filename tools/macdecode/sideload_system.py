#!/usr/bin/env python3
"""Put Apple's System file into an Executor data directory.

    tools/macdecode/sideload_system.py IMAGE DATA_DIR

Copies ":System Folder:System" from the HFS disk IMAGE into
DATA_DIR/System Folder/ in the form Executor reads (empty data fork
`System` + AppleDouble `%System` holding Finder info and the resource
fork). Run Executor with --executor-data DATA_DIR; it fills in its own
Browser, Printer etc. around it. Executor's default data directory is
left alone.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rsrc import Disk, macbinary_forks  # noqa: E402

AD_MAGIC, AD_VERSION = 0x00051607, 0x00020000
FINDER_INFO, RESOURCE_FORK = 9, 2


def appledouble(finder_info, rsrc):
    entries = [(FINDER_INFO, finder_info), (RESOURCE_FORK, rsrc)]
    header = 26 + 12 * len(entries)
    out = struct.pack(">II16sH", AD_MAGIC, AD_VERSION, b"\0" * 16, len(entries))
    offset, body = header, b""
    for eid, data in entries:
        out += struct.pack(">III", eid, offset, len(data))
        body += data
        offset += len(data)
    return out + body


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    image, data_dir = sys.argv[1], Path(sys.argv[2]).expanduser()
    sysdir = data_dir / "System Folder"
    sysdir.mkdir(parents=True, exist_ok=True)
    d = Disk(image, data_dir / ".sideload-cache")
    try:
        d.resources(b"System Folder:System")          # extracts + caches the MacBinary
        blob = next((data_dir / ".sideload-cache").glob("*.bin")).read_bytes()
    finally:
        d.close()
    data, rsrc = macbinary_forks(blob)
    finder_info = blob[65:65 + 8] + blob[73:74] + b"\0" + blob[75:81] + b"\0" * 16  # type/creator/flags/loc/fldr
    (sysdir / "System").write_bytes(data)
    (sysdir / "%System").write_bytes(appledouble(finder_info.ljust(32, b"\0")[:32], rsrc))
    print(f"System from {image}: {len(rsrc)} bytes of resources -> {sysdir}")


if __name__ == "__main__":
    main()
