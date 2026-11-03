# Executor's tab: settings

One JSON document, described by one JSON Schema:
`web/executor/settings.schema.json`. The schema is where every setting's
category, default, type, range, label and help text are; nothing else
repeats them. Two implementations check a document against it, with
the same subset of JSON Schema and the same messages:

| | Where | Keeps |
|---|---|---|
| `tools/standalone` (`schema.go`, `settings.go`) | the Go server | `settings.json` (`--config`; default in the data directory, Standalone.md): all of it |
| `web/executor/settings.js` | the tab | on the server, through `/api/settings` (without it: the defaults, kept nowhere) |

`web/tests/settings_cases.json` holds them to the same answers (go test,
`tab_settings.mjs`). The Go server embeds a copy of the
schema (`tools/standalone/settings.schema.json`; go test fails if it
differs: copy it after editing).

## The document

```json
{
  "version": 2,
  "machine":  { "system": "", "ram_mb": 0, "screen": "640x480", "depth": 0, "ppc": false },
  "disks":    { "mounted": [], "images": "", "include": ["*.img", "*.iso"], "exclude": [], "max_image_mb": 4096 },
  "input":    { "command_key": "meta", "keyboard_capture": true, "right_click": "control", "wheel_scroll": true },
  "display":  { "scale": 0, "smoothing": false, "fullscreen_button": true },
  "sound":    { "enabled": true, "volume": 80 },
  "network":  { "bind": "127.0.0.1", "port": 8090, "relay_enabled": true, "relay_ports": [...], ... },
  "advanced": { "logtraps": "", "logtraps_nesting": 6, "debug": "", "ppc_break": "", "screenshot_button": true }
}
```

The schema's own keywords, beside the standard ones:

- `x-scope`: `page` (the tab's) or `server` (tools/standalone's own). A
  hosted server offers and keeps only the page's; its own settings are
  its `server.toml`. Without a server, the page's only, in the browser.
- `x-apply`: `live` (at once), `executor` (Executor restarts: the dialog
  and the bar say "Restart Executor to apply" until it has), `server` (at
  the server's next start: `restart_needed`).
- `x-labels` (an `enum`'s, or its `examples`'), `x-group` (a heading
  before the setting), `x-widget` (`number`, `range`, `path`, `words`,
  `disk-list`, `screen`: the `examples` and Custom…), `x-choices`
  (options the page knows: `systems`), `x-auto` (a setting with an
  automatic value: `{ value, choices, labels }`, `value` the one stored
  for Auto, `0` or `"auto"`; the dialog shows an Auto box, then the
  choices, or a number without them).
- `readOnly`: answered, never written (`advanced.data`: the data
  directory).
- `x-version`: the document's version. A new version that renames or
  moves settings gets a step in `migrate` (`schema.go`); a setting added
  needs none (missing settings take their defaults).

## The API

`GET /api/settings` → `{ version, settings, scopes, restart_needed,
can_change_server }` (the Go server adds `file`).

`POST /api/settings` with a JSON merge patch (RFC 7396) of `settings`:
`{"machine": {"ram_mb": 96}}`; `null` puts a setting back to its default,
`{"input": null}` a whole category. The answer is the GET's; an invalid
value is 400 `{ error, path }` (`path` dotted: `machine.ram_mb`,
`disks.mounted[1]`), a setting outside the server's scopes 403. The
patch is checked whole: nothing changes if any of it is wrong.

## The dialog

The bar's Settings button opens it (`Settings.open(category)`). Its
changes are a draft: **Save** sends them in one patch (checked whole; a
refused value is said under its setting and the dialog stays open), then
closes it; **Cancel** and Escape forget them. The footer says what is
unsaved and, before Save, which changes Executor (`x-apply: executor`) or
the server (`x-apply: server`) must restart for; a changed row has a blue
dot, one saved but waiting for Executor's restart a yellow one (and the
bar's "Restart Executor to apply"). The Reset buttons put defaults into
the draft. A setting's `description` is its help: a tooltip
(`role=tooltip`, the control's `aria-describedby`) shown when the mouse
rests on the setting or the keyboard's focus is in it. The screen size is
a list of the classic Macs' (`examples`, named by `x-labels`) with
Custom… for a width and a height (100 to 9999). A setting with `x-auto`
(Memory, Colours, Sharpness, Port, Secure cookies) has an Auto box; while
it is ticked the choices beside it are disabled and show what Auto picks
here (settings.js `AUTO_PICKS`: Memory and Colours from the System
chosen, Sharpness from the display); unticked, that pick is the choice.
No help text says "0 =": the stored 0 is the dialog's Auto.

## The page

`settings.js` loads before `main.js`. `Settings.get(path)` is what
applies: the saved value, unless the page's address overrides it for the
visit (`?size=`, `?scale=`, `?ppc`, `?system=`, `?logtraps=`,
`?nesting=`, `?debug=`, `?ppcbreak=`; the dialog says so). main.js reads
the machine's at each start of Executor; the bar's PowerPC box and the
Disks panel's System choice write them. The library disks picked:
`disks.mounted`, in order (live: the page puts them in and takes them out
of the running Mac, Disks.md).

What applies live: the Command key (`keys.js`: Command on Meta, Ctrl or
Alt, Option and Control the other two), the right button (a click, or
Control-click), smoothing, the bar's Screenshot and Fullscreen buttons,
the volume (a GainNode). Fullscreen with `keyboard_capture` takes the
keyboard through the Keyboard Lock API (Chromium-based browsers), so
Ctrl+W and the like reach the Mac; holding Esc leaves.

"Restart Executor" in the dialog saves what changed and reloads the page.

The bar's CPU meter (main.js `CpuMeter`, `#cpu`): how much of each half
second the Mac's emulator thread spent working rather than waiting for
events (`hostidle::busy()`: time inside its waits, which yield to the
browser, against the clock; timestamps around the waits only). The Worker
passes it on each half second (`'cpu'`); the bar shows the last 15 seconds
as bars (orange above 90%) and the latest as a %, "Mac CPU busy" as the
tooltip; hidden until the first frame and when Executor stops.
`window.executor.cpu` has `{ busy, history }`; the Worker's `stats`
request `cpu`. The idle Finder reads a few %, a game running on 100%
(`executor.web.cpu`). Natively the same number is `/api/status`'s
`cpu_busy`.

Beside it, Executor's memory (an estimate, worker.js `memoryUse`, with
each CPU sample): the module's heap (`HEAP8.buffer`, the Mac's RAM in it:
MemTop, from the module), the library disks' pieces read from the server
(`RemoteImage` chunks), the disks' changes (`Overlay` data) and Executor's
volume (`/executor` in MEMFS: the files' sizes, from the walk the server
sync makes (`scanVolume`), or one when that is over 5 s old). The tooltip gives the
parts; the browser's own memory (the page, its scripts, uploaded files) is
not counted. On 7.6.1 with the games disk: about 315 MB (heap 256, of it
RAM 126; caches 44; volume 15). Where the browser measures memory itself
(`performance.measureUserAgentSpecificMemory`: cross-origin isolated pages,
Chromium; the tab is), the tooltip gives its figure too, taken at most
every 10 s (it can wait for a garbage collection): 331 MB there.

## Not yet

- Disks: the overlays' revert and download are in the Disks panel
  (Disks.md); uploaded images are not part of the settings (they are files).
- Network: the relay settings are the Go server's; the tab itself has no
  network setting (it uses the relay whenever the server offers one).
- Hosted: the owner edits the server's settings in `server.toml`, not in
  the dialog.
