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

### Top-Level Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `architecture` | string | `"m68k"` | CPU architecture (`"m68k"` or `"ppc"`) |
| `cpu_backend` | string | `"uae"` | CPU backend (`"uae"`, `"unicorn"`, `"dualcpu"`) |
| `ram_mb` | int | `32` | RAM size in megabytes |
| `rom` | string | `""` | ROM file path (absolute, or relative to `storage_dir/roms/`) |
| `disks` | array | `[]` | Disk image paths (absolute, or relative to `storage_dir/images/`) |
| `cdroms` | array | `[]` | CD-ROM image paths (absolute, or relative to `storage_dir/images/`) |
| `floppies` | array | `[]` | Floppy image paths (absolute, or relative to `storage_dir/images/`) |
| `extfs` | string | `""` | Host filesystem directory to share with the emulator |
| `screen` | string | `"640x480"` | Display resolution (`"WxH"`) |
| `audio` | bool | `true` | Enable audio |
| `bootdrive` | int | `0` | Boot drive number |
| `bootdriver` | int | `0` | Boot driver (`0` = any disk, `-62` = CD-ROM) |
| `codec` | string | `"png"` | Video codec (`"png"`, `"h264"`, `"vp9"`, `"webp"`) |
| `mousemode` | string | `"absolute"` | Mouse mode (`"absolute"` or `"relative"`) |
| `http_port` | int | `8000` | HTTP server port |
| `signaling_port` | int | `8090` | WebRTC signaling port |
| `storage_dir` | string | `"~/storage"` | Root directory for ROMs and disk images |
| `nocdrom` | bool | `false` | Don't install CD-ROM driver |
| `nosound` | bool | `false` | Disable sound |
| `zappram` | bool | `true` | Clear PRAM on startup |
| `frameskip` | int | `6` | Frames to skip between refreshes |
| `yearofs` | int | `0` | Year offset for Mac clock |
| `dayofs` | int | `0` | Day offset for Mac clock |
| `udptunnel` | bool | `false` | Tunnel network packets over UDP |
| `udpport` | int | `6066` | UDP port for network tunneling |
| `log_level` | int | `0` | Log verbosity (0=milestones, 1=important, 2=all, 3=+registers) |
| `debug_connection` | bool | `false` | Log WebRTC connection details |
| `debug_mode_switch` | bool | `false` | Log video mode switches |
| `debug_perf` | bool | `false` | Log performance stats |

### M68K Sub-Object (`m68k`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `cpu_type` | int | `4` | CPU type (0=68000, 1=68010, 2=68020, 3=68030, 4=68040) |
| `fpu` | bool | `true` | Enable FPU emulation |
| `modelid` | int | `14` | Mac model ID (Gestalt Model ID minus 6) |
| `jit` | bool | `true` | Enable JIT compiler (UAE backend only) |
| `jitfpu` | bool | `true` | JIT-compile FPU instructions |
| `jitdebug` | bool | `false` | Enable JIT debugger |
| `jitcachesize` | int | `8192` | JIT translation cache size in KB |
| `jitlazyflush` | bool | `true` | Lazy invalidation of JIT cache |
| `jitinline` | bool | `true` | Inline constant jumps in JIT |
| `jitblacklist` | string | `""` | Opcodes to exclude from JIT |
| `idlewait` | bool | `true` | Sleep when Mac OS is idle |
| `ignoresegv` | bool | `true` | Skip illegal memory accesses |
| `swap_opt_cmd` | bool | `true` | Swap Option and Command keys |
| `keyboardtype` | int | `5` | Mac keyboard type |

### PPC Sub-Object (`ppc`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `cpu_type` | int | `4` | PPC CPU type |
| `fpu` | bool | `true` | Enable FPU |
| `modelid` | int | `14` | Mac model ID |
| `jit` | bool | `true` | Enable JIT compiler |
| `jit68k` | bool | `false` | JIT-compile 68K code in PPC mode |
| `idlewait` | bool | `true` | Sleep when idle |
| `ignoresegv` | bool | `true` | Skip illegal memory accesses |
| `ignoreillegal` | bool | `false` | Skip illegal instructions |
| `keyboardtype` | int | `5` | Mac keyboard type |

## Path Resolution

Relative paths in `rom`, `disks`, `cdroms`, and `floppies` are resolved against `storage_dir`:

| Field | Resolved to |
|-------|-------------|
| `rom` | `storage_dir/roms/<path>` |
| `disks` | `storage_dir/images/<path>` |
| `cdroms` | `storage_dir/images/<path>` |
| `floppies` | `storage_dir/images/<path>` |

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
  "architecture": "m68k",
  "cpu_backend": "uae",
  "rom": "1MB ROMs/Quadra 950.ROM",
  "disks": ["system-7.6.img"],
  "cdroms": [],
  "ram_mb": 64,
  "screen": "800x600",
  "audio": true,
  "bootdriver": 0,
  "codec": "vp9",
  "mousemode": "relative",
  "http_port": 8000,
  "signaling_port": 8090,
  "storage_dir": "/home/user/storage",
  "m68k": {
    "cpu_type": 4,
    "fpu": true,
    "modelid": 14,
    "jit": true,
    "idlewait": true,
    "ignoresegv": true,
    "swap_opt_cmd": true,
    "keyboardtype": 5
  }
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
