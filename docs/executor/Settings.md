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
- `x-labels` (an `enum`'s), `x-group` (a heading before the setting),
  `x-widget` (`number`, `range`, `path`, `words`,
  `disk-list`), `x-choices` (options the page knows: `systems`).
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

## Not yet

- Disks: the overlays' revert and download are in the Disks panel
  (Disks.md); uploaded images are not part of the settings (they are files).
- Network: the relay settings are the Go server's; the tab itself has no
  network setting (it uses the relay whenever the server offers one).
- Hosted: the owner edits the server's settings in `server.toml`, not in
  the dialog.
