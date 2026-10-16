#!/usr/bin/env python3
"""macfetch: find, pin and provision classic Mac software.

Finding: a local index of file names (built once, `update`), from

  garden   The Macintosh Garden's files: listed from its mirror FTP
           (ftp.macintosh.garden: apps, games, manuals; current), merged
           with its 2018-2020 snapshot on the Internet Archive (collection
           Macintosh_Garden_Collection), which also gives most files an MD5.
  infomac  The Info-Mac archive (frozen 2005), via its all-files.txt index.

and, for titles, years and descriptions, the Garden's own site: `garden`
runs one search there, `page` fetches one title page (kept in the cache).
Nothing crawls the site.

Pinning: `pin` writes manifest entries: file name, size, MD5 and every place
the file can be fetched from (Archive item, Garden path, Info-Mac path, URL).
Hashes come from the Archive's metadata, the Garden page or the Garden FTP
(XMD5), so pinning downloads nothing except Info-Mac files and plain URLs.

Provisioning: `provision MANIFEST` makes the output directory hold exactly
the pinned files: ones already there with the right size and MD5 are kept,
the rest are fetched from the first source that works and checked against
the pin. Any file that can't be fetched or doesn't match fails the run.
A manifest needs nothing else: not the index, not the cache.

  macfetch.py search QUERY [-s SRC]    offline search of file names
  macfetch.py garden QUERY             search the Garden site (one request)
  macfetch.py page N|URL               a Garden title page: metadata + files
  macfetch.py info N                   where a result lives (+ Info-Mac abstract)
  macfetch.py get N [N ...] [-o DIR]   download results, verified
  macfetch.py pin N [N ...] -m FILE    add results to a manifest
  macfetch.py pin --ia ITEM/FILE ...   ... an Internet Archive file
  macfetch.py pin --url URL ...        ... any URL (downloaded to hash it)
  macfetch.py provision FILE [-o DIR]  fetch and verify everything pinned
  macfetch.py update                   rebuild the index
  macfetch.py [tui [QUERY]]            interactive browser

N refers to the numbered results of the last search or page.
Standard library only.
"""

import argparse
import concurrent.futures
import curses
import ftplib
import glob
import hashlib
import html
import json
import netrc
import os
import re
import ssl
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")), "macfetch")
INDEX = os.path.join(CACHE, "index.json")
LAST = os.path.join(CACHE, "last.json")
PAGES = os.path.join(CACHE, "pages")
STORAGE = os.path.expanduser(os.environ.get("MACEMU_STORAGE") or "~/storage")
DEFAULT_OUT = os.path.join(STORAGE, "downloads")
UA = "macfetch/1.0 (classic Mac software fetcher)"

GARDEN_SITE = "https://macintoshgarden.org"
GARDEN_COLLECTION = "Macintosh_Garden_Collection"
# The Garden's public mirror account; a ~/.netrc entry for the host wins.
GARDEN_FTP = "ftp.macintosh.garden"
GARDEN_FTP_DIRS = ("apps", "games", "manuals")
# HTTP copies of the Garden's file store, by path ("apps/X.sit"). The first
# is the mirror the Garden's own pages link; the second a February 2026 copy.
GARDEN_MIRRORS = ["https://old.mac.gdn/", "https://oldapplestuff.com/download/Macintosh/Macintosh_Garden/"]
INFOMAC_MIRRORS = ["https://ftp.funet.fi/pub/mac/info-mac/", "https://archive.info-mac.org/"]
SOURCES = ("garden", "infomac")
# Where a pinned file can come from, best first: the Archive is the fastest.
LOCATORS = ("ia", "garden", "infomac", "url")

# Files in the Garden items that are not software: IA's own metadata and
# the per-item cover images.
SKIP_EXT = (".jpg", ".jpeg", ".png", ".gif", ".md5", ".torrent", "_meta.xml", "_files.xml", "_meta.sqlite")

# macintoshgarden.org doesn't send its intermediate certificate; this is it
# (Sectigo Public Server Authentication CA DV R36, issued by the Root R46
# that system trust stores carry).
TLS = ssl.create_default_context()
TLS.load_verify_locations(os.path.join(HERE, "sectigo-dv-r36.pem"))


def http_get(url, timeout=60, headers=None):
    req = urllib.request.Request(url, headers={"User-Agent": UA, **(headers or {})})
    return urllib.request.urlopen(req, timeout=timeout, context=TLS)


def http_json(url, timeout=60):
    with http_get(url, timeout) as r:
        return json.load(r)


def http_text(url, timeout=60):
    with http_get(url, timeout) as r:
        return r.read().decode("utf-8", "replace")


def human(n):
    if n is None:
        return "?"
    for unit in ("B", "K", "M", "G"):
        if n < 1024 or unit == "G":
            return f"{n:.0f}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024


def quote(path):
    return urllib.parse.quote(path, safe="/")


# ---------------------------------------------------------------- index

