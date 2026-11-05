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
  `disk-list`, `screen`: the `examples` and Custom…, and Auto when it
  has `x-auto-setting`, the boolean setting Auto is: `display.fit_window`),
  `x-shown-in` (a setting the dialog shows in another's row, not its own),
  `x-choices`
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
Auto (`display.fit_window`: the Mac's screen follows the window size; the
size stays stored for when Auto is off), then a list of the classic Macs'
(`examples`, named by `x-labels`) with Custom… for a width and a height
(100 to 9999); a size turns Auto off. A setting with `x-auto`
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
Disks panel's System choice write them. The System is one setting,
`machine.system` (a disk image's name): the Disks panel's choice and
Settings > Machine's follow it both ways, both through main.js
`pickSystem`. A library System disk chosen (by its url; the dialog offers
one not in yet by its url) goes into `disks.mounted`; the one the last
choice put in comes out when the choice changes (a disk that was in
already, as a disk of its own, stays). Library disks are known by url
(`fetchedFrom`: the url each fetched image came from), local images by
name. The dialog's list is the page's
(`Settings.provide('systems', draft => …)`: the disks in, and the library's
System disks): made again when the dialog opens, after each change in it,
and when the page's disks change while it is open
(`Settings.choicesChanged()`); a setting changed by the page while the
dialog is open reaches the draft unless the draft had changed it. The library disks picked:
`disks.mounted`, in order (live: the page puts them in and takes them out
of the running Mac, Disks.md).

What applies live: the Command key (`keys.js`: Command on Meta, Ctrl or
Alt, Option and Control the other two), the right button (a click, or
Control-click), smoothing, the bar's Screenshot and Fullscreen buttons,
the volume (a GainNode), the screen size and Sharpness (the display
scale: below). Fullscreen with `keyboard_capture` takes the
keyboard through the Keyboard Lock API (Chromium-based browsers), so
Ctrl+W and the like reach the Mac; holding Esc leaves.

"Restart Executor" in the dialog saves what changed and reloads the page.

The bar's Screenshot button sends the canvas's pixels (and Executor's 1x
frame at a scale above 1) to the server beside the session's log
(`/api/shot`: `logs/DAY/SESSION-HHMMSS.png` and a `# screenshot` line),
with a note asked for first. **Ctrl+Alt+S** does the same and works with
the mouse button down: a menu tracked, a drag, a button held, to catch
drawing mid-action. The page catches it in the capture phase (as
Ctrl+Alt+D) and the Mac gets neither the S's down nor its up. With the
button up it asks for the note first; down, a dialog would end the press,
so the shot goes at once (note "Ctrl+Alt+S, the mouse button down") and
the note is asked for when the button comes up, a `[web] screenshot NAME
note: ...` line in the log. Executor answers the 1x frame (`'screen1x'`)
at its yields too, not only at event calls, which a menu track makes none
of (`web_main.cpp` `on_yield`). The Screenshot button's tooltip and the
`?diag` overlay say so. Test: `executor.web.shotkey` (tab_shotkey.mjs: the
Special menu held open on 7.6.1 at dpr 2, both PNGs with the menu in them).

The bar's screen size (main.js `ScreenBar`, `#res`, before the CPU meter):
**Auto** first (`display.fit_window`: the Mac's screen follows the window
size, 400 ms after the window settles; tooltip "the Mac's screen follows
the window size"), then the dialog's sizes (the schema's `examples`, shown
"640 × 480", the Mac named in each option's tooltip). The dialog's Screen
size has the same Auto first (`x-auto-setting`; there is no separate row
for it). A size turns Auto off. The bar writes the same settings,
`machine.screen` (and `display.fit_window` off) or `display.fit_window` on,
through `Settings.set`, so the change is live as the dialog's is
(DisplayModes.md), saved, and reaches an open dialog's draft; a saved
dialog, or any other change, comes back to it through `Settings.on`. It
shows the Mac's real size, from Executor's `'screen'` message: in the Auto
option ("Auto · 1240 × 780") while Auto is on, else as the size chosen, or
as an option of its own, after Auto, when the list hasn't it (a Custom… size, or one an application set with
`DMSetDisplayMode`; the tooltip then says what the setting asked for). Its
tooltip says the size and that it is the same setting as Settings'; with
`?size=` in the address it is disabled (the address sets it for the visit).
The Monitors control panels, which can't change Executor's screen, are
left out (DisplayModes.md).

The bar's HiDPI box (main.js `HiDpiBar`, `#hidpi`, after the screen size):
`display.scale` 2 ticked, 1 not, through `Settings.set` as the screen size
is; its tooltip "draw the Mac at 2x: sharp on high-DPI screens; costs speed
and memory", then the scale drawn now and that it is the same setting as
Settings > Display > Sharpness. That one keeps Auto (0: `devicePixelRatio`
rounded, 1 to 4) as its default and offers 1× to 4×; the box shows the
scale in effect (ticked at Auto on a dpr 2 display, at 3× too). With
`?scale=` in the address it is disabled. Live (HiDPI.md, "The scale
changed live"): the page asks Executor (`'scale'`), the next frames come
at the new size and the Worker's canvas follows them; clicks and the
pointer are mapped in the Mac's 1x pixels at any scale (`guestPoint`,
`cursor.js`), so nothing else on the page changes. A module built without
Skia answers that it draws at 1× only, which the tooltip then says.

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
