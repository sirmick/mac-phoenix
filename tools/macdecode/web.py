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
        pages = ("", "lowmem", "traps", "needs", "zones", "resources", "placeholders") + \
            (("diff",) if self.cand else ())
        nav = "".join(f'<a href="/{p}">{p or "summary"}</a>' for p in pages)
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
        w, s, db = self.world, self.world.snap, self.world.db
        show_all = "all" in q
        rows = []
        for g in db.lowmem:
            if g["address"] >= md.LOWMEM_END:
                continue
            size = db.size(g["type"]) or 0
            raw = s.ram[g["address"]:g["address"] + min(size, 32)].hex()
            rows.append((g["address"], f"""<tr><td class="mono">${g['address']:04x}</td>
<td><b>{esc(g['name'])}</b></td><td class="mono">{esc(g['type'])}</td><td class="mono">{size}</td>
<td class="val mono">{esc(md.fmt_value(w, g['type'], g['address']))}</td>
<td class="mono mut raw" title="{raw}">{raw}</td><td>{source_cell(g)}</td>
<td class="mut cmt" title="{esc((g.get('comment') or '').strip())}">{esc(first_line(g.get('comment')))}</td></tr>"""))
        runs, _ = md.uncovered_runs(w)
        for a, e in runs:
            v = s.u32(a) if e - a == 4 else None
            hint = f" → {w.where(v)}" if v else ""
            rows.append((a, f"""<tr class="unnamed"><td class="mono">${a:04x}</td>
<td><i>unnamed</i></td><td class="mono">Byte[{e - a}]</td><td class="mono">{e - a}</td>
<td class="val mono">{esc(hint.strip(' →'))}</td><td class="mono mut raw">{s.ram[a:min(e, a + 32)].hex()}</td>
<td colspan="2"><a href="/placeholders#p{a:x}">placeholder</a></td></tr>"""))
        rows.sort(key=lambda r: r[0])
        body = ("<p>Every multiversal low-memory global, decoded with its multiversal type, merged with "
                "the nonzero bytes multiversal doesn't name (highlighted). Trap tables are on their own page.</p>"
                "<div class='wrap'><table><tr><th>Addr</th><th>Name</th><th>Type</th><th>Size</th>"
                "<th>Decoded</th><th>Bytes</th><th>Source</th><th>Comment</th></tr>"
                + "".join(r for _, r in rows) + "</table></div>")
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
<td>{names}</td><td>{addr_link(r['addr'])}</td><td>{origin}</td><td class="mut">{where}</td></tr>""")
        disk = getattr(w.origins, "disk", None)
        note = (f"Origins matched against <code>{esc(disk)}</code>." if disk else
                "<span class='warn'>No boot disk: pass --disk to match origins.</span>")
        body = (f"<p>Show: {links} — {len(trs)} entries. {note}</p>"
                f"<p class='mut'>Implemented entries by origin: {origin_links}</p>"
                "<div class='wrap'><table><tr><th>Trap</th>"
                "<th>Slot</th><th>Names (multiversal / learned)</th><th>Address</th><th>Origin</th>"
                "<th>Points into</th></tr>"
                + "".join(trs) + "</table></div>")
        return self.page("Trap tables", body)

    def needs(self, q):
        """The trap table, crossed out where we know Executor won't need it.

        Needed = called (in the traced real boot) from code Executor runs:
        Finder and the System definition procedures it doesn't replace.
        Everything only Apple ROM/patch code calls, hardware/boot traps,
        and traps called only by System code Executor replaces are out.
        """
        snap_dir = self.world.snap.dir
        if not (snap_dir / "atraps.tsv").exists():
            return self.page("Needs", "<p>This snapshot has no <code>atraps.tsv</code>: boot with "
                             "<code>--trace-atraps</code> and snapshot again.</p>")
        import atraps
        ext = q.get("ext", ["0"])[0] == "1"
        key = (ext, learned.mtime(self.learned))
        if key not in self.needs_cache:
            self.needs_cache = {key: atraps.Analysis(str(snap_dir), extensions=ext)}
        an = self.needs_cache[key]
        show = q.get("v", ["all"])[0]
        w = self.world
        by_slot = {}
        for r in an.rows:
            by_slot.setdefault(r["slot"], []).append(r)
        entries = {(r["table"], r["index"]): r for r in md.trap_entries(w)}
        order = ("todo", "basilisk", "unresolved", "done", "out")
        nm, ds = an.names, an.names.ds
        # Every routine we know of: slots the real table fills, slots any
        # source names, every dispatcher selector, plus what the trace saw.
        slots = (set(by_slot) | {k for k, e in entries.items() if not e["unimplemented"]}
                 | set(w.db.traps) | set(nm.ui) | set(ds.slots))
        trs, tally, rtally = [], {}, {}

        def ex_label(ex):
            return {"implemented": "Executor has it", "whole-trap": "Executor has it (whole trap)",
                    "basilisk": "host code in Basilisk II core", "missing": "Executor lacks it",
                    "unknown": "not in multiversal"}.get(ex, ex)

        for slot in sorted(slots, key=lambda k: (k[0] != "tool", k[1])):
            routines = by_slot.get(slot, [])
            e = entries.get(slot)
            word = (0xA800 | slot[1]) if slot[0] == "tool" else (0xA000 | slot[1])
            d = ds.slots.get(slot)
            # Known selectors the trace never saw.
            seen = {(r["sel"] & (d["mask"] or 0xFFFFFFFF)) for r in routines if d and r["sel"] is not None}
            unseen = [(v, sel) for v, sel in sorted(d["selectors"].items()) if v not in seen] if d else []
            verdict = next((v for v in order if any(r["verdict"] == v for r in routines)), "never")
            tally[verdict] = tally.get(verdict, 0) + 1
            for r in routines:
                rtally[r["verdict"]] = rtally.get(r["verdict"], 0) + 1
            rtally["never"] = rtally.get("never", 0) + len(unseen) + (0 if routines or d else 1)
            if show != "all" and verdict != show and not any(r["verdict"] == show for r in routines) \
                    and not (show == "never" and unseen):
                continue
            origin = esc(e["origin"]) if e and e["origin"] else (
                "" if e and not e["unimplemented"] else "<i>empty in the 7.5.5 table</i>")
            calls = sum(r["calls"] for r in routines)
            if verdict == "never":
                why = "not called on the way to Finder"
                if not d:
                    why += "; " + ex_label(nm.executor(word))
            else:
                why = "; ".join(sorted({r["reason"] or r["executor"] for r in routines}))
            kind = f" <span class='pill'>dispatcher · {len(d['selectors'])} known</span>" if d else ""
            trs.append(f'''<tr class="v-{verdict}"><td class="mono slot">${word:04X}</td>
<td class="nm">{esc(nm.trap(word))}{kind}</td><td><span class="v v-{verdict}-p">{verdict}</span></td>
<td class="mono">{calls or ""}</td><td class="mut">{esc(why)}</td><td class="mut">{origin}</td></tr>''')
            subs = [("seen", r) for r in routines if r["sel"] is not None or r["obj"] is not None]
            subs += [("known", (v, sel)) for v, sel in unseen]
            subs.sort(key=lambda x: (x[1]["sel"] or 0) if x[0] == "seen" else x[1][0])
            for kind_, x in subs:
                if kind_ == "seen":
                    r = x
                    if show != "all" and r["verdict"] != show:
                        continue
                    g = ", ".join(f"{k} {n}" for k, n in list(r["groups"].items())[:3])
                    p = ", ".join(f"{k} {n}" for k, n in list(r["parents"].items())[:2])
                    trs.append(f'''<tr class="sel v-{r['verdict']}"><td class="mono mut">{'' if r['sel'] is None else f"{r['sel']:X}"}</td>
<td class="nm">{esc(r['name'])}</td><td><span class="v v-{r['verdict']}-p">{r['verdict']}</span></td>
<td class="mono">{r['calls']}</td><td class="mut">{esc(r['reason'] or ex_label(r['executor']))}</td>
<td class="mut">by {esc(g)} · in {esc(p)}</td></tr>''')
                else:
                    if show not in ("all", "never"):
                        continue
                    v, sel = x
                    trs.append(f'''<tr class="sel v-never"><td class="mono mut">{v:X}</td>
<td class="nm">{esc(sel['name'])}</td><td><span class="v v-never-p">never</span></td><td></td>
<td class="mut">{esc(ex_label(nm.executor(word, v)))}</td><td class="mut">named by {esc(sel['source'])}</td></tr>''')
        links = " · ".join((f"<b>{v} {tally.get(v, 0)}</b>" if v == show else
                            f'<a href="/needs?v={v}&ext={int(ext)}">{v}</a> {tally.get(v, 0)}')
                           for v in ("todo", "basilisk", "unresolved", "done", "out", "never"))
        links += " · " + (f'<a href="/needs?v={show}&ext={int(ext)}">all</a>' if show != "all" else "<b>all</b>")
        toggle = (f'<a href="/needs?v={show}&ext={0 if ext else 1}">'
                  + ("extensions count as run: on" if ext else "extensions count as run: off") + "</a>")
        rlinks = ", ".join(f"{v} {rtally.get(v, 0)}" for v in ("todo", "basilisk", "unresolved", "done", "out", "never"))
        body = (f"<p>Trap slots by verdict: {links} — {toggle}</p>"
                f"<p class='mut'>Routines (slots unrolled into selectors, drivers, components): {rlinks}; "
                f"{sum(rtally.values())} known in all.</p>"
                "<p class='mut'>Needed = called in the traced real 7.5.5 boot from code Executor runs "
                "(Finder; System WDEF/MDEF/CDEF/LDEF/MBDF not served by Executor; extensions if on). "
                "<b class='warn'>todo</b>: Executor lacks it. <b style='color:var(--acc)'>basilisk</b>: "
                "host code exists in the Basilisk II core. <span class='bad'>Struck out</span>: only Apple "
                "ROM/patch code, hardware/boot, or System code Executor replaces calls it. Dispatchers list "
                "their selectors below the slot.</p>"
                "<div class='wrap'><table><tr><th>Trap</th><th>Routine</th><th>Verdict</th><th>Calls</th>"
                "<th>Why</th><th>Entry in the real table / callers</th></tr>" + "".join(trs) + "</table></div>")
        return self.page("Needs (to Finder)", body)

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
                     "/needs": lambda: site.needs(q),
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
