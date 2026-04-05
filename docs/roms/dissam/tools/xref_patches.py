# -*- coding: utf-8 -*-
# Ghidra postScript: cross-reference src/core/rom_patches.cpp into the ROM
# listing. For every `ROMBaseHost + 0xXXXX` occurrence in the patch function(s)
# that apply to the current machine, add an EOL comment at ROM base + 0xXXXX
# pointing back to the C source file/line and the nearby comment.
#
# Also leaves a bookmark so analysts can list all patched sites from Ghidra.
#
# Invocation: -postScript xref_patches.py <machine>
# @category Mac.ROM
# @runtime Jython

import sys
import os
import re
from ghidra.program.model.symbol import SourceType

args = getScriptArgs()
if len(args) < 1:
    print("xref_patches.py: expected <machine> argument")
    sys.exit(1)

machine = args[0]
script_dir = os.path.dirname(os.path.abspath(sourceFile.getAbsolutePath()))
# tools/vendor/.. -> dissam -> docs/roms -> docs -> repo root
repo_root = os.path.normpath(os.path.join(script_dir, "..", "..", "..", ".."))
patches_file = os.path.join(repo_root, "src", "core", "rom_patches.cpp")

ROM_BASES = {
    "se":     0x00400000,
    "macii":  0x40800000,
    "maciix": 0x40800000,
    "iici":   0x40800000,
}

# Which patch functions apply to each machine (see PatchROM switch in
# rom_patches.cpp line ~2994).
MACHINE_FUNCTIONS = {
    "se":     ["patch_rom_classic"],
    "macii":  ["patch_rom_ii"],
    "maciix": ["patch_rom_ii"],  # same ROM version ($0178) — same patches
    "iici":   ["patch_rom_32", "patch_rom_iici"],
}

if machine not in ROM_BASES:
    print("xref_patches.py: unknown machine '%s'" % machine)
    sys.exit(1)

if not os.path.exists(patches_file):
    print("xref_patches.py: rom_patches.cpp not found at %s" % patches_file)
    sys.exit(1)

base = ROM_BASES[machine]
target_functions = set(MACHINE_FUNCTIONS[machine])
space = currentProgram.getAddressFactory().getDefaultAddressSpace()

# ---------------------------------------------------------------------------
# Parse rom_patches.cpp: extract every `ROMBaseHost + 0xXXXX` inside one of
# the target functions, with line number + context comment.
# ---------------------------------------------------------------------------
fn_start_re = re.compile(
    r"^\s*static\s+(?:bool|void)\s+(patch_rom_\w+)\s*\(")
addr_re = re.compile(r"ROMBaseHost\s*\+\s*0x([0-9a-fA-F]+)")

current_fn = None
brace_depth = 0
# Collected entries: list of (offset_int, cpp_lineno, context_snippet, fn_name)
xrefs = []

# Trail the last comment seen on a preceding line so we can attach it.
trailing_comment = ""

try:
    f = open(patches_file, "r")
    for lineno, raw in enumerate(f, start=1):
        line = raw.rstrip("\n")

        # Pick up leading-line comments to use as context if the patch line
        # itself has no inline comment.
        stripped = line.strip()
        if stripped.startswith("//"):
            trailing_comment = stripped[2:].strip()
        elif stripped == "":
            pass  # keep trailing_comment across blank lines
        # else: fall through; trailing_comment is still valid if used immediately

        # Function boundary tracking (very simple brace counter — works for
        # the flat style in rom_patches.cpp).
        fn_match = fn_start_re.match(line)
        if fn_match:
            current_fn = fn_match.group(1)
            brace_depth = 0
            trailing_comment = ""

        if current_fn is not None:
            brace_depth += line.count("{") - line.count("}")

        # Only record offsets inside one of the target functions.
        if current_fn in target_functions:
            for m in addr_re.finditer(line):
                try:
                    off = int(m.group(1), 16)
                except ValueError:
                    continue
                # Use inline comment if present, else the trailing comment.
                inline = ""
                cmt_pos = line.find("//")
                if cmt_pos >= 0:
                    inline = line[cmt_pos + 2:].strip()
                ctx = inline if inline else trailing_comment
                xrefs.append((off, lineno, ctx, current_fn))

        # After processing, if we're in a function and brace_depth returned
        # to 0 at a closing brace line, clear current_fn.
        if current_fn is not None and brace_depth == 0 and "}" in line:
            current_fn = None
            trailing_comment = ""
    f.close()
except Exception as e:
    print("xref_patches.py: parse error: %s" % e)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Group by offset so multiple patch lines for the same address collapse.
# ---------------------------------------------------------------------------
by_offset = {}
for off, lineno, ctx, fn_name in xrefs:
    by_offset.setdefault(off, []).append((lineno, ctx, fn_name))

# ---------------------------------------------------------------------------
# Emit Ghidra EOL comments + bookmarks.
# ---------------------------------------------------------------------------
applied = 0
skipped_oor = 0
for off, entries in by_offset.items():
    rom_addr = base + off
    try:
        addr = space.getAddress(rom_addr)
    except Exception:
        skipped_oor += 1
        continue

    # Build a multi-line comment: one line per cpp reference.
    #
    # IMPORTANT: these patches are WORK IN PROGRESS. None of the non-IIci
    # ROMs currently boot to a usable state with this patch set, and even
    # the IIci path has unresolved issues. Treat every PATCH-WIP annotation
    # as "something the emulator tried", not as verified correct behavior.
    # Revision is expected.
    lines = ["!! WIP PATCH -- does not currently boot; unverified !!"]
    for lineno, ctx, fn_name in entries:
        if ctx:
            lines.append("PATCH-WIP %s:%d (%s) -- %s" %
                         ("rom_patches.cpp", lineno, fn_name, ctx[:80]))
        else:
            lines.append("PATCH-WIP %s:%d (%s)" %
                         ("rom_patches.cpp", lineno, fn_name))
    comment = "\n".join(lines)

    # Prepend to existing EOL comment if present so we don't clobber anything.
    existing = getEOLComment(addr)
    if existing:
        if "PATCH-WIP rom_patches.cpp" in existing:
            # Already annotated on a prior run — replace.
            new_comment = comment
        else:
            new_comment = existing + "\n" + comment
    else:
        new_comment = comment

    try:
        setEOLComment(addr, new_comment)
        # Bookmark for easy listing from Ghidra UI
        createBookmark(addr, "PATCH-WIP", entries[0][2] or entries[0][2])
        applied += 1
    except Exception as e:
        print("warn: annotate 0x%08x: %s" % (rom_addr, e))

print("xref_patches.py: %s  applied=%d  skipped_oor=%d  functions=%s" %
      (machine, applied, skipped_oor, ",".join(sorted(target_functions))))
