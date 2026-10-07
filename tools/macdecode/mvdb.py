"""Multiversal as a database: names, types, sizes, lowmem addresses, traps.

Loads src/executor/multiversal/defs/*.yaml — the same files Executor's
headers are generated from — so a fix made there (a renamed lowmem
global, a placeholder turned into a real type) changes both the C++
and the decoder.

Sizes follow generator.rb plus 68k struct layout: fields wider than a
byte start on an even offset, structs round up to an even size, and an
explicit `size:` wins.
"""
import re
from pathlib import Path

import yaml

DEFAULT_DEFS = Path(__file__).resolve().parents[2] / "src/executor/multiversal/defs"

PRIMITIVES = {  # name: (size, signed)
    "uint8_t": (1, False), "uint16_t": (2, False), "uint32_t": (4, False),
    "uint64_t": (8, False), "int8_t": (1, True), "int16_t": (2, True),
    "int32_t": (4, True), "int64_t": (8, True), "char": (1, True),
    "unsigned char": (1, False), "double": (8, True), "void": (0, False),
}

KINDS = ("typedef", "struct", "union", "funptr", "enum", "lowmem",
         "function", "dispatcher", "common")

ARRAY = re.compile(r"^(.*)\[([^\[\]]*)\]$")


class Field:
    def __init__(self, name, offset, type_):
        self.name, self.offset, self.type = name, offset, type_


class DB:
    def __init__(self, defs=DEFAULT_DEFS):
        self.defs = Path(defs)
        self.mtime = defs_mtime(self.defs)
        self.typedefs, self.structs, self.funptrs, self.enums = {}, {}, set(), set()
        self.consts = {}
        self.lowmem = []          # dicts: name, type, address, comment, file, ...
        self.traps = {}           # (table, index) -> [names]
        self.trap_info = {}       # (table, index) -> [{name, file, kind, ...}]
        self._sizes = {}
        for f in sorted(self.defs.glob("*.yaml")):
            for item in yaml.safe_load(f.read_text()) or []:
                if item.get("api") == "carbon":
                    continue
                kind = next((k for k in item if k in KINDS), None)
                v = item.get(kind)
                if not isinstance(v, dict):
                    continue
                name = v.get("name")
                if kind == "typedef":
                    self.typedefs[name] = v["type"]
                elif kind in ("struct", "union"):
                    if v.get("members") or name not in self.structs:
                        self.structs[name] = dict(v, kind=kind)
                elif kind == "funptr":
                    self.funptrs.add(name)
                elif kind == "enum":
                    if name:
                        self.enums.add(name)
                    for val in v.get("values", []):
                        if isinstance(val.get("value"), int):
                            self.consts[val["name"]] = val["value"]
                elif kind == "lowmem":
                    self.lowmem.append(dict(v, file=f.name))
                elif kind in ("function", "dispatcher") and isinstance(v.get("trap"), int):
                    slot = trap_slot(v["trap"])
                    self.traps.setdefault(slot, []).append(name)
                    self.trap_info.setdefault(slot, []).append(
                        {"name": name, "file": f.name, "kind": kind, "trap": v["trap"],
                         "executor": v.get("executor"), "comment": v.get("comment")})
        self.lowmem.sort(key=lambda g: g["address"])

    # ── types ────────────────────────────────────────────────────────
    def resolve(self, t):
        """Follow typedefs to the underlying spelling."""
        seen = set()
        while t in self.typedefs and t not in seen:
            seen.add(t)
            t = self.typedefs[t]
        return t

    def is_pointer(self, t):
        t = t.strip()
        if t.endswith("*") or t in self.funptrs or t == "ProcPtr":
            return True
        r = self.resolve(t)
        return r != t and self.is_pointer(r)

    def array(self, t):
        m = ARRAY.match(t.strip())
        if not m:
            r = self.resolve(t)
            return self.array(r) if r != t else None
        n = m.group(2)
        n = int(n, 0) if re.match(r"^(0x)?[0-9a-fA-F]+$", n) else self.consts.get(n)
        return (m.group(1).strip(), n)

    def size(self, t):
        if t in self._sizes:
            return self._sizes[t]
        self._sizes[t] = None  # recursion guard
        s = self._size(t.strip())
        self._sizes[t] = s
        return s

    def _size(self, t):
        if self.is_pointer(t):
            return 4
        if t in PRIMITIVES:
            return PRIMITIVES[t][0]
        if t == "Point":
            return 4
        a = self.array(t)
        if a:
            es = self.size(a[0])
            return es * a[1] if es is not None and a[1] is not None else None
        if t in self.structs:
            st = self.structs[t]
            if "size" in st:
                return st["size"]
            fields, size = self.layout(st.get("members") or [], st["kind"] == "union")
            return size
        if t in self.enums:
            return 2
        r = self.resolve(t)
        return self.size(r) if r != t else None

    def signed(self, t):
        r = self.resolve(t)
        return PRIMITIVES.get(r, (0, False))[1]

    def layout(self, members, union=False):
        """[(Field...)], size — 68k rules, nested struct/union/common expanded."""
        fields, off, size = [], 0, 0
        for m in members:
            if "common" in m:
                sub = self.common(m["common"])
                sf, ss = self.layout(sub)
                group = [(f, ss) for f in sf]
                ms = ss
            elif "struct" in m or "union" in m:
                sf, ss = self.layout(m.get("struct") or m.get("union"), "union" in m)
                prefix = m["name"] + "." if m.get("name") else ""
                group = [(Field(prefix + f.name, f.offset, f.type), ss) for f in sf]
                ms = ss
            else:
                ms = self.size(m["type"])
                group = [(Field(m["name"], 0, m["type"]), ms)]
            if ms is None:
                return fields, None
            start = 0 if union else (off + 1 & ~1 if ms > 1 else off)
            for f, _ in group:
                fields.append(Field(f.name, start + f.offset, f.type))
            if union:
                size = max(size, ms)
            else:
                off = start + ms
                size = off
        if size > 1:
            size = size + 1 & ~1
        return fields, size

    def common(self, name):
        for f in sorted(self.defs.glob("*.yaml")):
            for item in yaml.safe_load(f.read_text()) or []:
                if "common" in item and item["common"].get("name") == name:
                    return item["common"]["members"]
        return []

    def fields(self, t):
        st = self.structs.get(self.resolve(t)) or self.structs.get(t)
        if not st or not st.get("members"):
            return None
        return self.layout(st["members"], st["kind"] == "union")[0]


def trap_slot(word):
    """Trap word -> ('tool'|'os', index). Toolbox: bit 11 set, 10-bit index."""
    if word & 0x0800:
        return ("tool", word & 0x3FF)
    return ("os", word & 0xFF)


def defs_mtime(defs):
    return max((f.stat().st_mtime for f in Path(defs).glob("*.yaml")), default=0)
