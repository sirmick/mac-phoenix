/*
 * web_main.cpp - Executor in a browser tab (docs/executor/DIRECTION.md, W2).
 *
 * The module runs in a Worker (web/executor/worker.js). main() starts
 * Executor through the same executor_host contract mac-phoenix uses, on
 * this thread: there is no other. Executor's idle waits and a tick about
 * once a frame yield to the browser (rsys/hostidle.h); on the way back the
 * yield hook takes the input the page queued (Module.input) and renders
 * what changed, which arrives in on_frame and goes to Module.onFrame as
 * RGBA for the page's canvas.
 *
 * Automation (W3): the page's requests (Module.requests: [id, op, arg]) are
 * answered at the next event call of the running application
 * (executor_host::set_event_poll_hook), a safe point in a process's world:
 *
 *   app              the current application's name (CurApName)
 *   windows          its windows: title, global bounds, visible
 *   launch PATH      LaunchApplication of "Volume:Folder:App"; its OSErr
 *
 * The Worker answers 'read' and 'write' itself: files in the module's
 * filesystem, such as the Host volume (/host).
 */
#include "executor_host.h"
#include "phoenix.h"
#include "hfs_sideload.h"
#include <rsys/hostidle.h>

#include <base/common.h>
#include <FileMgr.h>
#include <ProcessMgr.h>
#include <wind/wind.h>

#include <emscripten.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* The page's input, oldest first: [kind, a, b] per event (kind 1: mouse to
 * a, b; 2: button down a; 3: key down a, Mac key code b). Returns how many
 * events it copied. */
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

EM_JS(void, js_reply, (int id, const char *json), {
    if (Module.onReply)
        Module.onReply(id, JSON.parse(UTF8ToString(json)));
});

/* One 20 ms frame of 48 kHz 16-bit big-endian stereo. */
EM_JS(void, js_audio, (const uint8_t *s16be, int frames), {
    if (Module.onAudio)
        Module.onAudio(HEAPU8.subarray(s16be, s16be + frames * 4));
});

EM_JS(void, js_frame, (const uint8_t *rgba, int width, int height), {
    if (Module.onFrame)
        Module.onFrame(HEAPU8.subarray(rgba, rgba + width * height * 4), width, height);
});

namespace {

std::vector<uint32_t> rgba;

/* 0xAARRGGBB to the canvas's R, G, B, A bytes (0xAABBGGRR, little-endian). */
void on_frame(const uint32_t *argb, int width, int height)
{
    size_t n = (size_t)width * height;
    rgba.resize(n);
    for(size_t i = 0; i < n; i++)
    {
        uint32_t p = argb[i];
        rgba[i] = (p & 0xFF00FF00) | (p >> 16 & 0xFF) | (p & 0xFF) << 16;
    }
    js_frame((const uint8_t *)rgba.data(), width, height);
}

using namespace Executor;

std::string json_string(const std::string &s)
{
    std::string out = "\"";
    for(unsigned char c : s)
    {
        if(c == '"' || c == '\\')
            out += '\\', out += c;
        else if(c < 0x20 || c >= 0x7F)
        {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        }
        else
            out += c;
    }
    return out + "\"";
}

std::string pstring(const unsigned char *p)
{
    return p ? std::string((const char *)p + 1, p[0]) : std::string();
}

std::string windows_json()
{
    std::string out = "[";
    for(WindowPeek w = LM(WindowList); w; w = WINDOW_NEXT_WINDOW(w))
    {
        Rect r = (*WINDOW_STRUCT_REGION(w))->rgnBBox;
        StringHandle t = WINDOW_TITLE(w);
        if(out.size() > 1)
            out += ",";
        out += "{\"title\":" + json_string(t ? pstring(*t) : "")
               + ",\"rect\":[" + std::to_string(r.top) + "," + std::to_string(r.left) + ","
               + std::to_string(r.bottom) + "," + std::to_string(r.right) + "]"
               + ",\"visible\":" + (WINDOW_VISIBLE(w) ? "true" : "false") + "}";
    }
    return out + "]";
}

OSErr launch(const std::string &path)
{
    Str255 name;
    if(path.size() > 255)
        return bdNamErr;
    name[0] = path.size();
    memcpy(name + 1, path.data(), path.size());
    FSSpec spec;
    OSErr err = FSMakeFSSpec(0, 0, name, &spec);
    if(err != noErr)
        return err;
    LaunchParamBlockRec pb = {};
    pb.launchBlockID = extendedBlock;
    pb.launchEPBLength = extendedBlockLen;
    pb.launchControlFlags = launchContinue | launchNoFileFlags;
    pb.launchAppSpec = &spec;
    return LaunchApplication(&pb);
}

/* At an event call of the running application. */
void on_event_poll()
{
    char buf[1024];
    while(int id = js_take_request(buf, sizeof buf))
    {
        std::string op = buf, arg = buf + op.size() + 1;
        std::string reply;
        if(op == "app")
            reply = json_string(executor_host::current_app_name());
        else if(op == "windows")
            reply = windows_json();
        else if(op == "launch")
            reply = std::to_string(launch(arg));
        else
            reply = "{\"error\":" + json_string("unknown request " + op) + "}";
        js_reply(id, reply.c_str());
    }
}

void on_yield()
{
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
            }
        }
    if(auto *v = dynamic_cast<PhoenixVideoDriver *>(Executor::vdriver.get()))
        v->pump();
}

}

/* web_main [--size WxH] [--disk IMAGE]... [--system IMAGE] [APP]: the
 * screen size, disk images to mount (read-only), the image whose System
 * Folder to run on (Apple's System and Finder: copied into Executor's
 * volume, and the Finder started), and an application to start instead of
 * Executor's Browser or the Finder (paths in the module's filesystem). */
int main(int argc, char **argv)
{
    executor_host::Config c;
    std::string system_image;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if(a == "--size" && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &c.width, &c.height);
        else if(a == "--disk" && i + 1 < argc)
            c.disks.push_back(argv[++i]);
        else if(a == "--system" && i + 1 < argc)
            system_image = argv[++i];
        else
            c.app = a;
    }
    c.bpp = 8;
    c.ram_mb = 32;
    c.data_dir = "/executor"; /* in memory; Executor makes its System Folder */
    if(!system_image.empty())
    {
        bool finder = false;
        std::string err;
        if(executor_host::sideload_system(system_image, c.data_dir, &finder, &err))
        {
            fprintf(stderr, "[web] System Folder from %s%s\n", system_image.c_str(),
                    finder ? ", starting the Finder" : "");
            if(finder && c.app.empty())
                c.app = c.data_dir + "/System Folder/Finder";
            c.ram_mb = 64;
        }
        else
            fprintf(stderr, "[web] no System Folder from %s: %s\n", system_image.c_str(), err.c_str());
    }
    /* "Host", as mac-phoenix names its first shared folder: files the page
       reads and writes (worker.js 'read', 'write'). */
    c.shared_folders.push_back("/host");
    c.open_transport = true;  /* answers with errors: no network yet */
    c.on_frame = on_frame;
    /* Sound: always taken; the page's AudioWorklet keeps a short buffer
       and drops what falls behind (the Sound Manager runs on the clock). */
    c.on_audio = [](const uint8_t *s16be, uint32_t frames) {
        js_audio(s16be, (int)frames);
        return true;
    };
    Executor::hostidle::set_yield_hook(on_yield);
    executor_host::set_event_poll_hook(on_event_poll);
    fprintf(stderr, "[web] Executor %dx%d\n", c.width, c.height);
    return executor_host::run(c);
}
