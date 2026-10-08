# Shared by the macdecode Ghidra scripts (PyGhidra): labels file access and
# A-line trap / selector naming.
import json


def load_labels(path):
    with open(path) as f:
        return json.load(f)


def slot(word):
    return ("tool", word & 0x3FF) if word & 0x0800 else ("os", word & 0xFF)


def key(word):
    t, i = slot(word)
    return f"{t}:{i:x}"


def selector_before(prev, kind):
    """The selector a dispatcher would read, from the instruction before
    the trap (MOVEQ / MOVE.W / MOVE.L #imm to D0 or -(SP)), or None."""
    if prev is None:
        return None
    b = bytes([x & 0xFF for x in prev.getBytes()])
    op = int.from_bytes(b[:2], "big")
    if kind == "D0":
        if (op & 0xFF00) == 0x7000:
            v = op & 0xFF
            return (v - 0x100 if v & 0x80 else v) & 0xFFFFFFFF
        if op == 0x303C and len(b) >= 4:
            return int.from_bytes(b[2:4], "big")
        if op == 0x203C and len(b) >= 6:
            return int.from_bytes(b[2:6], "big")
    if kind == "StackW" and op == 0x3F3C and len(b) >= 4:
        return int.from_bytes(b[2:4], "big")
    if kind == "StackL" and op == 0x2F3C and len(b) >= 6:
        return int.from_bytes(b[2:6], "big")
    return None


def trap_comment(labels, ins):
    """'_Name' or '_Dispatcher → Routine' for an atrap instruction."""
    word = int.from_bytes(bytes([x & 0xFF for x in ins.getBytes()])[:2], "big")
    k = key(word)
    name = labels["traps"].get(k, f"_{word:04X}")
    d = labels["dispatchers"].get(k)
    if d and d["kind"] in ("D0", "StackW", "StackL"):
        sel = selector_before(ins.getPrevious(), d["kind"])
        if sel is not None:
            sel &= d["mask"]
            routine = d["selectors"].get(f"{sel:x}")
            return f"{name} → {routine or f'#{sel:X}'}"
        return f"{name} (selector not inline)"
    if d and d["kind"] == "TrapBits":
        routine = d["selectors"].get(f"{word & d['mask']:x}")
        return f"{name} → {routine}" if routine else name
    return name


def annotate_atraps(set_comment, labels, instructions):
    n = 0
    for ins in instructions:
        if ins.getMnemonicString() == "atrap":
            set_comment(ins.getAddress(), trap_comment(labels, ins))
            n += 1
    return n
