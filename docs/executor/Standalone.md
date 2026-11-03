# The standalone server: one program, the tab inside

`tools/standalone` is Executor's tab and what it needs from a server as
one Go program per OS, with no other files: the page and the wasm module
are embedded, the library is a directory of images, and the server keeps
everything the tab changes (P1 in the plan: images only, no accounts).
It is the tab's only server (the hosted Python one, with accounts, was
retired 2026-11-02: DIRECTION.md); its API is below. The first browser
to open it has it until it stops; no other gets anything from it (Who
uses it, below). Putting it on a host for others: WebSite.md.

## Building

```bash
make -C tools/standalone WASM_BUILD=build-wasm        # site/ (make_site.sh), ./executor-standalone
make -C tools/standalone dist WASM_BUILD=build-wasm   # dist/: a release (below)
make -C tools/standalone test                         # go test
```

Go 1.24 or later, no cgo, no modules beyond the standard library:
`dist` cross-compiles linux, darwin and windows for amd64 and arm64 from
any of them (about 15 MB each, the module included). The site's large
files (`executor-web.wasm`, `executor-web.js`) are embedded as their
`.gz` only; a browser that doesn't take gzip, or asks for a range, gets
them decompressed as they go out. `make nosite` builds with whatever
`site/` has (nothing: `/` says so; `--site DIR` serves a directory).

## A release

`make dist [VERSION=V]` (V defaults to `git describe --always --dirty`)
makes `dist/` from scratch: `executor-standalone-V-OS-ARCH[.exe]` for the
six targets, `SHA256SUMS` (`sha256sum -c SHA256SUMS` checks them) and
`README.txt` for whoever downloads them (`tools/standalone/README.dist.txt`,
V filled in): which file is which, checking a download, first run,
where images go. Build it from a clean tree, so the name has no `-dirty`,
with `WASM_BUILD` a Skia, `EXECUTOR_NET=wasm` Release build (WebSite.md).

The binaries are not signed. What signing would take:

- **macOS** (both architectures): an Apple Developer Program membership
  (99 USD a year) and its "Developer ID Application" certificate; sign
  with the hardened runtime, `codesign --sign "Developer ID Application:
  NAME (TEAM)" --options runtime --timestamp FILE` (on macOS, or with
  rcodesign from Linux, which also notarizes); notarize with `xcrun
  notarytool submit FILE.zip --keychain-profile P --wait` (a bare Mach-O
  can't be stapled: ship it in a zip, notarized, or in a signed and
  stapled .pkg or .dmg, `xcrun stapler staple`). The program needs no
  entitlements (no JIT in the server; the tab's JIT is the browser's).
  Unsigned, Gatekeeper refuses a downloaded binary until the user
  right-clicks > Open or removes `com.apple.quarantine` (README.txt says so).
- **Windows**: an Authenticode code-signing certificate from a CA (OV,
  or EV for SmartScreen reputation at once; since 2023 the key lives on a
  hardware token or a cloud HSM such as Azure Trusted Signing); sign with
  `signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256
  FILE.exe`, or `osslsigncode` from Linux. Unsigned, SmartScreen warns
  until a download builds reputation.
- **Linux**: nothing to sign the binary with; publish SHA256SUMS signed
  (`gpg --detach-sign` or `minisign`/`cosign`) and the key beside it.

