# Hardware Drivers and Emulation

This document covers the emulated hardware subsystems (drivers) in macemu.

## Driver Architecture Pattern

All hardware drivers follow a common pattern:

```cpp
// Initialization (called at startup)
bool XxxInit(void);

// Cleanup (called at shutdown)
void XxxExit(void);

// Periodic update (called from main loop or timer)
void XxxInterrupt(void);
```

## Video Subsystem

### Core Interface (`video.cpp`, `video.h`)

The video system abstracts display rendering across multiple backends.

#### Key Structures

```cpp
// Video mode descriptor
struct video_mode {
    uint32 x;           // Width in pixels
    uint32 y;           // Height in pixels
    uint32 resolution_id;
    uint32 bytes_per_row;
    video_depth depth;  // VDEPTH_1BIT through VDEPTH_32BIT
};

// Monitor descriptor
struct monitor_desc {
    vector<video_mode> modes;
    video_mode current_mode;
    uint8 *frame_buffer;
};
```

#### Backend Implementations

| File | Backend | Notes |
|------|---------|-------|
| `SDL/video_sdl2.cpp` | SDL 2.x | Primary modern backend |
| `SDL/video_sdl3.cpp` | SDL 3.x | Newest backend |
| `Unix/video_x.cpp` | X11 | Legacy Unix |
| `Unix/video_headless.cpp` | Headless | For web streaming |
| `Windows/video_windows.cpp` | Win32/DirectX | Windows native |

#### Adding a New Video Backend

1. Implement required functions:
```cpp
bool VideoInit(void);           // Initialize display
void VideoExit(void);           // Cleanup
void VideoRefresh(void);        // Redraw screen
void VideoInterrupt(void);      // VBL handler
void video_set_palette(uint8 *pal, int num);
```

2. Handle mode switching via `video_switch_to_mode()`

3. Register with build system in `configure.ac`

## Audio Subsystem

### Core Interface (`audio.cpp`, `audio.h`)

#### Key Constants

```cpp
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS 2
#define AUDIO_BITS 16
```

#### Backend Implementations

| File | Backend | Notes |
|------|---------|-------|
| `SDL/audio_sdl.cpp` | SDL | Cross-platform |
| `Unix/audio_oss_esd.cpp` | OSS/ESD | Legacy Linux |
| `MacOSX/audio_macosx.cpp` | CoreAudio | macOS native |
| `Windows/audio_windows.cpp` | DirectSound | Windows native |

#### Audio Flow

```
MacOS Sound Manager → audio.cpp → Platform Backend → Host Audio
         ↓                              ↑
    Sound buffer                   Callback pulls
    writes here                    samples when ready
```

#### Implementing Audio Backend

```cpp
bool AudioInit(void);           // Open audio device
void AudioExit(void);           // Close audio device
void audio_callback(uint8 *buf, int len);  // Fill buffer

// Optional streaming support
bool audio_open_stream(int rate, int bits, int channels);
void audio_close_stream(void);
```

## Storage Subsystem

### Block Devices (`disk.cpp`, `cdrom.cpp`, `sony.cpp`)

All block devices implement a common interface:

```cpp
// Device operations
int16 DiskOpen(uint32 pb, uint32 dce);
int16 DiskPrime(uint32 pb, uint32 dce);   // Read/Write
int16 DiskControl(uint32 pb, uint32 dce);
int16 DiskStatus(uint32 pb, uint32 dce);
int16 DiskClose(uint32 pb, uint32 dce);
```

#### Disk Types

| Driver | Device | File Types |
|--------|--------|------------|
| `disk.cpp` | Hard disks | .img, .dsk, raw devices |
| `cdrom.cpp` | CD-ROM | .iso, .toast, .cue/.bin |
| `sony.cpp` | Floppy | .dsk, .img (400K/800K/1.4M) |

#### CD-ROM BIN/CUE Support

`bincue.cpp` provides parsing for .cue sheet files:
- Track layout parsing
- Audio track extraction
- Mixed-mode CD support

### SCSI Manager (`scsi.cpp`)

Emulates the Mac SCSI Manager for direct device access:

```cpp
int16 SCSICmd(int id, uint8 *cdb, int cdb_len,
              uint8 *data, int data_len, bool read);
```

