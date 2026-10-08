#include "executor_host.h"
#include "phoenix.h"

#include <base/common.h>
#include <mman/mman_private.h>
#include <hfs/hfs.h>
#include <file/file.h>
#include <vdriver/vdriver.h>
#include <rsys/toolevent.h>
#include <error/error.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>

int executor_main_entry(int argc, char **argv);

using namespace Executor;

namespace {
std::atomic<int> screenW{640}, screenH{480};
std::atomic<bool> running{false};
std::mutex inputMutex;
bool buttonDown = false;

IEventListener *sink()
{
    return running ? EventSink::instance.get() : nullptr;
}
}

namespace executor_host {

int run(const Config& c)
{
    namespace fs = std::filesystem;

    // Executor's folder settings (fileMisc.cpp InitPaths). Set in-process:
    // setenv() would race getenv() on the host's IPC thread.
    auto& paths = ROMlib_path_overrides;
    paths.clear();
    if(!c.data_dir.empty())
    {
        fs::path d = c.data_dir;
        fs::create_directories(d);
        paths["SystemFolder"] = (d / "System Folder").string();
        paths["Configuration"] = (d / "Configuration").string();
        paths["ExecutorDirectoryMap"] = (d / "cnidmap").string();
        paths["OffsetFile"] = (d / "offset_file").string();
        paths["PrintersIni"] = (d / "printers.ini").string();
        paths["PrintDef"] = (d / "printdef.ini").string();
    }
    std::string vols;
    for(const auto& disk : c.disks)
    {
        if(!vols.empty())
            vols += ';';
        vols += disk;
    }
    paths["MacVolumes"] = vols;
    ROMlib_readonly_images = true;
    ROMlib_heap_death_dialog = false;

    screenW = c.width;
    screenH = c.height;
    PhoenixVideoDriver::cursorX = c.width / 2;
    PhoenixVideoDriver::cursorY = c.height / 2;
    PhoenixVideoDriver::frameHook = c.on_frame;

    // Executor parses its own command line; build one from the config.
    // System heap: 3MB, stack: 256KB, the rest is the application heap.
    int applMB = std::max(4, c.ram_mb - 4);
    std::vector<std::string> args = {
        "mac-phoenix",
        "--size", std::to_string(c.width) + "x" + std::to_string(c.height),
        "--bpp", std::to_string(c.bpp),
        "--applzone", std::to_string(applMB) + "M",
    };
    if(c.logtraps)
        args.push_back("--logtraps");
    if(!c.app.empty())
        args.push_back(c.app);

    std::vector<char *> argv;
    for(auto& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    running = true;
    int rc = executor_main_entry((int)args.size(), argv.data());
    running = false;
    return rc;
}

void mouse_moved(int x, int y)
{
    x = std::clamp(x, 0, screenW.load() - 1);
    y = std::clamp(y, 0, screenH.load() - 1);
    PhoenixVideoDriver::cursorX = x;
    PhoenixVideoDriver::cursorY = y;
    if(auto *s = sink())
        s->mouseMoved(x, y);
}

void mouse_moved_relative(int dx, int dy)
{
    mouse_moved(PhoenixVideoDriver::cursorX + dx, PhoenixVideoDriver::cursorY + dy);
}

void mouse_button(bool down)
{
    std::lock_guard lk(inputMutex);
    if(down == buttonDown)
        return;
    buttonDown = down;
    if(auto *s = sink())
        s->mouseButtonEvent(down, PhoenixVideoDriver::cursorX, PhoenixVideoDriver::cursorY);
}

void key(bool down, uint8_t mac_keycode)
{
    if(auto *s = sink())
        s->keyboardEvent(down, mac_keycode);
}

void get_mouse(int& x, int& y)
{
    x = PhoenixVideoDriver::cursorX;
    y = PhoenixVideoDriver::cursorY;
}

bool app_idle()
{
    return running && ROMlib_app_polled_events.load(std::memory_order_relaxed);
}

std::string current_app_name()
{
    if(!running)
        return {};
    // CurApName ($910): Pascal string, max 31 chars. Identity addressing.
    const uint8_t *p = (const uint8_t *)(uintptr_t)0x910;
    int n = std::min<int>(p[0], 31);
    std::string name;
    for(int i = 0; i < n; i++)
    {
        uint8_t ch = p[1 + i];
        name += (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    return name;
}

static std::function<void()> poll_hook;

static void call_poll_hook()
{
    if(poll_hook)
        poll_hook();
}

void get_registers(uint32_t d[8], uint32_t a[8])
{
    for(int i = 0; i < 8; i++)
    {
        d[i] = EM_DREG(i);
        a[i] = EM_AREG(i);
    }
}

static std::function<void(const char *)> fatal_hook;

void set_fatal_hook(std::function<void(const char *)> hook)
{
    fatal_hook = std::move(hook);
    if(fatal_hook)
        ROMlib_fatal_hook = [](const char *m) { fatal_hook(m); };
    else
        ROMlib_fatal_hook = nullptr;
}

void set_event_poll_hook(std::function<void()> hook)
{
    poll_hook = std::move(hook);
    ROMlib_event_poll_hook = poll_hook ? call_poll_hook : nullptr;
}

uint32_t guest_ram_size()
{
    return (uint32_t)ROMlib_memtop;
}

}  // namespace executor_host
