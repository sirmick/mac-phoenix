# Configuration

mac-phoenix uses a single flat JSON config file. All fields are optional — defaults are used for anything not specified. CLI arguments override config file values.

## Config File Location

| Priority | Location |
|----------|----------|
| 1 (highest) | `--config /path/to/config.json` |
| 2 | `~/.config/mac-phoenix/config.json` |
| 3 (lowest) | Defaults |

The config file is created automatically when you save settings from the web UI.

## Priority Order

Settings are resolved in this order (highest wins):

1. **Command-line arguments** (`--port 9000`)
2. **JSON config file** (`config.json`)
3. **Defaults**

CLI arguments affect the runtime config only — they are never saved to the config file. When the web UI saves settings, only UI-modified values are persisted, merged into the existing config file.

## Minimal Config

You only need to specify values that differ from defaults:

```json
{
  "rom": "quadra650.rom",
  "disks": ["system.img"],
  "storage_dir": "~/storage"
}
```

## Full Schema

All fields are flat at the top level, apart from the `keyboard` and
`configs` objects. Keys the loader doesn't know are ignored, including the
legacy `architecture`, `cpu_backend`, `m68k` / `ppc` objects and `jit68k`.
They are not removed: a save writes back what the file held, plus the UI's
changes.

### CPU

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `backend` | string | `"uae"` | `"uae"` (68k Mac, ROM), `"kpx"` (Power Mac, ROM) or `"executor"` (68k, Executor's Toolbox, no ROM). Determines the architecture; there is no separate `architecture` field. An unknown value falls back to `"uae"`. |
| `cpu_core` | string | `"uae"` | CPU core for the backend's architecture: `"uae"` or `"mame-68k"` for `uae` and `executor`, `"kpx"` or `"mame-ppc"` for `kpx`. A core that doesn't fit the backend is replaced by the backend's default (`uae` / `kpx`); `"musashi"` is read as `"mame-68k"`. |
| `jit` | bool | `false` | Enable the core's JIT (uae; kpx; mame-ppc's native back-end). |
| `idlewait` | bool | `true` | Pause CPU when guest is idle (m68k rsrc patch + ppc SynchIdleTime). |

### UAE JIT internals (rarely tuned)

Only used with the UAE core and `jit: true`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `jit_fpu` | bool | `true` | JIT-compile FPU instructions |
| `jit_debug` | bool | `false` | Enable JIT debugger |
| `jit_cache_size` | int | `8192` | JIT translation cache size in KB |
| `jit_lazy_flush` | bool | `true` | Lazy invalidation of JIT cache |
| `jit_inline` | bool | `true` | Inline constant jumps in JIT |
| `jit_blacklist` | string | `""` | Opcodes to exclude from JIT |

