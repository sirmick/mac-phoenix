# The site: Executor in a tab, as a private service

M1's site is `tools/web/server` (what it serves and its API: the docstring
of `tools/web/server/app.py`) in front of three things this page says how
to put in place: the tab (the wasm build), the library (boot and
application disks) and the data directory (accounts and every user's
synced changes). Signed-in users pick a System and add disks from the
library; the disks are read by Range requests as Executor needs them, and
what they change (a disk's blocks, Executor's own volume) is kept in the
browser and synced to their account (`web/executor/sync.js`).

## The tab

```bash
source ~/emsdk/emsdk_env.sh
tools/skia/build-wasm.sh                                 # once: Skia for wasm, ~/storage/deps/skia-m144-wasm-wasm
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release -DEXECUTOR_SKIA=ON -DEXECUTOR_NET=wasm
cmake --build build-wasm -j2
tools/web/make_site.sh /srv/executor-web/site            # the page, the module, .gz copies
```

The site's module is built with Skia (`-DEXECUTOR_SKIA=ON`, HiDPI.md):
the tab draws at the screen's `devicePixelRatio` (`?scale=N` to choose),
and at 2 text is drawn from outlines, sharp, rather than the 1x screen
scaled up. Without it the module is 4 MB smaller (1.3 MB gzipped) and
text at 2x is blurred; `make_site.sh` warns.

`-DEXECUTOR_NET=wasm` (MacTCP and Open Transport through the server's
`/relay`) is the default for a new build directory, but CMake keeps a
cached value: a `build-wasm` configured before the relay keeps `none`,
and its module has no network (MacTCP calls fail, `executor.web.relay`
fails) until it is configured again with `-DEXECUTOR_NET=wasm`. Check
with `grep EXECUTOR_NET build-wasm/CMakeCache.txt`.

`make_site.sh` copies what the page needs out of the build tree, so a
rebuild doesn't change the running site until it is run again. Nothing
else is needed: the tab needs no cross-origin isolation headers, but it
does need a secure context (HTTPS, or `localhost`) for OPFS, where it
keeps disks and changes; over plain http to another host it runs with
nothing kept.

## The library

