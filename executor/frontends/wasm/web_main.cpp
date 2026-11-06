/*
 * web_main.cpp - Executor in a browser tab (docs/executor/DIRECTION.md, W2).
 *
 * The module runs in a Worker (executor/web/worker.js). main() starts
 * Executor through the same executor_host contract mac-phoenix uses, on
 * this thread: there is no other. Executor's idle waits and a tick about
 * once a frame yield to the browser (rsys/hostidle.h); on the way back the
 * yield hook takes the input the page queued (Module.input) and renders
 * what changed, which arrives in on_rects and goes to Module.onRects: the
 * changed rectangles only, as RGBA, for the page's canvas. The cursor is
 * never in them: the page draws it (Module.onCursor, a CSS cursor).
 *
 * Automation (W3): the page's requests (Module.requests: [id, op, arg]) are
 * answered at the next event call of the running application
 * (executor_host::set_event_poll_hook), a safe point in a process's world,
 * by executor/host (session.h request(): app, windows, menus, launch, mount,
 * eject, volumes, ...); here only what is the page's own: screen1x (the 1x
 * frame as pixels) and the test-fatal and test-trap of tab_stopped.mjs.
 * Disk images going away (Module.onEjected(path)) and disks' System Folder
 * additions copied in (Module.onAdditions(path, {copied, kept, restart,
 * live})) are host/'s hooks, told to the page.
 *
 * The Worker answers 'read' and 'write' itself: files in the module's
 * filesystem, such as the Host volume (/host).
 */
#include "build_id.h"
#include "executor_host.h"
#include "phoenix.h"
#include "session.h"
#include <rsys/hostidle.h>

#include <base/common.h>
#include <error/error.h>
#include <hfs/hfs.h>

#include <emscripten.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

/* The page's input, oldest first: [kind, a, b] per event (kind 1: mouse to
 * a, b; 2: button down a; 3: key down a, Mac key code b; 4: the wheel, a
 * lines right and b down). Returns how many events it copied. */
EM_JS(int, js_take_input, (int32_t *out, int max), {
    const q = Module.input || [];
    const n = Math.min(q.length, max);
    for (let i = 0; i < n; i++) {
        const e = q[i];
        HEAP32[(out >> 2) + 3 * i] = e[0];
        HEAP32[(out >> 2) + 3 * i + 1] = e[1];
        HEAP32[(out >> 2) + 3 * i + 2] = e[2];
    }
    q.splice(0, n);
    return n;
});

EM_JS_DEPS(web_main, "$stringToUTF8,$lengthBytesUTF8,$UTF8ToString");

/* The oldest request: its id, and "op\0arg" in buf. 0: none. */
EM_JS(int, js_take_request, (char *buf, int max), {
    const q = Module.requests || [];
    if (!q.length)
        return 0;
    const [id, op, arg] = q.shift();
    const n = lengthBytesUTF8(op);
    stringToUTF8(op, buf, max);                          /* NUL-terminated */
    stringToUTF8(arg || '', buf + n + 1, max - n - 1);
    return id;
});

/* The oldest 'screen1x' request, taken out of the queue: its id, 0: none. */
EM_JS(int, js_take_screen1x, (), {
    const q = Module.requests || [];
    const i = q.findIndex((r) => r[1] === 'screen1x');
    return i < 0 ? 0 : q.splice(i, 1)[0][0];
});

EM_JS(void, js_reply, (int id, const char *json), {
    if (Module.onReply)
        Module.onReply(id, JSON.parse(UTF8ToString(json)));
});

/* One 20 ms frame of 48 kHz 16-bit big-endian stereo. */
EM_JS(void, js_audio, (const uint8_t *s16be, int frames), {
    if (Module.onAudio)
        Module.onAudio(HEAPU8.subarray(s16be, s16be + frames * 4));
});

/* The image's size (the screen's at the display scale), the Mac's screen
 * (1x: what clicks are in), and n rectangles (top, left, bottom, right in
 * rects) whose RGBA pixels follow one another in pixels, row by row. */
EM_JS(void, js_rects, (int width, int height, int mac_width, int mac_height,
                       const int32_t *rects, int n, const uint8_t *pixels, int bytes), {
    if (Module.onRects)
        Module.onRects(width, height, HEAP32.subarray(rects >> 2, (rects >> 2) + 4 * n),
                       HEAPU8.subarray(pixels, pixels + bytes), mac_width, mac_height);
});

