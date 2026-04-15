#!/usr/bin/env python3
"""Populate an HFS disk image from everything in ~/storage/installers.

Recursively walks the source tree and dispatches each file by extension:
  .dsk                  -> copy contents of HFS disk image
  .sit/.sea/.hqx/.bin   -> extract archive (unar) and rebuild MacBinary with forks
  .img_.bin             -> MacBinary-wrapped disk image (raw extract + MacBinary copy)
  .img/.iso/.toast/...  -> raw data fork copy
  everything else       -> skipped with notice

Destination layout on the HFS image mirrors the source directory structure.
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time


# ----- hfsutils wrappers ---------------------------------------------------


def run(cmd, **kwargs):
    result = subprocess.run(cmd, capture_output=True, **kwargs)
    if result.returncode != 0:
        stderr = result.stderr.decode("mac_roman", errors="replace") if isinstance(result.stderr, bytes) else (result.stderr or "")
        cmd_str = " ".join(a if isinstance(a, str) else a.decode("mac_roman", errors="replace") for a in cmd)
        print(f"  FAILED: {cmd_str}", file=sys.stderr)
        if stderr.strip():
            print(f"  stderr: {stderr.strip()}", file=sys.stderr)
    return result


def run_text(cmd):
    result = subprocess.run(cmd, capture_output=True)
    stdout = result.stdout.decode("mac_roman", errors="replace") if result.stdout else ""
    stderr = result.stderr.decode("mac_roman", errors="replace") if result.stderr else ""
    return result.returncode, stdout, stderr


def hls_raw():
    result = subprocess.run(["hls"], capture_output=True)
    if result.returncode == 0 and result.stdout.strip():
        return [line for line in result.stdout.strip().split(b"\n")]
    return []


def hls(path=None):
    cmd = ["hls"] + ([path] if path else [])
    rc, stdout, _ = run_text(cmd)
    if rc == 0 and stdout.strip():
        return stdout.strip().split("\n")
    return []


def hmount_image(image_path):
    rc, _, _ = run_text(["hmount", image_path])
    return rc == 0


def humount_image():
    run_text(["humount"])


def hmkdir(path):
    if isinstance(path, str):
        path = path.encode("mac_roman", errors="replace")
    rc, _, _ = run_text(["hmkdir", path])
    return rc == 0


def hmkdir_p(hfs_path):
    """Create each component of a colon-delimited HFS path (bytes or str)."""
    if isinstance(hfs_path, bytes):
        parts = [p for p in hfs_path.split(b":") if p]
        sep = b":"
        prefix = b":"
    else:
        parts = [p for p in hfs_path.split(":") if p]
        sep = ":"
        prefix = ":"
    cur = prefix
    for p in parts:
        cur = cur + p + sep
        hmkdir(cur.rstrip(sep) if isinstance(cur, str) else cur.rstrip(b":"))


def hcopy_macbinary_in(src_path, hfs_dest):
    if isinstance(hfs_dest, str):
        hfs_dest = hfs_dest.encode("mac_roman", errors="replace")
    return run(["hcopy", "-m", src_path, hfs_dest]).returncode == 0


def hcopy_raw_in(src_path, hfs_dest):
    if isinstance(hfs_dest, str):
        hfs_dest = hfs_dest.encode("mac_roman", errors="replace")
    return run(["hcopy", "-r", src_path, hfs_dest]).returncode == 0


# ----- MacBinary helpers ---------------------------------------------------


def extract_macbinary_data_fork(bin_path, out_path):
    with open(bin_path, "rb") as f:
        header = f.read(128)
        data_len = struct.unpack(">I", header[83:87])[0]
        data = f.read(data_len)
    with open(out_path, "wb") as f:
        f.write(data)
    return data_len


def parse_appledouble(data):
    if len(data) < 26 or data[:4] != b"\x00\x05\x16\x07":
        return None
    num_entries = struct.unpack(">H", data[24:26])[0]
    rsrc_fork = None
    finder_info = None
    for i in range(num_entries):
        off = 26 + i * 12
        entry_id = struct.unpack(">I", data[off:off + 4])[0]
        entry_off = struct.unpack(">I", data[off + 4:off + 8])[0]
        entry_len = struct.unpack(">I", data[off + 8:off + 12])[0]
        if entry_id == 2:
            rsrc_fork = data[entry_off:entry_off + entry_len]
        elif entry_id == 9:
            finder_info = data[entry_off:entry_off + entry_len]
    return rsrc_fork, finder_info


def _macbinary_crc(data):
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


def build_macbinary(name_bytes, file_type, file_creator, data_fork, rsrc_fork,
                    create_date=0, modify_date=0, finder_flags=0):
    name_bytes = name_bytes[:63]
    name_len = len(name_bytes)
    data_len = len(data_fork)
    rsrc_len = len(rsrc_fork)
    header = bytearray(128)
    header[1] = name_len
    header[2:2 + name_len] = name_bytes
    header[65:69] = file_type[:4]
    header[69:73] = file_creator[:4]
    header[73] = (finder_flags >> 8) & 0xFF
    struct.pack_into(">I", header, 83, data_len)
    struct.pack_into(">I", header, 87, rsrc_len)
    struct.pack_into(">I", header, 91, create_date)
    struct.pack_into(">I", header, 95, modify_date)
    header[101] = finder_flags & 0xFF
    header[122] = 130
    header[123] = 129
    crc = _macbinary_crc(header[:124])
    struct.pack_into(">H", header, 124, crc)

    def pad128(d):
        rem = len(d) % 128
        return d + b"\x00" * (128 - rem) if rem else d

    result = bytes(header)
    if data_len:
        result += pad128(data_fork)
    if rsrc_len:
        result += pad128(rsrc_fork)
    return result


def _fourcc_bytes(val):
    if val == 0:
        return b"    "
    return struct.pack(">I", val)


def _mac_timestamp(iso_str):
    if not iso_str:
        return 0
    try:
        dt_str = iso_str.rsplit(" ", 1)[0] if "+" in iso_str or "-" in iso_str[11:] else iso_str
        t = time.strptime(dt_str, "%Y-%m-%d %H:%M:%S")
        return int(time.mktime(t)) + 2082844800
    except (ValueError, OverflowError):
        return 0


# ----- Handlers ------------------------------------------------------------


def handle_stuffit(src_path, image_path, hfs_parent, folder_name):
    """Extract an archive (lsar/unar) and copy with forks preserved."""
    print(f"  Archive: {os.path.basename(src_path)}")

    result = subprocess.run(["lsar", "-json", src_path], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  WARNING: lsar failed for {src_path}", file=sys.stderr)
        return False

    clean_json = re.sub(r'[\x00-\x08\x0b\x0c\x0e-\x1f]', '', result.stdout)
    try:
        archive_info = json.loads(clean_json)
    except json.JSONDecodeError as e:
        print(f"  WARNING: lsar json parse failed for {src_path}: {e}", file=sys.stderr)
        return False
    entries = archive_info.get("lsarContents", [])

    file_meta = {}
    has_rsrc = set()
    has_data = set()
    for entry in entries:
        name = entry.get("XADFileName", "")
        if entry.get("XADIsDirectory"):
            continue
        if entry.get("XADIsResourceFork", 0):
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

    with tempfile.TemporaryDirectory(prefix="ar_extract_") as tmpdir:
        if run(["unar", "-o", tmpdir, "-f", src_path]).returncode != 0:
            print(f"  WARNING: unar failed for {src_path}", file=sys.stderr)
            return False

        if not hmount_image(image_path):
            return False
        hmkdir_p(f":{hfs_parent}:{folder_name}" if hfs_parent else f":{folder_name}")

        base_hfs = (f":{hfs_parent}:{folder_name}" if hfs_parent else f":{folder_name}").encode("mac_roman", errors="replace")
        created_dirs = set()

        for name in sorted(has_data | has_rsrc):
            meta = file_meta.get(name, {})
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
                ad = parse_appledouble(rsrc_raw)
                if ad is not None:
                    rsrc_fork = ad[0] or b""
                    if ad[1] and len(ad[1]) >= 10:
                        finder_flags = struct.unpack(">H", ad[1][8:10])[0]
                else:
                    rsrc_fork = rsrc_raw
            if not data_fork and not rsrc_fork:
                continue

            basename = os.path.basename(name)
            name_bytes = basename.encode("mac_roman", errors="replace")
            macbin = build_macbinary(
                name_bytes, meta.get("type", b"    "), meta.get("creator", b"    "),
                data_fork, rsrc_fork,
                meta.get("create_date", 0), meta.get("modify_date", 0), finder_flags,
            )
            macbin_path = os.path.join(tmpdir, f"_macbin_{basename}.bin")
            with open(macbin_path, "wb") as f:
                f.write(macbin)

            parts = name.split("/")
            parts_mr = [p.encode("mac_roman", errors="replace") for p in parts]
            if len(parts_mr) > 1:
                for i in range(len(parts_mr) - 1):
                    subdir = b":".join(parts_mr[: i + 1])
                    dir_key = base_hfs + b":" + subdir
                    if dir_key not in created_dirs:
                        hmkdir(dir_key)
                        created_dirs.add(dir_key)

            hfs_dest = base_hfs + b":" + b":".join(parts_mr)
            success = hcopy_macbinary_in(macbin_path, hfs_dest)
            forks = []
            if data_fork: forks.append(f"data:{len(data_fork)}")
            if rsrc_fork: forks.append(f"rsrc:{len(rsrc_fork)}")
            print(f"    {'ok' if success else 'FAILED':6s} {name} ({', '.join(forks)})")

        humount_image()
        return True


def handle_dsk(src_path, image_path, hfs_parent, folder_name):
    """Copy all files from an HFS .dsk image onto the target."""
    print(f"  Disk image: {os.path.basename(src_path)}")
    with tempfile.TemporaryDirectory(prefix="dsk_extract_") as tmpdir:
        if not hmount_image(src_path):
            return False
        macbin_files = []
        for fb in hls_raw():
            fb = fb.strip()
            if not fb:
                continue
            disp = fb.decode("mac_roman", errors="replace")
            if disp in ("Desktop", "Icon\r", "Icon"):
                continue
            local = os.path.join(tmpdir, disp.replace("/", "_").replace(":", "_") + ".bin")
            if run(["hcopy", "-m", b":" + fb, local]).returncode == 0 and os.path.exists(local):
                macbin_files.append((fb, disp, local))
        humount_image()

        if not macbin_files:
            print(f"  WARNING: no copyable files from {src_path}", file=sys.stderr)
            return False

        if not hmount_image(image_path):
            return False
        hmkdir_p(f":{hfs_parent}:{folder_name}" if hfs_parent else f":{folder_name}")
        base_prefix = (f":{hfs_parent}:{folder_name}:" if hfs_parent else f":{folder_name}:").encode("mac_roman", errors="replace")
        for fb, disp, local in macbin_files:
            hcopy_macbinary_in(local, base_prefix + fb)
            print(f"    + {disp}")
        humount_image()
        return True


def handle_raw_file(src_path, image_path, hfs_parent, filename):
    """Copy raw file (disk image, etc.) with no fork handling."""
    print(f"  Raw file: {os.path.basename(src_path)}")
    if not hmount_image(image_path):
        return False
    if hfs_parent:
        hmkdir_p(f":{hfs_parent}")
        hfs_dest = f":{hfs_parent}:{filename}"
    else:
        hfs_dest = f":{filename}"
    success = hcopy_raw_in(src_path, hfs_dest)
    size_mb = os.path.getsize(src_path) / (1024 * 1024)
    print(f"    {'+ ' if success else 'FAILED '}{filename} ({size_mb:.1f}MB)")
    humount_image()
    return success


def handle_macbinary_disk_image(src_path, image_path, hfs_parent, folder_name):
    """MacBinary-wrapped disk image: copy .bin onto HFS, also extract raw .img alongside source."""
    print(f"  MacBinary disk image: {os.path.basename(src_path)}")
    source_dir = os.path.dirname(src_path)
    raw_name = os.path.basename(src_path).replace(".img_.bin", ".img")
    raw_path = os.path.join(source_dir, raw_name)
    data_len = extract_macbinary_data_fork(src_path, raw_path)
    print(f"    Extracted raw image: {raw_path} ({data_len / (1024 * 1024):.1f}MB)")

    if not hmount_image(image_path):
        return False
    hmkdir_p(f":{hfs_parent}:{folder_name}" if hfs_parent else f":{folder_name}")
    basename = os.path.basename(src_path).replace(".img_.bin", "")
    dest = (f":{hfs_parent}:{folder_name}:{basename}" if hfs_parent else f":{folder_name}:{basename}")
    success = hcopy_macbinary_in(src_path, dest)
    if success:
        print(f"    + {basename} (open with Disk Copy)")
    humount_image()
    return success


# ----- Dispatch ------------------------------------------------------------

ARCHIVE_EXTS = {".sit", ".sea", ".hqx", ".cpt"}
RAW_EXTS = {".img", ".iso", ".toast", ".cdr", ".smi"}


def dispatch(src_path):
    """Return (handler, folder_or_filename) or (None, None) to skip."""
    name = os.path.basename(src_path)
    lower = name.lower()
    stem, ext = os.path.splitext(name)
    ext_l = ext.lower()

    if lower.endswith(".img_.bin"):
        return handle_macbinary_disk_image, stem.replace(".img_", "")
    if ext_l == ".dsk":
        return handle_dsk, stem
    if ext_l in ARCHIVE_EXTS:
        return handle_stuffit, stem
    if ext_l == ".bin":
        # Generic MacBinary archive — lsar/unar handle these.
        return handle_stuffit, stem
    if ext_l in RAW_EXTS:
        return handle_raw_file, name
    return None, None


def main():
    parser = argparse.ArgumentParser(
        description="Populate an HFS disk image from ~/storage/installers."
    )
    parser.add_argument("-i", "--image", required=True, help="Target HFS image")
    parser.add_argument(
        "-s", "--source-dir",
        default=os.path.expanduser("~/storage/installers"),
        help="Source directory (default: ~/storage/installers)",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    image = os.path.abspath(args.image)
    if not os.path.exists(image):
        print(f"Error: image {image} does not exist", file=sys.stderr)
        sys.exit(1)

    for tool in ["hmount", "hcopy", "humount", "hls", "hmkdir", "unar", "lsar"]:
        if not shutil.which(tool):
            print(f"Error: {tool} not found (apt install hfsutils unar)", file=sys.stderr)
            sys.exit(1)

    source_dir = os.path.abspath(args.source_dir)
    if not os.path.isdir(source_dir):
        print(f"Error: {source_dir} does not exist", file=sys.stderr)
        sys.exit(1)

    to_process = []
    for root, dirs, files in os.walk(source_dir):
        dirs.sort()
        for fname in sorted(files):
            if fname.startswith("."):
                continue
            fpath = os.path.join(root, fname)
            handler, folder = dispatch(fpath)
            if handler is None:
                continue
            rel_dir = os.path.relpath(root, source_dir)
            hfs_parent = "" if rel_dir == "." else rel_dir.replace("/", ":")
            to_process.append((fpath, handler, hfs_parent, folder))

    if not to_process:
        print("No files to process.")
        return

    print(f"Target image: {image}")
    print(f"Source dir:   {source_dir}")
    print(f"Files to process: {len(to_process)}")
    print()

    if args.dry_run:
        for fpath, handler, hfs_parent, folder in to_process:
            size_mb = os.path.getsize(fpath) / (1024 * 1024)
            loc = f":{hfs_parent}:{folder}" if hfs_parent else f":{folder}"
            print(f"  {os.path.relpath(fpath, source_dir)} ({size_mb:.1f}MB) -> {loc} [{handler.__name__}]")
        return

    successes = 0
    failures = 0
    for fpath, handler, hfs_parent, folder in to_process:
        rel = os.path.relpath(fpath, source_dir)
        print(f"\nProcessing: {rel}")
        try:
            if handler(fpath, image, hfs_parent, folder):
                successes += 1
            else:
                failures += 1
        except Exception as e:
            print(f"  ERROR: {e}", file=sys.stderr)
            failures += 1

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