`tools/web/make_library.py` makes the site's disks from the provisioned
images (`provisioning/build.py`'s, in `~/storage/images`), each with its
record (`NAME.img.json`: title, tags and note for the library, and the
applications on it, which the site's cells open):

```bash
TMPDIR=~/storage/tmp tools/web/make_library.py /srv/executor-web/library
```

| Disk | Size | What | Systems |
|---|---|---|---|
| `boot-7.6.1.img` | 20 MB | Mac OS 7.6.1, `make_boot_disk.py`'s cut (APPLE-DISKS.md) | |
| `boot-8.6.img` | 44 MB | Mac OS 8.6, the same, with SimpleText | |
| `productivity.img` | 50 MB | AppleWorks 5.0.3, ClarisWorks 4.0v4 | 7.6.1, 8.6 |
| `development.img` | 392 MB | MPW, CodeWarrior Pro 4 (IDE 3.2), THINK C 6, THINK Pascal 4.5, HyperCard 2.4.1, MacPerl 5.2, ResEdit 2.1.3, BBEdit Lite 4.1 | 7.6.1, 8.6 |
| `games-68k.img` | 71 MB | 18 games that run on the 68k side (Lemmings, Maelstrom, SimEarth, Monkey Island, ...) | 7.6.1 |
| `games-ppc.img` | 476 MB | fat and PowerPC games (Marathon 2, Escape Velocity, EV Nova, Ferazel's Wand, ...) | 8.6, PowerPC ticked |

Whole application folders are copied as installed (`make_disk.py`, both
forks and Finder info as MacBinary through hfsutils), so an application
finds its files beside it. A disk is made again when an image it comes
from is newer (`--force`: always). Which applications run yet:
SCOREBOARD.md; the site's cells (below) check them in the tab.

A link opens a System with disks already added, e.g.
`/?disk=disks/boot-8.6.img&disk=disks/games-ppc.img&system=boot-8.6.img&ppc=1`
(the page's controls do the same: System, PowerPC, "Add a disk…").

## The server

As a systemd user service of an account on the host (or a system service
with `User=`; the unit is `tools/web/server/executor-web.service`):

```bash
mkdir -p ~/.config/executor-web ~/.config/systemd/user
cp tools/web/server/server.example.toml ~/.config/executor-web/server.toml
cp tools/web/server/executor-web.service ~/.config/systemd/user/
```

`server.toml` for this layout (every key: `server.example.toml`):

```toml
[server]
bind = "127.0.0.1"                    # the proxy in front has the public address
port = 8090
site = "/srv/executor-web/site"
data = "/srv/executor-web/data"       # accounts.sqlite, users/ID/
secure_cookies = true                 # behind HTTPS
trusted_proxies = ["127.0.0.1"]       # the proxy's X-Forwarded-For is believed
max_upload_mb = 64

[library]
images = "/srv/executor-web/library"
include = ["*.img"]
```

The unit runs `python3 ~/src/mac-phoenix/tools/web/server` (Python 3.11 or
later, no other packages); point `ExecStart` at the checkout in use. Then:

```bash
python3 tools/web/server --config ~/.config/executor-web/server.toml adduser NAME --owner
systemctl --user daemon-reload && systemctl --user enable --now executor-web
loginctl enable-linger $USER          # keeps running signed out
journalctl --user -u executor-web     # its log (--quiet: errors only)
```

## HTTPS in front

The server speaks plain HTTP on 127.0.0.1; a reverse proxy has the
certificate. It must pass `Host` through (the server checks a write's
`Origin` against it), add `X-Forwarded-For`, pass WebSocket upgrades
(`/relay`), not buffer (disk ranges stream), and allow bodies of
`max_upload_mb` (sync uploads). nginx, with a certificate from certbot:

```nginx
server {
    listen 443 ssl http2;
    server_name mac.example.org;
    ssl_certificate     /etc/letsencrypt/live/mac.example.org/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/mac.example.org/privkey.pem;
    location / {
        proxy_pass http://127.0.0.1:8090;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $http_connection;
        proxy_http_version 1.1;
        proxy_buffering off;
        client_max_body_size 64m;
        proxy_read_timeout 120s;
    }
}
server { listen 80; server_name mac.example.org; return 301 https://$host$request_uri; }
```

Caddy does the same, certificate included:
`mac.example.org { reverse_proxy 127.0.0.1:8090 }` (it passes Host and
X-Forwarded-For and upgrades by itself; buffering is off for streams).

The proxy must not cache or recompress `/disks/`: the tab asks for byte
ranges and checks the reply is a 206 with `Content-Range`, else it
downloads the whole disk before starting.

## Accounts

The owner invites: from `/account` (signed in), or

```bash
python3 tools/web/server --config ~/.config/executor-web/server.toml invite --days 7
# -> CODE; send https://mac.example.org/login?invite=CODE
```

The invited person picks a name and password there. `users`, `passwd NAME`
and `deluser NAME` (removes their synced data too) manage the rest. A
user's changes are theirs only; the library's disks are shared and never
written (changes are overlays).

## Where things live

| Path | What | Back up |
|---|---|---|
| `/srv/executor-web/site` | the tab (`make_site.sh`) | no: made again from the build |
| `/srv/executor-web/library` | disks and records (`make_library.py`) | no: made again from the provisioned images |
| `data/accounts.sqlite` | accounts, sessions, invitations | yes |
| `data/users/ID/sync.sqlite` | the user's synced disk blocks and volume manifests | yes |
| `data/users/ID/objects/` | the files of their Executor volumes, by SHA-256 | yes |
| `data/image-ids.json` | the library disks' content ids (a cache) | no |

A library disk's id is its content's: changing a disk (a new
`make_library.py` run that changes it) gives it a new id, and users'
changes to the old one are dropped with a notice when they next open it.
Make library disks again only when their contents should change.

## Backups

The databases are SQLite in WAL mode, written while the server runs:
copying the files can catch half a transaction. `backup` copies them
through SQLite's backup API, then the objects DEST lacks:

```bash
python3 tools/web/server --config ~/.config/executor-web/server.toml backup /srv/backup/executor-web
```

Run it from a timer (a `systemd --user` timer or cron, daily) and keep
DEST's history with the backup tool in use (restic, borg, rsnapshot):
objects never change once written, so DEST grows only by what is new. To
restore: stop the service, copy DEST's contents into the data directory,
start it.

## The site's cells

`web/tests/tab_site.mjs` opens every application in a library disk's
record on a System, as a visitor would: signed in to a scratch server
whose library is `make_library.py`'s output, the boot disk and the
application disk added, each application launched in turn (the page
reloaded between them) and checked: in front, a window of its own (or,
full screen, the screen redrawn), its menu bar there and a menu drawn
when pressed (games: the menus there; a modal dialog at launch is
cancelled first, and an application with no window gets File > New), and
still in front with no error alert (a dialog whose text, ParamText
included, says so: the alert's text is the result's reason). SCOREBOARD.md's verdict (column
e761 on 7.6.1, e86 on 8.6) says what to expect: a cell fails only when an
application the scoreboard says runs fails; an expected failure prints
XFAIL, and one that now works XPASS (bring the scoreboard up to date and
it is expected from then on).

```bash
ctest --test-dir build-wasm -L site            # EXECUTOR_WEB_LIBRARY=~/storage/web-library
SITE_APPS=MacPerl ctest --test-dir build-wasm -R site.8.6.development -V
```

`executor.web.hidpi` (the Finder at scale 1 and 2, text redrawn from
outlines at 2: fails without Skia) is labelled `site` too, so the run
checks the module is the site's. Labelled `site`, not `executor` (about
20 minutes for the six cells). The canvas after each application's checks,
and the Finder's first, are saved as PNGs in `~/storage/tmp/site/shots/`
(`SYSTEM-APP.png`, `SITE_SHOTS=DIR` elsewhere); `SITE_DPR=2` runs the page
at devicePixelRatio 2, so they are the 2x canvas (`SYSTEM-APP@2x.png`).