/* The module's build (build_id.h: git describe), for the page's log and
   status line. */
EM_JS(void, js_build, (const char *id), {
    if (Module.onBuild)
        Module.onBuild(UTF8ToString(id));
});

/* The cursor, for the page to draw (a CSS cursor; executor/web/cursor.js). */
EM_JS(void, js_cursor, (const uint8_t *data, const uint8_t *mask, int hotX, int hotY, int visible), {
    if (Module.onCursor)
        Module.onCursor(HEAPU8.slice(data, data + 32), HEAPU8.slice(mask, mask + 32), hotX, hotY, !!visible);
});

/* The 1x frame as RGBA bytes, the answer to request id ('screen1x'). */
EM_JS(void, js_reply_pixels, (int id, int width, int height, const uint8_t *rgba), {
    if (Module.onReply)
        Module.onReply(id, { width, height, data: HEAPU8.slice(rgba, rgba + width * height * 4) });
});

/* A disk image whose volumes are all gone (ejected). */
EM_JS(void, js_ejected, (const char *path), {
    if (Module.onEjected)
        Module.onEjected(UTF8ToString(path));
});

/* A fatal error of Executor's: the page says Executor stopped (its own
   alert is up in the guest meanwhile, waiting for Restart). */
EM_JS(void, js_additions, (const char *path, const char *json), {
    if (Module.onAdditions)
        Module.onAdditions(UTF8ToString(path), JSON.parse(UTF8ToString(json)));
});

EM_JS(void, js_fatal, (const char *message), {
    if (Module.onFatal)
        Module.onFatal(UTF8ToString(message));
});

/* Time the front end spent producing frames (rendering, converting). */
EM_JS(void, js_frame_time, (double ms), {
    if (Module.stats)
        Module.stats.hostMs += ms;
});

/* The emulator thread's load (hostidle::busy), each half second, and the
   Mac's RAM (in the module's heap): the page's CPU and memory meter. */
EM_JS(void, js_cpu, (double busy, double ram), {
    if (Module.onCpu)
        Module.onCpu(busy, ram);
});

namespace session = executor_host::session;

namespace {

using namespace Executor;

std::vector<uint32_t> rgba;
std::vector<int32_t> rect_list;

/* What changed, as RGBA, the cursor drawn into the rectangles it is in. */
void on_rects(const uint32_t *argb, int width, int height,
              const std::vector<PhoenixVideoDriver::Rect> &rects)
{
    session::rects_rgba(argb, width, rects, rgba, rect_list);
    js_rects(width, height, Executor::vdriver->width(), Executor::vdriver->height(),
             rect_list.data(), (int)rects.size(),
             (const uint8_t *)rgba.data(), (int)(rgba.size() * 4));
}

/* Executor's volume: its System Folder is the one it runs on. */
const char DATA_DIR[] = "/executor";

/* The answer to request id 'screen1x': the 1x frame as last rendered. */
void reply_screen1x(int id)
{
    std::vector<uint32_t> px;
    int w, h;
    if(session::frame_rgba(px, w, h))
        js_reply_pixels(id, w, h, (const uint8_t *)px.data());
    else
        js_reply(id, "null");
}

/* At an event call of the running application. */
void on_event_poll()
{
    char buf[1024];
    session::event_call_begin();
    while(int id = js_take_request(buf, sizeof buf))
    {
        std::string op = buf, arg = buf + op.size() + 1;
        std::string reply;
        if(op == "screen1x")
        {
            reply_screen1x(id);
            continue;
        }
        /* The page's side of Executor stopping (executor/tests/web/tab_stopped.mjs):
           a fatal error of its own, the wasm trapping (an access out of
           bounds: far past any memory it has grown to) */
        else if(op == "test-fatal")
            gui_fatal("asked for by the page (%s)", arg.c_str());
        else if(op == "test-trap")
        {
            volatile uint32_t *far = (volatile uint32_t *)(uintptr_t)0xFFFFFFF0u;
            reply = std::to_string(*far);
        }
        else if(auto r = session::request(op, arg))
            reply = *r;
        else
            reply = "{\"error\":" + session::json_string("unknown request " + op) + "}";
        js_reply(id, reply.c_str());
    }
    session::event_call_end();
}

void on_yield()
{
    /* the 1x frame at once, not at an event call: the page's Ctrl+Alt+S
       takes it while a menu is tracked or a button held (no event call) */
    while(int id = js_take_screen1x())
        reply_screen1x(id);
    int32_t ev[3 * 64];
    int n;
    while((n = js_take_input(ev, 64)) > 0)
        for(int i = 0; i < n; i++)
        {
            const int32_t *e = &ev[3 * i];
            switch(e[0])
            {
                case 1:
                    executor_host::mouse_moved(e[1], e[2]);
                    break;
                case 2:
                    executor_host::mouse_button(e[1] != 0);
                    break;
                case 3:
                    executor_host::key(e[1] != 0, (uint8_t)e[2]);
                    break;
                case 4:
                    executor_host::wheel(e[1], e[2]);
                    break;
            }
        }
    if(auto *v = dynamic_cast<PhoenixVideoDriver *>(Executor::vdriver.get()))
    {
        double t0 = emscripten_get_now();
        v->pump();
        js_frame_time(emscripten_get_now() - t0);
    }
    static double cpu_at;
    double now = emscripten_get_now();
    if(now - cpu_at >= 500)
    {
        cpu_at = now;
        double busy = Executor::hostidle::busy();
        if(busy >= 0)
            js_cpu(busy, executor_host::guest_ram_size());
    }
}

}

