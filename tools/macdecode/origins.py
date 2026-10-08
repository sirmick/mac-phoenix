"""Where did this RAM come from? Match RAM against resources on disk.

The patch loader copies `lpch`/`ptch` code into the System heap and
releases the resources, and INITs load their code the same way, so most
patched trap entries point into anonymous blocks. Matching the bytes
against the boot disk's resources names them: `System 'lpch' 4 +1a2c`.

Method: index every even offset of the searched RAM by its first 8 bytes
(numpy), look up 4-byte-spaced chunks of each resource, confirm over 16
bytes, then merge hits with the same (resource, RAM-offset delta) into
runs. Bytes the loader relocated don't match, so small gaps inside a run
are bridged.
"""
import bisect
import re

import numpy as np

CHUNK = 16          # bytes confirmed per hit
STRIDE = 4          # resource chunk spacing
GAP = 512           # bridge unmatched bytes (relocations) inside a run
SKIP_TYPES = {"ICN#", "icl4", "icl8", "ics#", "ics4", "ics8", "ICON", "cicn", "PICT", "snd ",
              "FONT", "NFNT", "sfnt", "FOND", "STR ", "STR#", "TEXT", "styl", "vers", "hfdr",
              "hmnu", "hdlg", "hrct", "hwin", "DITL", "DLOG", "ALRT", "WIND", "MENU", "CNTL",
              "BNDL", "FREF", "icns", "PAT ", "PAT#", "ppat", "crsr", "CURS", "clut", "KCHR",
              "itl0", "itl1", "itl2", "itl4", "itlb", "itlc", "itlk", "kcs#", "kcs4", "kcs8",
              "fbnd", "finf", "ttin", "ttis", "mcky", "pltt", "wctb", "cctb", "dctb", "actb",
              "fctb", "ictb", "mctb", "tlst", "flst", "card"}


# Opcodes followed by a 32-bit absolute address or immediate pointer:
# JMP/JSR abs.L, LEA abs.L,An, PEA abs.L, MOVEA.L #imm,An, MOVE.L #imm,-(SP).
ABS_LONG_OPS = {0x4EF9, 0x4EB9, 0x4879, 0x2F3C} | {0x41F9 + (r << 9) for r in range(8)} | \
    {0x207C + (r << 9) for r in range(8)}


def _keys(region):
    """8-byte big-endian keys at every even offset of region."""
    n = (len(region) - 8) // 2 + 1
    out = np.empty(n, dtype=np.uint64)
    for phase in range(4):  # offsets 0,2,4,6 mod 8
        start = 2 * phase
        usable = (len(region) - start) // 8 * 8
        if usable <= 0:
            continue
        v = np.frombuffer(region[start:start + usable], dtype=">u8").astype(np.uint64)
        out[phase::4][:len(v)] = v[:len(out[phase::4])]
    return out


