#!/usr/bin/env python3
"""Populate an HFS disk image with Mac installer files.

Target apps: MPW, ResEdit, Disk Copy, StuffIt Expander, MacTCP Ping.

Strategy:
  - StuffIt Expander: extract from .dsk (HFS image) directly — this is the
    bootstrap tool needed to extract everything else inside Mac OS.
  - .sit archives (ResEdit, Disk Copy): copy the .sit file itself onto the
    HFS image. Extract inside Mac OS using StuffIt Expander — this preserves
    resource forks perfectly.
  - MPW (.img_.bin): MacBinary-wrapped disk image. Copy .bin onto HFS image
    for Disk Copy to mount, and also extract the raw .img for direct use.
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time


def run(cmd, **kwargs):
    """Run a command, returning CompletedProcess. Prints stderr on failure."""
    result = subprocess.run(cmd, capture_output=True, **kwargs)
    if result.returncode != 0:
        stderr = result.stderr.decode("mac_roman", errors="replace") if isinstance(result.stderr, bytes) else (result.stderr or "")
        cmd_str = " ".join(a if isinstance(a, str) else a.decode("mac_roman", errors="replace") for a in cmd)
        print(f"  FAILED: {cmd_str}", file=sys.stderr)
        if stderr.strip():
            print(f"  stderr: {stderr.strip()}", file=sys.stderr)
    return result


def run_text(cmd):
    """Run a command, decode stdout as mac_roman (hfsutils native encoding)."""
    result = subprocess.run(cmd, capture_output=True)
    stdout = result.stdout.decode("mac_roman", errors="replace") if result.stdout else ""
    stderr = result.stderr.decode("mac_roman", errors="replace") if result.stderr else ""
    return result.returncode, stdout, stderr


def hls_raw():
    """List files on currently mounted HFS volume, returning raw byte strings.

    hfsutils outputs filenames in Mac Roman encoding. We return them as raw
    bytes so they can be passed back to hcopy/hmkdir without re-encoding.
    """
    result = subprocess.run(["hls"], capture_output=True)
    if result.returncode == 0 and result.stdout.strip():
        return [line for line in result.stdout.strip().split(b"\n")]
    return []


def hmount_image(image_path):
    """Mount an HFS image for hfsutils operations."""
    rc, _, _ = run_text(["hmount", image_path])
    return rc == 0


def humount_image():
    """Unmount current hfsutils volume."""
    run_text(["humount"])


def hls(path=None):
    """List files on currently mounted HFS volume."""
    cmd = ["hls"] + ([path] if path else [])
    rc, stdout, _ = run_text(cmd)
    if rc == 0 and stdout.strip():
        return stdout.strip().split("\n")
    return []


def hmkdir(path):
    """Create directory on mounted HFS volume.

    Accepts str (will be encoded to Mac Roman) or bytes.
    """
    if isinstance(path, str):
        path = path.encode("mac_roman", errors="replace")
    rc, _, _ = run_text(["hmkdir", path])
    return rc == 0


def hcopy_macbinary_in(src_path, hfs_dest):
    """Copy a local file onto mounted HFS volume with MacBinary translation.

    hfs_dest accepts str (will be encoded to Mac Roman) or bytes.
    """
    if isinstance(hfs_dest, str):
        hfs_dest = hfs_dest.encode("mac_roman", errors="replace")
    return run(["hcopy", "-m", src_path, hfs_dest]).returncode == 0


def hcopy_raw_in(src_path, hfs_dest):
    """Copy a local file onto mounted HFS volume (raw, data fork only).

    hfs_dest accepts str (will be encoded to Mac Roman) or bytes.
    """
    if isinstance(hfs_dest, str):
        hfs_dest = hfs_dest.encode("mac_roman", errors="replace")
    return run(["hcopy", "-r", src_path, hfs_dest]).returncode == 0


def hcopy_macbinary_out(hfs_src, local_dest):
    """Copy a file from mounted HFS volume to local filesystem as MacBinary."""
    return run(["hcopy", "-m", hfs_src, local_dest]).returncode == 0


def extract_macbinary_data_fork(bin_path, out_path):
    """Extract the data fork from a MacBinary file."""
    with open(bin_path, "rb") as f:
        header = f.read(128)
        data_len = struct.unpack(">I", header[83:87])[0]
        data = f.read(data_len)
    with open(out_path, "wb") as f:
        f.write(data)
    return data_len


def parse_appledouble(data):
    """Parse an AppleDouble file, returning (rsrc_fork, finder_info) or None.

    unar writes .rsrc companion files in AppleDouble format (magic 0x00051607).
    We need to extract the raw resource fork data and Finder info from it.
    """
    if len(data) < 26 or data[:4] != b"\x00\x05\x16\x07":
        return None  # Not AppleDouble

    num_entries = struct.unpack(">H", data[24:26])[0]
    rsrc_fork = None
    finder_info = None

    for i in range(num_entries):
        off = 26 + i * 12
        entry_id = struct.unpack(">I", data[off:off + 4])[0]
        entry_off = struct.unpack(">I", data[off + 4:off + 8])[0]
        entry_len = struct.unpack(">I", data[off + 8:off + 12])[0]
        if entry_id == 2:  # Resource fork
            rsrc_fork = data[entry_off:entry_off + entry_len]
        elif entry_id == 9:  # Finder info (FInfo + FXInfo = 32 bytes)
            finder_info = data[entry_off:entry_off + entry_len]

    return rsrc_fork, finder_info


def build_macbinary(name_bytes, file_type, file_creator, data_fork, rsrc_fork,
                    create_date=0, modify_date=0, finder_flags=0):
    """Build a MacBinary II file from components.

    Args:
        name_bytes: Filename as bytes (Mac Roman, max 63 chars)
        file_type: 4-byte file type (e.g. b'APPL')
        file_creator: 4-byte creator code (e.g. b'RSED')
        data_fork: Data fork contents as bytes
        rsrc_fork: Resource fork contents as bytes
        create_date: Mac timestamp (seconds since 1904-01-01)
        modify_date: Mac timestamp
        finder_flags: 16-bit Finder flags (hasBundle, inited, etc.)
    Returns:
        Complete MacBinary II file as bytes
    """
    name_bytes = name_bytes[:63]
    name_len = len(name_bytes)
    data_len = len(data_fork)
    rsrc_len = len(rsrc_fork)

    # MacBinary II header (128 bytes)
    header = bytearray(128)
    header[0] = 0  # old version
    header[1] = name_len
    header[2:2 + name_len] = name_bytes
    header[65:69] = file_type[:4]
    header[69:73] = file_creator[:4]
    header[73] = (finder_flags >> 8) & 0xFF  # Finder flags (high byte)
    # bytes 74: zero
    # bytes 75-76: vertical position
    # bytes 77-78: horizontal position
    # bytes 79-80: window/folder ID
    # byte 81: protected flag
    # byte 82: zero
    struct.pack_into(">I", header, 83, data_len)
    struct.pack_into(">I", header, 87, rsrc_len)
    struct.pack_into(">I", header, 91, create_date)
    struct.pack_into(">I", header, 95, modify_date)
    # bytes 99-100: Get Info comment length
    # byte 101: Finder flags (low byte)
    header[101] = finder_flags & 0xFF
    # bytes 102-115: reserved
    # bytes 116-119: unpacked total length (optional)
    # bytes 120-121: secondary header length
    header[122] = 130  # MacBinary II version (0x82)
    header[123] = 129  # minimum version to read (0x81)

    # CRC-16 of header bytes 0-123
    crc = _macbinary_crc(header[:124])
    struct.pack_into(">H", header, 124, crc)

    # Assemble: header + data fork (padded) + resource fork (padded)
    def pad128(data):
        remainder = len(data) % 128
        if remainder:
            return data + b"\x00" * (128 - remainder)
        return data

    result = bytes(header)
    if data_len > 0:
        result += pad128(data_fork)
    if rsrc_len > 0:
        result += pad128(rsrc_fork)
    return result


def _macbinary_crc(data):
    """Calculate CRC-16/XMODEM for MacBinary II header."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def _fourcc_bytes(val):
    """Convert a 32-bit integer FourCC to 4 bytes."""
    if val == 0:
        return b"    "
    return struct.pack(">I", val)


