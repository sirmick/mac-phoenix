#pragma once
/*
 * executor_host.h - how mac-phoenix drives the Executor core in-process.
 *
 * Deliberately free of Executor headers: src/main.cpp includes this.
 * run() blocks on the calling thread (the front-end/render loop) and runs
 * the Toolbox on its own low-stack thread. The input functions are safe to
 * call from any thread once run() has started.
 */
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace executor_host {

struct Config
{
    int width = 640;
    int height = 480;
    int bpp = 8;
    int ram_mb = 64;

    // Executor's own files (System Folder, Configuration, cnid map).
    std::string data_dir;
    // HFS disk images to mount. Always read-only: they are shared with
    // the ROM-based backends.
    std::vector<std::string> disks;

    // Host folders the guest sees as volumes (besides data_dir, which holds
    // the System Folder). Never the host root.
    std::vector<std::string> shared_folders;

    bool logtraps = false;
    int logtraps_nesting = 1; // --logtraps-nesting: log calls nested this deep
    bool writable_images = false; // disk images read-write (default: read-only)
    std::string cpu = "uae";      // 68k core: "uae" or "musashi"

    // Application to start instead of Executor's Browser (host path).
    std::string app;

    // Rendered frames, 0xAARRGGBB per pixel (BGRA bytes on little-endian),
    // cursor included. Called on the thread that called run().
    std::function<void(const uint32_t *pixels, int width, int height)> on_frame;

    // Sound: one 20 ms frame of 48 kHz 16-bit big-endian stereo (960
    // frames), from the emulator thread. Returns false when the host cannot
    // take it now (it is kept and offered again). Unset: no sound output
    // (Executor only pretends to play).
    std::function<bool(const uint8_t *s16be, uint32_t samples)> on_audio;
};

// Runs Executor until the guest application exits. Returns its exit code.
int run(const Config& config);

// Input. Coordinates are guest screen pixels.
void mouse_moved(int x, int y);
void mouse_moved_relative(int dx, int dy);
void mouse_button(bool down);
void key(bool down, uint8_t mac_keycode);
void get_mouse(int& x, int& y);

// True once the application has reached its event loop.
bool app_idle();

// Name of the running Mac application (CurApName), empty before launch.
std::string current_app_name();

// Called on the emulator thread each time the app asks for events
// (GetNextEvent/WaitNextEvent): a safe point to inspect guest memory.
void set_event_poll_hook(std::function<void()> hook);

// Called first on a fatal Executor error, with its message, while guest
// memory is still as the failure left it.
void set_fatal_hook(std::function<void(const char *)> hook);

// The 68k registers as Executor last synced them (cpu_state).
void get_registers(uint32_t d[8], uint32_t a[8]);

// Guest RAM: guest addresses 0 .. guest_ram_size()-1, contiguous on the
// host from guest_ram() (the host address of guest address 0).
const uint8_t *guest_ram();
uint32_t guest_ram_size();

}  // namespace executor_host
