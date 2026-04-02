# Key Files Quick Reference

Quick lookup for the most important files when working on specific tasks.

## By Task

### "I want to understand how the emulator starts up"
- `BasiliskII/src/Unix/main_unix.cpp` - Unix entry point
- `BasiliskII/src/main.cpp` - Cross-platform initialization (`InitAll()`)

### "I want to modify video/display handling"
- `BasiliskII/src/video.cpp` - Core video logic
- `BasiliskII/src/SDL/video_sdl2.cpp` - SDL2 backend (most common)
- `BasiliskII/src/include/video.h` - Video structures and modes
- `BasiliskII/src/CrossPlatform/video_blit.cpp` - Pixel format conversion

### "I want to modify audio handling"
- `BasiliskII/src/audio.cpp` - Core audio logic
- `BasiliskII/src/SDL/audio_sdl.cpp` - SDL audio backend
- `BasiliskII/src/include/audio.h` - Audio definitions

### "I want to understand CPU emulation"
- `BasiliskII/src/include/cpu_emulation.h` - CPU interface
- `BasiliskII/src/uae_cpu/newcpu.cpp` - 68k interpreter main loop
- `BasiliskII/src/uae_cpu/newcpu.h` - CPU state structure
- `BasiliskII/src/uae_cpu/compiler/compemu.cpp` - JIT compiler (x86)

### "I want to add/modify disk support"
- `BasiliskII/src/disk.cpp` - Hard disk driver
- `BasiliskII/src/cdrom.cpp` - CD-ROM driver
- `BasiliskII/src/sony.cpp` - Floppy driver
- `BasiliskII/src/bincue.cpp` - BIN/CUE CD image parsing
- `BasiliskII/src/extfs.cpp` - Host filesystem mounting

### "I want to modify networking"
- `BasiliskII/src/ether.cpp` - Ethernet driver interface
- `BasiliskII/src/Unix/ether_unix.cpp` - Unix TAP/TUN
- `BasiliskII/src/slirp/slirp.c` - Userspace networking entry

### "I want to modify input handling"
- `BasiliskII/src/adb.cpp` - Keyboard/mouse (ADB) logic
- `BasiliskII/src/SDL/video_sdl2.cpp` - SDL input events (in video file)

### "I want to understand ROM patching"
- `BasiliskII/src/rom_patches.cpp` - ROM modification logic
- `BasiliskII/src/rsrc_patches.cpp` - Resource fork patches
- `BasiliskII/src/emul_op.cpp` - Emulator opcode handlers
- `BasiliskII/src/include/emul_op.h` - Opcode definitions

### "I want to add a new platform"
- `BasiliskII/src/Unix/sysdeps.h` - Type definitions template
- `BasiliskII/src/include/sysdeps.h` - Main sysdeps include
- `BasiliskII/src/CrossPlatform/` - Reusable utilities

### "I want to modify preferences/configuration"
- `BasiliskII/src/prefs.cpp` - Preferences handling
- `BasiliskII/src/include/prefs.h` - Preference definitions
- `BasiliskII/src/Unix/prefs_editor_gtk.cpp` - GTK preferences UI

### "I want to work on web streaming"
- `web-streaming/server/datachannel_webrtc.cpp` - WebRTC server + VP8 encoder
- `web-streaming/server/datachannel_webrtc.h` - C API header
- `BasiliskII/src/SDL/video_headless.cpp` - Headless video driver
- See [WEBRTC_STREAMING.md](WEBRTC_STREAMING.md) for full documentation

## File Size Reference (Complexity Indicator)

| File | Lines | Complexity |
|------|-------|------------|
| `extfs.cpp` | ~2200 | Very High |
| `rom_patches.cpp` | ~1500 | High |
| `cdrom.cpp` | ~1200 | High |
| `video.cpp` | ~900 | Medium-High |
| `audio.cpp` | ~750 | Medium |
| `ether.cpp` | ~500 | Medium |
| `disk.cpp` | ~500 | Medium |
| `adb.cpp` | ~400 | Medium |
| `timer.cpp` | ~450 | Medium |
| `serial.cpp` | ~250 | Low |
| `scsi.cpp` | ~250 | Low |
| `xpram.cpp` | ~100 | Very Low |

## Header Files Summary

All in `BasiliskII/src/include/`:

| Header | Defines |
|--------|---------|
| `sysdeps.h` | Platform types, endianness, macros |
| `cpu_emulation.h` | Memory access, CPU control |
| `video.h` | video_mode, monitor_desc, depth enums |
| `audio.h` | Audio constants, buffer interface |
| `ether.h` | Ethernet packet structures |
| `macos_util.h` | Mac data type conversions |
| `emul_op.h` | M68K_EMUL_OP_* opcode definitions |
| `prefs.h` | Preference key names |
| `user_strings.h` | Localized string IDs |

## Generated Files (Do Not Edit)

These are auto-generated during build:

```
uae_cpu/cpuemu.cpp      # Generated interpreter dispatch (~1MB)
uae_cpu/cpustbl.cpp     # Generated CPU tables
uae_cpu/cputbl.h        # Generated CPU headers
uae_cpu/compiler/compemu.cpp  # Generated JIT compiler (~2MB)
uae_cpu/compiler/comptbl.h    # Generated compiler tables
```

Regenerate with: `make cpuemu.cpp` or `./build68k`

## Configuration Files

| File | Purpose |
|------|---------|
| `Unix/configure.ac` | Autoconf build configuration |
| `Unix/Makefile.in` | Makefile template |
| `Unix/.basilisk_ii_prefs` | Runtime preferences (user home) |
| `Windows/BasiliskII.sln` | Visual Studio solution |
| `MacOSX/BasiliskII.xcodeproj` | Xcode project |

## Debugging Entry Points

| Scenario | Look Here |
|----------|-----------|
| Crash at startup | `main_unix.cpp:main()` → `InitAll()` |
| Display issues | `VideoInit()` in active video backend |
| No sound | `AudioInit()` in active audio backend |
| Network not working | `EtherInit()` → `slirp_init()` |
| ROM not loading | `main.cpp:InitAll()` → ROM loading section |
| Disk not mounting | `disk.cpp:DiskOpen()` |
| Keyboard not working | `adb.cpp:ADBKeyDown()` |
| Emulation too slow | Check if JIT enabled (`--enable-jit-compiler`) |

## Common Patterns in Code

### Error Handling
```cpp
// Most init functions return bool
if (!VideoInit()) {
    ErrorAlert("Video initialization failed");
    return false;
}
```

### Big-Endian Conversion
```cpp
// Mac is big-endian, host may not be
uint32 mac_long = ReadMacInt32(addr);  // Handles swap
WriteMacInt32(addr, value);            // Handles swap
```

### Interrupt Signaling
```cpp
// From worker thread
SetInterruptFlag(INTFLAG_AUDIO);
TriggerInterrupt();
// Main loop will call AudioInterrupt()
```

### Preferences Access
```cpp
// Read preference
const char *rom = PrefsFindString("rom");
int ramsize = PrefsFindInt32("ramsize");

// Set preference
PrefsReplaceString("rom", "/path/to/rom");
PrefsReplaceBool("nogui", true);
```