def garden_items():
    q = urllib.parse.urlencode([("q", f"collection:{GARDEN_COLLECTION}"), ("fl[]", "identifier"),
                                ("rows", "2000"), ("output", "json")])
    docs = http_json("https://archive.org/advancedsearch.php?" + q)["response"]["docs"]
    return sorted(d["identifier"] for d in docs)


def garden_ftp_login():
    try:
        auth = netrc.netrc().authenticators(GARDEN_FTP)
        if auth:
            return auth[0], auth[2]
    except (OSError, netrc.NetrcParseError):
        pass
    return "mirror", "mirror"


def garden_ftp():
    ftp = ftplib.FTP(GARDEN_FTP, timeout=120)
    ftp.login(*garden_ftp_login())
    return ftp


def garden_ftp_files():
    ftp = garden_ftp()
    out = []
    try:
        for d in GARDEN_FTP_DIRS:
            for name, facts in ftp.mlsd(d, ["type", "size", "modify"]):
                if facts.get("type") != "file":
                    continue
                m = facts.get("modify", "")
                out.append({"src": "garden", "name": name, "size": int(facts.get("size", 0)) or None,
                            "date": f"{m[:4]}-{m[4:6]}-{m[6:8]}" if m else "", "where": d,
                            "garden": f"{d}/{name}"})
    finally:
        ftp.close()
    return out


def ia_file_entry(ident, f):
    e = {"name": f["name"].rsplit("/", 1)[-1], "size": int(f["size"]) if f.get("size") else None,
         "ia": f"{ident}/{f['name']}"}
    if f.get("md5"):
        e["md5"] = f["md5"]
    return e


def garden_ia_files(ident):
    meta = http_json(f"https://archive.org/metadata/{quote(ident)}", timeout=120)
    group = ident.removeprefix("Macintosh_Garden_").replace("_", " ")
    out = []
    for f in meta.get("files", []):
        name = f["name"]
        if f.get("source") != "original" or name.lower().endswith(SKIP_EXT) or name.startswith("__ia_"):
            continue
        # No date: the item's mtimes are upload times, not the software's.
        out.append({"src": "garden", "date": "", "where": group, **ia_file_entry(ident, f)})
    return out


