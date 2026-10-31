"""Shared libraries: what programs import, what Apple's libraries export,
what Executor answers (docs/executor/LIBRARIES.md).

    tools/macdecode/libraries.py --scan [IMAGE ...]   build the index
    tools/macdecode/libraries.py                       print a summary
    tools/macdecode/libraries.py --census [--rom ROM] [--system FOLDER]
        [--logs DIR-OR-LOG ...] [--out FILE.md]
        the libraries Executor answers: each symbol Apple's export and
        Executor lacks, ranked by calls seen in session logs (a grid's
        NNN.log, unimplemented.log) and importers, weak imports flagged
    tools/macdecode/libraries.py --coverage [--rom ROM] [--system FOLDER]
        Executor's exports against Apple's libraries in a Power Mac ROM and
        a System file (an extracted System Folder, Executor's %System form):
        their names, read from the user's files each time

The index (<storage>/macdecode/libraries.json) has two parts:
  imports  every PEF container (PowerPC and CFM-68K) on the disk images,
           read for its import tables: per library and symbol, how many
           containers import it and on which images;
  apple    the export tables of Apple's libraries as MPW ships them (the
           stub libraries in MPW's SharedLibraries folder: names only).
What Executor answers is its export tables as CFM binds them, written by
the executor_tests export test (build/executor-exports.json; $EXECUTOR_EXPORTS
for another build), each entry annotated from the definitions (which file,
how). Without that file, the definitions alone (`definition_exports`: the
names the trap macros expand, LMGet/LMSet, Sync/Async, flag variants, are
missed). The web view (`macdecode.py SNAPSHOT --serve`, /libraries) joins
the three.
"""
import json
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
STORAGE = Path(os.environ.get("MACEMU_STORAGE", Path.home() / "storage"))
INDEX = STORAGE / "macdecode" / "libraries.json"
DEFS = REPO / "src/executor/multiversal/defs"
MPW_DISK = STORAGE / "images" / "mpw.img"
STUBS = ":Interfaces&Libraries:Libraries:SharedLibraries:"


# ---- PEF ----------------------------------------------------------------

def loader(b, off):
    """The loader section's offset in container `off`, or None."""
    nsec = struct.unpack_from(">H", b, off + 32)[0]
    for s in range(nsec):
        h = off + 40 + 28 * s
        _, _, _, _, _, coff, kind = struct.unpack_from(">iIIIIIB", b, h)
        if kind == 4:
            return off + coff
    return None


def cstr(b, at):
    end = b.find(b"\0", at)
    return b[at:end].decode("mac_roman", "replace")


NAME = re.compile(r"^[^\x00-\x1f\x7f]{1,63}$")


def pef_imports(b, off):
    """{library: [(symbol, class), ...]} of the container at `off`; {} for
    something that only looks like one ('Joy!peff' inside other data)."""
    if struct.unpack_from(">I", b, off + 12)[0] != 1 or struct.unpack_from(">H", b, off + 32)[0] > 64:
        return {}
    L = loader(b, off)
    if L is None:
        return {}
    f = struct.unpack_from(">iIiIiIIIIIIIII", b, L)
    nlib, strs = f[6], f[10]
    if nlib > 256 or f[7] > 100000:
        return {}
    symtab = L + 56 + 24 * nlib
    out = {}
    for k in range(nlib):
        no, _, _, cnt, first, options = struct.unpack_from(">IIIIIB", b, L + 56 + 24 * k)
        weak_lib = bool(options & 0x40)        # kPEFWeakImportLibMask
        syms = []
        for j in range(first, first + cnt):
            w = struct.unpack_from(">I", b, symtab + 4 * j)[0]
            weak = weak_lib or bool(w & 0x80000000)   # kPEFWeakImportSymMask
            syms.append((cstr(b, L + strs + (w & 0xFFFFFF)), ((w >> 24) & 0xF) | (0x80 if weak else 0)))
        name = cstr(b, L + strs + no)
        if not NAME.match(name) or any(not NAME.match(x) for x, _ in syms[:8]):
            return {}
        out[name] = syms
    return out


