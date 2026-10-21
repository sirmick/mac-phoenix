# Platform adapter pattern

Every driver subsystem (video, audio, scsi, serial, ether, disk, platform)
has the same shape: a small adapter with Basilisk II's function names that
defers to a function pointer in `g_platform`, a default filled in by
`platform_init()`, and real drivers that overwrite those pointers when the
process wants the feature.

## Pattern

```cpp
/* adapter: core code calls Basilisk II's name, never a driver */
void SCSIInit(void) { g_platform.scsi_init(); }      /* scsi_adapter.cpp */

/* null driver: a safe no-op */
void scsi_null_init(void) { }                        /* scsi_null.cpp */

/* platform_init() (src/common/platform.cpp) sets the defaults */
void platform_init(void) {
    g_platform.scsi_init = scsi_null_init;
    g_platform.serial_init = serial_unix_init;       /* some defaults are real */
    g_platform.sys_open = platform_unix_sys_open;
    /* … */
}

/* startup code swaps in real drivers (src/main.cpp, IPC child) */
g_platform.video_init = video_ipc_m68k_init;
g_platform.video_refresh = video_ipc_refresh;
if (emu_config.audio_enabled)
    g_platform.audio_init = []() { audio_direct_init(); };
```

The driver entries are always set, so callers use them without NULL
checks. The CPU entries are different: the CPU installer sets them, and the
PPC-only ones (`cpu_get_gpr`, `cpu_execute_ppc`, `ppc_emulop_handler`, …)
stay NULL on 68k; code that uses those checks first.

## Subsystems

| Subsystem | Adapter | Default (`platform_init`) | Real drivers |
|-----------|---------|---------------------------|--------------|
| SCSI | `scsi/scsi_adapter.cpp` | `scsi_null.cpp` | — |
| Video | `video/video_adapter.cpp` | `video_null.cpp` | IPC child: `video_ipc_m68k_init` (`src/main.cpp`) + `video_ipc_refresh` (`src/ipc/video_ipc_ppc.cpp`); PPC: a no-op `video_init` + `video_ipc_refresh`, the Mac-side driver being `src/cpu/kpx/video_ppc.cpp`; headless `--screenshots`: `video_screenshot.cpp` |
| Audio | `audio/audio_adapter.cpp` | `audio_null.cpp` | `audio_direct.cpp` (child: Apple Mixer → SHM ring); the parent side is `audio_ipc_reader.cpp` → `audio_output.cpp` → `audio_encoder_thread.cpp` (Opus) |
| Serial | `serial/serial_adapter.cpp` | `serial_unix.cpp` (PTY / tty, from `--serial-a/-b`) | same |
| Ethernet | `ether/ether_adapter.cpp` | `ether_null.cpp` | `ether_socket.cpp` (Unix socket to net-bridge, `--network socket`) |
| Disk | none: `src/core/disk.cpp` calls `Sys_*` | — (`disk_null.cpp` is empty) | — |
| Platform (`Sys_*`, file and CD-ROM I/O, prefs) | `platform/platform_adapter.cpp` | `platform_unix.cpp` | same; `platform_null.cpp` exists but isn't installed |

Paths are under `src/drivers/` unless given.

CPU installers use the same table: `cpu_m68k_install()` (with
`cpu::platform_install()` for the memory and register entries),
`cpu_ppc_kpx_install()` and `cpu_ppc_mame_install()` fill the CPU and
memory fields on top of the driver defaults.

## Files

- `src/common/include/platform.h` — the `Platform` struct and the null and
  Unix driver declarations.
- `src/common/platform.cpp` — `platform_init()`.
- `src/drivers/CMakeLists.txt` — build wiring.
- `src/drivers/<subsystem>/<name>_adapter.cpp` — adapters.
- `src/drivers/<subsystem>/<name>_null.cpp` — no-op defaults.
- `src/drivers/null_drivers.cpp` — stubs for clipboard, prefs editor, user
  strings and parameter RAM hooks that need no driver.

## Why

- **The core stays Basilisk II's code**: it calls `SCSIInit()`,
  `VideoRefresh()`, `Sys_open()` as before.
- **Runtime selection**: the IPC child, headless mode and the PPC machine
  pick different drivers in one binary.
- **Always initialised**: no NULL checks on driver entries.
