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

All fields are flat at the top level. There are no `m68k.*` / `ppc.*`
sub-objects in the new schema (legacy keys are still accepted on load and
silently dropped on first save).

### CPU

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `backend` | string | `"uae"` | `"uae"`, `"unicorn-m68k"`, `"unicorn-ppc"`, `"kpx"`, `"dualcpu"`. Determines architecture; there is no separate `architecture` field. |
| `jit` | bool | `false` | Enable backend's primary JIT (uae, kpx). No-op for unicorn-* backends. |
| `jit68k` | bool | `true` | Enable 68k-on-PPC DR JIT (kpx only). |
| `idlewait` | bool | `true` | Pause CPU when guest is idle (m68k rsrc patch + ppc SynchIdleTime). |

### UAE JIT internals (rarely tuned)

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
| `screen` | string | `"640x480"` | Display resolution (`"WxH"`) |
| `rom` | string | `""` | ROM file path (absolute, or relative to `storage_dir/roms/`) |
| `disks` | array | `[]` | Disk image paths (absolute, or relative to `storage_dir/images/`) |
| `cdroms` | array | `[]` | CD-ROM image paths (same resolution rules) |
| `extfs` | array | `[]` | Host filesystem directories to share with the emulator |
| `bootdriver` | int | `0` | Boot driver (`0` = first disk, `-62` = CD-ROM) |
| `audio` | bool | `false` | Enable audio (Opus over WebRTC) |

### Streaming

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `codec` | string | `"vp9"` | Video codec (`"png"`, `"webp"`, `"h264"`, `"vp9"`, `"httpstream"`) |
| `mousemode` | string | `"absolute"` | Mouse mode (`"absolute"` or `"relative"`) |

### Networking

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `network` | string | `"none"` | `"none"` or `"socket"` (Unix socket to net-bridge) |
| `network_if` | string | `""` | Socket path when `network` is `"socket"` |

### System

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `zappram` | bool | `false` | Clear PRAM on startup |
| `dismiss_shutdown_dialog` | bool | `true` | Auto-dismiss "improper shutdown" dialog |
| `bridge_enabled` | bool | `false` | Enable the automation bridge (BridgeAgent + ExtFS) |
| `browser_enabled` | bool | `false` | Run the MacBrowser host pipeline (Xvfb + Firefox); needs `--browser` runtime deps |

### Server

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `http_port` | int | `11000` | HTTP server port (also hosts `/ws` signaling WebSocket) |
| `storage_dir` | string | `"~/storage"` | Root directory for ROMs and disk images |
| `client_dir` | string | `"./client"` | Path to the web UI; resolved relative to the binary if not absolute |

### Logging

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `log_level` | int | `0` | Log verbosity (0=milestones, 1=important, 2=all, 3=+registers) |
| `debug_connection` | bool | `false` | Log WebRTC connection details |
| `debug_mode_switch` | bool | `false` | Log video mode switches |
| `debug_perf` | bool | `false` | Log performance stats |
| `debug_network` | bool | `false` | Log net-bridge / lwIP NAT/DNS/ICMP/TCP/UDP |

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
  roms/           — ROM files (.rom), scanned recursively
  images/         — Disk and CD-ROM images (.img, .dsk, .iso, etc.)
```

The web UI's file picker scans these directories via `GET /api/storage`.

## Example Config

```json
{
  "rom": "1MB ROMs/Quadra 950.ROM",
  "disks": ["system-7.6.img"],
  "cdroms": [],

  "backend": "uae",
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

The web UI settings dialog uses these endpoints. Changes take effect on next boot (some settings like codec and mousemode take effect immediately).

## CLI Flags

CLI flags override config file values. See `--help` or [Commands.md](Commands.md) for the full list.

```bash
# Override ROM and port from CLI
./build/mac-phoenix --port 9000 --ram 64 /path/to/quadra.rom

# Use a specific config file
./build/mac-phoenix --config /path/to/config.json

# Ignore config file entirely
./build/mac-phoenix --config /dev/null --disk system.img quadra.rom
```

## Environment Variables

The emulator binary does not read environment variables for configuration — use CLI flags instead (e.g. `--backend`, `--timeout`, `--log-level`, `--screenshots`).

| Variable | Scope | Description |
|----------|-------|-------------|
| `MACEMU_ROM` | Test scripts only | Default ROM path (not read by the binary) |
