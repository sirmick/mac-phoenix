# Licensing

**MacPhoenix as a whole** — the `mac-phoenix` binary, the packages, this
repository taken together — is distributed under the **GNU GPL, version 2
or later** ([LICENSE](LICENSE)). It has to be: it contains GPL code from
UAE, Basilisk II and SheepShaver.

**Most of the code is permissive.** Everything written for MacPhoenix
(the web server, WebRTC, the browser UI, the encoders, configuration, the
CPU-core interface, net-bridge, BridgeAgent, provisioning, the tools and
tests) is also available under the **MIT licence** ([LICENSE-MIT](LICENSE-MIT)),
so it can be reused outside MacPhoenix without the GPL. The Executor core
is MIT (its own licence, and MacPhoenix's changes to it), and the MAME CPU
cores are BSD-3-Clause.

A file you take out of this repository carries the licence of the row it
falls under below, not the GPL of the whole.

## By path

### MIT: written for MacPhoenix ([LICENSE-MIT](LICENSE-MIT))

| Path | What |
|---|---|
| `src/webserver/`, `src/webrtc/` | HTTP/WebSocket server, API, WebRTC signaling and media |
| `src/config/`, `src/ipc/`, `src/main.cpp` | configuration, parent/child IPC, entry point |
| `src/drivers/` (except the GPL files below) | video/audio pipelines and encoders, MacBrowser host side, socket ethernet, timer, adapters |
| `src/core/`: `boot_progress`, `command_bridge`, `cpu_context`, `emulator_init`, `emulator_subprocess`, `executor_systems`, `guest_ui`, `mac_roman`, `network_info`, `snapshot`, `text_feed`, `atrap_dispatchers.inc` | boot tracking, the command bridge, `/api/ui` and `/api/text`, snapshots |
| `src/common/`: `crash_handler_init.cpp`, `guest/`, `guest_watch.cpp`, `platform.cpp`; in `include/`: `bridge_paths.h`, `config.h`, `crash_handler_init.h`, `guest_bridge.h`, `guest_watch.h`, `m68k_registers.h`, `MacBrowser.h`, `memory_access.h`, `mon.h`, `platform.h`, `ppc_boundary_trace.h`, `video_modes.h` | the Platform API and shared host code |
| `src/cpu/core/` (except `uae_core.cpp`), `src/cpu/cpu_m68k.*`, `src/cpu/cpu_trace.*`, `src/cpu/emulop_c_wrapper.cpp` | `cpu::Core`, `GuestMemory`, the 68k ROM machine on it |
| `src/cpu/mame/`: `mame_68k_core.cpp`, `mame_ppc_core.cpp`, `shim/`, `CMakeLists.txt` | MAME cores as `cpu::Core`s, the stand-in for MAME's framework |
| `client/` (code) | the browser UI |
| `net-bridge/` | the Rust NAT (its crates: smoltcp 0BSD; log, env_logger, libc MIT/Apache-2.0) |
| `BridgeAgent/`, `MacBrowser/` (sources) | the guest-side apps |
| `provisioning/`, `tools/`, `tests/`, `docs/`, `cmake/`, `packaging/`, `debian/`, `rpm/`, the `CMakeLists.txt` files | everything else |

### MIT: Executor ([src/executor/COPYING](src/executor/COPYING), [src/executor/LICENSE-CONTRIBUTIONS](src/executor/LICENSE-CONTRIBUTIONS))

`src/executor/` is Executor 2000 (© 1986–2004 Abacus Research & Development,
© 2018– Wolfgang Thaller), imported from
[autc04/executor](https://github.com/autc04/executor); see
[docs/executor/README.md](docs/executor/README.md). MacPhoenix's changes to it
are MIT too, so they can go back upstream.

### BSD and other permissive: third-party code

| Path | Licence |
|---|---|
| `src/cpu/mame/m68000/`, `powerpc/`, `cpu/`, `emu/`, `third_party/util/` | BSD-3-Clause (MAME; [LICENSE-MAME](src/cpu/mame/LICENSE-MAME), each file's header) |
| `src/cpu/mame/third_party/softfloat3/` | BSD-3-Clause (Berkeley SoftFloat 3e) |
| `src/cpu/mame/third_party/asmjit/` | zlib ([LICENSE-asmjit](src/cpu/mame/LICENSE-asmjit)) |
| `subprojects/libdatachannel/` | MPL-2.0; its `deps/`: libjuice MPL-2.0, libsrtp and usrsctp BSD-3-Clause, plog and nlohmann/json MIT |
| `src/executor/third_party/googletest/` | BSD-3-Clause |
| `tools/macdecode/dcmp0_ext.py` | MIT (adapted from rsrcfork) |

### GPL-2.0-or-later: UAE, Basilisk II, SheepShaver and code adapted from them

| Path | Origin |
|---|---|
| `src/cpu/uae_cpu/` | UAE 68k core (Bernd Schmidt and contributors) |
| `src/cpu/uae_wrapper.*`, `src/cpu/core/uae_core.cpp` | glue to it |
| `src/cpu/kpx/` (all) | SheepShaver: the Kheperix PowerPC core and the PowerPC Mac machine (ROM and resource patches, name registry, thunks, video, NativeOps), with MacPhoenix's adaptations (`cpu_ppc_kpx.cpp`, `cpu_ppc_mame.cpp`, `host_ppc.cpp`, `compat/`) |
| `src/core/`: `adb`, `audio`, `cdrom`, `disk`, `emul_op`, `ether`, `extfs`, `macos_util`, `main`, `rom_patches`, `rsrc_patches`, `scsi`, `serial`, `slot_rom`, `sony`, `timer`, `timer_unix`, `user_strings`, `video`, `xpram` | Basilisk II's 68k Mac machine (Christian Bauer and contributors) |
| `src/common/sigsegv.cpp`, the rest of `src/common/include/` (the files with Christian Bauer's header, `crash_handler.h` among them) | Basilisk II |
| `src/drivers/platform/platform_unix.cpp`, `disk_unix.h`, `src/drivers/null_drivers.cpp`, `serial/serial_unix.cpp`, `video/video_null.cpp` | Basilisk II's Unix drivers, adapted |
| `ldscripts/` | Basilisk II / SheepShaver |

The ROM machines (`--backend uae`, `--backend kpx`) depend on this code; the
Executor backend on the MAME cores doesn't, apart from shared type headers
(`sysdeps.h`) and the binary they are linked into.

## Assets and built files

* `tools/icons/*.png`: GNOME HighContrast theme, LGPL-2.1-or-later.
* `client/*.svg` logos (Apple, Motorola, PowerPC): from Wikimedia Commons;
  trademarks of their owners.
* Committed guest binaries (`BridgeAgent/*.bin`, `MacBrowser/*.bin`/`.dsk`,
  `tests/guest/**/*.bin`) are built from MIT sources but contain runtime code
  from the toolchain that built them (Retro68; Apple's MPW libraries;
  Metrowerks' MSL), under that toolchain's terms.
* ROMs, system software and disk images are not part of MacPhoenix and are
  not distributed.

## How this was worked out, and keeping it right

Each file's history (`git log --follow`) and header were checked: files
whose every commit is by the MacPhoenix author (Mick, `sirmick@gmail.com`,
`mick@dev.home.arpa`, `mick@macphoenix.dev`) and whose header doesn't name
another source are MIT; files with Basilisk II/SheepShaver/UAE history or
headers ("adapted from", "lifted from", Christian Bauer's notice) are GPL.

New code: write it from documentation and observed behaviour, not by
adapting GPL source, and it is MIT under this file. Code adapted from a GPL
file keeps that file's GPL header and goes in the GPL table.