### Memory & media

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ram_mb` | int | `64` | RAM size in megabytes |
| `screen` | string | `"640x480"` | Boot display resolution (`"WxH"`) |
| `max_resolution` | string | `""` | Cap on the screen modes offered to the guest (`"WxH"`; empty = no cap). CLI: `--max-resolution` |
| `rom` | string | `""` | ROM file path (absolute, or relative to `storage_dir/roms/`) |
| `disks` | array | `[]` | Disk image paths (absolute, or relative to `storage_dir/images/`) |
| `cdroms` | array | `[]` | CD-ROM image paths (same resolution rules) |
| `extfs` | array | `[]` | Host folders to share. Only the first is mounted (the Host volume); a string is read as a one-element array |
| `bootdriver` | int | `0` | Boot driver (`0` = any, `-62` = CD-ROM) |
| `audio` | bool | `false` | Send sound to the browser: Opus on the WebRTC track with H.264/VP9, over `/ws` with PNG/WebP. The Mac has its sound hardware either way |

### Streaming

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `codec` | string | `"vp9"` | Video codec (`"png"`, `"webp"`, `"h264"`, `"vp9"`, `"httpstream"`) |
| `mousemode` | string | `"absolute"` | Mouse mode (`"absolute"` or `"relative"`) |
| `keyboard` | object | see below | Modifier remap applied by the web client: `ctrl`, `alt`, `meta`, `fn` (each `"command"`, `"control"`, `"option"`, `"shift"` or `"off"`; empty = the client's per-platform default) and `release_on_blur` (bool, default `true`: release held keys when the window loses focus) |

### Networking

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `network` | string | `"none"` | `"none"` or `"socket"` (Unix socket to net-bridge) |
| `network_if` | string | `""` | Socket path when `network` is `"socket"` (default `/tmp/mac-ether.sock`) |

### Serial

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `serial_a` | string | `""` | Port A: `"pty"` allocates a pseudo-tty, any other value is a device path; empty, `"none"` or `"off"` disables the port |
| `serial_b` | string | `""` | Port B, same values |

### System

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `zappram` | bool | `false` | Clear PRAM on startup |
| `model_id` | int | `0` | Machine type the Mac is told (Gestalt ID - 6). `0` = the ROM machine's own; `5` = IIci, for Systems before 7.5 on the 1 MB 68k ROMs. Written only when set |
| `dismiss_shutdown_dialog` | bool | `true` | Auto-dismiss "improper shutdown" dialog |
| `bridge_enabled` | bool | `false` | Enable the automation bridge: the emulator launches BridgeAgent from the Host volume and answers it through the bridge trap `$ABE5` ([CommandBridge.md](CommandBridge.md)) |
| `browser_enabled` | bool | `false` | Run the MacBrowser host pipeline (in-process Chromium via Qt6 WebEngine). Needs a `-DBUILD_BROWSER=ON` build; ignored otherwise |
| `browser_available` | bool | | Read-only, in `GET /api/config`: whether the build has MacBrowser |

### Executor

Used with `"backend": "executor"`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `executor_system` | string | `""` | A disk image in `storage_dir/images` whose System Folder Executor runs on (extracted into `storage_dir/executor-systems` on first use; the image is mounted beside it), or the name of a System prepared there. Empty: Executor's own data folder |
| `executor_start` | string | `"finder"` | What to start: `"finder"`, `"browser"` (Executor's Browser) or a path inside the System |
| `executor_fresh` | bool | `true` | Restore the System from its clean copy before each run |
| `executor_data_dir` | string | `""` | Executor's data folder; overrides `executor_system`. Empty: `storage_dir/executor` |
| `executor_app` | string | `""` | Host path of the application to start; overrides `executor_start` |

The other Executor options (`--no-executor-ot`, `--executor-ppc`,
`--executor-writable-images`, `--executor-logtraps`, `--executor-debug`,
`--watch`) are CLI-only.

### Server

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `http_port` | int | `11000` | HTTP server port (also hosts `/ws` signaling WebSocket) |
| `storage_dir` | string | `"~/storage"` | Root directory for ROMs and disk images |
| `client_dir` | string | `"./client"` | Path to the web UI. A relative path is looked up as `<binary dir>/../<client_dir>`, then `<binary dir>/../share/mac-phoenix/client` |

### Saved configurations

| Field | Type | Description |
|-------|------|-------------|
| `configs` | object | The web UI's saved presets: name to config snapshot. The emulator only stores and returns it |

### Logging

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `log_level` | int | `0` | Log verbosity (0=milestones, 1=important, 2=all, 3=+registers) |
| `debug_connection` | bool | `false` | Log WebRTC connection details |
| `debug_mode_switch` | bool | `false` | Log video mode switches |
| `debug_perf` | bool | `false` | Log performance stats |
| `debug_network` | bool | `false` | Log net-bridge NAT/DNS/ICMP/TCP/UDP |

## Path Resolution

Relative paths in `rom`, `disks`, and `cdroms` are resolved against `storage_dir`:

| Field | Resolved to |
|-------|-------------|
| `rom` | `storage_dir/roms/<path>` |
| `disks` | `storage_dir/images/<path>` |
| `cdroms` | `storage_dir/images/<path>` |

Absolute paths (starting with `/`) are used as-is. The `~` prefix is expanded to `$HOME`.

## Storage Directory Layout

```
storage_dir/
  roms/              ROM files, scanned recursively
  images/            Disk and CD-ROM images (.img, .dsk, .iso, ...), with
                     their records (<name>.img.json) from provisioning/build.py
  executor/          Executor's own data folder
  executor-systems/  System Folders extracted for Executor (<name>, <name>.clean)
  tmp/               Scratch (the bridge's shared folder when no --extfs is given)
```

The web UI's file picker scans these directories via `GET /api/storage`.

## Example Config

```json
{
  "rom": "1MB ROMs/Quadra 950.ROM",
  "disks": ["system-7.6.img"],
  "cdroms": [],

  "backend": "uae",
  "cpu_core": "uae",
  "jit": true,
  "idlewait": true,

  "ram_mb": 64,
  "screen": "800x600",
  "audio": true,

  "codec": "vp9",
  "mousemode": "relative",

  "http_port": 11000,
  "storage_dir": "/home/user/storage"
}
```

## Web UI Integration

The config is read and written via the HTTP API:

- **`GET /api/config`** — returns the full config as JSON
- **`POST /api/config`** — accepts a partial JSON object, merges it into the live config, and saves to disk

The web UI settings dialog uses these endpoints. Most changes take effect when the emulator is next started; the codec has its own endpoint (`POST /api/codec`) that switches at once.

## CLI Flags

CLI flags override config file values. See `--help` or [Commands.md](Commands.md) for the full list.

```bash
# Override ROM and port from CLI
./build/mac-phoenix --port 9000 --ram 64 /path/to/quadra.rom

# Executor on Apple's 7.5.5 System, no ROM
./build/mac-phoenix --backend executor --executor-system 7.5.5.img

# Use a specific config file
./build/mac-phoenix --config /path/to/config.json

# Ignore config file entirely
./build/mac-phoenix --config /dev/null --disk system.img quadra.rom
```

## Environment Variables

The emulator binary does not read environment variables for configuration; use CLI flags instead (e.g. `--backend`, `--timeout`, `--log-level`, `--screenshots`). Debug and trace variables are listed in [Commands.md](Commands.md#environment-variables).

| Variable | Scope | Description |
|----------|-------|-------------|
| `MACEMU_ROM` | Test scripts only | Default ROM path (not read by the binary) |