class Origins:
    def __init__(self, ram, ranges, files, is_address=None):
        """ranges: [(start, end)] of RAM to search; files: [(name, resources)].

        is_address(v) says whether a 32-bit RAM value looks like a pointer
        the loader wrote (ROM or RAM address); the masked fallback treats
        such fields as wildcards.
        """
        self.ram, self.files = ram, files
        self.is_address = is_address or (lambda v: False)
        self._masked = {}
        self.runs = []                      # (start, end, file, type, id, res_offset_at_start)
        for start, end in ranges:
            self._search(ram, start, end, files)
        self.runs.sort()
        self.starts = [r[0] for r in self.runs]

    def _search(self, ram, base, end, files):
        region = ram[base:end]
        if len(region) < CHUNK:
            return
        keys = _keys(region)
        order = np.argsort(keys, kind="stable")
        skeys = keys[order]
        hits = {}                            # (file,type,id,delta) -> [ram_addr...]
        for fname, res in files:
            for (rtype, rid), (_, data) in res.items():
                if rtype in SKIP_TYPES or len(data) < CHUNK:
                    continue
                offs = range(0, len(data) - CHUNK + 1, STRIDE)
                chunks = [data[o:o + 8] for o in offs]
                ck = np.frombuffer(b"".join(chunks), dtype=">u8").astype(np.uint64)
                lo = np.searchsorted(skeys, ck, "left")
                hi = np.searchsorted(skeys, ck, "right")
                for o, l, h in zip(offs, lo, hi):
                    if h - l == 0 or h - l > 8:      # absent, or too common to mean anything
                        continue
                    piece = data[o:o + CHUNK]
                    if len(set(piece)) < 6:
                        continue
                    for idx in order[l:h]:
                        p = int(idx) * 2
                        if region[p:p + CHUNK] == piece:
                            hits.setdefault((fname, rtype, rid, base + p - o), []).append(base + p)
        for (fname, rtype, rid, delta), addrs in hits.items():
            addrs.sort()
            run_start = prev = addrs[0]
            for a in addrs[1:] + [None]:
                if a is not None and a - prev <= GAP:
                    prev = a
                    continue
                if prev + CHUNK - run_start >= 2 * CHUNK or len(addrs) == 1:
                    self.runs.append((run_start, prev + CHUNK, fname, rtype, rid, run_start - delta))
                if a is not None:
                    run_start = prev = a

    def at(self, a):
        """Best run covering address a (longest wins), or None."""
        i = bisect.bisect_right(self.starts, a)
        best = None
        for r in self.runs[max(0, i - 64):i]:
            if r[0] <= a < r[1] and (best is None or r[1] - r[0] > best[1] - best[0]):
                best = r
        return best

    def masked(self, a, window=64):
        """Fallback for code the loader relocated or compacted: match the
        RAM at a against every code resource, with fields that hold
        addresses as wildcards. Returns (file, type, id, offset) if unique."""
        if a in self._masked:
            return self._masked[a]
        result = None
        for size in (window // 2, window, 2 * window):
            ram = self.ram[a:a + size]
            if len(ram) < size:
                break
            pat, i, fixed = b"", 0, 0
            while i < len(ram):
                op = int.from_bytes(ram[i:i + 2], "big") if i % 2 == 0 and i + 2 <= len(ram) else None
                val = int.from_bytes(ram[i:i + 4], "big") if i + 4 <= len(ram) else None
                if op is not None and i + 6 <= len(ram) and op in ABS_LONG_OPS:
                    # JMP/JSR/LEA/PEA abs.L, MOVEA.L #imm: the operand is an
                    # address the loader filled in (a link token on disk).
                    pat += re.escape(ram[i:i + 2]) + b".{4}"
                    fixed += 2
                    i += 6
                elif val is not None and self.is_address(val) and (val >= 0x100000 or not val >> 24 == 0):
                    pat += b".{4}"
                    i += 4
                elif op is not None and i + 4 <= len(ram) and (
                        op in (0x4EBA, 0x4EFA, 0x6100, 0x41FA, 0x43FA, 0x45FA, 0x47FA, 0x49FA,
                               0x4BFA, 0x4DFA, 0x487A) or (op & 0xF0FF) == 0x6000):
                    # PC-relative: JSR/JMP/LEA/PEA d16(PC), BSR.W, Bcc.W.
                    # In an 'lpch' a call to another routine is a 4-byte
                    # link token the loader rewrites into the instruction
                    # (RAM 4EBA FF82 <- resource 0009 9771), and compaction
                    # changes displacements anyway: wildcard all 4 bytes.
                    pat += b".{4}"
                    i += 4
                elif op is not None and (op & 0xFF00) == 0x6100:
                    pat += re.escape(ram[i:i + 1]) + b"."      # BSR.S
                    fixed += 1
                    i += 2
                else:
                    pat += re.escape(ram[i:i + 1])
                    fixed += 1
                    i += 1
            if fixed < max(12, size // 3):
                continue
            rx = re.compile(pat, re.DOTALL)
            found = []
            for fname, res in self.files:
                for (rtype, rid), (_, data) in res.items():
                    if rtype in SKIP_TYPES:
                        continue
                    for m in rx.finditer(data):
                        found.append((fname, rtype, rid, m.start()))
                        if len(found) > 8:
                            break
                    if len(found) > 8:
                        break
                if len(found) > 8:
                    break
            if len(found) == 1:
                result = found[0] + (1,)
                break
            if not found:
                break
            if len({f[:3] for f in found}) == 1 and len(found) <= 8:
                # Same resource, near-identical stubs: the resource is
                # certain, the offset is the first candidate.
                result = found[0] + (len(found),)
        self._masked[a] = result
        return result

    def describe(self, a):
        r = self.at(a)
        if r:
            start, end, fname, rtype, rid, roff = r
            return f"{fname} '{rtype}' {rid} +{roff + (a - start):x}"
        m = self.masked(a)
        if m:
            amb = f", 1 of {m[4]}" if m[4] > 1 else ""
            return f"{m[0]} '{m[1]}' {m[2]} +{m[3]:x} (masked match{amb})"
        return None
