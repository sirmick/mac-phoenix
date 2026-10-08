#!/usr/bin/env python3
"""Put Apple's System file and Finder into an Executor data directory.

    tools/macdecode/sideload_system.py IMAGE DATA_DIR [FILE...]

Copies ":System Folder:<FILE>" (default: System and Finder) from the HFS
disk IMAGE into DATA_DIR/System Folder/ in the form Executor reads (data
fork `<FILE>` + AppleDouble `%<FILE>` holding Finder info and the
resource fork). Run Executor with --executor-data DATA_DIR, and
--executor-app "DATA_DIR/System Folder/Finder" to start Finder. Executor
fills in its own Browser, Printer etc. around them. Executor's default
data directory is left alone.
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
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    image, data_dir = sys.argv[1], Path(sys.argv[2]).expanduser()
    names = sys.argv[3:] or ["System", "Finder"]
    sysdir = data_dir / "System Folder"
    sysdir.mkdir(parents=True, exist_ok=True)
    d = Disk(image, data_dir / ".sideload-cache")
    try:
        for name in names:
            blob = d.macbinary(b"System Folder:" + name.encode("mac_roman"))
            if blob is None:
                print(f"{name}: not on {image}")
                continue
            data, rsrc = macbinary_forks(blob)
            # MacBinary: type/creator/flags at 65..74, location 75..80.
            finder_info = blob[65:65 + 8] + blob[73:74] + b"\0" + blob[75:81] + b"\0" * 16
            (sysdir / name).write_bytes(data)
            (sysdir / f"%{name}").write_bytes(appledouble(finder_info.ljust(32, b"\0")[:32], rsrc))
            print(f"{name} from {image}: {len(data)} data, {len(rsrc)} resource bytes -> {sysdir}")
    finally:
        d.close()


if __name__ == "__main__":
    main()
