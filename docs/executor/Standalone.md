# The standalone server: one program, the tab inside

`tools/standalone` is Executor's tab and what it needs from a server as
one Go program per OS, with no other files: the page and the wasm module
are embedded, the library is a directory of images, and the server keeps
everything the tab changes (P1 in the plan: images only, no accounts).
It has `tools/web/server`'s API (WebSite.md, `app.py`'s docstring), so
the tab talks to either; what differs is below.

## Building

```bash
make -C tools/standalone WASM_BUILD=build-wasm        # site/ (make_site.sh), ./executor-standalone
make -C tools/standalone dist WASM_BUILD=build-wasm   # dist/, six targets
make -C tools/standalone test                         # go test + test_server.py against it
```

Go 1.24 or later, no cgo, no modules beyond the standard library:
`dist` cross-compiles linux, darwin and windows for amd64 and arm64 from
any of them (about 15 MB each, the module included). The site's large
files (`executor-web.wasm`, `executor-web.js`) are embedded as their
`.gz` only; a browser that doesn't take gzip, or asks for a range, gets
them decompressed as they go out. `make nosite` builds with whatever
`site/` has (nothing: `/` says so; `--site DIR` serves a directory).

## Running

```bash
./executor-standalone                       # http://127.0.0.1:8090/, DATA in the config directory
./executor-standalone --images ~/Disks --port 9000
./executor-standalone --bind 0.0.0.0 --tls-cert lan.crt --tls-key lan.key   # on a LAN: set a password
./executor-standalone passwd                # the password, from stdin ("" for none)
```

| Flag | Default | |
|---|---|---|
| `--data DIR` | `os.UserConfigDir()/executor-web` | settings, overlays of disks not in the library, volumes, logs |
| `--images DIR` | settings' `images`, `DATA/images` | the library |
| `--bind`, `--port` | `127.0.0.1`, `8090` | `--port 0`: any (the first line printed says which) |
| `--site DIR` | the embedded site | |
| `--password-file F` | settings' | the password for this run |
| `--tls-cert F`, `--tls-key F` | | HTTPS directly (OPFS and sound need a secure context off localhost) |
| `--quiet` | | errors only |

Flags hold for the run and are never saved; `DATA/settings.json` holds
the rest.

## Settings

`GET /api/settings` gives them, `POST /api/settings` changes some (a JSON
object of the keys to change; unknown keys are refused). Without a
password only this machine may change them (a LAN client gets 403).

| Key | Default | Applies |
|---|---|---|
| `images`, `include`, `exclude` | `DATA/images`, `["*.img"]`, `[]` | at once |
| `relay` | `tools/web/server`'s `[relay]`, plus `nameserver` (QUERY's; "": the host's) | new relay connections |
| `password` | none | at once: a string sets it (stored as PBKDF2), `""` clears it |
| `trusted_proxies`, `secure_cookies`, `max_upload_mb` | `[]`, `"auto"`, 64 | at once |
| `bind`, `port`, `tls_cert`, `tls_key` | | at the next start (`restart_needed` in the answer lists them) |

## What differs from tools/web/server

- **No accounts.** Without a password everything is open (it listens on
  127.0.0.1 unless told otherwise). With one, `/login` asks for it and
  `POST /api/login {password}` sets the same session cookie as the
  Python server's (sessions last 30 days, across restarts). `/api/me` is
  `{ name: "local", owner: true, mode: "standalone" }`, always (signed out
  too): the Python server's `mode = "standalone"` answer.
- **One controlling tab** (`session.go`, as `tools/web/server/session.py`):
  `POST /api/session?id=ID[&take=1]` every 2 s gives `{ control, held,
  handing, viewers }`; the first tab has control, a silent one lets go
  after 10 s, `take=1` takes it (the old one is `handing` until it
  releases: `POST /api/session/release?id=ID`). `PUT /api/session/screen`
  (the controller's PNG, 8 MB at most; 409 from anyone else) and `GET`
  it (404 when there is none) show viewers its screen.
- **`GET /api/server`**, no sign-in needed: `{ standalone: true, version,
  password, signed_in }`: how the tab knows which server it has.
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

`test_server.py` runs against the binary with
`EXECUTOR_WEB_SERVER_BINARY=PATH`: the password stands for the owner's,
and the checks of invitations, account backups and static.py's range
parser (go test has the Go one) are skipped: 17 of 20 run, StandaloneTest
with no password. `go test`
covers what the API can't show: the overlay log across restarts, a
write cut short, compaction, merge, the gzip-only files, the session
lease.
`ctest --test-dir build-wasm -R executor.web.standalone` runs both.
