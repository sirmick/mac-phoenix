"""HTML view of a decoded snapshot next to what multiversal says.

Started by `macdecode.py SNAPSHOT --serve`. Every page is rebuilt from
the decode; multiversal is reloaded whenever a defs/*.yaml file
changes, so editing a name or type there shows up on the next refresh.
"""
import html
import threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

import learned
import macdecode as md
from mvdb import defs_mtime

CSS = """
:root{--bg:#fbfbf8;--fg:#1d1d1b;--mut:#6b6b66;--line:#e2e1da;--acc:#2456a6;--hi:#fff4c2;
--ok:#2e7d32;--warn:#b26a00;--bad:#b3261e;--code:#f2f1ea}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#161614;--fg:#e8e6df;
--mut:#9a988f;--line:#2e2d29;--acc:#8ab4f8;--hi:#3a3420;--ok:#81c784;--warn:#ffb74d;--bad:#f28b82;--code:#22211e}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.45 ui-sans-serif,system-ui,-apple-system,sans-serif}
header{position:sticky;top:0;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;
display:flex;gap:18px;align-items:baseline;flex-wrap:wrap;z-index:2}
header b{font-size:15px}nav a{margin-right:14px}a{color:var(--acc);text-decoration:none}a:hover{text-decoration:underline}
main{padding:12px 16px 40px;max-width:1600px}
table{border-collapse:collapse;width:100%;font-size:13px}th,td{text-align:left;padding:3px 8px;
border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--mut);font-weight:600}
td.cmt{max-width:300px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
code,.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12.5px}
.mut{color:var(--mut)}.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
tr.unnamed td{background:var(--hi)}pre{background:var(--code);padding:10px;overflow-x:auto;border-radius:4px}
.pill{display:inline-block;padding:0 6px;border-radius:9px;border:1px solid var(--line);font-size:11.5px}
.wrap{overflow-x:auto}input{font:inherit;padding:3px 6px}
.val{min-width:380px;max-width:760px;overflow-wrap:anywhere}
td.raw{max-width:190px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
tr.v-out td.nm,tr.v-out td.slot{text-decoration:line-through;color:var(--bad)}tr.v-out td{opacity:.75}
tr.v-never td.nm{text-decoration:line-through;color:var(--mut)}
tr.v-todo td{background:var(--hi)}tr.v-todo td.nm{font-weight:600}
.v{display:inline-block;min-width:74px;padding:0 6px;border-radius:9px;font-size:11.5px;text-align:center;border:1px solid}
.v-todo-p{color:var(--warn);border-color:var(--warn)}.v-done-p{color:var(--ok);border-color:var(--ok)}
.v-basilisk-p{color:var(--acc);border-color:var(--acc)}.v-out-p{color:var(--bad);border-color:var(--bad)}
.v-unresolved-p,.v-never-p{color:var(--mut);border-color:var(--line)}
tr.sel td.nm{padding-left:26px}
"""


def esc(s):
    return html.escape(str(s))


def source_cell(g):
    if g.get("source") != "learned":
        return f"<span class='mut'>{esc(g['file'])}</span>"
    st = g.get("status") or ""
    cls = {"known": "ok", "guess": "warn", "placeholder": "bad"}.get(st, "")
    out = f"<span class='pill {cls}'>learned · {esc(st)}</span>"
    mv = g.get("multiversal")
    if mv:
        out += f"<br><span class='mut'>replaces {esc(mv['name'])} : {esc(mv['type'])} ({esc(mv['file'])})</span>"
    return out


def first_line(c):
    lines = [l for l in (c or "").strip().splitlines() if l.strip()]
    return lines[-1] if lines else ""


def addr_link(a, text=None):
    return f'<a class="mono" href="/addr?a={a:x}">{esc(text or f"${a:08x}")}</a>'