/* web_main [--size WxH] [--disk IMAGE]... [--additions IMAGE]... [--system IMAGE] [--ppc] [--ram MB]
 * [--depth BPP] [APP]: session.h parse_args (paths in the module's
 * filesystem). The page's settings (executor/web/settings.js) say which. */
int main(int argc, char **argv)
{
    session::Options o = session::parse_args(argc, argv);
    executor_host::Config &c = o.config;
    /* Disk images are writable (CDs, --cdrom, are not): the page keeps
       their changes in an overlay (executor/web/overlay.js); the images
       themselves are never written. */
    c.writable_images = true;
    /* Removable, as a floppy or a CD put in: the Finder puts them away
       without asking (the System's image stays a fixed disk). */
    Executor::ROMlib_ejectable_images = true;
    /* Memory: 32 MB; on Apple's System 128, as a Quadra 650 or a G3 could
       have. EV Nova asks for a 53 MB partition (M1.md): 64 left the Finder
       and it no room. The browser commits it all (the wasm memory grows to
       about twice it, up to the 4 GB a wasm32 module may have). Executor's
       volume is in memory; Executor makes its System Folder. Disk images
       the page puts in later are in /uploads/. */
    session::Setup setup;
    setup.data_dir = DATA_DIR;
    setup.images = "/uploads/";
    setup.log_prefix = "[web]";
    setup.ram_mb = 32;
    setup.apple_ram_mb = 128;
    session::Hooks hooks;
    hooks.ejected = [](const std::string &path) { js_ejected(path.c_str()); };
    hooks.additions = [](const std::string &path, const std::string &json) {
        js_additions(path.c_str(), json.c_str());
    };
    session::prepare(o, setup, hooks);
    /* "Host", as mac-phoenix names its first shared folder: files the page
       reads and writes (worker.js 'read', 'write'). */
    c.shared_folders.push_back("/host");
    c.open_transport = true;  /* answers with errors: no network yet */
    PhoenixVideoDriver::rectsHook = on_rects;
    c.on_cursor = [](const uint8_t data[32], const uint8_t mask[32], int hot_x, int hot_y, bool visible) {
        js_cursor(data, mask, hot_x, hot_y, visible);
    };
    /* Sound: always taken; the page's AudioWorklet keeps a short buffer
       and drops what falls behind (the Sound Manager runs on the clock). */
    c.on_audio = [](const uint8_t *s16be, uint32_t frames) {
        js_audio(s16be, (int)frames);
        return true;
    };
    Executor::hostidle::set_yield_hook(on_yield);
    executor_host::set_event_poll_hook(on_event_poll);
    executor_host::set_fatal_hook([](const char *message) { js_fatal(message); });
    fprintf(stderr, "[web] Executor %dx%d, build %s\n", c.width, c.height, EXECUTOR_WEB_BUILD);
    js_build(EXECUTOR_WEB_BUILD);
    return executor_host::run(c);
}