### Extended Filesystem (`extfs.cpp`)

Maps host directories as Mac volumes (~69KB, largest driver):

```cpp
// Mount a host directory as Mac volume
void ExtFSMount(const char *path, const char *name);
```

Features:
- HFS-like directory structure
- Resource fork emulation via AppleDouble
- File type/creator mapping

## Input Subsystem

### ADB (Apple Desktop Bus) - `adb.cpp`

Handles keyboard and mouse input:

```cpp
void ADBKeyDown(int keycode);
void ADBKeyUp(int keycode);
void ADBMouseMoved(int dx, int dy);
void ADBMouseButton(bool down);

// ADB device polling
void ADBInterrupt(void);
```

#### Keycode Translation

Mac keycodes differ from host keycodes. Translation tables in:
- `Unix/main_unix.cpp` - X11 keysyms → Mac
- `SDL/video_sdl2.cpp` - SDL keycodes → Mac
- `Windows/main_windows.cpp` - VK codes → Mac

### Clipboard (`clip.cpp`)

Bidirectional clipboard sharing:

```cpp
void ClipInit(void);
void ClipExit(void);
void PutScrap(uint32 type, void *data, int len);
void GetScrap(uint32 type, void **data, int *len);
```

## Networking Subsystem

### Ethernet Driver (`ether.cpp`)

```cpp
bool EtherInit(void);
void EtherExit(void);
void EtherInterrupt(void);

// Packet I/O
int EtherReadPacket(uint8 *buf, int len);
int EtherWritePacket(uint8 *buf, int len);
```

### SLiRP User-Mode Networking (`slirp/`)

Full TCP/IP stack in userspace - no root privileges needed:

```
Mac Network Stack → ether.cpp → slirp/ → Host Sockets
                                  ↓
                            NAT translation
                            DHCP server
                            DNS forwarding
```

Key slirp files:
- `slirp.c` - Main interface
- `tcp_*.c` - TCP implementation
- `udp.c` - UDP implementation
- `bootp.c` - DHCP server

## Serial Ports (`serial.cpp`)

Emulates Mac serial ports (modem/printer):

```cpp
bool SerialOpen(int port);
void SerialClose(int port);
int SerialRead(int port, uint8 *buf, int len);
int SerialWrite(int port, uint8 *buf, int len);
```

Platform implementations connect to:
- Physical serial ports
- PTYs (Unix)
- Named pipes (Windows)

## Timer System (`timer.cpp`)

Provides timing services for MacOS Time Manager:

```cpp
void TimerInit(void);
void TimerExit(void);
void TimerInterrupt(void);

// Install/remove timer tasks
void InsertTimerTask(TMTask *task);
void RemoveTimerTask(TMTask *task);
```

### Platform Timing

| Platform | Mechanism |
|----------|-----------|
| Unix/Linux | `setitimer()` / `timer_create()` |
| macOS | `mach_absolute_time()` |
| Windows | `QueryPerformanceCounter()` |

## NVRAM/PRAM (`xpram.cpp`)

Persistent storage for Mac settings:

```cpp
void XPRAMInit(const char *filename);
void XPRAMExit(void);
uint8 XPRAM[256];  // 256 bytes of PRAM
```

Saved settings include:
- Display preferences
- Startup disk
- Mouse tracking speed
- Sound volume
- Serial port configuration

## Driver Summary Table

| Driver | File | Size | Complexity |
|--------|------|------|------------|
| Video | `video.cpp` | ~28KB | High |
| Audio | `audio.cpp` | ~25KB | Medium |
| Disk | `disk.cpp` | ~16KB | Medium |
| CD-ROM | `cdrom.cpp` | ~41KB | High |
| Floppy | `sony.cpp` | ~16KB | Medium |
| SCSI | `scsi.cpp` | ~7KB | Low |
| ExtFS | `extfs.cpp` | ~69KB | Very High |
| Ethernet | `ether.cpp` | ~14KB | Medium |
| ADB | `adb.cpp` | ~13KB | Medium |
| Serial | `serial.cpp` | ~7KB | Low |
| Timer | `timer.cpp` | ~16KB | Medium |
| XPRAM | `xpram.cpp` | ~3KB | Low |
