#pragma once
/*
 * session.h - a front end's side of an Executor session, above the
 * executor_host contract (frontends/phoenix/executor_host.h): what the tab
 * (frontends/wasm) and a native desktop front end both need, without their
 * transport (the tab's is emscripten's JS glue).
 *
 *   parse_args     the command line: screen, disks, the System image, RAM...
 *   prepare        before executor_host::run(): the System Folder (one saved
 *                  in Executor's volume, else the System image's copied in),
 *                  the memory, the disks' System Folder additions
 *   event_call_begin/_end  at an event call of the running application
 *                  (executor_host::set_event_poll_hook), before and after
 *                  the front end's requests: additions waiting; disk
 *                  images ejected
 *   request        automation: an op and its argument, the reply as JSON
 *   rects_rgba     frames as RGBA, the cursor drawn in
 *
 * Everything but parse_args and prepare runs on the emulator thread, at an
 * event call (request, event_call_*) or from the video driver's hooks.
 */
#include "executor_host.h"
#include "phoenix.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace executor_host::session {

/* The command line (the tab's: web_main [--size WxH] [--disk IMAGE]...
   [--additions IMAGE]... [--system IMAGE] [--ppc] [--ram MB] [--depth BPP]
   [APP]; frontends/wasm/web_main.cpp). */
struct Options
{
    Config config;                      // screen, disks, CDs, app, debug options
    std::string system_image;           // --system: the System Folder to run on
    std::vector<std::string> additions; // --additions: disks whose additions go in
    int ram_mb = 0;                     // --ram (0: as prepare decides)
};
Options parse_args(int argc, char **argv);

struct Setup
{
    std::string data_dir;     // Executor's volume (its System Folder is the one it runs on)
    std::string images;       // where disk images put in later are (a path prefix): watched for ejects
    std::string log_prefix;   // the front end's log lines' ("[web]")
    int ram_mb = 32;          // on Executor's own System
    int apple_ram_mb = 128;   // on Apple's
};

struct Hooks
{
    // A disk image whose volumes are all gone (ejected, put away)
    std::function<void(const std::string &path)> ejected;
    // A disk's System Folder additions copied in: {copied, kept, live, restart}
    std::function<void(const std::string &path, const std::string &json)> additions;
};

/* Before executor_host::run(): o.config's data_dir, memory, System Folder
   and application (the Finder on Apple's System) from o and setup;
   additions copied in now or at the first event call. */
void prepare(Options &o, const Setup &setup, const Hooks &hooks);

/* At an event call of the running application, before the front end's
   requests (the additions waiting) and after them (disk images whose
   volumes went: Hooks::ejected). */
void event_call_begin();
void event_call_end();

/* An automation request at an event call: app, windows [items], menus,
   mouse, drawn, launch PATH, volumes, mount PATH, mount-additions PATH,
   mount-cd PATH, eject PATH, flush, touch VOL:NAME, resize WxH, scale N. The reply as JSON;
   nothing for an op it doesn't know. */
std::optional<std::string> request(const std::string &op, const std::string &arg);

/* A Mac string (MacRoman) as a JSON string. */
std::string json_string(const std::string &mac);

/* Video: the changed rectangles of an 0xAARRGGBB frame as RGBA bytes, one
   after another, row by row, the cursor drawn into those it is in; RECTS
   gets top, left, bottom, right of each. */
void rects_rgba(const uint32_t *argb, int width, const std::vector<PhoenixVideoDriver::Rect> &rects,
                std::vector<uint32_t> &rgba, std::vector<int32_t> &rect_list);
/* The 1x frame as last rendered, as RGBA bytes. False: none yet. */
bool frame_rgba(std::vector<uint32_t> &rgba, int &width, int &height);

}  // namespace executor_host::session
