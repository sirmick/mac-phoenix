# Platform adapter pattern

Every driver subsystem (video, audio, scsi, serial, ether, disk,
platform) follows the same shape: a small adapter that defers to a
function pointer in `g_platform`, a null driver that fills the pointer
with safe defaults at startup, and a real driver that overrides those
pointers when the system actually wants the feature.

## Pattern

```cpp
/* core code calls the adapter — never the backend */
void SCSIInit(void) { g_platform.scsi_init(); }

/* null driver — installed at startup, never crashes */
void scsi_null_init(void) { /* no-op */ }

/* platform_init() wires every g_platform.* to its null variant */
void platform_init(void) {
    g_platform.scsi_init = scsi_null_init;
    /* … */
}

/* a real driver swaps in its own functions when initialised */
void cpu_uae_install(Platform *p) {
    p->cpu_execute_one = uae_execute_one;
    /* … */
}
```

Core code calls `g_platform.thing()` directly — there are no
`if (g_platform.thing)` checks anywhere because the null defaults make
the pointer always safe.

## Subsystems

| Subsystem | Adapter | Null driver | Real drivers |
|-----------|---------|-------------|--------------|
| SCSI | `scsi_adapter.cpp` | `scsi_null.cpp` | — |
| Video | `video_adapter.cpp` | `video_null.cpp` | `video_webrtc.cpp` |
| Audio | `audio_adapter.cpp` | `audio_null.cpp` | `audio_webrtc.cpp` (encoder thread infra; no Sound Manager hookup yet) |
| Serial | `serial_adapter.cpp` | — | `serial_unix.cpp` (PTY/tty backend) |
| Ethernet | `ether_adapter.cpp` | `ether_null.cpp` | `ether_socket.cpp` (Unix socket → net-bridge) |
| Disk | (uses `Sys_*` layer in core) | — | `platform_unix.cpp` |
| Platform | `platform_adapter.cpp` | `platform_null.cpp` | `platform_unix.cpp` |

CPU backends use the same scheme — `cpu_uae_install`,
`cpu_unicorn_install`, `cpu_unicorn_ppc_install`, `cpu_ppc_kpx_install`,
`cpu_dualcpu_install` each fill in CPU-related fields on top of the
null defaults.

## Files

- `src/common/include/platform.h` — the `Platform` struct, ~100+
  function pointers covering CPU + every driver subsystem.
- `src/common/platform.cpp` — `platform_init()` wires null defaults.
- `src/drivers/CMakeLists.txt` — build wiring.
- `src/drivers/<subsystem>/<name>_adapter.cpp` — adapter calls.
- `src/drivers/<subsystem>/<name>_null.cpp` — safe defaults.
- Real driver files swap in via their `_install(Platform *p)` entry
  points from `init_m68k` / `init_ppc` / `webserver_main` / etc.

## Why

- **Zero changes to the core** when adding a backend or a new driver.
- **Runtime selection** of every subsystem.
- **Always initialised** — no NULL-check noise in callers.
- **Trivially mockable** for tests.