def _mac_timestamp(iso_str):
    """Convert an ISO date string to Mac timestamp (seconds since 1904-01-01).

    Returns 0 on failure.
    """
    if not iso_str:
        return 0
    try:
        # lsar format: "1996-06-20 20:34:41 -0700"
        # Parse just the datetime part, ignore timezone for simplicity
        dt_str = iso_str.rsplit(" ", 1)[0] if "+" in iso_str or "-" in iso_str[11:] else iso_str
        t = time.strptime(dt_str, "%Y-%m-%d %H:%M:%S")
        unix_ts = int(time.mktime(t))
        # Mac epoch is 1904-01-01, Unix is 1970-01-01 = 2082844800 seconds apart
        return unix_ts + 2082844800
    except (ValueError, OverflowError):
        return 0


def handle_stuffit(sit_path, image_path, folder_name):
    """Extract a StuffIt archive and copy contents onto the HFS image.

    Uses lsar to get file metadata (type, creator), unar to extract
    data forks and resource forks, then reassembles them into MacBinary
    files that hcopy -m can properly write to HFS with both forks intact.
    """
    print(f"  StuffIt: {os.path.basename(sit_path)}")

    # Get metadata from archive
    result = subprocess.run(
        ["lsar", "-json", sit_path], capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"  WARNING: lsar failed for {sit_path}", file=sys.stderr)
        return False

    archive_info = json.loads(result.stdout)
    entries = archive_info.get("lsarContents", [])

    # Build metadata index: filename -> {type, creator, create_date, modify_date}
    # Also track which files have resource forks
    file_meta = {}  # filename -> {type, creator, ...}
    has_rsrc = set()  # filenames that have resource fork entries
    has_data = set()

    for entry in entries:
        name = entry.get("XADFileName", "")
        if entry.get("XADIsDirectory"):
            continue
        is_rsrc = entry.get("XADIsResourceFork", 0)
        if is_rsrc:
            has_rsrc.add(name)
        else:
            has_data.add(name)

        if name not in file_meta:
            file_meta[name] = {
                "type": _fourcc_bytes(entry.get("XADFileType", 0)),
                "creator": _fourcc_bytes(entry.get("XADFileCreator", 0)),
                "create_date": _mac_timestamp(entry.get("XADCreationDate", "")),
                "modify_date": _mac_timestamp(entry.get("XADLastModificationDate", "")),
            }

    with tempfile.TemporaryDirectory(prefix="sit_extract_") as tmpdir:
        # Extract with unar (produces data fork + .rsrc companion files)
        result = run(["unar", "-o", tmpdir, "-f", sit_path])
        if result.returncode != 0:
            print(f"  WARNING: unar failed for {sit_path}", file=sys.stderr)
            return False

        if not hmount_image(image_path):
            return False

        hmkdir(f":{folder_name}")

        # Process each unique filename (not .rsrc companions)
        all_names = sorted(has_data | has_rsrc)
        created_dirs = set()

        for name in all_names:
            meta = file_meta.get(name, {})
            ftype = meta.get("type", b"    ")
            fcreator = meta.get("creator", b"    ")
            create_date = meta.get("create_date", 0)
            modify_date = meta.get("modify_date", 0)

            # Find extracted files on disk
            data_path = os.path.join(tmpdir, name)
            rsrc_path = os.path.join(tmpdir, name + ".rsrc")

            data_fork = b""
            rsrc_fork = b""
            finder_flags = 0

            if os.path.isfile(data_path):
                with open(data_path, "rb") as f:
                    data_fork = f.read()
            if os.path.isfile(rsrc_path):
                with open(rsrc_path, "rb") as f:
                    rsrc_raw = f.read()
                # unar writes .rsrc files in AppleDouble format — unwrap it
                ad = parse_appledouble(rsrc_raw)
                if ad is not None:
                    rsrc_fork = ad[0] or b""
                    if ad[1] and len(ad[1]) >= 8:
                        # FInfo: type(4) + creator(4) + flags(2) ...
                        # type/creator already from lsar; grab Finder flags
                        finder_flags = struct.unpack(">H", ad[1][8:10])[0]
                else:
                    rsrc_fork = rsrc_raw

            if not data_fork and not rsrc_fork:
                continue

            # Get just the filename part for the MacBinary name
            basename = os.path.basename(name)
            # Encode to Mac Roman for the MacBinary header
            name_bytes = basename.encode("mac_roman", errors="replace")

            # Build MacBinary
            macbin = build_macbinary(
                name_bytes, ftype, fcreator, data_fork, rsrc_fork,
                create_date, modify_date, finder_flags
            )

            # Write temp MacBinary file
            macbin_path = os.path.join(tmpdir, f"_macbin_{basename}.bin")
            with open(macbin_path, "wb") as f:
                f.write(macbin)

            # Create HFS directory structure
            # Encode path components to Mac Roman for hfsutils
            parts = name.split("/")
            parts_mr = [p.encode("mac_roman", errors="replace") for p in parts]
            folder_mr = folder_name.encode("mac_roman", errors="replace")
            if len(parts_mr) > 1:
                for i in range(len(parts_mr) - 1):
                    subdir = b":".join(parts_mr[: i + 1])
                    dir_key = b":" + folder_mr + b":" + subdir
                    if dir_key not in created_dirs:
                        hmkdir(dir_key)
                        created_dirs.add(dir_key)

            hfs_dest = b":" + folder_mr + b":" + b":".join(parts_mr)
            success = hcopy_macbinary_in(macbin_path, hfs_dest)
            fork_info = []
            if data_fork:
                fork_info.append(f"data:{len(data_fork)}")
            if rsrc_fork:
                fork_info.append(f"rsrc:{len(rsrc_fork)}")
            status = "ok" if success else "FAILED"
            print(f"    {status:6s} {name} ({', '.join(fork_info)})")

        humount_image()
        return True


