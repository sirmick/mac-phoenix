"""Compare two decoded worlds: the reference (real boot) and a candidate
(Executor). The point is a number that goes down as Executor's C++
builds memory that looks like the real thing.

Pointers can't be compared by value, so they're compared by shape: do
both point into the same kind of place (nil, ROM, lowmem, System heap
pointer/handle/resource, app heap…)?
"""
import macdecode as md


def place(world, v):
    """Coarse class of where a pointer value lands."""
    if v == 0:
        return "nil"
    w = world.where(v)
    if w.startswith("ROM"):
        return "ROM"
    if w.startswith("lowmem") or "trap table" in w:
        return "lowmem"
    head = w.split(" ")[0]
    if head == "SysZone":
        kind = w.split(" ")[1] if len(w.split(" ")) > 1 else ""
        return f"SysZone {kind}" if kind in ("ptr", "handle", "rsrc", "header+0") else "SysZone"
    z = world.zone_chain(v)
    if z is not None:
        if z.parent is None and z.addr != world.snap.u32(0x2A6):
            return "top-level heap"
        return "app heap"
    return w.split(" ")[0]


def lowmem(ref, cand):
    rows = []
    for g in ref.db.lowmem:
        a, t = g["address"], g["type"]
        if a >= md.LOWMEM_END:
            continue
        size = ref.db.size(t) or 0
        rb, cb = ref.snap.ram[a:a + size], cand.snap.ram[a:a + size]
        if rb == cb:
            status = "same"
        elif ref.db.is_pointer(t) and size == 4:
            rp, cp = place(ref, ref.snap.u32(a)), place(cand, cand.snap.u32(a))
            status = "same shape" if rp == cp else "differs"
        elif not any(cb) or all(c == 0xFF for c in cb):
            status = "unset"
        else:
            status = "differs"
        rows.append({"global": g, "status": status,
                     "ref": md.fmt_value(ref, t, a), "cand": md.fmt_value(cand, t, a)})
    return rows


def zones(ref, cand):
    def summary(w):
        out = []
        for z in w.zones.values():
            out.append({"name": z.name, "addr": z.addr, "end": z.end,
                        "parent": z.parent.name if z.parent else None,
                        "blocks": len(z.blocks)})
        return out
    return summary(ref), summary(cand)


def system_resources(ref, cand):
    """Loaded resources of the System file map in each world."""
    def loaded(w):
        for m in w.maps:
            if m["file"] == "System":
                return {(r["type"], r["id"]) for r in m["resources"] if r["handle"]
                        and w.snap.ok(r["handle"], 4) and w.snap.u32(r["handle"])}
        return set()
    return loaded(ref), loaded(cand)


def traps(ref, cand):
    rr = {(r["table"], r["index"]): r for r in md.trap_entries(ref)}
    cr = {(r["table"], r["index"]): r for r in md.trap_entries(cand)}
    impl_ref = [k for k, r in rr.items() if not r["unimplemented"]]
    impl_both = [k for k in impl_ref if not cr[k]["unimplemented"]]
    return {"ref_implemented": len(impl_ref), "cand_implemented_too": len(impl_both),
            "cand_implemented": sum(1 for r in cr.values() if not r["unimplemented"])}


def score(ref, cand):
    lm = lowmem(ref, cand)
    counts = {}
    for r in lm:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    rs, cs = system_resources(ref, cand)
    t = traps(ref, cand)
    return {"lowmem": counts, "lowmem_total": len(lm),
            "sys_resources_ref": len(rs), "sys_resources_both": len(rs & cs),
            "traps": t}


def text(ref, cand, out):
    s = score(ref, cand)
    out.append(f"# diff: reference {ref.snap.dir.name}  vs  candidate {cand.snap.dir.name}")
    lm = s["lowmem"]
    out.append(f"  lowmem globals ({s['lowmem_total']}): " +
               ", ".join(f"{k} {lm.get(k, 0)}" for k in ("same", "same shape", "differs", "unset")))
    out.append(f"  System resources loaded: reference {s['sys_resources_ref']}, "
               f"also loaded in candidate {s['sys_resources_both']}")
    t = s["traps"]
    out.append(f"  trap tables @ $400/$E00: reference implements {t['ref_implemented']}, "
               f"candidate has {t['cand_implemented_too']} of those ({t['cand_implemented']} total)")
    rz, cz = zones(ref, cand)
    out.append("  zones (reference | candidate):")
    for i in range(max(len(rz), len(cz))):
        a = rz[i] if i < len(rz) else None
        b = cz[i] if i < len(cz) else None
        fa = f"{a['name'][:22]:<22} ${a['addr']:08x} {(a['end'] - a['addr']) >> 10:6} KB" if a else ""
        fb = f"{b['name'][:22]:<22} ${b['addr']:08x} {(b['end'] - b['addr']) >> 10:6} KB" if b else ""
        out.append(f"    {fa:<48} | {fb}")
    out.append("  lowmem that differs or is unset:")
    for r in lowmem(ref, cand):
        if r["status"] in ("differs", "unset"):
            g = r["global"]
            out.append(f"    {g['address']:04x} {g['name']:<16} {r['status']:<7} ref {r['ref'][:60]}")
            out.append(f"    {'':<4} {'':<16} {'':<7} got {r['cand'][:60]}")
