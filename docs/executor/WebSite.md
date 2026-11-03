# The site: Executor in a tab, served by tools/standalone

The site is `tools/standalone` (Standalone.md: the program, its API and
who may use it) in front of three things this page says how to put in
place: the tab (the wasm build, embedded in the program or `--site
DIR`), the library (boot and application disks) and the data directory
(settings, the changes made to disks not in the library, Executor's own
volumes, the tab's logs). A visitor picks a System and adds disks from
the library; the disks are read by Range requests as Executor needs it,
and what it changes (a disk's blocks, Executor's own volume) is kept by
the server (`web/executor/sync.js`, `Store`).

It is a private service: **the first browser to open the tab after the
server starts has it until the server stops** (Standalone.md, "Who uses
it"); any other gets a page saying it is in use. Start the server, open
it in your browser, and it is yours. To hand it to another browser,
restart the server. There are no accounts (the hosted Python server that
had them was retired 2026-11-02; DIRECTION.md).

## The tab

```bash
source ~/emsdk/emsdk_env.sh
tools/skia/build-wasm.sh                                 # once: Skia for wasm, ~/storage/deps/skia-m144-wasm-wasm
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release -DEXECUTOR_SKIA=ON -DEXECUTOR_NET=wasm
cmake --build build-wasm -j2
make -C tools/standalone WASM_BUILD=$PWD/build-wasm      # the site embedded: ./tools/standalone/executor-standalone
```

The site's module is built with Skia (`-DEXECUTOR_SKIA=ON`, HiDPI.md):
the tab draws at the screen's `devicePixelRatio` (`?scale=N` to choose),
and at 2 text is drawn from outlines, sharp, rather than the 1x screen
scaled up. Without it the module is 4 MB smaller (1.3 MB gzipped) and
text at 2x is blurred; `make_site.sh` (which the Makefile runs) warns.
`executor.web.hidpi` and `executor.web.hidpi_gworld` fail without it.

`-DEXECUTOR_NET=wasm` (MacTCP and Open Transport through the server's
`/relay`) is the default for a new build directory, but CMake keeps a
cached value: a `build-wasm` configured before the relay keeps `none`,
and its module has no network (MacTCP calls fail, `executor.web.relay`
fails) until it is configured again with `-DEXECUTOR_NET=wasm`. Check
with `grep EXECUTOR_NET build-wasm/CMakeCache.txt`.

The binary has the tab in it as it was when `make` ran: a rebuild of
build-wasm changes nothing until `make` runs again (or serve
`tools/web/make_site.sh DIR`'s copy with `--site DIR`). The tab keeps
nothing in the browser (the server keeps it all), so it needs no secure
context, but sound does: HTTPS, or `localhost` (below). It runs only on
its server: served by anything else it says so and does not start.

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
| `productivity.img` | 208 MB | AppleWorks 5.0.3, ClarisWorks 4.0v4, ClarisDraw 1.0v4, Corel WordPerfect 3.5e, Microsoft Excel 4.0, FileMaker Pro 4.0 and Aldus Persuasion 3.0 (personalised with the owner's serials); Microsoft Office 98 (PowerPC, asks for its CD key) | 7.6.1, 8.6 (Office: 8.6, PowerPC ticked) |
| `creative.img` | 106 MB | Kid Pix 2.5, Adobe Photoshop 4.0, Adobe Illustrator 6.0, Macromedia FreeHand 5.5 and Deneba Canvas 5.0.2 (personalised with the owner's serials) | 7.6.1, 8.6 |
| `development.img` | 392 MB | MPW, CodeWarrior Pro 4 (IDE 3.2), THINK C 6, THINK Pascal 4.5, HyperCard 2.4.1, MacPerl 5.2, ResEdit 2.1.3, BBEdit Lite 4.1 | 7.6.1, 8.6 |
| `games-68k.img` | 71 MB | 18 games that run on the 68k side (Lemmings, Maelstrom, SimEarth, Monkey Island, ...) | 7.6.1 |
| `games-ppc.img` | 476 MB | fat and PowerPC games (Marathon 2, Escape Velocity, EV Nova, Ferazel's Wand, ...) | 8.6, PowerPC ticked |
| `macaddict-*.iso` | 650-675 MB each | MacAddict's cover CDs #16 (Dec 1997), #25 (Sep 1998), #58 (Jun 2001), #63 (Nov 2001), as they came, with cover scans | any |

Whole application folders are copied as installed (`make_disk.py`, both
forks and Finder info as MacBinary through hfsutils), so an application
finds its files beside it, and what their installers put in the System
Folder goes on the disk too, for the tab to copy into its System
(Disks.md, "System Folder additions"). A disk is made again when an image
or snapshot it comes from is newer (`--force`: always). Which applications run yet:
SCOREBOARD.md; the site's cells (below) check them in the tab.

The CDs are `provisioning/cds.json`'s (docs/Provisioning.md, "CDs"):
hard links to the pinned images, each with its record (`NAME.iso.json`)
and its cover cut down to 480 pixels (`NAME.cover.jpg`, the record's
`cover`). `library.json` gives a disk's cover as `covers/NAME`, and the
tab's Disks panel shows those disks by their covers (Disks.md).

A link opens a System with disks already added, e.g.
`/?disk=disks/boot-8.6.img&disk=disks/games-ppc.img&system=boot-8.6.img&ppc=1`
(the page's controls do the same: the Disks panel's System choice and
library, PowerPC).

## The server

As a systemd user service of an account on the host (or a system
service with `User=`):

```ini
# ~/.config/systemd/user/executor-web.service
[Unit]
Description=Executor in a tab (tools/standalone)
After=network.target

[Service]
ExecStart=%h/bin/executor-standalone --data /srv/executor-web/data --images /srv/executor-web/library --bind 127.0.0.1 --port 8090 --quiet
Restart=on-failure

[Install]
WantedBy=default.target
```

```bash
cp tools/standalone/executor-standalone ~/bin/
systemctl --user daemon-reload && systemctl --user enable --now executor-web
loginctl enable-linger $USER          # keeps running signed out
journalctl --user -u executor-web     # its log (--quiet: errors only); the first line, its address
```

Every other setting is in `DATA/settings.json` (Standalone.md,
"Settings"; the tab's Settings dialog edits it): the library's
`disks.include` (`["*.img", "*.iso"]` for the CDs below), the relay's
`network.relay_*`, `network.trusted_proxies`. A restart of the service
frees the claim: the browser that had the server reloads to have it
again, and whichever browser opens it first then has it.

## HTTPS

Sound needs a secure context: HTTPS, or `localhost`. On a LAN the server
serves HTTPS itself: `--tls auto` makes a self-signed certificate once
(`DATA/tls/`, for localhost, the machine's name and addresses; its
SHA-256 printed to compare with what the browser shows when it asks the
first time), or `--tls-cert`/`--tls-key` (settings' `network.tls_cert`,
`tls_key`) for one of your own.

## HTTPS in front

Or the server speaks plain HTTP on 127.0.0.1 and a reverse proxy has the
certificate (`network.trusted_proxies: ["127.0.0.1"]` so its
`X-Forwarded-For` and `X-Forwarded-Proto` are believed: the claim's
cookie is then Secure). It must pass `Host` through (the server checks a
write's `Origin` against it), add `X-Forwarded-For` and
`X-Forwarded-Proto`, pass WebSocket upgrades
(`/relay`), not buffer (disk ranges stream), and allow bodies of
`network.max_upload_mb` (the tab's uploads to the store) and
`disks.max_image_mb` (disk images added through the tab). nginx, with a certificate from certbot:

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
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $http_connection;
        proxy_http_version 1.1;
        proxy_buffering off;
        client_max_body_size 4096m;
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

## Where things live

| Path | What | Back up |
|---|---|---|
| `/srv/executor-web/library` | disks and records (`make_library.py`); the changes to each disk beside it, `NAME-ID.overlay` | the `.overlay` files |
| `/srv/executor-web/library/uploads` | disk images added through the tab, their records and overlays | yes |
| `data/settings.json` | the settings | yes |
| `data/volumes/`, `data/objects/` | Executor's own volumes: manifests, and their files by SHA-256 | yes |
| `data/overlays/` | changes to disks not in the library (or when the library is read-only) | yes |
| `data/logs/` | the tab's logs, 14 days | no |
| `data/image-ids.json` | the library disks' content ids (a cache) | no |
| `data/tls/` | `--tls auto`'s certificate | yes, or it is made again (the browser asks again) |

A library disk's id is its content's: changing a disk (a new
`make_library.py` run that changes it) gives it a new id, and the
changes made to the old one stay beside it, unused, until deleted. Make
library disks again only when their contents should change.

## Backups

The overlay files are logs that are crash-consistent, and everything
else is written atomically: copying the data directory and the
`.overlay` files with any tool while the server runs is a backup (restic,
borg, rsnapshot, from a daily timer). To restore: stop the service, copy
them back, start it.

## The site's cells

`web/tests/tab_site.mjs` opens every application in a library disk's
record on a System, as a visitor would: on a scratch `tools/standalone`
whose library is `make_library.py`'s output, the boot disk and the
application disk added, each application launched in turn (the page
reloaded between them) and checked: in front, a window of its own (or,
full screen, the screen redrawn), its menu bar there and a menu drawn
when pressed (games: the menus there; a modal dialog at launch is
cancelled first, and an application with no window gets File > New), and
still in front with no error alert (a dialog whose text, ParamText
included, says so: the alert's text is the result's reason). SCOREBOARD.md's verdict says
what to expect: its tab columns, w761 on 7.6.1 and w86 on 8.6, made from
these cells' own results (the native e761 or e86 where a row has none
yet). A cell fails only when an application the scoreboard says runs
fails; an expected failure prints XFAIL, and one that now works XPASS
(bring the scoreboard up to date and it is expected from then on):
`scoreboard.py tab` runs every cell into `~/storage/executor-grid/m1/
w761|w86/` (results and screenshots), `scoreboard.py table` writes the
columns.

```bash
ctest --test-dir build-wasm -L site            # EXECUTOR_WEB_LIBRARY=~/storage/web-library
SITE_APPS=MacPerl ctest --test-dir build-wasm -R site.8.6.development -V
tools/executor/scoreboard.py tab --build build-wasm --force && tools/executor/scoreboard.py table
```

`executor.web.hidpi` (the Finder at scale 1 and 2, text redrawn from
outlines at 2: fails without Skia) is labelled `site` too, so the run
checks the module is the site's. Labelled `site`, not `executor` (about
20 minutes for the six cells). The canvas after each application's checks,
and the Finder's first, are saved as PNGs in `~/storage/tmp/site/shots/`
(`SYSTEM-APP.png`, `SITE_SHOTS=DIR` elsewhere); `SITE_DPR=2` runs the page
at devicePixelRatio 2, so they are the 2x canvas (`SYSTEM-APP@2x.png`).