def handle_dsk(dsk_path, image_path, folder_name):
    """Copy files from an HFS disk image onto the target HFS image.

    Mounts the source .dsk, copies each file out as MacBinary (preserving
    resource forks), then copies them into the target image.
    """
    print(f"  Disk image: {os.path.basename(dsk_path)}")

    with tempfile.TemporaryDirectory(prefix="dsk_extract_") as tmpdir:
        if not hmount_image(dsk_path):
            return False

        # Get raw byte filenames to avoid Mac Roman -> UTF-8 expansion
        files = hls_raw()
        macbinary_files = []
        for fname_bytes in files:
            fname_bytes = fname_bytes.strip()
            if not fname_bytes:
                continue
            fname_display = fname_bytes.decode("mac_roman", errors="replace")
            if fname_display in ("Desktop", "Icon\r", "Icon"):
                continue

            # Local filename: sanitize for Linux filesystem
            local_name = fname_display.replace("/", "_").replace(":", "_")
            local_path = os.path.join(tmpdir, local_name + ".bin")

            # Pass raw bytes as HFS path to avoid UTF-8 re-encoding
            hfs_src = b":" + fname_bytes
            result = run(["hcopy", "-m", hfs_src, local_path])
            if result.returncode == 0 and os.path.exists(local_path):
                macbinary_files.append((fname_bytes, fname_display, local_path))
            else:
                # Might be a directory
                sub = hls(f":{fname_display}")
                if sub:
                    print(f"    (skipping directory: {fname_display})")

        humount_image()

        if not macbinary_files:
            print(f"  WARNING: no copyable files from {dsk_path}", file=sys.stderr)
            return False

        if not hmount_image(image_path):
            return False

        hmkdir(f":{folder_name}")
        for fname_bytes, fname_display, local_path in macbinary_files:
            # Use raw bytes for the HFS destination path
            hfs_dest = f":{folder_name}:".encode("mac_roman") + fname_bytes
            hcopy_macbinary_in(local_path, hfs_dest)
            print(f"    + {fname_display}")

        humount_image()
        return True


