# The standalone server: one program, the tab inside

`tools/standalone` is Executor's tab and what it needs from a server as
one Go program per OS, with no other files: the page and the wasm module
are embedded, the library is a directory of images, and the server keeps
everything the tab changes (P1 in the plan: images only, no accounts).
It has `tools/web/server`'s API (WebSite.md, `app.py`'s docstring), so
the tab talks to either; what differs is below. The first browser to
open it has it until it stops; no other gets anything from it (Who uses
it, below).

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
| `--tls-cert F`, `--tls-key F` | | HTTPS with this certificate (OPFS and sound need a secure context off localhost) |
| `--open` | | open the default browser at the address (xdg-open, open, or url.dll on Windows; an any-address bind opens 127.0.0.1) |
| `--quiet` | | errors only |

Flags hold for the run and are never saved; `settings.json` holds the
rest. `os.UserConfigDir()` is `~/.config` (or `$XDG_CONFIG_HOME`) on
Linux, `~/Library/Application Support` on macOS, `%AppData%` on Windows.

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

## What differs from tools/web/server

- **No accounts:** the claim (above); no `/login`, `/api/login`,
  `/api/logout`. `/api/me` is `{ name: "local", owner: true, mode:
  "standalone" }`: the Python server's `mode = "standalone"` answer.
- **One controlling tab** of the browser that has the server
  (`session.go`, as `tools/web/server/session.py`):
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
  manifest }`) and `DATA/objects/AB/SHA`, collected as in sync.py.
- **`GET /api/relay`**: `{ connections, sockets }` open now.
- No backup command: the overlay logs are crash-consistent and the rest
  is written atomically, so copying `DATA` and the `.overlay` files with
  any tool is a backup.
- Every answer is cross-origin isolated (COOP `same-origin`, COEP
  `require-corp`, CORP `same-origin`).

## Tests

`go test` (`make test`; ctest `executor.web.standalone_server` in
build-wasm): the API over a real connection as the claiming browser (the
page and library, logs, disk ranges, overlay and volume sync, hostile
requests, cross-site writes, settings, the controlling tab, the relay's
TCP, UDP, DNS, limits and framing: what test_server.py checked of this
program before); the claim (`claim_test.go`: every route in the table
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
`claim-shots/`).