A CI release job would run `make dist` on a tag, then the signing steps
on the macOS and Windows files (their secrets in the CI's store), then
SHA256SUMS again over the signed files.

## Running

```bash
./executor-standalone --open                # http://127.0.0.1:8090/ in the browser, DATA in the config directory
./executor-standalone --images ~/Disks --port 9000
./executor-standalone --bind 0.0.0.0 --tls auto     # on a LAN: HTTPS, a self-signed certificate made once
```

It prints where it listens, then the address to open:

```
Serving (embedded) on https://0.0.0.0:8090/
Open https://127.0.0.1:8090/ (the first browser to open it has it until the server stops)
  or on the LAN: https://192.168.1.20:8090/
The certificate (self-signed: the browser asks once), SHA-256 AB:12:...
```

| Flag | Default | |
|---|---|---|
| `--data DIR` | `os.UserConfigDir()/executor-web` | settings, overlays of disks not in the library, volumes, logs |
| `--config FILE` | `DATA/settings.json` | the settings file |
| `--images DIR` | settings' `disks.images`, `DATA/images` | the library |
| `--bind`, `--port` | `127.0.0.1`, `8090` | `--port 0`: any (the first line printed says which) |
| `--site DIR` | the embedded site | |
| `--tls auto` | settings' `network.tls_cert` (`auto` there too) | HTTPS with a self-signed certificate made once, `DATA/tls/cert.pem` and `key.pem` (for localhost, the machine's name and its addresses then; the SHA-256 printed to compare with the browser's); `--tls off`: plain HTTP whatever the settings say |
| `--tls-cert F`, `--tls-key F` | | HTTPS with this certificate (sound needs a secure context off localhost) |
| `--open` | | open the default browser at the address (xdg-open, open, or url.dll on Windows; an any-address bind opens 127.0.0.1) |
| `--quiet` | | errors only |

Flags hold for the run and are never saved; `settings.json` holds the
rest. `os.UserConfigDir()` is `~/.config` (or `$XDG_CONFIG_HOME`) on
Linux, `~/Library/Application Support` on macOS, `%AppData%` on Windows.

While Executor starts, the tab shows its steps over the screen until the
first frame (`web/executor/loading.js`): the module (about 70 MB the
first time: megabytes and percent as it streams, then compiling; after
that from the browser's Cache Storage, revalidated by ETag), the disks,
the files the server keeps (Executor's disk, the disks' changes), the Mac
starting. A step that fails says so there.

## Settings

One file, version 2, nested by category, described by
`web/executor/settings.schema.json` (Settings.md): the tab's Settings
dialog edits it. The page's settings (machine, disks mounted, keys,
display, sound, logging) are kept here for the tab; the server's own
(`disks.images`/`include`/`exclude`, `network.*`) are this program's;
the browser that has the server may change either (`can_change_server`
is always true).
Version 1 files (flat, before) are migrated when read and saved as 2.

`GET /api/settings` gives `{ version, settings, scopes, restart_needed,
can_change_server, file }`; `POST /api/settings` takes a JSON merge patch
(`null`: the default, of a setting or a whole category) and answers the
same, or 400 `{ error, path }` for an invalid value. `network.bind`,
`port`, `tls_cert`, `tls_key` apply at the next start (`restart_needed`
lists the ones changed since it started); the rest at once (the relay's
for new connections). A password from before the claim (`password_hash`,
`network.password`) and a paired list (`network.paired`) are dropped when
the file is read.

## Who uses it

The first browser to load the tab has the server until it stops
(`claim.go`); no password, key or account. The first `GET /` (or
`/index.html`) while no browser has it makes a token (256 random bits)
and answers with the tab and the token in a cookie (`executor_claim_PORT`,
by the port asked for, as two servers on one machine are one host to a
browser; HttpOnly, SameSite=Strict, Secure over TLS or behind a trusted
proxy saying `X-Forwarded-Proto: https`; 400 days). Only a page load from
this site or the address bar claims it (`Sec-Fetch-Dest: document`,
`Sec-Fetch-Site` `none` or `same-origin`, or no such headers): another
site can't take it by loading the address in the background. Every route
needs the cookie: the site, the API, `/disks` (Range requests too),
`/relay` (before the upgrade), the logs. That browser's reloads and other
tabs keep working (one of them in control, below).

Another browser gets, for a page, a page saying Executor is in use by
another browser (403), and 403 `{ error: "in use by another browser" }`
from everything else: it can't read or write anything.

The claim is in memory only (the token's SHA-256): every start of the
server begins unclaimed, so restarting the server is how another browser
gets it. A tab open across a restart gets 401 (`{ reload: true }`; 403 if
another browser loaded first) at its next call, every 2 s
(`/api/session`), and says to reload, with a Reload button (`main.js`);
the reload claims it again. Loopback is nothing special.

## The API

```
/                 the tab (embedded, or --site DIR)
/library.json     the library (library.go): [{ title, url, note, tags, size, cover }];
                  /disks/NAME its images, with Range; /covers/NAME their covers
/api/server       { standalone: true, version, tls }
/api/me           { name: "local", owner: true, mode: "standalone" }: the tab's
                  sign that it is on its server (else it does not start)
/api/session      POST ?id=ID[&take=1] the controlling tab (session.go);
                  /api/session/release, PUT/GET /api/session/screen
/api/settings     GET the settings; POST a merge patch (settings.go; above)
/api/sync/...     overlays (overlay.go) and volumes (volumes.go):
    GET    overlays/KEY?since=V   records (u32 LE block, 4 KB) newer than V;
                                  X-Sync-Version, X-Sync-Reset
    POST   overlays/KEY?stage=T   records kept for a commit with T
    POST   overlays/KEY?base=V[&stage=T][&replace=1][&name=&size=] -> { version }
                                  (409 { version } when V isn't the server's)
    DELETE overlays/KEY           Revert -> { version }
    POST   overlays/KEY/merge     into the library image -> { blocks, id, key }
    GET/PUT/DELETE volumes/KEY    { version, manifest }
    GET    volumes/KEY/bundle     the volume and every object it names, one stream
                                  (gzip for a browser that takes it; below)
    POST   volumes/KEY/bundle     { have: [sha] }: the same, less the objects the
                                  browser's cache has
    POST   objects/missing        { shas } -> { missing }
    POST   objects/bundle         objects up, a bundle (Content-Encoding: gzip or none)
                                  -> { stored }
    PUT/GET objects/SHA           one object
    GET    (nothing after sync/)  { overlays, volumes }
/api/uploads/NAME POST the image as the body[?replace=1] -> its library entry
                  (uploads/NAME); DELETE: it, its record and its overlay (uploads.go)
/api/relay        GET { connections, sockets } open now
/relay            WebSocket: the tab's sockets (relay.go; DIRECTION.md, "The network relay")
/api/log, /api/shot, /api/logs[/DAY/NAME]   the tab's logs (logs.go)
```

Writes and `/relay` from another site are refused (Origin against Host,
Sec-Fetch-Site). In more detail:

- **No accounts:** the claim (above).
- **One controlling tab** of the browser that has the server
  (`session.go`):
  `POST /api/session?id=ID[&take=1]` every 2 s gives `{ control, held,
  handing, viewers }`; the first tab has control, a silent one lets go
  after 10 s, `take=1` takes it (the old one is `handing` until it
  releases: `POST /api/session/release?id=ID`). `PUT /api/session/screen`
  (the controller's PNG, 8 MB at most; 409 from anyone else) and `GET`
  it (404 when there is none) show viewers its screen.
- **`GET /api/server`**: `{ standalone: true, version, tls }`.
- **Overlays beside their images.** An overlay whose key is a library
  disk's (`NAME-ID`, main.js) is `IMAGES/NAME-ID.overlay`, next to the
  image (`DATA/overlays/` when the directory is read-only, and for disks
  not in the library). The file is a log: each upload is appended with a
  commit record and synced, a load drops anything after the last commit,
  and it is written again without superseded blocks once they are most
  of it (format: `overlay.go`). A changed image has a new id and so a new
  key: its old overlay stays beside it until deleted.
- **`POST /api/sync/overlays/KEY/merge`**: writes a library disk's
  overlay into the image and removes the overlay; the answer has the
  image's new id and key (`{ blocks, id, key }`). A tab with the disk open
  must reload it.
- **Volumes and objects** are files: `DATA/volumes/KEY.json` (`{ version,
  manifest }`) and `DATA/objects/AB/SHA`; an object no manifest names
  is removed an hour after it was written.
- **Bundles** (`volumes.go`): records of a sha (32 bytes), a length
  (uint32 LE) and that many bytes, ended by a record of a zero sha and
  length (without it, the bundle was cut short). `GET volumes/KEY/bundle`
  starts with the volume itself (a uint32 LE length, then the JSON `{
  version, manifest }`) and has each object once, the manifest's paths
  in order; gzip (`BestSpeed`) as `Content-Encoding`, so the browser
  decompresses it as it streams. An object gone meanwhile (collected
  under a newer manifest) ends it short. `POST objects/bundle` checks
  each object against its sha; `network.max_upload_mb` limits the body
  and, decompressed, the objects. The tab (`sync.js`) restores the volume
  from the bundle, writing each file into Executor's MEMFS as it arrives
  (the request goes out while the module may still be compiling; the
  files wait for it); a server without bundles (404) or a bundle cut
  short: the files still missing by `GET objects/SHA`, six at once. It
  saves with `objects/missing`, then bundles of about 8 MB
  (CompressionStream gzip), `PUT objects/SHA` six at once on a server
  without them, then the manifest.
- **The browser caches, never the store** (`overlay.js` `IdbCache`:
  IndexedDB, bytes by key and `{ size, used }` for the eviction, the least
  recently used out past a cap). `executor-objects` (512 MB): the volume's
  objects by sha, from the bundles and the saves; a start POSTs the
  shas it has, the server's manifest decides what the volume is, each
  cached object read back is checked against its sha (a bad one is
  dropped and fetched). `executor-chunks` (256 MB): disk image chunks
  (256 KB) by the image's ETag (its content id here) and number; Executor
  reads them synchronously, so an image's most recently read ones (8 MB
  at most: what the last start read) go into its memory before Executor
  starts, the ones fetched and read written back every 2 s. Nothing goes
  to the server from either; no IndexedDB, an error, or a deleted
  database: the network, said only in the log.
- **Uploads** (`uploads.go`): a disk image the tab adds (main.js
  `uploadImage`) is `POST /api/uploads/NAME` with the image as the body
  (an image's extension, no path; at most `disks.max_image_mb`, written
  beside it and renamed, so a cut-short upload leaves nothing) and goes
  into the library as `IMAGES/uploads/NAME` with its record `NAME.json`
  (tags `["uploaded"]`), offered whatever `include`/`exclude` say:
  `disks/uploads/NAME`, overlaid beside it like the library's own.
  409 when there is one of that name (`?replace=1` replaces an upload and
  drops its overlay; a top-level library image is never replaced).
  `DELETE /api/uploads/NAME` removes it, its record and its overlay.
- **`GET /api/relay`**: `{ connections, sockets }` open now.
- **Stopping:** SIGTERM or SIGINT closes the listener and gives the
  answers under way (a disk's download) 60 s to finish; a second signal
  stops it at once.
- No backup command: the overlay logs are crash-consistent and the rest
  is written atomically, so copying `DATA` and the `.overlay` files with
  any tool is a backup.
- Every answer is cross-origin isolated (COOP `same-origin`, COEP
  `require-corp`, CORP `same-origin`).

## Start-up costs

Measured 2026-11-03 with `web/tests/bench_start.mjs SITE DATA IMAGES
[RUNS] [--slow]` (a copy of a server's data directory each run: its
settings, the disks mounted, the saved volumes; times from the
navigation; `--slow` is Chromium's network emulation, 50 Mbit/s down,
20 down/up, 20 ms latency; `BENCH_TRACE=FILE` writes every request and
Worker line with its time). The owner's settings: Mac OS 8.6, nine
library disks mounted; the volume a saved 7.6.1 one of 1010 files, 45 MB
(732 objects).

| | before | after |
|---|---|---|
| the volume restored, loopback | 0.70 s (1010 sequential GETs) | 0.28 s (one bundle) |
| the volume restored, 50 Mbit/s | 31.4 s | 4.5 s |
| the Finder, loopback | 4.0–5.6 s | 3.8–4.2 s |
| the Finder, 50 Mbit/s | 47.1 s | 21.1 s |
| the same with the owner's saved 8.6 volume (108 files, 16 MB) | 24.9 s | 21.3 s |
| first save of a fresh 8.6 System (363 files, 31 MB), loopback | 1.8 s (251 PUTs) | 1.5 s (4 bundles) |
| the same at 20 Mbit/s up | 19.3 s | 7.5 s |

Cold and warm (`--warm`: the tab loaded once to settle the volume, then
with the browser's caches deleted, then twice with them; the same
server state, 1378 files / 78 MB, nine disks):

| | cold | warm |
|---|---|---|
| the volume restored, loopback | 0.66 s | 0.31 s (every object from the cache) |
| the volume restored, 50 Mbit/s | 8.1 s | 0.36 s |
| the Finder, loopback | 2.7 s | 2.3–2.5 s |
| the Finder, 50 Mbit/s | 12.1 s | 2.9 s (0–3 Range requests, was 14) |

The objects cache alone took the 50 Mbit/s warm start to 4.6–6.5 s;
the chunk cache the rest (the disks' catalogs and the System Folder
additions' reads).

What else a start costs at 50 Mbit/s (the trace of the run above):
the module 2.2 s (6.9 MB gzip; Cache Storage after the first visit);
the nine disks' overlays 0.05 s (their requests at once now, one after
another before); **the System sideloaded from boot-8.6.img when the
volume has no `System Folder/System`: 5.9 s** (`sideload_system`, 23
Range reads of ~1 MB one after another, RemoteImage's chunks; 0.25 s on
loopback); **the disks' System Folder additions: 10 s** (creative.img 3.9
s, games-ppc.img 1.7 s, productivity.img 4.1 s: their files read by
Range, one chunk at a time, when they aren't in the System Folder yet;
fewer reads but still a few seconds when most are); Executor from main
to the Finder 2.6 s. Both are Range reads in sequence and
bandwidth-bound (the chunks are the image's bytes, uncompressed).

Open: the saved volumes of the owner's server (8.6 and 7.6.1) have
`%System` but no `System` (its empty data fork), so web_main.cpp doesn't
take the System Folder as saved: every start sideloads it again over the
restored one and copies the additions again (the 16 s above at 50 Mbit/s,
both in the 21 s). A fresh volume keeps `System`; what removes it later
is not known yet.

## Tests

`go test` (`make test`; ctest `executor.web.standalone_server` in
build-wasm): the API over a real connection as the claiming browser (the
page and library, logs, disk ranges, overlay and volume sync, hostile
requests, cross-site writes, settings, the controlling tab, the relay's
TCP, UDP, DNS, limits and framing); the claim (`claim_test.go`: every route in the table
needs it and is refused, 401 unclaimed and 403 claimed; the first load
claims, and not a fetch, an image, HEAD or another site's; reloads keep
it; a second browser can neither read nor write; a restart frees it; a
cookie per port; the relay); the program itself, run as a child (the
printed address, `--tls auto` with its certificate pinned, the claim over
HTTPS with a Secure cookie, a restart freeing it); and what the API can't show:
the overlay log across restarts, a write cut short, compaction, merge,
the gzip-only files, the session lease. In build-wasm,
`executor.web.claim` is the claim in a browser (`web/tests/tab_claim.mjs`:
the first browser has it, HttpOnly; its reloads and tabs; another
browser's in-use page and refusals; a restart, the tab's reload notice
and Reload; another browser first after a restart; screenshots in
`claim-shots/`). The tab's other cells on it (each builds it with go):
`executor.web.standalone` (a change survives a reload; a second tab
watches, then takes control), `volume` (`tab_volume.mjs`: 150 files saved
in bundles, no PUT; restored from the one bundle request, every file the
same; warm, every object from the browser's cache and a disk's chunks
too; two cached objects spoiled, the cache deleted: from the server, no
notice; through `objects/SHA` and PUTs when the bundle routes answer 404;
from a bundle cut in half), `server_lazy` (a large disk read by Range),
`settings`, `insecure` (plain http to a LAN name), `relay` (MacTCP and
Open Transport through `/relay`), and the `site` cells (WebSite.md).