def handle_macbinary_disk_image(bin_path, image_path, folder_name):
    """Handle a MacBinary-wrapped disk image (like MPW-PR.img_.bin).

    1. Copy the .bin file onto the HFS image (Disk Copy can open it inside Mac)
    2. Extract the raw .img into the same host-side source directory
    """
    print(f"  MacBinary disk image: {os.path.basename(bin_path)}")

    # Extract raw disk image into the same directory as the source .bin
    source_dir = os.path.dirname(bin_path)
    raw_name = os.path.basename(bin_path).replace(".img_.bin", ".img")
    raw_path = os.path.join(source_dir, raw_name)

    data_len = extract_macbinary_data_fork(bin_path, raw_path)
    print(f"    Extracted raw image: {raw_path} ({data_len / (1024 * 1024):.1f}MB)")

    # Copy the MacBinary file onto the HFS image for use with Disk Copy
    if not hmount_image(image_path):
        return False

    hmkdir(f":{folder_name}")
    basename = os.path.basename(bin_path).replace(".img_.bin", "")
    success = hcopy_macbinary_in(bin_path, f":{folder_name}:{basename}")
    if success:
        print(f"    + {basename} (disk image, open with Disk Copy)")

    humount_image()
    return success


# Files to process: (relative_path, handler, hfs_folder_name)
SOURCE_FILES = [
    ("MPW/MPW-PR.img_.bin", handle_macbinary_disk_image, "MPW"),
    ("ResEdit/ResEdit3.0.sit", handle_stuffit, "ResEdit"),
    ("Disk Copy/Disk_Copy_(v6.3.3).sit", handle_stuffit, "Disk Copy"),
    ("StuffIt Expander/Stuffit_Expander_5.5.dsk", handle_dsk, "StuffIt Expander"),
    ("MacTCP_Ping_2.0.2.sea.bin", handle_stuffit, "MacTCP Ping"),
]


def main():
    parser = argparse.ArgumentParser(
        description="Populate an HFS disk image with Mac installer files."
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

    # Check required tools
    for tool in ["hmount", "hcopy", "humount", "hls", "hmkdir"]:
        if not shutil.which(tool):
            print(f"Error: {tool} not found. Install hfsutils:", file=sys.stderr)
            print("  sudo apt install hfsutils", file=sys.stderr)
            sys.exit(1)

    for tool in ["unar", "lsar"]:
        if not shutil.which(tool):
            print(f"Error: {tool} not found. Install unar:", file=sys.stderr)
            print("  sudo apt install unar", file=sys.stderr)
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
