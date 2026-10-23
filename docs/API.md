# HTTP API

Everything is served from one port (default 11000, `--port`): the web UI,
the endpoints below, the `/ws` WebSocket and the HTTP video stream.
Request bodies are JSON. Routes are dispatched in
`src/webserver/api_handlers.cpp`; the guest-side commands in
`src/core/command_bridge.cpp` and `BridgeAgent/BridgeAgent.c`.

Endpoints marked **bridge** need BridgeAgent running in the guest:
`--bridge` (or `bridge_enabled` in the config, or `--headless-http`) makes
the emulator start it from the Host volume once the desktop is up;
`/api/status` says `bridge_agent_connected`. They answer
`{"success": false, "error": "bridge agent not running"}` until then.
Read commands (`/api/app`, `/api/windows`, `/api/ui`, `/api/text`) read
guest memory directly and need nothing in the guest. How both work:
[CommandBridge.md](CommandBridge.md).

## Status and screen

| Endpoint | | |
|---|---|---|
| `/api/status` | GET | `emulator_running`, `cpu_state`, `boot_phase`, `boot_elapsed`, `checkload_count`, `bridge_agent_connected`, `annotations`, the guest's text clipboard (`mac`), and a crash report if the child died |
| `/api/screenshot` | GET | the screen as PNG (503 before the first frame) |
| `/api/frame` | GET | one frame for HTTP polling: 45-byte header (dirty rect, size, cursor) + PNG; `?sid=` from the `X-Frame-Sid` header of the first reply; 204 when nothing changed |
| `/api/stream` | GET | the same frames as one chunked HTTP response (`?fps=N`) |
| `/api/resolutions` | GET | screen modes the machine offers, with the configured cap |
| `/api/resolution` | POST | `{"w":N,"h":N}`: switch the guest's screen to the nearest mode (bridge) |

## Input

| Endpoint | | |
|---|---|---|
| `/api/mouse` | GET | cursor position `{x, y}` |
| `/api/mouse` | POST | `{"x":N,"y":N}` absolute or `{"dx":N,"dy":N}` relative; `"button"`/`"down"` for clicks |
| `/api/keypress` | POST | `{"key":"return"}` or a Mac key code `{"key":36}`; `"down": true/false` for a press or release alone |
| `/api/clipboard` | POST | `{"text_b64": ...}`: MacRoman bytes into the guest's scrap (bridge) |
| `/api/invoke-debug` | POST | the Programmer's Key (NMI on 68k, Cmd+Power on PowerPC) |

## Emulator and configuration

| Endpoint | | |
|---|---|---|
| `/api/emulator/start` | POST | start the machine (with the web server, the emulator waits for this or the UI's Start) |
| `/api/emulator/stop` | POST | stop it (hard: the child process is killed) |
| `/api/emulator/restart`, `/api/emulator/reset` | POST | stop and start again |
| `/api/config` | GET | the configuration ([JsonConfig.md](JsonConfig.md)) |
| `/api/config` | POST | merge keys into it and save; most take effect at the next start |
| `/api/codecs` | GET | video codecs and which were compiled in |
| `/api/codec` | POST | `{"codec":"png"}` (`h264`, `vp9`, `png`, `webp`) |
| `/api/storage` | GET | ROMs (identified by checksum), disk images with their titles and tags, Executor Systems |
| `/api/storage/create-image` | POST | `{"filename", "size_mb", "volume_name", "format": "hfs"/"hfs+"}`: a blank disk image in `<storage>/images` |
| `/api/executor/system` | POST | `{"image": "7.5.5.img"[, "name"]}`: copy an image's System Folder out for Executor |

## Reading the guest

| Endpoint | | |
|---|---|---|
| `/api/app` | GET | the front application's name |
| `/api/windows` | GET | the window list (in-process runs only, e.g. `--headless-http`; with the emulator in a child process it answers empty: use `/api/ui`) |
| `/api/ui` | GET | the front process's windows (screen rectangles), controls, dialog items and menus, open pop-ups included: click by name, not pixels |
| `/api/text` | GET | the text on screen as QuickDraw drew it, with rectangles, font, window and application; `?since=N`: every draw since sequence N, off screen too (when they'd pass 256 KiB the oldest go, counted in `dropped`); `?raw=1`: unmerged |
| `/api/wait` | POST | `{"condition": "boot=Finder" \| "app=Name" \| "text=Substring", "timeout": 30}`: wait until it holds |

## Commands in the guest (bridge)

| Endpoint | | |
|---|---|---|
| `/api/launch` | POST | `{"path":"Macintosh HD:App"}`; `"open": true` opens a document with its creator (`aevt`/`odoc`) |
| `/api/quit` | POST | quit the front application (`aevt`/`quit`); `"agent": true` quits BridgeAgent |
| `/api/script` | POST | `{"creator":"McPL", "script": ...}` (or `"script_b64"` for exact MacRoman): a `misc`/`dosc` event to any scriptable application (MacPerl, Frontier, MPW Shell, ToolServer, Script Editor) |
| `/api/exec` | POST | `{"creator":"MPSX", "command": ...}`: do-script with the reply: `exit_status`, `stdout_b64`, `stderr_b64` (ToolServer) |
| `/api/copy` | POST | `{"from":"Vol:Item", "to":"Vol:Folder:"}`: File Manager copy, both forks and Finder info; `"replace": true` merges into an existing folder |
| `/api/shutdown` | POST | shut the guest down (`ShutDwnPower`); the emulator exits when it has |
| `/api/restart` | POST | restart the guest (`ShutDwnStart`) |

## Tools and diagnostics

| Endpoint | | |
|---|---|---|
| `/api/snapshot` | POST | `{"name": ...}`: guest RAM and ROM to `<storage>/snapshots/NAME/` (read by `tools/macdecode`) |
| `/api/annotate` | POST | `{"text": ...}`: a line shown over the screen in the web UI and kept in `/api/status` (provisioning posts each step); `""` clears |
| `/api/log`, `/api/error` | POST | the browser's warnings and errors, printed by the server |

## WebSocket

`/ws` carries WebRTC signaling, mouse and keyboard input, and, with the
PNG/WebP codecs, the frames and Opus sound. Transports and proxying:
[Deployment.md](Deployment.md).
