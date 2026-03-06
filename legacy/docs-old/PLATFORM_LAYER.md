# Platform Abstraction Layer

This document explains how macemu abstracts platform-specific code to support multiple operating systems.

## Platform Directory Structure

```
BasiliskII/src/
├── Unix/           # Linux, macOS, FreeBSD, Solaris, IRIX
├── Windows/        # Windows NT/2000/XP+
├── MacOSX/         # macOS-specific (Cocoa, Xcode project)
├── BeOS/           # BeOS/Haiku
├── AmigaOS/        # AmigaOS 3.x/4.x
├── SDL/            # Cross-platform SDL layer
└── CrossPlatform/  # Shared utilities
```

## Cross-Platform Utilities

Located in `BasiliskII/src/CrossPlatform/`:

### Signal Handling (`sigsegv.cpp`)

Portable SIGSEGV handler for memory protection tricks:

```cpp
// Install handler
bool sigsegv_install_handler(sigsegv_handler_t handler);

// Handler signature
sigsegv_return_t handler(sigsegv_info_t *info);

// Get fault address
void *sigsegv_get_fault_address(sigsegv_info_t *info);
```

Used for:
- VOSF (video dirty page tracking)
- JIT memory access validation
- Direct memory addressing mode

### Virtual Memory (`vm_alloc.cpp`)

Portable memory allocation with protection control:

```cpp
// Allocate with hint address
void *vm_acquire(size_t size, int options);
void *vm_acquire_fixed(void *addr, size_t size, int options);

// Release memory
void vm_release(void *addr, size_t size);

// Change protection
int vm_protect(void *addr, size_t size, int prot);

// Protection flags
#define VM_PAGE_READ     1
#define VM_PAGE_WRITE    2
#define VM_PAGE_EXECUTE  4
```

### Video Blitting (`video_blit.cpp`)

Optimized pixel format conversion:

```cpp
// Blit Mac framebuffer to host display
void Screen_blit(uint8 *dest, const uint8 *src,
                 int dest_pitch, int src_pitch,
                 int width, int height, int depth);
```

Handles conversions:
- 1/2/4/8-bit indexed → 16/24/32-bit
- Big-endian → little-endian
- Different RGB channel orderings

## Unix Platform Layer

### Main Entry Point (`Unix/main_unix.cpp`)

```cpp
int main(int argc, char **argv) {
    // Parse command line
    // Initialize preferences
    PrefsInit(argc, argv);

    // Initialize all subsystems
    if (!InitAll())
        QuitEmulator();

    // Enter emulation loop
    emul_thread();

    // Cleanup
    ExitAll();
}
```

### System Dependencies (`Unix/sysdeps.h`)

Platform detection and type definitions:

```cpp
// Architecture detection
#if defined(__i386__) || defined(__x86_64__)
#define CPU_X86  1
#endif

// Endianness
#ifdef WORDS_BIGENDIAN
#define BE_WORD(x) (x)
#else
#define BE_WORD(x) bswap_16(x)
#endif

// Type aliases
typedef uint32_t uint32;
typedef int32_t int32;
```

### Unix-Specific Drivers

| File | Purpose |
|------|---------|
| `video_x.cpp` | X11 display backend |
| `audio_oss_esd.cpp` | OSS/ESD audio |
| `ether_unix.cpp` | TAP/TUN networking |
| `serial_unix.cpp` | Serial port via TTY |
| `sys_unix.cpp` | File/device I/O |
| `timer_unix.cpp` | POSIX timers |
| `prefs_editor_gtk.cpp` | GTK preferences GUI |

### Unix Configuration (`Unix/configure.ac`)

Autoconf-based feature detection:

```bash
# Key configure tests
AC_CHECK_LIB([SDL2], [SDL_Init])
AC_CHECK_HEADERS([linux/if_tun.h])
AC_CHECK_FUNCS([mmap posix_memalign])
```

## SDL Abstraction Layer

SDL provides a unified API across platforms:

### SDL Video (`SDL/video_sdl2.cpp`)

```cpp
// Initialize SDL video
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;

bool VideoInit() {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("BasiliskII", ...);
    renderer = SDL_CreateRenderer(window, -1, ...);
    texture = SDL_CreateTexture(renderer, ...);
}

void VideoRefresh() {
    SDL_UpdateTexture(texture, NULL, framebuffer, pitch);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}
```

### SDL Audio (`SDL/audio_sdl.cpp`)