def pef_exports(b, off):
    """[(symbol, class), ...] the container at `off` exports."""
    L = loader(b, off)
    if L is None:
        return []
    f = struct.unpack_from(">iIiIiIIIIIIIII", b, L)
    strs, hoff, hpow, nexp = f[10], f[11], f[12], f[13]
    keys = L + hoff + 4 * (1 << hpow)
    syms = keys + 4 * nexp
    out = []
    for i in range(nexp):
        nlen = struct.unpack_from(">I", b, keys + 4 * i)[0] >> 16
        cls_name = struct.unpack_from(">I", b, syms + 10 * i)[0]
        at = L + strs + (cls_name & 0xFFFFFF)
        out.append((b[at:at + nlen].decode("mac_roman", "replace"), (cls_name >> 24) & 0xF))
    return out


def macbinary_forks(path):
    b = Path(path).read_bytes()
    dl, rl = struct.unpack_from(">II", b, 83)
    roff = 128 + ((dl + 127) // 128) * 128
    return b[128:128 + dl], b[roff:roff + rl]


def cfrg_members(rf):
    """[(arch, name, offset, length)] from a resource fork's 'cfrg' 0."""
    do, mo, _, ml = struct.unpack_from(">IIII", rf, 0)
    m = rf[mo:mo + ml]
    tl = struct.unpack_from(">H", m, 24)[0]
    nt = struct.unpack_from(">H", m, 28)[0] + 1
    for i in range(nt):
        t, _, ro = struct.unpack_from(">4sHH", m, 30 + 8 * i)
        if t != b"cfrg":
            continue
        a3 = struct.unpack_from(">I", m, tl + ro + 4)[0] & 0xFFFFFF
        ln = struct.unpack_from(">I", rf, do + a3)[0]
        c = rf[do + a3 + 4:do + a3 + 4 + ln]
        n = struct.unpack_from(">I", c, 28)[0]
        o, out = 32, []
        for _ in range(n):
            arch = c[o:o + 4].decode()
            off, length = struct.unpack_from(">II", c, o + 24)
            size = struct.unpack_from(">H", c, o + 40)[0]
            out.append((arch, c[o + 43:o + 43 + c[o + 42]].decode("mac_roman"), off, length))
            o += size
        return out
    return []


CLASS = {0: "code", 1: "data", 2: "tvect", 3: "toc", 4: "glue"}


# ---- scanning -------------------------------------------------------------

def scan_images(images):
    """imports[arch][library][symbol] = {"n": containers, "images": [...]}"""
    imports = defaultdict(lambda: defaultdict(dict))
    for img in images:
        n = 0
        with open(img, "rb") as f:
            b = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        i = 0
        while True:
            i = b.find(b"Joy!peff", i)
            if i < 0:
                break
            arch = b[i + 8:i + 12].decode("latin-1")
            if arch in ("pwpc", "m68k"):
                try:
                    libs = pef_imports(b, i)
                except Exception:
                    libs = {}
                n += 1
                for lib, syms in libs.items():
                    for s, c in syms:
                        e = imports[arch][lib].setdefault(s, {"n": 0, "weak": 0, "images": [],
                                                              "class": CLASS.get(c & 0xF, c & 0xF)})
                        e["n"] += 1
                        e["weak"] += bool(c & 0x80)
                        if Path(img).stem not in e["images"]:
                            e["images"].append(Path(img).stem)
            i += 12
        print(f"{Path(img).name}: {n} containers", file=sys.stderr)
    return imports


def scan_apple(mpw_disk=MPW_DISK):
    """apple[library] = {"arch": [...], "exports": {symbol: class}} from MPW's
    stub libraries (a fat stub holds a fragment per library: OpenTransportLib
    carries OTClientLib, OTUtilityLib, ...)."""
    apple = {}
    if not mpw_disk.exists() or not shutil.which("hcopy"):
        return apple
    with tempfile.TemporaryDirectory(dir=STORAGE / "tmp" if (STORAGE / "tmp").is_dir() else None) as tmp:
        copy = Path(tmp) / "mpw.img"
        shutil.copy(mpw_disk, copy)          # hmount would write the original
        env = dict(os.environ, HOME=tmp)     # hfsutils keeps its state in ~/.hcwd
        subprocess.run(["hmount", str(copy)], env=env, check=True, capture_output=True)
        names = subprocess.run(["hls", "-1", STUBS], env=env, capture_output=True, text=True,
                               encoding="mac_roman").stdout.split("\n")
        for name in filter(None, (n.strip() for n in names)):
            out = Path(tmp) / "stub.bin"
            r = subprocess.run(["hcopy", "-m", STUBS + name, str(out)], env=env, capture_output=True)
            if r.returncode:
                continue
            try:
                data, rf = macbinary_forks(out)
                for arch, lib, off, length in cfrg_members(rf):
                    b = data[off:off + length] if length else data[off:]
                    if b[:8] != b"Joy!peff":
                        continue
                    e = apple.setdefault(lib, {"arch": [], "exports": {}, "file": name})
                    if arch not in e["arch"]:
                        e["arch"].append(arch)
                    for s, c in pef_exports(b, 0):
                        e["exports"][s] = CLASS.get(c, c)
            except Exception as ex:
                print(f"{name}: {ex}", file=sys.stderr)
        subprocess.run(["humount"], env=env, capture_output=True)
    return apple


def default_images():
    skip = re.compile(r"^(test-|.*-service-|installers-)")
    return sorted(str(p) for p in (STORAGE / "images").glob("*.img") if not skip.match(p.name))


def scan(images=None):
    index = {"imports": scan_images(images or default_images()), "apple": scan_apple()}
    INDEX.parent.mkdir(parents=True, exist_ok=True)
    INDEX.write_text(json.dumps(index))
    return index


def load_index():
    return json.loads(INDEX.read_text()) if INDEX.exists() else None


# ---- what Executor answers, from the definitions ---------------------------

# Libraries a CFM-68K program imports that Executor answers from another
# library's exports (builtinlibs.cpp, interfacelib_aliases68k; NuStdCLib).
ALIASES = {"NuStdCLib": "StdCLib", "NuIntEnv": "StdCLib",
           **{n: "InterfaceLib" for n in ("DragLib", "ThreadsLib", "AppearanceLib", "WindowsLib",
                                            "ControlsLib", "MenusLib", "DialogsLib",
                                            "PrivateInterfaceLib", "ObjectSupportLib")}}


EXPORTS = Path(os.environ.get("EXECUTOR_EXPORTS", REPO / "build" / "executor-exports.json"))


def executor_exports():
    """{library: {symbol: how}}, PowerPC and CFM-68K together: Executor's
    export tables (EXPORTS), annotated from the definitions; how is "typed",
    "raw", "ot", "component", "trap" (InterfaceLib's export of a trap), "lowmem"
    (an accessor) or "generated" (a trap's Sync/Async or flag variant)."""
    defs = definition_exports()
    if not EXPORTS.exists():
        print(f"libraries.py: no {EXPORTS} (ctest -R executor.Exports): "
              "from the definitions alone", file=sys.stderr)
        return defs
    out = defaultdict(dict)
    for libs in json.loads(EXPORTS.read_text()).values():
        for lib, names in libs.items():
            for n in names:
                info = defs.get(lib, {}).get(n)
                if not info:
                    how = "lowmem" if n.startswith(("LMGet", "LMSet")) else "generated"
                    info = {"how": how, "file": "", "aslm": None, "executor": None}
                out[lib].setdefault(n, info)
    return out


def definition_exports():
    """{library: {symbol: how}} as the definitions name them: how is "typed",
    "raw", "ot" (both 68k ASLM and PowerPC), or "trap" (InterfaceLib's export
    of a trap Executor implements)."""
    out = defaultdict(dict)
    files = {f: yaml.safe_load(f.read_text()) or [] for f in sorted(DEFS.glob("*.yaml"))}
    component_dispatchers = {i["dispatcher"]["name"] for items in files.values() for i in items
                             if isinstance(i, dict) and "dispatcher" in i
                             and i["dispatcher"].get("selector-location") == "Component"}
    for f, items in files.items():
        for item in items:
            fn = item.get("function") if isinstance(item, dict) else None
            if not fn:
                continue
            lib = fn.get("library")
            if lib:
                how = ("ot" if fn.get("aslm") else "raw" if fn.get("raw")
                       else "component" if fn.get("dispatcher") in component_dispatchers else "typed")
                for one in lib if isinstance(lib, list) else [lib]:
                    # a lowercase C routine (`cglue:`): Executor's cglue_NAME
                    impl = fn.get("executor") or (fn.get("cglue") and "cglue_" + fn["name"])
                    out[one][fn["name"]] = {"how": how, "file": f.stem,
                                            "aslm": fn.get("aslm"), "executor": impl}
            elif fn.get("executor") and item.get("api") != "carbon":
                out["InterfaceLib"].setdefault(fn["name"], {"how": "trap", "file": f.stem,
                                                            "aslm": None, "executor": fn.get("executor")})
    return out


# Apple's system libraries beyond those MPW's stubs and Executor know.
SYSTEM_EXTRA = {"CarbonLib", "OTKernelLib", "OTConfigLib", "OTKernelUtilLib", "OTClientLib",
                "OTClientUtilLib", "OTUtilityLib", "OTInetClientLib", "OTGlobalLib", "OT68KClientContextLib",
                "OTATalkGlobalLib", "OTATalkServicesLib", "OTStackSwapLib", "NavigationLib", "AppleScriptLib",
                "ColorSyncLib", "QuickTimeLib", "QuickDraw™ 3D", "DrawSprocketLib", "InputSprocketLib",
                "SoundSprocketLib", "NetSprocketLib", "TextEncodingConverter", "UnicodeConverter",
                "UTCUtils", "DateTimeLib", "ATSUnicodeLib", "ContextualMenu", "HTMLRenderingLib",
                "URLAccessLib", "FontManager", "CFMPriv_QuickTimeLib", "AppleGuideLib", "SpeechLib",
                "SpeechRecognitionLib", "ControlStripLib", "Translation", "NameRegistryLib",
                "DriverServicesLib", "DriverLoaderLib", "PCILib", "VideoServicesLib", "USBServicesLib",
                "CFMPriv_InterfaceLib", "FileSharingSupportLib", "TelephoneLib", "PCCardLib",
                "PowerMgrLib", "CollectionsLib", "LocalesLib", "TextCommon", "MenusLib", "WindowsLib",
                "ControlsLib", "DialogsLib", "SystemSoundLib", "QuickTimeVRLib", "QTStreamLib",
                "ImageCompressionLib", "ObjectSupportLib", "AppearanceLib", "DragLib", "ThreadsLib",
                "PrivateInterfaceLib", "CursorDevicesGlue", "MathLib", "StdCLib", "InterfaceLib",
                "NuStdCLib", "NuIntEnv", "CarbonCore.vlib", "SLMGlobal"}


def rows(index):
    """One row per (library, symbol) any of the three knows."""
    ex = executor_exports()
    apple = index.get("apple", {}) if index else {}
    imports = index.get("imports", {}) if index else {}
    libs = set(ex) | set(apple) | {l for a in imports.values() for l in a}
    out = []
    for lib in sorted(libs):
        target = ALIASES.get(lib, lib)
        syms = set(ex.get(target, {})) if lib == target else set()
        syms |= set(apple.get(lib, {}).get("exports", {}))
        imp = {a: imports.get(a, {}).get(lib, {}) for a in ("pwpc", "m68k")}
        syms |= set(imp["pwpc"]) | set(imp["m68k"])
        for s in sorted(syms):
            e = ex.get(target, {}).get(s)
            a = apple.get(lib, {}).get("exports", {})
            out.append({
                "lib": lib, "sym": s, "alias": target if target != lib else "",
                "system": lib in apple or lib in ex or lib in ALIASES or lib in SYSTEM_EXTRA,
                "how": e["how"] if e else "", "def": e["file"] if e else "",
                "aslm": e["aslm"] if e else None, "impl": e["executor"] if e else None,
                "apple": a.get(s, ""),
                "ppc": imp["pwpc"].get(s, {}).get("n", 0), "cfm68k": imp["m68k"].get(s, {}).get("n", 0),
                "images": sorted(set(imp["pwpc"].get(s, {}).get("images", []))
                                 | set(imp["m68k"].get(s, {}).get("images", []))),
                "class": (imp["pwpc"].get(s) or imp["m68k"].get(s) or {}).get("class") or a.get(s, ""),
            })
    return out


# ---- coverage against Apple's own libraries ------------------------------

def rom_libraries(path):
    """{library: {symbol, ...}} from an Old World Power Mac ROM: its 'nlib'
    ROM resources (type, ID, name; the entry's data pointer just before),
    each a PEF container. A library in more than one container (the G3's
    InterfaceLib: the Toolbox's and the math routines') is their union."""
    b = Path(path).read_bytes()
    out = defaultdict(set)
    for m in re.finditer(b"nlib", b):
        i = m.start()
        n = b[i + 7] if i + 7 < len(b) else 0
        name = b[i + 8:i + 8 + n]
        if not n or not all(32 <= c < 127 for c in name):
            continue
        data = struct.unpack_from(">I", b, i - 4)[0]
        if b[data:data + 8] != b"Joy!peff":
            continue
        out[name.decode()] |= {s for s, _ in pef_exports(b, data)}
    return dict(out)


def appledouble_resource_fork(path):
    """The resource fork of an AppleDouble header file (Executor's %NAME)."""
    d = Path(path).read_bytes()
    for i in range(struct.unpack_from(">H", d, 24)[0]):
        eid, off, ln = struct.unpack_from(">III", d, 26 + 12 * i)
        if eid == 2:
            return d[off:off + ln]
    return b""


def system_libraries(folder):
    """{library: {symbol, ...}} a System file brings: its 'nlib' resources
    and the fragments in its data fork its 'cfrg's list (8.x's Toolbox
    libraries). `folder`: an extracted System Folder (Executor's, the
    resource fork in %System)."""
    sys.path.insert(0, str(Path(__file__).parent))
    import rsrc
    data = (Path(folder) / "System").read_bytes()
    res = rsrc.parse_resource_fork(appledouble_resource_fork(Path(folder) / "%System"))
    out = defaultdict(set)
    for (rtype, _), (name, b) in res.items():
        if rtype == "nlib" and name and b[:8] == b"Joy!peff":
            out[name] |= {s for s, _ in pef_exports(b, 0)}
        elif rtype == "cfrg":
            n, o = struct.unpack_from(">I", b, 28)[0], 32
            for _ in range(n):
                where = b[o + 0x17]
                off, ln = struct.unpack_from(">II", b, o + 0x18)
                size = struct.unpack_from(">H", b, o + 0x28)[0]
                lib = b[o + 0x2b:o + 0x2b + b[o + 0x2a]].decode("mac_roman")
                if where == 1 and b[o:o + 4] == b"pwpc" and data[off:off + 8] == b"Joy!peff":
                    out[lib] |= {s for s, _ in pef_exports(data, off)}
                o += size
    return dict(out)


def coverage(rom, system, index):
    """Per Apple library: its exports, how many Executor has, how many the
    disk images' programs import and of those Executor lacks."""
    apple = defaultdict(set)
    for source in (rom_libraries(rom) if rom else {}, system_libraries(system) if system else {}):
        for lib, syms in source.items():
            apple[lib] |= syms
    if not EXPORTS.exists():
        sys.exit(f"no {EXPORTS}: run ctest -R executor.Exports")
    ex = json.loads(EXPORTS.read_text())["pwpc"]
    imports = index.get("imports", {}).get("pwpc", {}) if index else {}
    print(f"{'library':24} {'Apple':>6} {'Executor':>9} {'%':>4} {'imported':>9} {'lacking':>8}")
    for lib in sorted(apple, key=lambda l: -len(apple[l])):
        have = set(ex.get(lib, []))
        used = set(imports.get(lib, {}))
        print(f"{lib:24} {len(apple[lib]):6} {len(apple[lib] & have):9} "
              f"{100 * len(apple[lib] & have) // max(1, len(apple[lib])):4} "
              f"{len(used & apple[lib]):9} {len((used & apple[lib]) - have):8}")


def calls_seen(logs):
    """{symbol: (calls, weak-nil, unresolved)} from Executor session logs (the
    grid's NNN.log, a run's log) and <storage>/executor/unimplemented.log:
    what programs really called or bound and Executor lacked."""
    seen = defaultdict(lambda: [0, 0, 0])
    pats = [(re.compile(r"\[unimplemented\] PowerPC (\w+)"), 0),
            (re.compile(r"\tPowerPC (\w+)"), 0),
            (re.compile(r"CFM: weak import (\w+) unresolved"), 1),
            (re.compile(r"CFM: unresolved import (\w+)"), 2)]
    for log in logs:
        try:
            text = Path(log).read_text(errors="replace")
        except OSError:
            continue
        for pat, k in pats:
            for m in pat.finditer(text):
                seen[m.group(1)][k] += 1
    return seen


def census(rom, system, index, logs, out):
    """The libraries Executor answers itself, each symbol Apple's exports and
    Executor lacks: how many programs import it (weakly), how often a
    session called it or bound it to nothing. Markdown, most wanted first."""
    apple = defaultdict(set)
    for source in (rom_libraries(rom) if rom else {}, system_libraries(system) if system else {}):
        for lib, syms in source.items():
            apple[lib] |= syms
    if not EXPORTS.exists():
        sys.exit(f"no {EXPORTS}: run ctest -R executor.Exports")
    ex = json.loads(EXPORTS.read_text())["pwpc"]
    imports = index.get("imports", {}).get("pwpc", {}) if index else {}
    seen = calls_seen(logs)
    lines = ["# Executor's libraries against Apple's (PowerPC)", "",
             f"Apple's exports: {rom or '-'} and {system or '-'}. Importers: the disk "
             "images' containers (libraries.py --scan). Calls: session logs "
             f"({len(logs)} files): called unimplemented / weak import left nil / "
             "unresolved at load.", "",
             "| library | Apple | Executor | missing | missing & imported |",
             "|---|---:|---:|---:|---:|"]
    rows_ = []
    for lib in sorted(ex):
        if lib not in apple:
            continue
        have = set(ex[lib])
        missing = apple[lib] - have
        used = imports.get(lib, {})
        lines.append(f"| {lib} | {len(apple[lib])} | {len(apple[lib] & have)} | {len(missing)} "
                     f"| {sum(1 for m in missing if m in used)} |")
        for m in missing:
            u = used.get(m, {})
            c = seen.get(m, [0, 0, 0])
            rows_.append((lib, m, u.get("n", 0), u.get("weak", 0), len(u.get("images", [])), c))
    rows_.sort(key=lambda r: (-(r[5][0] + r[5][1]), -r[5][2], -r[2], r[0], r[1]))
    lines += ["", "## Missing symbols, most wanted first", "",
              "| library | symbol | importers | weak | images | called | weak nil | unresolved |",
              "|---|---|---:|---:|---:|---:|---:|---:|"]
    for lib, m, n, weak, imgs, c in rows_:
        if not (n or any(c)):
            continue
        lines.append(f"| {lib} | {m} | {n} | {weak} | {imgs} | {c[0]} | {c[1]} | {c[2]} |")
    text = "\n".join(lines) + "\n"
    if out:
        Path(out).write_text(text)
        print(out)
    else:
        print(text)


def main():
    if "--census" in sys.argv:
        args = sys.argv[sys.argv.index("--census") + 1:]
        opt = lambda flag: args[args.index(flag) + 1] if flag in args else None
        logs = [str(STORAGE / "executor" / "unimplemented.log")]
        for d in (a for i, a in enumerate(args) if i and args[i - 1] == "--logs"):
            logs += [str(p) for p in Path(d).glob("*.log")] if Path(d).is_dir() else [d]
        census(opt("--rom"), opt("--system"), load_index(), logs, opt("--out"))
        return
    if "--coverage" in sys.argv:
        args = sys.argv[sys.argv.index("--coverage") + 1:]
        opt = lambda flag: args[args.index(flag) + 1] if flag in args else None
        if not opt("--rom") and not opt("--system"):
            sys.exit("--coverage [--rom ROM] [--system EXTRACTED-SYSTEM-FOLDER]")
        coverage(opt("--rom"), opt("--system"), load_index())
        return
    if "--scan" in sys.argv:
        images = [a for a in sys.argv[1:] if a != "--scan"]
        scan(images or None)
    index = load_index()
    if not index:
        sys.exit(f"no index at {INDEX}: run with --scan")
    by = defaultdict(lambda: [0, 0, 0, 0])
    for r in rows(index):
        if not r["system"]:
            continue
        t = by[r["lib"]]
        t[0] += 1
        t[1] += bool(r["how"])
        used = r["ppc"] or r["cfm68k"]
        t[2] += bool(used)
        t[3] += bool(used and not r["how"])
    print(f"{'library':28} {'symbols':>8} {'Executor':>9} {'imported':>9} {'lacking':>8}")
    for lib, (n, has, used, lack) in sorted(by.items(), key=lambda x: -x[1][2]):
        print(f"{lib:28} {n:8} {has:9} {used:9} {lack:8}")


if __name__ == "__main__":
    main()