MONTHS = {m: i for i, m in enumerate("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split(), 1)}


def infomac_files():
    last_err = None
    for base in INFOMAC_MIRRORS:
        try:
            with http_get(base + "help/all-files.txt", timeout=120) as r:
                text = r.read().decode("latin-1")
            break
        except OSError as e:
            last_err = e
    else:
        raise last_err
    out = []
    # "-r    161034 May 21 1997 dev/card/guide-to-hypercard.hqx"; links start with "l".
    line_re = re.compile(r"^(\S+)\s+(\d+)\s+(\w{3})\s+(\d+)\s+(\d{4})\s+(.+)$")
    for line in text.splitlines():
        m = line_re.match(line)
        if not m or m.group(1).startswith("l") or "/" not in m.group(6):
            continue
        path = m.group(6)
        date = f"{m.group(5)}-{MONTHS.get(m.group(3), 0):02d}-{int(m.group(4)):02d}"
        out.append({"src": "infomac", "name": path.rsplit("/", 1)[1], "size": int(m.group(2)),
                    "date": date, "where": path.rsplit("/", 1)[0], "infomac": path})
    return out


def merge_garden(ftp_files, ia_files):
    """One entry per Garden file (name, size). The Archive's yearly snapshots
    repeat most files; an FTP file also in a snapshot gets its Archive copy
    and MD5. The date is the FTP's, the Garden's upload date."""
    seen = {(e["name"].lower(), e["size"]): e for e in ftp_files}
    for e in sorted(ia_files, key=lambda e: e["where"], reverse=True):
        key = (e["name"].lower(), e["size"])
        if key in seen:
            seen[key].setdefault("ia", e["ia"])
            if "md5" in e:
                seen[key].setdefault("md5", e["md5"])
        else:
            seen[key] = e
    return list(seen.values())


def build_index(log=print):
    os.makedirs(CACHE, exist_ok=True)
    entries = []
    log("Info-Mac: fetching all-files.txt ...")
    entries += infomac_files()
    log(f"Info-Mac: {len(entries)} files")
    log(f"Garden: listing ftp://{GARDEN_FTP}/ ...")
    try:
        ftp_files = garden_ftp_files()
        log(f"Garden: {len(ftp_files)} files on the FTP")
    except (OSError, ftplib.Error) as e:
        ftp_files = []
        log(f"Garden: FTP unavailable ({e}); using the Internet Archive snapshot only")
    log(f"Garden: listing {GARDEN_COLLECTION} on the Internet Archive ...")
    items = garden_items()
    done = n = 0
    failed = []
    ia_files = []
    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        futs = {pool.submit(garden_ia_files, i): i for i in items}
        for fut in concurrent.futures.as_completed(futs):
            done += 1
            try:
                files = fut.result()
                ia_files += files
                n += len(files)
            except Exception as e:
                failed.append(futs[fut])
                log(f"  {futs[fut]}: {e}")
            if done % 20 == 0 or done == len(items):
                log(f"Garden: {done}/{len(items)} Archive items, {n} files")
    garden = merge_garden(ftp_files, ia_files)
    log(f"Garden: {len(garden)} distinct files, {sum('ia' in e for e in garden)} on the Archive, "
        f"{sum('md5' in e for e in garden)} with an MD5")
    entries += garden
    with open(INDEX + ".tmp", "w") as f:
        json.dump({"version": 2, "built": time.time(), "entries": entries, "failed": failed}, f)
    os.replace(INDEX + ".tmp", INDEX)
    if failed:
        log(f"{len(failed)} Garden items could not be listed; run `update` again to retry.")
    return entries


def load_index(log=print):
    if os.path.exists(INDEX):
        with open(INDEX) as f:
            index = json.load(f)
        if index.get("version") == 2:
            return index["entries"]
        log("Index is from an older macfetch; rebuilding it (about a minute).")
    else:
        log("No index yet; building it (one-off, about a minute).")
    return build_index(log)


# --------------------------------------------------------- Garden pages

def strip_tags(s):
    return re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", " ", s))).strip()


def garden_search(query, pages=1):
    """Titles from the Garden's own search (apps and games), best title
    matches first; the site itself orders by recent activity."""
    hits = []
    for p in range(pages):
        url = f"{GARDEN_SITE}/search/node/{quote(query + ' type:app,game')}" + (f"?page={p}" if p else "")
        text = http_text(url)
        for m in re.finditer(r'<dt class="title">\s*<a href="([^"]+)">(.*?)</a>.*?'
                             r'<p class="search-info">([^<]*)</p>', text, re.S):
            kind = m.group(3).split(" - ")[0].strip()
            hits.append({"kind": "page", "title": strip_tags(m.group(2)), "page": m.group(1),
                         "type": kind})
        if 'class="pager-next' not in text:
            break
    words = norm(query).split()
    return sorted(hits, key=lambda h: -sum(w in norm(h["title"]).split() for w in words))


def page_url(ref):
    if ref.startswith("http"):
        return ref.replace("http://", "https://", 1)
    return f"{GARDEN_SITE}/{ref.strip('/')}"


def page_cache_path(url):
    return os.path.join(PAGES, urllib.parse.urlsplit(url).path.strip("/").replace("/", "__") + ".json")


def parse_garden_page(text, url):
    def row(label):
        m = re.search(rf"<strong>{label}:</strong></td><td>(.*?)</td></tr>", text, re.S)
        if not m:
            return []
        links = re.findall(r"<a [^>]*>(.*?)</a>", m.group(1), re.S)
        return [strip_tags(x) for x in links] if links else [strip_tags(m.group(1))]

    files = []
    for block in re.split(r'<div class="note (?=download|manual)', text)[1:]:
        block = re.split(r'<div class="note (?!numeral)', block)[0]
        m = re.search(r'//old\.mac\.gdn/((?:apps|games|manuals)/[^"]+)"', block) or \
            re.search(r'//download\.macintoshgarden\.org/((?:apps|games|manuals)/[^"?]+)', block)
        if not m:
            continue
        path = urllib.parse.unquote(html.unescape(m.group(1)))
        md5 = re.search(r">([0-9a-f]{32})</a>", block)
        date = re.search(r"#\d+, (\d{4}-\d\d-\d\d)", block)
        system = re.search(r"For\s+(.*?)\s*</div>", block, re.S)
        files.append({"name": path.rsplit("/", 1)[1], "garden": path,
                      "md5": md5.group(1) if md5 else None,
                      "date": date.group(1) if date else "",
                      "system": strip_tags(system.group(1)) if system else "",
                      "manual": block.startswith("manual")})
    body = text[:text.find('<div id="comments">')] if '<div id="comments">' in text else text
    tail = body[body.rfind('<div class="note '):]
    paras = [strip_tags(p) for p in re.findall(r"<p>(.*?)</p>", tail, re.S)]
    arch = re.search(r"Architecture:\s*([^<]*)", body)
    reqs = re.search(r"Architecture:[^<]*<ul>(.*?)</ul>", body, re.S)
    title = re.search(r"<h1>(.*?)</h1>", text, re.S)
    year = row("Year released")
    return {"page": url, "title": strip_tags(title.group(1)) if title else "",
            "category": row("Category"), "year": int(year[0]) if year and year[0].isdigit() else None,
            "authors": row("Author"), "publishers": row("Publisher"),
            "architecture": arch.group(1).split() if arch else [],
            "requirements": [strip_tags(li) for li in re.findall(r"<li>(.*?)</li>", reqs.group(1), re.S)]
            if reqs else [],
            "description": [p for p in paras if p], "files": files, "fetched": time.strftime("%Y-%m-%d")}


def garden_page(ref, refresh=False):
    url = page_url(ref)
    cached = page_cache_path(url)
    if not refresh and os.path.exists(cached):
        with open(cached) as f:
            return json.load(f)
    page = parse_garden_page(http_text(url), url)
    if not page["title"] and not page["files"]:
        raise ValueError(f"{url} is not a Garden title page")
    os.makedirs(PAGES, exist_ok=True)
    with open(cached, "w") as f:
        json.dump(page, f, indent=1)
    return page


def cached_pages():
    """garden path -> (title, year, page) for every title page fetched so far."""
    out = {}
    for path in glob.glob(os.path.join(PAGES, "*.json")):
        with open(path) as f:
            p = json.load(f)
        for fl in p["files"]:
            out[fl["garden"]] = (p["title"], p["year"], p["page"])
    return out


# --------------------------------------------------------------- search

def norm(s):
    return re.sub(r"[^a-z0-9]+", " ", s.lower())


def search(entries, query, src=None, limit=200):
    words = norm(query).split()
    if not words:
        return []
    titles = cached_pages()
    hits = []
    for e in entries:
        if src and e["src"] != src:
            continue
        name = norm(e["name"])
        t = titles.get(e.get("garden"))
        hay = name + " " + norm(e["where"]) + (" " + norm(t[0]) if t else "")
        if all(w in hay for w in words):
            # Whole-word hits in the file name first, then shorter names.
            score = sum(2 if re.search(rf"\b{re.escape(w)}", name) else 0 for w in words)
            hits.append((-score, len(e["name"]), e["name"].lower(), e))
    hits.sort(key=lambda h: h[:3])
    out = [dict(h[3]) for h in hits[:limit]]
    for e in out:
        t = titles.get(e.get("garden"))
        if t:
            e["title"], e["year"], e["page"] = t
    return out


def urls_of(e):
    """Where e can be fetched from, best first."""
    urls = []
    if "ia" in e:
        urls.append(f"https://archive.org/download/{quote(e['ia'])}")
    if "garden" in e:
        urls += [m + quote(e["garden"]) for m in GARDEN_MIRRORS]
        urls.append(f"ftp://{GARDEN_FTP}/{quote(e['garden'])}")
    if "infomac" in e:
        urls += [m + quote(e["infomac"]) for m in INFOMAC_MIRRORS]
    if "url" in e:
        urls.append(e["url"])
    return urls


def url_of(e):
    return urls_of(e)[0]


def infomac_abstract(e):
    """The text header Info-Mac put at the top of each .hqx."""
    if "infomac" not in e:
        return None
    with http_get(url_of(e), timeout=30, headers={"Range": "bytes=0-8191"}) as r:
        head = r.read(8192).decode("latin-1")
    cut = head.find("(This file must be converted with BinHex")
    return (head[:cut] if cut >= 0 else head[:2000]).replace("\r", "\n").strip()


# ------------------------------------------------------------- download

class Cancelled(Exception):
    pass


class Mismatch(OSError):
    pass


STALL_SECONDS, STALL_RATE = 30, 16 << 10


def copy(read, f, total, progress, md5=None):
    """A source that trickles (under STALL_RATE for STALL_SECONDS) is given
    up on, so the next one gets a turn: the socket timeout alone never fires
    while a few bytes keep arriving."""
    done = 0
    window, window_done = time.monotonic(), 0
    while chunk := read(1 << 16):
        f.write(chunk)
        if md5:
            md5.update(chunk)
        done += len(chunk)
        if progress:
            progress(done, total)
        if time.monotonic() - window >= STALL_SECONDS:
            if done - window_done < STALL_RATE * STALL_SECONDS:
                raise Mismatch(f"stalled: under {STALL_RATE >> 10} KB/s for {STALL_SECONDS} s")
            window, window_done = time.monotonic(), done
    return done


def ftp_fetch(path, f, total, progress, md5):
    ftp = garden_ftp()
    try:
        ftp.voidcmd("TYPE I")
        with ftp.transfercmd("RETR " + path) as conn:
            copy(conn.recv, f, total, progress, md5)
        ftp.voidresp()
    finally:
        # close(), not quit(): after a cancel the server is still sending,
        # and waiting for its reply would block until the file is done.
        ftp.close()


def fetch(e, f, url, progress=None):
    """Write e from url to the open file f; returns the MD5 of what came."""
    md5 = hashlib.md5()
    if url.startswith("ftp:"):
        ftp_fetch(e["garden"], f, e.get("size"), progress, md5)
    else:
        with http_get(url, timeout=60) as r:
            if "text/html" in r.headers.get("Content-Type", "") and not e["name"].lower().endswith((".htm", ".html")):
                raise Mismatch(f"{url} answered with a web page")
            total = int(r.headers.get("Content-Length") or 0) or e.get("size")
            copy(r.read, f, total, progress, md5)
    return md5.hexdigest()


def file_ok(path, e):
    """path holds e: right size and, if pinned, right MD5."""
    if not os.path.isfile(path) or (e.get("size") and os.path.getsize(path) != e["size"]):
        return False
    if not e.get("md5"):
        return True
    h = hashlib.md5()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest() == e["md5"]


def download(e, out_dir, progress=None, log=None):
    """Fetch e into out_dir from the first source that gives the pinned size
    and MD5; progress(done, total) may raise Cancelled."""
    dest = os.path.join(out_dir, pin_path(e))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    part = dest + ".part"
    errors = []
    for url in urls_of(e):
        try:
            with open(part, "wb") as f:
                digest = fetch(e, f, url, progress)
            size = os.path.getsize(part)
            if e.get("size") and size != e["size"]:
                raise Mismatch(f"{url}: {size} bytes, pinned {e['size']}")
            if e.get("md5") and digest != e["md5"]:
                raise Mismatch(f"{url}: MD5 {digest}, pinned {e['md5']}")
            os.replace(part, dest)
            return dest
        except Cancelled:
            os.unlink(part)
            raise
        except (OSError, ftplib.Error) as err:
            errors.append(f"{url}: {err}" if not isinstance(err, Mismatch) else str(err))
            if log:
                log(f"  {errors[-1]}")
            if os.path.exists(part):
                os.unlink(part)
    raise OSError("; ".join(errors) or "no source to fetch from")


# ----------------------------------------------------------------- pins

PIN_KEYS = ("name", "as", "dir", "size", "md5") + LOCATORS + ("local", "title", "year", "page", "note")


def pin_path(p):
    """Where p goes, relative to the output directory."""
    return os.path.join(p.get("dir") or "", p.get("as") or p["name"])


def to_pin(e):
    return {k: e[k] for k in PIN_KEYS if e.get(k) not in (None, "")}


def resolve(e, log=print):
    """Fill in size and MD5 from the cheapest authority: the Archive's
    metadata, the Garden FTP (SIZE, XMD5), or else a download."""
    e = dict(e)
    if e.get("md5") and e.get("size"):
        return e
    if "local" in e:
        h = hashlib.md5()
        with open(e["local"], "rb") as f:
            while chunk := f.read(1 << 20):
                h.update(chunk)
        e["md5"], e["size"] = h.hexdigest(), os.path.getsize(e["local"])
        e["local"] = True
        return e
    if "garden" in e:
        try:
            ftp = garden_ftp()
            try:
                e["size"] = e.get("size") or ftp.size(e["garden"])
                e["md5"] = e.get("md5") or ftp.sendcmd("XMD5 " + e["garden"]).split()[-1].lower()
                return e
            finally:
                ftp.close()
        except (OSError, ftplib.Error) as err:
            log(f"  Garden FTP: {err}")
    if "ia" in e:
        ident, name = e["ia"].split("/", 1)
        meta = http_json(f"https://archive.org/metadata/{quote(ident)}")
        for f in meta.get("files", []):
            if f["name"] == name:
                e.setdefault("size", int(f["size"]))
                if f.get("md5"):
                    e.setdefault("md5", f["md5"])
                if e.get("md5") and e.get("size"):
                    return e
    log(f"  hashing {e['name']} (download) ...")
    errors = []
    for url in urls_of(e):
        try:
            with open(os.devnull, "wb") as f:
                e["md5"] = fetch(e, f, url)
            if not e.get("size"):
                with http_get(url) as r:
                    e["size"] = int(r.headers.get("Content-Length") or 0) or None
            return e
        except (OSError, ftplib.Error) as err:
            errors.append(f"{url}: {err}")
    raise OSError("; ".join(errors))


def load_manifest(path):
    if not os.path.exists(path):
        return {"files": []}
    with open(path) as f:
        return json.load(f)


def save_manifest(path, manifest):
    with open(path + ".tmp", "w") as f:
        f.write('{\n  "files": [\n')
        f.write(",\n".join("    " + json.dumps(p, ensure_ascii=False) for p in manifest["files"]))
        f.write("\n  ]" + "".join(f',\n  {json.dumps(k)}: {json.dumps(v)}'
                                  for k, v in manifest.items() if k != "files") + "\n}\n")
    os.replace(path + ".tmp", path)


# ------------------------------------------------------------------ CLI

def fmt_row(i, e, width=None):
    if e.get("kind") == "page":
        line = f"{i:>4}  {e['type'].lower():<7} {e['title']}  <{e['page'].removeprefix(GARDEN_SITE + '/')}>"
    else:
        year = str(e.get("year") or e.get("date", "")[:4])
        title = f"  — {e['title']}" if e.get("title") else ""
        md5 = " " if e.get("md5") else "?"
        line = (f"{i:>4}  {e['src']:<7} {human(e['size']):>7}{md5} {year:<4}  "
                f"{e['name']}{title}  [{e['where']}]")
    if width and len(line) > width:
        line = line[:width - 1] + "…"
    return line


def remember(hits):
    os.makedirs(CACHE, exist_ok=True)
    with open(LAST, "w") as f:
        json.dump(hits, f)


def show(hits):
    remember(hits)
    if not hits:
        print("No matches.")
        return 1
    for i, e in enumerate(hits, 1):
        print(fmt_row(i, e))
    return 0


def cmd_search(args):
    return show(search(load_index(), " ".join(args.query), args.source, args.limit))


def cmd_garden(args):
    return show(garden_search(" ".join(args.query), args.pages))


def last_hits():
    if not os.path.exists(LAST):
        sys.exit("No previous search; run `search` or `garden` first.")
    with open(LAST) as f:
        return json.load(f)


def pick(hits, n, files=True):
    if not 1 <= n <= len(hits):
        sys.exit(f"No result {n} (last list had {len(hits)}).")
    e = hits[n - 1]
    if files and e.get("kind") == "page":
        sys.exit(f"Result {n} is a title page; run `page {n}` for its files.")
    return e


def cmd_page(args):
    ref = args.ref
    if ref.isdigit():
        ref = pick(last_hits(), int(ref), files=False).get("page") or sys.exit("Not a Garden result.")
    page = garden_page(ref, args.refresh)
    print(page["title"])
    print(page["page"])
    for label, key in (("Category", "category"), ("Author", "authors"), ("Publisher", "publishers"),
                       ("Architecture", "architecture")):
        if page[key]:
            print(f"{label + ':':<14}{', '.join(page[key])}")
    if page["year"]:
        print(f"{'Year:':<14}{page['year']}")
    for r in page["requirements"]:
        print(f"{'':<14}{r}")
    if page["description"]:
        print()
        print("\n".join(page["description"]))
    by_path = {e["garden"]: e for e in load_index() if "garden" in e}
    files = []
    for fl in page["files"]:
        e = dict(by_path.get(fl["garden"], {"src": "garden", "name": fl["name"], "size": None,
                                             "date": fl["date"], "where": fl["garden"].split("/")[0],
                                             "garden": fl["garden"]}))
        if fl["md5"]:
            e["md5"] = fl["md5"]
        e.update(title=page["title"], year=page["year"], page=page["page"])
        e["where"] = ("manual, " if fl["manual"] else "") + (fl["system"] or e["where"])
        files.append(e)
    print()
    return show(files)


def cmd_info(args):
    e = pick(last_hits(), args.n)
    print(f"{e['name']}  ({e['src']}, {human(e['size'])}, {e['date']}, {e['where']})")
    if e.get("title"):
        print(f"{e['title']} ({e.get('year') or '?'})  {e.get('page', '')}")
    print(f"MD5 {e['md5']}" if e.get("md5") else "MD5 not known yet (pin resolves it)")
    print("\n".join(urls_of(e)))
    text = infomac_abstract(e)
    if text:
        print()
        print(text)
    return 0


def progress_printer(name):
    def progress(done, total):
        pct = f"{100 * done // total:3d}%" if total else ""
        print(f"\r{name}: {human(done)} / {human(total)} {pct}\x1b[K", end="", flush=True)
    return progress


def cmd_get(args):
    hits = last_hits()
    for n in args.n:
        e = pick(hits, n)
        dest = download(e, args.out, progress_printer(e["name"]), log=print)
        print(f"\n  -> {dest}")
    return 0


def cmd_pin(args):
    picked = []
    if args.n:
        hits = last_hits()
        picked += [pick(hits, n) for n in args.n]
    for ia in args.ia or []:
        picked.append({"src": "ia", "name": ia.rsplit("/", 1)[-1], "ia": ia})
    for url in args.url or []:
        picked.append({"src": "url", "name": urllib.parse.unquote(url.rstrip("/").rsplit("/", 1)[-1]),
                       "url": url})
    for path in args.file or []:
        picked.append({"src": "local", "name": os.path.basename(path), "local": path})
    if not picked:
        sys.exit("Nothing to pin: give result numbers, --ia, --url or --file.")
    if args.as_ and len(picked) != 1:
        sys.exit("--as names one file; pin it on its own.")
    manifest = load_manifest(args.manifest)
    for e in picked:
        if args.as_:
            e["as"] = args.as_
        if args.dir:
            e["dir"] = args.dir
        pin = to_pin(resolve(e))
        key = pin_path(pin)
        old = next((p for p in manifest["files"] if pin_path(p) == key), {})
        # Keep what was added by hand (install steps, arch, ...); refresh the rest.
        pin = {**pin, **{k: v for k, v in old.items() if k not in PIN_KEYS}}
        manifest["files"] = [p for p in manifest["files"] if pin_path(p) != key] + [pin]
        where = ", ".join(k for k in LOCATORS if k in pin) or "local only"
        print(f"pinned {key}  {human(pin.get('size'))}  md5 {pin.get('md5')}  ({where})")
    save_manifest(args.manifest, manifest)
    return 0


def manifest_out(manifest_path, override=None):
    """The output directory: -o, else the manifest's "out" (relative to the
    storage root), else the default."""
    out = override or load_manifest(manifest_path).get("out")
    return os.path.join(STORAGE, os.path.expanduser(out)) if out else DEFAULT_OUT


def cmd_provision(args):
    manifest = load_manifest(args.manifest)
    if not manifest["files"]:
        sys.exit(f"{args.manifest}: no files")
    out = manifest_out(args.manifest, args.out)
    failed = 0
    for p in manifest["files"]:
        e = {"src": "pin", **p}
        name = pin_path(p)
        dest = os.path.join(out, name)
        if not p.get("md5") or not p.get("size"):
            failed += 1
            print(f"FAILED   {name}: no size and MD5 pinned (run pin again)")
            continue
        if file_ok(dest, e):
            print(f"ok       {name}")
            continue
        if not any(k in p for k in LOCATORS):
            failed += 1
            print(f"MISSING  {name}: local-only, put it at {dest}" + (f" ({p['note']})" if p.get("note") else ""))
            continue
        tty = sys.stdout.isatty()
        clear = "\r\x1b[K" if tty else ""
        try:
            download(e, out, progress_printer(name) if tty else None, log=lambda m: print(clear + m))
            print(f"{clear}fetched  {name}")
        except (OSError, ftplib.Error):
            failed += 1
            print(f"{clear}FAILED   {name}: no source gave the pinned file (above)")
    print(f"{len(manifest['files']) - failed}/{len(manifest['files'])} files in {out}")
    return 1 if failed else 0


def cmd_update(args):
    build_index()
    return 0


# ------------------------------------------------------------------ TUI

HELP = "/ search  Tab source  Enter download  i info  ↑↓ PgUp/PgDn move  q quit"


class Tui:
    def __init__(self, scr, entries, query, out_dir):
        self.scr, self.entries, self.out_dir = scr, entries, out_dir
        self.query, self.src_i = query, 0
        self.hits, self.sel, self.top = [], 0, 0
        self.status = f"{len(entries)} files indexed. Downloads go to {out_dir}"
        self.popup = None
        curses.curs_set(0)
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)
        curses.init_pair(2, curses.COLOR_YELLOW, -1)
        if query:
            self.run_search()

    @property
    def src(self):
        return ([None] + list(SOURCES))[self.src_i]

    def run_search(self):
        self.hits = search(self.entries, self.query, self.src, limit=2000)
        self.sel = self.top = 0
        self.status = f"{len(self.hits)} matches" + (" (first 2000)" if len(self.hits) == 2000 else "")

    def put(self, y, x, text, attr=0):
        h, w = self.scr.getmaxyx()
        if 0 <= y < h and x < w:
            try:
                self.scr.addnstr(y, x, text, w - x - (1 if y == h - 1 else 0), attr)
            except curses.error:
                pass

    def draw(self):
        self.scr.erase()
        h, w = self.scr.getmaxyx()
        self.put(0, 0, f" macfetch  source: {self.src or 'all'}  search: {self.query}".ljust(w),
                 curses.color_pair(1))
        rows = h - 3
        if self.sel < self.top:
            self.top = self.sel
        elif self.sel >= self.top + rows:
            self.top = self.sel - rows + 1
        for r, e in enumerate(self.hits[self.top:self.top + rows]):
            i = self.top + r
            self.put(1 + r, 0, fmt_row(i + 1, e, w).ljust(w),
                     curses.A_REVERSE if i == self.sel else 0)
        if not self.hits:
            self.put(2, 2, "No matches." if self.query else
                     "Press / and type a search, e.g. hypercard, stuffit, marathon.")
        self.put(h - 2, 0, self.status[:w], curses.color_pair(2))
        self.put(h - 1, 0, HELP)
        if self.popup:
            self.draw_popup(self.popup)
        self.scr.refresh()

    def draw_popup(self, text):
        h, w = self.scr.getmaxyx()
        lines = []
        for para in text.splitlines():
            while len(para) > w - 6:
                lines.append(para[:w - 6])
                para = para[w - 6:]
            lines.append(para)
        lines = lines[:h - 4]
        for y in range(len(lines) + 2):
            self.put(1 + y, 1, " " * (w - 2), curses.color_pair(1))
        for y, line in enumerate(lines):
            self.put(2 + y, 3, line, curses.color_pair(1))

    def prompt(self):
        h, w = self.scr.getmaxyx()
        curses.curs_set(1)
        buf = self.query
        while True:
            self.put(h - 2, 0, ("Search: " + buf).ljust(w - 1))
            self.scr.move(h - 2, min(8 + len(buf), w - 2))
            k = self.scr.get_wch()
            if k in ("\n", "\r", curses.KEY_ENTER):
                break
            if k == "\x1b":
                buf = None
                break
            if k in (curses.KEY_BACKSPACE, "\x7f", "\b"):
                buf = buf[:-1]
            elif k == "\x15":  # ^U
                buf = ""
            elif isinstance(k, str) and k.isprintable():
                buf += k
        curses.curs_set(0)
        if buf is not None:
            self.query = buf
            self.run_search()

    def download(self):
        e = self.hits[self.sel]
        self.scr.nodelay(True)
        start = shown = time.time()

        def progress(done, total):
            nonlocal shown
            if self.scr.getch() in (27, ord("q")):
                raise Cancelled
            if time.time() - shown < 0.1 and done != total:
                return
            shown = time.time()
            pct = f" {100 * done // total}%" if total else ""
            rate = human(done / max(time.time() - start, 0.01))
            self.status = f"Downloading {e['name']}: {human(done)}/{human(total)}{pct} ({rate}/s, Esc cancels)"
            self.draw()

        try:
            self.status = f"Downloading {e['name']} ..."
            self.draw()
            dest = download(e, self.out_dir, progress)
            self.status = f"Saved {dest}"
        except Cancelled:
            self.status = "Download cancelled."
        except OSError as err:
            self.status = f"Download failed: {err}"
        finally:
            self.scr.nodelay(False)

    def info(self):
        e = self.hits[self.sel]
        text = f"{e['name']}\n{human(e['size'])}  {e['date']}  {e['where']}\n"
        if e.get("title"):
            text += f"{e['title']} ({e.get('year') or '?'})\n"
        text += (f"MD5 {e['md5']}\n" if e.get("md5") else "") + "\n".join(urls_of(e)) + "\n"
        if "infomac" in e:
            self.status = "Fetching abstract ..."
            self.draw()
            try:
                text += "\n" + infomac_abstract(e)
            except OSError as err:
                text += f"\n(abstract unavailable: {err})"
        self.popup = text + "\n\n(any key closes)"
        self.status = ""

    def loop(self):
        while True:
            self.draw()
            k = self.scr.get_wch()
            if self.popup:
                self.popup = None
                continue
            h = self.scr.getmaxyx()[0]
            n = len(self.hits)
            if k in ("q", "Q"):
                return
            elif k == "/":
                self.prompt()
            elif k == "\t":
                self.src_i = (self.src_i + 1) % (len(SOURCES) + 1)
                self.run_search()
            elif k in (curses.KEY_DOWN, "j") and n:
                self.sel = min(self.sel + 1, n - 1)
            elif k in (curses.KEY_UP, "k") and n:
                self.sel = max(self.sel - 1, 0)
            elif k == curses.KEY_NPAGE and n:
                self.sel = min(self.sel + h - 3, n - 1)
            elif k == curses.KEY_PPAGE and n:
                self.sel = max(self.sel - (h - 3), 0)
            elif k == curses.KEY_HOME:
                self.sel = 0
            elif k == curses.KEY_END and n:
                self.sel = n - 1
            elif k in ("\n", "\r", curses.KEY_ENTER, "d") and n:
                self.download()
            elif k == "i" and n:
                self.info()


def cmd_tui(args):
    entries = load_index()
    os.environ.setdefault("ESCDELAY", "25")
    curses.wrapper(lambda scr: Tui(scr, entries, " ".join(args.query or []), args.out).loop())
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd")
    s = sub.add_parser("search", help="search the local index of file names")
    s.add_argument("query", nargs="+")
    s.add_argument("-s", "--source", choices=SOURCES)
    s.add_argument("-n", "--limit", type=int, default=50)
    s.set_defaults(fn=cmd_search)
    g = sub.add_parser("garden", help="search the Garden site for titles (one request per page)")
    g.add_argument("query", nargs="+")
    g.add_argument("--pages", type=int, default=1)
    g.set_defaults(fn=cmd_garden)
    pg = sub.add_parser("page", help="a Garden title page: metadata and files (cached)")
    pg.add_argument("ref", help="result number, URL, or path like apps/hypercard-241")
    pg.add_argument("--refresh", action="store_true", help="fetch again even if cached")
    pg.set_defaults(fn=cmd_page)
    i = sub.add_parser("info", help="where a result lives (and Info-Mac abstract)")
    i.add_argument("n", type=int)
    i.set_defaults(fn=cmd_info)
    d = sub.add_parser("get", help="download results by number, verified")
    d.add_argument("n", type=int, nargs="+")
    d.add_argument("-o", "--out", default=DEFAULT_OUT)
    d.set_defaults(fn=cmd_get)
    pn = sub.add_parser("pin", help="add files to a manifest, with size and MD5")
    pn.add_argument("n", type=int, nargs="*")
    pn.add_argument("-m", "--manifest", required=True)
    pn.add_argument("--ia", action="append", metavar="ITEM/FILE", help="an Internet Archive file")
    pn.add_argument("--url", action="append", help="any URL (downloaded once to hash it)")
    pn.add_argument("--file", action="append", metavar="PATH",
                    help="a file only you have: pinned by hash, never fetched")
    pn.add_argument("--as", dest="as_", metavar="NAME", help="file name to provision it as")
    pn.add_argument("--dir", help="subdirectory of the output directory to provision it into")
    pn.set_defaults(fn=cmd_pin)
    pv = sub.add_parser("provision", help="fetch and verify every file in a manifest")
    pv.add_argument("manifest")
    pv.add_argument("-o", "--out", help="output directory (default: the manifest's \"out\" under "
                    f"$MACEMU_STORAGE, else {DEFAULT_OUT})")
    pv.set_defaults(fn=cmd_provision)
    u = sub.add_parser("update", help="rebuild the index")
    u.set_defaults(fn=cmd_update)
    t = sub.add_parser("tui", help="interactive browser (default)")
    t.add_argument("query", nargs="*")
    t.add_argument("-o", "--out", default=DEFAULT_OUT)
    t.set_defaults(fn=cmd_tui)
    args = p.parse_args()
    if not args.cmd:
        args = p.parse_args(["tui"])
    try:
        return args.fn(args)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
