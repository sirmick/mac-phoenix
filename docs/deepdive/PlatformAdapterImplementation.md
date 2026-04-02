# Platform Adapter Implementation ✅ COMPLETE

## Overview

All driver subsystems use the adapter pattern: core code calls adapter functions which defer to `g_platform` function pointers. Null drivers provide safe defaults. Real drivers override at startup.

## Completed Adapters

| Subsystem | Adapter | Null Driver | Real Drivers |
|-----------|---------|-------------|--------------|
| SCSI | `scsi_adapter.cpp` | `scsi_null.cpp` | — |
| Video | `video_adapter.cpp` | `video_null.cpp` | `video_webrtc.cpp` |
| Audio | `audio_adapter.cpp` | `audio_null.cpp` | `audio_webrtc.cpp` |
| Serial | `serial_adapter.cpp` | `serial_null.cpp` | — |
| Ethernet | `ether_adapter.cpp` | `ether_null.cpp` | `ether_lwip.cpp`, `ether_raw.cpp` |
| Disk | — (uses Sys_* layer) | `disk_null.cpp` | `platform_unix.cpp` |
| Platform | `platform_adapter.cpp` | `platform_null.cpp` | `platform_unix.cpp` |

## Infrastructure

- **`src/common/include/platform.h`** — `Platform` struct with 100+ function pointers covering all subsystems plus CPU backends
- **`src/common/platform.cpp`** — `platform_init()` wires all pointers to null drivers at startup
- **`src/drivers/CMakeLists.txt`** — All adapters, null drivers, and real drivers compiled

## CPU Backend Support

The platform header declares backend installers that override CPU-related function pointers:
- `cpu_uae_install(Platform *p)` — M68K UAE interpreter
- `cpu_unicorn_install(Platform *p)` — M68K Unicorn JIT
- `cpu_dualcpu_install(Platform *p)` — M68K lockstep validation
- `cpu_ppc_kpx_install(Platform *p)` — PPC KPX interpreter/JIT

## Pattern

```cpp
// Adapter: core code calls this
void SCSIInit(void) { g_platform.scsi_init(); }

// Null driver: safe default
void scsi_null_init(void) { /* no-op */ }

// platform_init(): wires defaults
void platform_init(void) { g_platform.scsi_init = scsi_null_init; ... }

// Backend installer: overrides specific pointers
void cpu_uae_install(Platform *p) { p->cpu_execute_one = uae_execute_one; ... }
```

## Design Benefits

1. Zero changes to core BasiliskII code
2. Runtime driver selection via function pointers
3. Always initialized to safe null drivers (no NULL checks needed)
4. Easy for tests to inject custom implementations
