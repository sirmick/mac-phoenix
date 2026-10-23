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
 */
#include "executor_host.h"
#include "phoenix.h"
#include <rsys/hostidle.h>

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

/* web_main [--size WxH] [APP]: the screen size, and an application to start
 * instead of Executor's Browser (a path in the module's filesystem). */
int main(int argc, char **argv)
{
    executor_host::Config c;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if(a == "--size" && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &c.width, &c.height);
        else
            c.app = a;
    }
    c.bpp = 8;
    c.ram_mb = 32;
    c.data_dir = "/executor"; /* in memory; Executor makes its System Folder */
    c.open_transport = true;  /* answers with errors: no network yet */
    c.on_frame = on_frame;
    Executor::hostidle::set_yield_hook(on_yield);
    fprintf(stderr, "[web] Executor %dx%d\n", c.width, c.height);
    return executor_host::run(c);
}
