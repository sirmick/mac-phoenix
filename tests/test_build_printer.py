#!/usr/bin/env python3
"""test_build_printer.py -- build.py's "printer": the System file's STR -8192
and 'alis' -8192 as a 7.6.1 Quadra's Chooser writes them when the ImageWriter
is chosen (the record below is the Quadra's, its first 234 bytes: the
alias's own length), and the setting inherited from a built base but not part
of the base's content identity (bakes keep their keys). No emulator."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "provisioning"))
import build  # noqa: E402

failures = []


def expect(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


# The Quadra's System file after Chooser > ImageWriter on "Macintosh HD"
# (created $E6FB45AE; Extensions folder 19, System Folder 18, the driver's file ID)
QUADRA_STR = bytes.fromhex("0b496d616765577269746572") + bytes(20)
QUADRA_ALIS = bytes.fromhex(
    "0000000000ea000200000c4d6163696e746f7368204844000000000000000000000000000000e6fb45ae424400000000"
    "00130b496d61676557726974657200000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000105a5da6c4050524553495752540001000200000000000000000000"
    "0000000000000000000a457874656e73696f6e73000100080000001300000012000200314d6163696e746f7368204844"
    "3a53797374656d20466f6c6465723a457874656e73696f6e733a496d61676557726974657200ffff0000")
CNID = 261

string, alias = build.chooser_resources("ImageWriter", "Macintosh HD", 0xE6FB45AE, [19, 18], CNID)
expect(string == QUADRA_STR, "STR -8192: the driver's name, padded to 32 bytes")
expect(alias == QUADRA_ALIS, "'alis' -8192: the Quadra's record, byte for byte")
_, other = build.chooser_resources("ImageWriter", "Other Disk", 1, [30, 20], 40)
expect(other[10:21] == b"\x0aOther Disk" and other.endswith(b"Other Disk:System Folder:Extensions:ImageWriter\x00\xff\xff\x00\x00"),
       "another volume: its name in the record and the path")

d = build.load_def("7.6.1")
e = {k: v for k, v in d.items() if k != "printer"}
expect(d.get("printer") == "ImageWriter", "7.6.1 chooses the ImageWriter")
expect(build.printer_of(build.load_def("productivity-7.6.1")) == "ImageWriter",
       "productivity-7.6.1 (on standard-7.6.1, built from 7.6.1) inherits it")
try:
    same = build.content_identity(d) == build.content_identity(e)
    expect(same, "the printer isn't part of 7.6.1's content identity")
except SystemExit as x:                 # snapshots not on this box
    print(f"skip content identity: {x}")

sys.exit(1 if failures else 0)