```cpp
void audio_callback(void *userdata, Uint8 *stream, int len) {
    // Pull audio samples from Mac sound buffer
    AudioGetBuffer(stream, len);
}

bool AudioInit() {
    SDL_AudioSpec spec = {
        .freq = 44100,
        .format = AUDIO_S16,
        .channels = 2,
        .samples = 1024,
        .callback = audio_callback
    };
    SDL_OpenAudio(&spec, NULL);
    SDL_PauseAudio(0);
}
```

### SDL Input Handling

```cpp
void handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            ADBKeyDown(sdl_to_mac_keycode(event.key.keysym.sym));
            break;
        case SDL_MOUSEMOTION:
            ADBMouseMoved(event.motion.xrel, event.motion.yrel);
            break;
        case SDL_MOUSEBUTTONDOWN:
            ADBMouseButton(true);
            break;
        }
    }
}
```

## Windows Platform Layer

### Windows-Specific Files (`Windows/`)

| File | Purpose |
|------|---------|
| `main_windows.cpp` | WinMain entry point |
| `video_windows.cpp` | DirectDraw/GDI display |
| `audio_windows.cpp` | DirectSound audio |
| `ether_windows.cpp` | WinPcap/Npcap networking |
| `serial_windows.cpp` | COM port access |
| `sys_windows.cpp` | Win32 file I/O |
| `timer_windows.cpp` | Multimedia timers |

### Build System

Visual Studio solution: `Windows/BasiliskII.sln`

## macOS Platform Layer

### Cocoa Integration (`MacOSX/`)

- Xcode project: `BasiliskII.xcodeproj`
- Uses Cocoa for preferences UI
- CoreAudio for sound
- Native full-screen support

## Web Streaming Platform

### Architecture (`web-streaming/`)

Enables headless operation with browser-based display via WebRTC:

```
+-------------+                        +-------------+
| BasiliskII  |     WebRTC/DTLS       |   Browser   |
| (headless)  | <-------------------> |   Client    |
+-------------+                        +-------------+
       |                                      |
       v                                      v
  video_headless.cpp                   <video> element
  datachannel_webrtc.cpp               datachannel_client.js
       |
       v
  +------------------+
  | VP8 Encoder      | (libvpx)
  | WebRTC Transport | (libdatachannel)
  | HTTP Server      | (port 8000)
  | Signaling        | (port 8090)
  +------------------+
```

### Server Components (`web-streaming/server/`)

```cpp
// Initialize streaming (HTTP on 8000, signaling on port)
bool dc_webrtc_init(int signaling_port);

// Push video frame (RGBA format, encoded to VP8)
void dc_webrtc_push_frame(const uint8_t* rgba, int w, int h, int stride);

// Set input callbacks (mouse/keyboard via DataChannel)
void dc_webrtc_set_input_callbacks(mouse_cb, button_cb, key_cb);
```

### Client Components

The web client is embedded directly in the binary:
- HTML/JS served from built-in HTTP server on port 8000
- WebSocket signaling for WebRTC negotiation
- DataChannel for low-latency input

## Porting to a New Platform

### Required Implementations

1. **Entry point** - `main_xxx.cpp` with platform init
2. **System dependencies** - `sysdeps.h` for types/endianness
3. **Video backend** - Display and input handling
4. **Audio backend** - Sound output
5. **Timer** - High-resolution timing
6. **File I/O** - `sys_xxx.cpp` for disk/CD access

### Optional Implementations

- Ethernet (can use built-in SLiRP)
- Serial ports
- Preferences GUI
- Clipboard sharing

### Minimum Viable Port

A minimal port needs only:
1. Memory allocation (malloc or vm_alloc)
2. Video output (can start with SDL)
3. Timer (for emulation speed control)
4. File I/O (for ROM and disk images)

SDL handles most of this, making it the easiest starting point.

## Conditional Compilation

Platform selection via preprocessor:

```cpp
#ifdef __linux__
    // Linux-specific code
#elif defined(__APPLE__) && defined(__MACH__)
    // macOS-specific code
#elif defined(_WIN32)
    // Windows-specific code
#elif defined(__FreeBSD__)
    // FreeBSD-specific code
#endif
```

Build system sets defines:
- `HAVE_SDL` - SDL available
- `ENABLE_GTK` - GTK available
- `ENABLE_XF86_DGA` - XFree86 DGA extension
- `ENABLE_VOSF` - Video-on-SEGV enabled
