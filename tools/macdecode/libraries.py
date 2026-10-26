"""Shared libraries: what programs import, what Apple's libraries export,
what Executor answers (docs/executor/LIBRARIES.md).

    tools/macdecode/libraries.py --scan [IMAGE ...]   build the index
    tools/macdecode/libraries.py                       print a summary

The index (<storage>/macdecode/libraries.json) has three parts:
  imports  every PEF container (PowerPC and CFM-68K) on the disk images,
           read for its import tables: per library and symbol, how many
           containers import it and on which images;
  apple    the export tables of Apple's libraries as MPW ships them (the
           stub libraries in MPW's SharedLibraries folder: names only);
  sources  data exports registered in Executor's sources (exportData,
           addPPCData), which the definitions don't list yet.
What Executor answers comes from the definitions themselves, read each
time (`executor_exports`), so editing a YAML file shows on the next page
load. The web view (`macdecode.py SNAPSHOT --serve`, /libraries) joins
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
SOURCES = REPO / "src/executor/romlib"
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
        no, _, _, cnt, first, _ = struct.unpack_from(">IIIIIB", b, L + 56 + 24 * k)
        syms = []
        for j in range(first, first + cnt):
            w = struct.unpack_from(">I", b, symtab + 4 * j)[0]
            syms.append((cstr(b, L + strs + (w & 0xFFFFFF)), (w >> 24) & 0xF))
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
                        e = imports[arch][lib].setdefault(s, {"n": 0, "images": [], "class": CLASS.get(c, c)})
                        e["n"] += 1
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


def scan_sources():
    """sources[library] = [data symbol, ...] registered in Executor's code."""
    out = defaultdict(set)
    for p in SOURCES.rglob("*.cpp"):
        s = p.read_text(errors="replace")
        for m in re.finditer(r'addPPCData\("(\w+)", "([^"]+)"', s):
            out[m.group(1)].add(m.group(2))
        if "exportData(LIB" in s:
            lib = re.search(r'LIB(?:\[\])?\s*=\s*"(\w+)"', s)
            lib = lib and lib.group(1)
            for m in re.finditer(r'exportData\(LIB, "([^"]+)"', s):
                out[lib or "?"].add(m.group(1))
    return {k: sorted(v) for k, v in out.items()}


def default_images():
    skip = re.compile(r"^(test-|.*-service-|installers-)")
    return sorted(str(p) for p in (STORAGE / "images").glob("*.img") if not skip.match(p.name))


def scan(images=None):
    index = {"imports": scan_images(images or default_images()), "apple": scan_apple(),
             "sources": scan_sources()}
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


def executor_exports():
    """{library: {symbol: how}}: how is "typed", "raw", "ot" (both 68k ASLM
    and PowerPC), or "trap" (InterfaceLib's export of a trap Executor
    implements)."""
    out = defaultdict(dict)
    for f in sorted(DEFS.glob("*.yaml")):
        for item in yaml.safe_load(f.read_text()) or []:
            fn = item.get("function") if isinstance(item, dict) else None
            if not fn:
                continue
            lib = fn.get("library")
            if lib:
                how = "ot" if fn.get("aslm") else "raw" if fn.get("raw") else "typed"
                out[lib][fn["name"]] = {"how": how, "file": f.stem,
                                        "aslm": fn.get("aslm"), "executor": fn.get("executor")}
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
    src = index.get("sources", {}) if index else {}
    apple = index.get("apple", {}) if index else {}
    imports = index.get("imports", {}) if index else {}
    libs = set(ex) | set(src) | set(apple) | {l for a in imports.values() for l in a}
    out = []
    for lib in sorted(libs):
        target = ALIASES.get(lib, lib)
        syms = set(ex.get(target, {})) if lib == target else set()
        syms |= set(src.get(lib, [])) if lib == target else set()
        syms |= set(apple.get(lib, {}).get("exports", {}))
        imp = {a: imports.get(a, {}).get(lib, {}) for a in ("pwpc", "m68k")}
        syms |= set(imp["pwpc"]) | set(imp["m68k"])
        for s in sorted(syms):
            e = ex.get(target, {}).get(s)
            if not e and s in src.get(target, []):
                e = {"how": "data", "file": "sources", "aslm": None, "executor": None}
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


def main():
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