class Site:
    def __init__(self, snap_path, defs, learned_path, disk, compare=None):
        self.snap_path, self.defs, self.learned = snap_path, defs, learned_path
        self.cand_snap = md.Snapshot(compare) if compare else None
        self.cand_zones = md.find_zones(self.cand_snap) if compare else None
        self.cand = None
        self.lock = threading.Lock()
        self.snap = md.Snapshot(snap_path)
        # Zones and origins depend only on the RAM and disk: compute once.
        self.zone_map = md.find_zones(self.snap)
        self.origins = md.load_origins(self.snap, self.zone_map, disk)
        self.world = None
        self.needs_cache = {}
        self.load()

    def load(self):
        db = md.load_db(self.defs, self.learned)
        self.world = md.World(self.snap, db, self.origins, self.zone_map)
        if self.cand_snap:
            self.cand = md.World(self.cand_snap, db, None, self.cand_zones)

    def fresh(self):
        with self.lock:
            db = self.world.db
            if defs_mtime(self.defs) != db.mtime or learned.mtime(self.learned) != db.learned_mtime:
                self.load()
            return self.world

    # ── page frame ───────────────────────────────────────────────────
    def page(self, title, body):
        w = self.world
        s = w.snap
        pages = ("", "lowmem", "traps", "entrypoints", "zones", "resources", "placeholders") + \
            (("diff",) if self.cand else ())
        nav = "".join(f'<a href="/{p}">{ {"": "summary", "entrypoints": "entry points"}.get(p, p)}</a>'
                      for p in pages)
        return f"""<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>macdecode · {esc(title)}</title><style>{CSS}</style></head><body>
<header><b>macdecode</b><span class="mut">{esc(s.dir.name)} · {esc(s.meta.get('backend'))} ·
{esc(s.meta.get('boot_phase'))} · RAM {len(s.ram) >> 20} MB</span><nav>{nav}</nav>
<form action="/addr" style="margin-left:auto"><input name="a" placeholder="address (hex)" size="14"></form>
</header><main><h2>{esc(title)}</h2>{body}</main></body></html>"""

    # ── pages ────────────────────────────────────────────────────────
    def summary(self):
        w = self.world
        out = []
        md.sec_summary(w, out)
        rows = md.trap_entries(w)
        named_impl = sum(1 for r in rows if not r["unimplemented"] and r["names"])
        impl = sum(1 for r in rows if not r["unimplemented"])
        runs, covered = md.uncovered_runs(w)
        body = f"<pre>{esc(chr(10).join(out))}</pre>"
        body += f"""<table><tr><th>Area</th><th>In this RAM</th><th>In multiversal</th></tr>
<tr><td><a href="/lowmem">Low memory</a> $100–${md.LOWMEM_END:x}</td>
<td>{sum(e - s_ for s_, e in runs)} nonzero bytes not named, in {len(runs)} runs</td>
<td>{len(w.db.lowmem)} globals</td></tr>
<tr><td><a href="/traps">Trap tables</a></td><td>{impl} implemented entries</td>
<td>{named_impl} of them named; {impl - named_impl} unnamed</td></tr>
<tr><td><a href="/zones">Zones</a></td><td>{len(w.zones)} heaps</td><td>—</td></tr>
<tr><td><a href="/resources">Resource maps</a></td><td>{len(w.maps)} maps</td><td>—</td></tr></table>
<p class="mut">multiversal: <code>{esc(w.db.defs)}</code> — edit a YAML file and refresh.</p>"""
        return self.page("Summary", body)

    def lowmem(self, q):
        """Every low-memory global and touched ExpandMem field, with who
        touches it in the traced boot, Executor's value, and filters."""
        w, s, db = self.world, self.world.snap, self.world.db
        ext = q.get("ext", ["0"])[0] == "1"
        an = self.analysis(ext)
        mem = an.mem if an else []
        cand = self.cand

        def runs(scope):
            return scope == "run" or (ext and scope == "extension")

        # ── rows: named globals, unnamed nonzero runs, touched bytes nobody
        #    names, touched ExpandMem fields
        items = []
        for g in db.lowmem:
            if g["address"] < md.LOWMEM_END:
                items.append({"addr": g["address"], "size": max(db.size(g["type"]) or 1, 1),
                              "name": g["name"], "type": g["type"], "g": g,
                              "source": g.get("source", "multiversal"), "em": False})
        runs_, _ = md.uncovered_runs(w)
        for a, e in runs_:
            items.append({"addr": a, "size": e - a, "name": "", "type": f"Byte[{e - a}]", "g": None,
                          "source": "unnamed", "em": False})
        items.sort(key=lambda i: i["addr"])
        starts = [i["addr"] for i in items]
        import bisect

        def owner_row(m):
            if m["expandmem"]:
                return None
            k = bisect.bisect_right(starts, m["addr"]) - 1
            if k >= 0 and items[k]["addr"] <= m["addr"] < items[k]["addr"] + items[k]["size"]:
                return items[k]
            return None
        # Touched bytes nobody names: merge adjacent accesses into runs, one
        # row per run (low memory) or per field (ExpandMem).
        loose = sorted((m["addr"], m["addr"] + m["size"]) for m in mem
                       if not m["expandmem"] and owner_row(m) is None)
        merged = []
        for a, e in loose:
            if merged and a <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], e)
            else:
                merged.append([a, e])
        extra = {}
        for a, e in merged:
            extra[("lm", a)] = {"addr": a, "size": e - a, "em": False, "name": "",
                                "type": f"Byte[{e - a}]", "g": None, "source": "unnamed"}
        mstarts = [a for a, _ in merged]
        touched = {}
        for m in mem:
            if m["expandmem"]:
                off = m["addr"] - an.expandmem
                row = extra.setdefault(("em", off), {"addr": m["addr"], "size": m["size"], "em": True,
                                                     "name": f"ExpandMem+${off:03X}", "type": f"{m['size']} bytes",
                                                     "g": None, "source": "unnamed", "off": off})
            else:
                row = owner_row(m)
                if row is None:
                    k = bisect.bisect_right(mstarts, m["addr"]) - 1
                    row = extra[("lm", mstarts[k])]
            touched.setdefault(id(row), []).append(m)
        items += list(extra.values())
        items.sort(key=lambda i: (i["em"], i["addr"]))

        # ── Executor side (the --diff candidate snapshot)
        cstatus = {}
        if cand:
            import diff
            for r in diff.lowmem(w, cand):
                cstatus[r["global"]["address"]] = (r["status"], r["cand"])
        cem = cand.snap.u32(0x2B6) if cand else 0

        def executor_status(i):
            if not cand:
                return "no candidate", ""
            if i["g"] is not None and i["addr"] in cstatus:
                return cstatus[i["addr"]]
            a = i["addr"] if not i["em"] else (cem + i["off"] if cem and cand.snap.ok(cem, 4) else None)
            if a is None or not cand.snap.ok(a, i["size"]):
                return "unset", "no ExpandMem" if i["em"] else ""
            rb = s.ram[i["addr"]:i["addr"] + i["size"]]
            cb = cand.snap.ram[a:a + i["size"]]
            if rb == cb:
                return "same", cb.hex()
            if not any(cb) or all(c == 0xFF for c in cb):
                return "unset", cb.hex()
            return "differs", cb.hex()

        for i in items:
            ms = touched.get(id(i), [])
            i["reads"] = sum(m["count"] for m in ms if not m["write"])
            i["writes"] = sum(m["count"] for m in ms if m["write"])
            run_ms = [m for m in ms if runs(m["scope"])]
            who = {}
            for m in run_ms:
                who[m["origin"].split(" +")[0]] = who.get(m["origin"].split(" +")[0], 0) + m["count"]
            i["who"] = ", ".join(f"{k} {n}" for k, n in sorted(who.items(), key=lambda kv: -kv[1])[:3])
            i["run_rw"] = ("r" if any(not m["write"] for m in run_ms) else "") + \
                          ("w" if any(m["write"] for m in run_ms) else "")
            other = {}
            for m in ms:
                if not runs(m["scope"]):
                    other[m["scope"]] = other.get(m["scope"], 0) + m["count"]
            i["other"] = ", ".join(f"{k} {n}" for k, n in sorted(other.items(), key=lambda kv: -kv[1]))
            if run_ms:
                i["verdict"] = "needed"
            elif any(m["scope"] == "unknown" for m in ms):
                i["verdict"] = "unresolved"
            elif ms:
                i["verdict"] = "apple only"
            else:
                i["verdict"] = "untouched" if mem else "no trace"
            i["ex"], i["exval"] = executor_status(i)

        allv = ("needed", "unresolved", "apple only", "untouched")
        alle = ("same", "same shape", "differs", "unset", "no candidate")
        alls = ("multiversal", "learned", "unnamed")
        filtered = "f" in q
        vs = set(q.get("v", [])) if filtered else set(allv) | {"no trace"}
        es = set(q.get("e", [])) if filtered else set(alle)
        ss = set(q.get("s", [])) if filtered else set(alls)
        text = q.get("q", [""])[0].strip().lower()
        src = lambda i: "learned" if (i["source"] or "").startswith("learned") else i["source"] or "multiversal"
        shown = [i for i in items if i["verdict"] in vs | ({"no trace"} if not mem else set())
                 and i["ex"] in es and src(i) in ss
                 and (not text or text in i["name"].lower() or text in f"{i['addr']:04x}")]
        vcount = {v: sum(1 for i in items if i["verdict"] == v) for v in allv}
        ecount = {e: sum(1 for i in items if i["ex"] == e) for e in alle}

        def box(name, val, on, text_):
            return (f'<label style="margin-right:12px"><input type="checkbox" name="{name}" value="{val}"'
                    f'{" checked" if on else ""}> {esc(text_)}</label>')
        form = ('<form method="get" style="line-height:2"><input type="hidden" name="f" value="1">'
                + ('<input type="hidden" name="ext" value="1">' if ext else "")
                + "<b>Verdict</b> " + "".join(box("v", v, v in vs, f"{v} ({vcount[v]})") for v in allv)
                + "<br><b>Executor</b> " + "".join(box("e", e, e in es, f"{e} ({ecount[e]})") for e in alle)
                + "<br><b>Source</b> " + "".join(box("s", x, x in ss, x) for x in alls)
                + f' <input name="q" value="{esc(text)}" placeholder="search name or address" size="22">'
                ' <button>Filter</button></form>')
        every_e = "&".join(f"e={e.replace(' ', '+')}" for e in alle)
        every_s = "&".join(f"s={x}" for x in alls)
        presets = " · ".join(f'<a href="/lowmem?{qs}">{esc(t)}</a>' for t, qs in (
            ("everything", ""),
            ("needed for Finder", f"f=1&v=needed&{every_e}&{every_s}"),
            ("needed, wrong in Executor", f"f=1&v=needed&v=unresolved&e=differs&e=unset&{every_s}"),
            ("touched but unnamed", f"f=1&v=needed&v=unresolved&v=apple+only&{every_e}&s=unnamed")))
        toggle = (f'<a href="/lowmem?ext={0 if ext else 1}">extensions count as run: {"on" if ext else "off"}</a>')
        trs = []
        for i in shown:
            g = i["g"]
            v = i["verdict"].replace(" ", "-")
            decoded = md.fmt_value(w, g["type"], g["address"]) if g else \
                s.ram[i["addr"]:i["addr"] + min(i["size"], 32)].hex()
            name = (f"<b>{esc(i['name'])}</b>" if i["name"] else "<i>unnamed</i>")
            vcls = {"needed": "todo", "unresolved": "unresolved", "apple-only": "out",
                    "untouched": "never", "no-trace": "never"}[v]
            ecls = {"same": "done", "same shape": "done", "differs": "todo", "unset": "todo"}.get(i["ex"], "never")
            trs.append(f"""<tr class="v-{'out' if v == 'apple-only' else 'x'}"><td class="mono slot">${i['addr']:04x}</td>
<td class="nm">{name}</td><td class="mono">{esc(i['type'])}</td>
<td><span class="v v-{vcls}-p">{esc(i['verdict'])}</span></td>
<td class="mono">{esc(i['run_rw'])}</td><td class="mut">{esc(i['who'])}</td><td class="mut">{esc(i['other'])}</td>
<td class="val mono">{esc(decoded)}</td>
<td><span class="v v-{ecls}-p">{esc(i['ex'])}</span> <span class="mono mut">{esc(str(i['exval'])[:60])}</span></td>
<td>{source_cell(g) if g else '<span class="mut">unnamed</span>'}</td></tr>""")
        note = ("" if mem else "<p class='warn'>This snapshot has no <code>lowmem_access.tsv</code>: boot "
                "with <code>--trace-atraps</code> to see who touches what.</p>")
        cnote = (f"Executor column compares with <code>{esc(cand.snap.dir.name)}</code>." if cand else
                 "No Executor snapshot: start with <code>--diff CANDIDATE</code> for the Executor column.")
        body = (form + f"<p>Presets: {presets} — {toggle} — <b>{len(shown)}</b> of {len(items)}. {cnote}</p>"
                + note +
                "<p class='mut'>Needed = read or written directly (not through a trap) by code Executor runs: "
                "Finder, System defprocs not served by Executor (extensions if on). Apple only = only ROM, "
                "patches or replaced System code touch it. Exception vectors ($0-$FF) aren't watched.</p>"
                "<div class='wrap'><table><tr><th>Addr</th><th>Name</th><th>Type</th><th>Verdict</th>"
                "<th>R/W</th><th>By code we run</th><th>By others</th><th>Value (reference)</th>"
                "<th>Executor</th><th>Source</th></tr>" + "".join(trs) + "</table></div>")
        return self.page("Low memory", body)

    def traps(self, q):
        w = self.world
        filt = q.get("f", ["patched"])[0]
        ogroup = q.get("o", [None])[0]
        rows = md.trap_entries(w)
        counts = {}
        for r in rows:
            if not r["unimplemented"]:
                counts[r["origin_group"]] = counts.get(r["origin_group"], 0) + 1
        origin_links = " · ".join(
            (f"<b>{esc(k)} {v}</b>" if k == ogroup else
             f'<a href="/traps?f=implemented&o={esc(k)}">{esc(k)}</a> {v}')
            for k, v in sorted(counts.items(), key=lambda kv: -kv[1]))
        sel = {"all": lambda r: True,
               "implemented": lambda r: not r["unimplemented"],
               "patched": lambda r: not r["unimplemented"] and not w.snap.in_rom(r["addr"]),
               "unnamed": lambda r: not r["unimplemented"] and not r["names"],
               "rom": lambda r: w.snap.in_rom(r["addr"])}[filt]
        links = " · ".join(f'<a href="/traps?f={k}">{k}</a>' if k != filt else f"<b>{k}</b>"
                           for k in ("patched", "unnamed", "rom", "implemented", "all"))
        an = self.analysis()
        history = {}
        for ins in (an.installs if an else []):
            history.setdefault(ins["slot"], []).append(ins)

        def place(addr, recorded=""):
            if w.snap.in_rom(addr):
                return f"ROM +{addr - w.snap.rom_base:x}"
            return recorded or w.origin(addr) or "?"

        def chain(slot, r):
            hs = history.get(slot)
            if not hs:
                return "<span class='mut'>never patched</span>" if not r["unimplemented"] else ""
            steps = [f"<span class='mono'>{place(hs[0]['old'])}</span>"]
            for h in hs:
                steps.append(f"→ {addr_link(h['addr'])} {esc(place(h['addr'], h['target']))}"
                             f" <span class='mut'>by {esc(h['installer'] or place(h['pc']))}</span>")
            return "<br>".join(steps)

        trs = []
        for r in rows:
            if not sel(r) or (ogroup and r["origin_group"] != ogroup):
                continue
            info = w.db.trap_info.get((r["table"], r["index"]), [])
            names = "<br>".join(f"<b>{esc(i['name'])}</b> <span class='mut'>{esc(i['file'])}</span>"
                                + (f" <span class='pill ok'>executor</span>" if i.get("executor") else "")
                                + (f" <span class='pill warn'>learned · {esc(i.get('status') or '')}</span>"
                                   if i.get("kind") == "learned" else "")
                                for i in info) or "<i class='warn'>not in multiversal</i>"
            origin = esc(r["origin"]) if r["origin"] else (
                "" if r["unimplemented"] else "<span class='bad'>unknown</span>")
            where = ("<span class='mut'>unimplemented</span>" if r["unimplemented"]
                     else esc(r["where"]))
            trs.append(f"""<tr{' class="unnamed"' if not info and not r['unimplemented'] else ''}>
<td class="mono">${r['trap']:04X}</td><td class="mono mut">{r['table']}[{r['index']}]</td>
<td>{names}</td><td>{addr_link(r['addr'])}</td><td>{origin}</td><td class="mut">{where}</td>
<td class="mut" style="font-size:12px">{chain((r['table'], r['index']), r)}</td></tr>""")
        disk = getattr(w.origins, "disk", None)
        note = (f"Origins matched against <code>{esc(disk)}</code>." if disk else
                "<span class='warn'>No boot disk: pass --disk to match origins.</span>")
        body = (f"<p>Show: {links} — {len(trs)} entries. {note}</p>"
                f"<p class='mut'>Implemented entries by origin: {origin_links}</p>"
                "<div class='wrap'><table><tr><th>Trap</th>"
                "<th>Slot</th><th>Names (multiversal / learned)</th><th>Address</th><th>Origin</th>"
                "<th>Points into</th><th>Patch history (traced boot)</th></tr>"
                + "".join(trs) + "</table></div>")
        return self.page("Trap tables", body)

    def analysis(self, ext=False):
        """The traced boot's analysis (atraps.py), cached; None without a trace."""
        snap_dir = self.world.snap.dir
        if not (snap_dir / "atraps.tsv").exists():
            return None
        import atraps
        key = (ext, learned.mtime(self.learned))
        if key not in self.needs_cache:
            self.needs_cache = {key: atraps.Analysis(str(snap_dir), extensions=ext)}
        return self.needs_cache[key]

    def entry_points_list(self, an):
        """Every entry point we know of, unrolled: traced routines (with
        their verdicts) plus known selectors and named slots never seen."""
        w = self.world
        nm, ds = an.names, an.names.ds
        entries = {(r["table"], r["index"]): r for r in md.trap_entries(w)}
        by_slot = {}
        for r in an.rows:
            by_slot.setdefault(r["slot"], []).append(r)
        slots = (set(by_slot) | {k for k, e in entries.items() if not e["unimplemented"]}
                 | set(w.db.traps) | set(nm.ui) | set(ds.slots))
        out = []
        for slot in sorted(slots, key=lambda k: (k[0] != "tool", k[1])):
            word = (0xA800 | slot[1]) if slot[0] == "tool" else (0xA000 | slot[1])
            e = entries.get(slot)
            origin = (e["origin"] or "?") if e and not e["unimplemented"] else "empty in 7.5.5 table"
            d = ds.slots.get(slot)
            routines = by_slot.get(slot, [])
            for r in routines:
                kind = ("driver" if r["detail"] else "component" if r["sub"] is not None
                        else "selector" if r["sel"] is not None else "trap")
                out.append(dict(r, word=word, kind=kind, origin=origin,
                                loc=f"{d['kind']} & {d['mask']:X}" if d and kind == "selector" else ""))
            seen = {(r["sel"] & (d["mask"] or 0xFFFFFFFF)) for r in routines if d and r["sel"] is not None}
            if d:
                for v, sel in sorted(d["selectors"].items()):
                    if v in seen:
                        continue
                    tag = an.tags.get((slot, v)) or an.tags.get((slot, None))
                    out.append({"slot": slot, "word": word, "trap": f"{word:04X}", "sel": v, "sub": None,
                                "obj": None, "detail": "", "kind": "selector", "name": sel["name"],
                                "named_by": sel["source"].split(" ")[0], "executor": nm.executor(word, v),
                                "verdict": "never", "reason": "", "tag": tag[0] if tag else None,
                                "calls": 0, "groups": {}, "parents": {}, "origin": origin,
                                "loc": f"{d['kind']} & {d['mask']:X}"})
            elif not routines:
                tag = an.tags.get((slot, None))
                out.append({"slot": slot, "word": word, "trap": f"{word:04X}", "sel": None, "sub": None,
                            "obj": None, "detail": "", "kind": "trap", "name": nm.trap(word),
                            "named_by": nm.name_source(word), "executor": nm.executor(word),
                            "verdict": "never", "reason": "", "tag": tag[0] if tag else None,
                            "calls": 0, "groups": {}, "parents": {}, "origin": origin, "loc": ""})
        return out

    def entry_points(self, q):
        """Every entry point we know of, unrolled per dispatch kind, with
        checkbox filters (verdict, Executor status, kind) and a search."""
        ext = q.get("ext", ["0"])[0] == "1"
        an = self.analysis(ext)
        if an is None:
            return self.page("Entry points", "<p>This snapshot has no <code>atraps.tsv</code>: boot with "
                             "<code>--trace-atraps</code> and snapshot again.</p>")
        allv = ("todo", "basilisk", "unresolved", "done", "out", "never")
        alle = ("has", "lacks", "basilisk")
        allk = ("trap", "selector", "driver", "component")
        filtered = "f" in q            # the form was submitted
        vs = set(q.get("v", [])) if filtered else set(allv)
        es = set(q.get("e", [])) if filtered else set(alle)
        ks = set(q.get("k", [])) if filtered else set(allk)
        text = q.get("q", [""])[0].strip().lower()

        def estatus(ex):
            return "has" if ex in ("implemented", "whole-trap") else "basilisk" if ex == "basilisk" else "lacks"

        items = self.entry_points_list(an)
        counts = {v: sum(1 for i in items if i["verdict"] == v) for v in allv}
        shown = [i for i in items if i["verdict"] in vs and estatus(i["executor"]) in es and i["kind"] in ks
                 and (not text or text in i["name"].lower() or text in i["trap"].lower())]
        label = {"todo": "todo", "basilisk": "basilisk", "unresolved": "unresolved", "done": "done",
                 "out": "not needed", "never": "never called"}
        elabel = {"has": "Executor has it", "lacks": "Executor lacks it", "basilisk": "Basilisk host code"}

        def box(name, val, on, text_):
            return (f'<label style="margin-right:12px"><input type="checkbox" name="{name}" value="{val}"'
                    f'{" checked" if on else ""}> {esc(text_)}</label>')
        form = ('<form method="get" style="line-height:2">'
                '<input type="hidden" name="f" value="1">'
                + (f'<input type="hidden" name="ext" value="1">' if ext else "")
                + "<b>Verdict</b> " + "".join(box("v", v, v in vs, f"{label[v]} ({counts[v]})") for v in allv)
                + "<br><b>Executor</b> " + "".join(box("e", e, e in es, elabel[e]) for e in alle)
                + "<br><b>Kind</b> " + "".join(box("k", k, k in ks, k) for k in allk)
                + f' <input name="q" value="{esc(text)}" placeholder="search name or trap" size="22">'
                ' <button>Filter</button></form>')
        presets = " · ".join(f'<a href="/entrypoints?{qs}">{esc(t)}</a>' for t, qs in (
            ("everything", ""),
            ("needed for Finder", "f=1&v=todo&v=basilisk&v=unresolved&v=done&e=has&e=lacks&e=basilisk"
                                  "&k=trap&k=selector&k=driver&k=component"),
            ("work list", "f=1&v=todo&v=basilisk&v=unresolved&e=lacks&e=basilisk"
                          "&k=trap&k=selector&k=driver&k=component"),
            ("hide not needed", "f=1&v=todo&v=basilisk&v=unresolved&v=done&v=never&e=has&e=lacks&e=basilisk"
                                "&k=trap&k=selector&k=driver&k=component"),
            ("implemented in Executor", "f=1&v=todo&v=basilisk&v=unresolved&v=done&v=out&v=never&e=has"
                                        "&k=trap&k=selector&k=driver&k=component")))
        toggle = (f'<a href="/entrypoints?ext={0 if ext else 1}">extensions count as run: '
                  f'{"on" if ext else "off"}</a>')
        trs = []
        for i in shown:
            sel = "" if i["sel"] is None else f"{i['sel']:X}"
            g = ", ".join(f"{k} {n}" for k, n in list(i["groups"].items())[:3])
            p = ", ".join(f"{k} {n}" for k, n in list(i["parents"].items())[:2])
            why = i["reason"] or ("" if i["verdict"] == "never" else "")
            tag = f" <span class='pill'>{esc(i['tag'])}</span>" if i.get("tag") else ""
            v = i["verdict"]
            trs.append(f'''<tr class="v-{v}"><td class="mono slot">${i['word']:04X}</td><td class="mono mut">{sel}</td>
<td class="nm">{esc(i['name'])}{tag}</td><td class="mut">{i['kind']}{(' · ' + esc(i['loc'])) if i['loc'] else ''}</td>
<td><span class="v v-{v}-p">{label[v]}</span></td><td class="mut">{esc(elabel[estatus(i['executor'])])}</td>
<td class="mono">{i['calls'] or ''}</td><td class="mut">{esc(why)}</td>
<td class="mut">{('by ' + esc(g)) if g else ''}{(' · in ' + esc(p)) if p else ''}</td>
<td class="mut">{esc(i['named_by'])}</td><td class="mut">{esc(i['origin'])}</td></tr>''')
        body = (form + f"<p>Presets: {presets} — {toggle} — <b>{len(shown)}</b> of {len(items)} entry points.</p>"
                "<p class='mut'>Needed = called in the traced real 7.5.5 boot from code Executor runs (Finder; "
                "System defprocs not served by Executor; extensions if on). Not needed = only Apple ROM/patch "
                "code, hardware/boot/debug (tags in learned.yaml), or System code Executor replaces calls it. "
                "Never called = known, but the trace didn't reach it: not the same as not needed.</p>"
                "<div class='wrap'><table><tr><th>Trap</th><th>Sel</th><th>Entry point</th><th>Kind</th>"
                "<th>Verdict</th><th>Executor</th><th>Calls</th><th>Why</th><th>Callers / inside</th>"
                "<th>Named by</th><th>Slot in the real table</th></tr>" + "".join(trs) + "</table></div>")
        return self.page("Entry points", body)

    def zones(self, q):
        w = self.world
        if "z" in q:
            return self.zone(int(q["z"][0], 16))
        trs = []
        for z in w.zones.values():
            kinds = {}
            for b in z.blocks:
                k = kinds.setdefault(b["kind"], [0, 0])
                k[0] += 1
                k[1] += b["size"]
            trs.append(f"""<tr><td><a href="/zones?z={z.addr:x}">{esc(z.name)}</a></td>
<td>{addr_link(z.addr)}</td><td class="mono">${z.end:08x}</td><td>{(z.end - z.addr) >> 10} KB</td>
<td>{esc(z.parent.name) if z.parent else ''}</td>
<td>{', '.join(f'{k} {v[0]} ({v[1] >> 10} KB)' for k, v in sorted(kinds.items()))}</td>
<td class="bad">{esc(z.error or '')}</td></tr>""")
        body = ("<p>Zones named by low memory plus every well-formed zone found by scanning RAM; "
                "heaps are named after the application whose resource map lives in them.</p>"
                "<table><tr><th>Zone</th><th>Start</th><th>End</th><th>Size</th><th>Inside</th>"
                "<th>Blocks</th><th></th></tr>" + "".join(trs) + "</table>")
        return self.page("Zones", body)

    def zone(self, addr):
        w = self.world
        z = w.zones.get(addr)
        if not z:
            return self.page("Zone", "<p>No such zone.</p>")
        trs = []
        for b in z.blocks:
            what = ""
            if b["kind"] == "handle":
                hit = w.rsrc_by_mp.get(b["mp"])
                what = (f"rsrc '{esc(hit[1]['type'])}' {hit[1]['id']} ({esc(hit[0]['file'])})"
                        if hit else f"master ptr ${b['mp']:08x}")
            elif b["kind"] == "ptr":
                sub = w.zones.get(b["data"])
                what = f"zone {esc(sub.name)}" if sub else ""
            trs.append(f"""<tr><td>{addr_link(b['data'])}</td><td>{b['kind']}</td>
<td class="mono">{b['size'] - md.BLK_HDR - b['corr']}</td><td class="mono">{b['flags']:02x}</td>
<td>{what}</td></tr>""")
        body = (f"<p>{addr_link(z.addr)}..${z.end:08x}, zcbFree {z.free}, moreMast {z.more_mast}"
                f"{', inside ' + esc(z.parent.name) if z.parent else ''}</p>"
                "<table><tr><th>Data</th><th>Kind</th><th>Logical size</th><th>Flags</th><th>What</th></tr>"
                + "".join(trs) + "</table>")
        return self.page(f"Zone {z.name}", body)

    def resources(self, q):
        w = self.world
        parts = []
        for m in w.maps:
            loaded = [r for r in m["resources"] if r["handle"]]
            rows = "".join(
                f"<tr><td class='mono'>'{esc(r['type'])}'</td><td class='mono'>{r['id']}</td>"
                f"<td>{esc(r['name'] or '')}</td><td>{addr_link(r['handle'])}</td>"
                f"<td>{esc(w.where(w.snap.u32(r['handle'])) if w.snap.u32(r['handle']) else 'purged')}</td></tr>"
                for r in loaded)
            chain = "" if m["chained"] else f" <span class='pill warn'>not on current chain</span>"
            parts.append(f"<h3>{esc(m['file'])} <span class='mut'>({esc(m['ftype'])}, refnum {m['refnum']}, "
                         f"{len(m['resources'])} resources, {len(loaded)} loaded)</span>{chain}</h3>"
                         f"<details><summary>loaded resources</summary><table><tr><th>Type</th><th>ID</th>"
                         f"<th>Name</th><th>Handle</th><th>Data in</th></tr>{rows}</table></details>")
        return self.page("Resource maps", "".join(parts))

    def placeholders(self, q):
        w = self.world
        runs, _ = md.uncovered_runs(w)
        out = []
        md.sec_placeholders(w, out)
        items = []
        for (a, e), y in zip(runs, out):
            items.append(f'<div id="p{a:x}"><pre>{esc(y)}</pre></div>')
        body = (f"<p>Paste under <code>lowmem:</code> in <code>{esc(w.db.learned_path)}</code>, then rename, "
                "retype and set <code>status</code> as we learn what each one is. The page reloads the "
                "overlay on refresh; multiversal stays untouched.</p>" + "".join(items))
        return self.page("Placeholder YAML", body)

    def diff(self, q):
        import diff as df
        ref, cand = self.world, self.cand
        if not cand:
            return self.page("Diff", "<p>Start with <code>--serve --diff CANDIDATE</code>.</p>")
        sc = df.score(ref, cand)
        lm = sc["lowmem"]
        filt = q.get("f", ["differs"])[0]
        links = " · ".join(f'<a href="/diff?f={k}">{k} {lm.get(k, 0) if k != "all" else sc["lowmem_total"]}</a>'
                           if k != filt else f"<b>{k}</b>"
                           for k in ("differs", "unset", "same shape", "same", "all"))
        cls = {"same": "ok", "same shape": "ok", "differs": "bad", "unset": "warn"}
        rows = "".join(
            f"<tr><td class='mono'>${r['global']['address']:04x}</td><td><b>{esc(r['global']['name'])}</b></td>"
            f"<td class='mono'>{esc(r['global']['type'])}</td><td class='{cls[r['status']]}'>{r['status']}</td>"
            f"<td class='val mono'>{esc(r['ref'])}</td><td class='val mono'>{esc(r['cand'])}</td></tr>"
            for r in df.lowmem(ref, cand) if filt == "all" or r["status"] == filt)
        rz, cz = df.zones(ref, cand)
        zr = "".join(
            "<tr>" + "".join(
                (f"<td>{esc(z['name'])}</td><td class='mono'>${z['addr']:08x}</td>"
                 f"<td>{(z['end'] - z['addr']) >> 10} KB</td><td class='mut'>{esc(z['parent'] or '')}</td>")
                if z else "<td></td><td></td><td></td><td></td>"
                for z in (rz[i] if i < len(rz) else None, cz[i] if i < len(cz) else None)) + "</tr>"
            for i in range(max(len(rz), len(cz))))
        rs, cs = df.system_resources(ref, cand)
        missing = sorted(rs - cs)
        t = sc["traps"]
        body = f"""<p>Reference <b>{esc(ref.snap.dir.name)}</b> vs candidate <b>{esc(cand.snap.dir.name)}</b>.
Pointers count as <i>same shape</i> when both point into the same kind of place.</p>
<table><tr><th>Measure</th><th>Score</th></tr>
<tr><td>Low-memory globals</td><td>{lm.get('same', 0)} same, {lm.get('same shape', 0)} same shape,
{lm.get('differs', 0)} differ, {lm.get('unset', 0)} unset (of {sc['lowmem_total']})</td></tr>
<tr><td>System resources loaded</td><td>{sc['sys_resources_both']} of {sc['sys_resources_ref']}</td></tr>
<tr><td>Trap tables $400/$E00</td><td>{t['cand_implemented_too']} of {t['ref_implemented']} entries</td></tr></table>
<h3>Zones</h3><table><tr><th colspan=4>Reference</th><th colspan=4>Candidate</th></tr>{zr}</table>
<h3>Low memory</h3><p>Show: {links}</p><div class='wrap'><table><tr><th>Addr</th><th>Name</th><th>Type</th>
<th>Status</th><th>Reference</th><th>Candidate</th></tr>{rows}</table></div>
<h3>System resources loaded in the reference but not the candidate ({len(missing)})</h3>
<p class='mono'>{' '.join(esc(f"'{t_}' {i}") for t_, i in missing)}</p>"""
        return self.page("Diff", body)

    def addr(self, q):
        w, s = self.world, self.world.snap
        try:
            a = int(q.get("a", ["0"])[0].strip().lstrip("$").replace("0x", ""), 16)
        except ValueError:
            return self.page("Address", "<p>Bad address.</p>")
        lines = []
        base = a & ~15
        for row in range(base - 32, base + 96, 16):
            if not s.ok(row, 16):
                continue
            bs = s.ram[row:row + 16]
            asc = "".join(chr(c) if 32 <= c < 127 else "." for c in bs)
            mark = "◀" if row <= a < row + 16 else " "
            lines.append(f"{row:08x} {mark} {bs[:8].hex(' ')}  {bs[8:].hex(' ')}  {asc}")
        g = w.lowmem_by_addr.get(a)
        mv = (f"<p>multiversal: <b>{esc(g['name'])}</b> : <code>{esc(g['type'])}</code> "
              f"({esc(g['file'])}) = <code>{esc(md.fmt_value(w, g['type'], a))}</code></p>") if g else ""
        body = (f"<p><b class='mono'>${a:08x}</b> — {esc(w.where(a))}</p>{mv}"
                f"<pre>{esc(chr(10).join(lines)) or 'not in RAM'}</pre>")
        return self.page(f"${a:08x}", body)


def serve(snap_path, defs, learned_path, disk, host, port, compare=None):
    site = Site(snap_path, defs, learned_path, disk, compare)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query, keep_blank_values=True)
            site.fresh()
            route = {"/": lambda: site.summary(), "/lowmem": lambda: site.lowmem(q),
                     "/traps": lambda: site.traps(q), "/zones": lambda: site.zones(q),
                     "/entrypoints": lambda: site.entry_points(q),
                     "/needs": lambda: site.entry_points(q),
                     "/resources": lambda: site.resources(q),
                     "/placeholders": lambda: site.placeholders(q),
                     "/diff": lambda: site.diff(q),
                     "/addr": lambda: site.addr(q)}.get(u.path)
            if not route:
                self.send_error(404)
                return
            try:
                body = route().encode()
                code = 200
            except Exception as e:  # show decode bugs in the page, keep serving
                import traceback
                body = f"<pre>{esc(traceback.format_exc())}</pre>".encode()
                code = 500
            self.send_response(code)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    httpd = ThreadingHTTPServer((host, port), Handler)
    print(f"macdecode: serving {snap_path} on http://{host}:{port}/", flush=True)
    httpd.serve_forever()
