/*
 *  cmd.cpp — guest command dispatcher. See cmd.h.
 */
#include "cmd.h"
#include "bidi.h"
#include "randr_resize.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace browser {

namespace {

std::atomic<BidiClient*> g_bidi{nullptr};
std::atomic<int>         g_display{-1};

/* Read big-endian fields from the guest payload. Guest is m68k, so
 * everything in the ring arrives BE. */
uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
int32_t  be32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                     (uint32_t)p[2] <<  8 | (uint32_t)p[3]);
}

/* navigate / reload / back / forward run on a detached thread —
 * Firefox's `wait: complete` blocks until DOMContentLoaded which
 * can take seconds. Mouse/key actions are sub-100 ms; do them
 * synchronously so the guest sees deterministic ordering. */
void dispatch_async(std::function<void(BidiClient*)> fn)
{
    BidiClient* b = g_bidi.load(std::memory_order_acquire);
    if (!b) return;
    std::thread([b, fn = std::move(fn)]() { fn(b); }).detach();
}

void log_remote_err(const char* method, const std::string& err)
{
    if (err.empty()) return;
    fprintf(stderr, "[Cmd] %s: %s\n", method, err.c_str());
}

}  // namespace

void cmd_set_bidi(BidiClient* bidi)
{
    g_bidi.store(bidi, std::memory_order_release);
}

void cmd_set_display(int display)
{
    g_display.store(display, std::memory_order_release);
}

bool cmd_dispatch(uint16_t type, const uint8_t* payload, uint16_t len)
{
    BidiClient* b = g_bidi.load(std::memory_order_acquire);

    switch (type) {
    case BR_CMD_NAV: {
        std::string url((const char*)payload, len);
        dispatch_async([url](BidiClient* c) {
            std::string err;
            if (!c->navigate(url, &err)) log_remote_err("navigate", err);
        });
        return true;
    }
    case BR_CMD_RELOAD:
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->reload(&err)) log_remote_err("reload", err);
        });
        return true;
    case BR_CMD_BACK:
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->go_back(&err)) log_remote_err("back", err);
        });
        return true;
    case BR_CMD_FORWARD:
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->go_forward(&err)) log_remote_err("forward", err);
        });
        return true;

    case BR_CMD_CLICK: {
        if (len < 10) return false;
        int32_t  x = be32(payload + 0);
        int32_t  y = be32(payload + 4);
        uint8_t  btn = payload[8];
        uint8_t  count = payload[9];
        if (!b) return true;
        std::string err;
        if (!b->click(x, y, btn, count ? count : 1, &err))
            log_remote_err("click", err);
        return true;
    }
    case BR_CMD_MOUSE_MOVE: {
        if (len < 8) return false;
        int32_t x = be32(payload + 0);
        int32_t y = be32(payload + 4);
        if (!b) return true;
        std::string err;
        if (!b->mouse_move(x, y, &err))
            log_remote_err("mouse_move", err);
        return true;
    }
    case BR_CMD_MOUSE_OUT:
        /* No direct BiDi equivalent — most pages don't need explicit
         * mouseout. Move pointer well off-screen as a best-effort. */
        if (b) b->mouse_move(-1, -1);
        return true;

    case BR_CMD_KEY_DOWN: {
        /* u16 vk, u16 mods, u8 text_len, u8 text[]
         * For text input we use the trailing utf8 bytes; vk + mods are
         * for special-key handling (arrows, F-keys, modifiers) which
         * is M5+. */
        if (len < 5) return false;
        uint8_t text_len = payload[4];
        if (5u + text_len > len) return false;
        if (text_len == 0 || !b) return true;
        std::string text((const char*)payload + 5, text_len);
        std::string err;
        if (!b->type(text, &err)) log_remote_err("type", err);
        return true;
    }
    case BR_CMD_KEY_UP:
        /* Per-character keyDown+keyUp is already done in BR_CMD_KEY_DOWN
         * (BiDi's input.performActions emits both for us). KEY_UP from
         * the guest is currently a no-op; revisit when we add held
         * modifiers. */
        return true;

    case BR_CMD_SCROLL: {
        if (len < 8) return false;
        int32_t dx = be32(payload + 0);
        int32_t dy = be32(payload + 4);
        if (!b) return true;
        std::string err;
        /* Scroll origin = viewport center; refine when guest reports
         * cursor coords with the scroll command. */
        if (!b->scroll(320, 240, dx, dy, &err))
            log_remote_err("scroll", err);
        return true;
    }
    case BR_CMD_RESIZE: {
        if (len < 4) return false;
        uint16_t w = be16(payload + 0);
        uint16_t h = be16(payload + 2);
        /* RandR first: physically resize Xvfb's root → Firefox kiosk
         * follows automatically. Then setViewport so Firefox's BiDi
         * layout viewport matches. The two together produce one
         * reflow + repaint at the new dimensions. */
        int disp = g_display.load(std::memory_order_acquire);
        if (disp >= 0) randr_resize(disp, w, h);
        if (!b) return true;
        std::string err;
        if (!b->set_viewport(w, h, &err))
            log_remote_err("setViewport", err);
        return true;
    }

    case BR_CMD_GET_SELECTION:
        /* TODO(M6): script.evaluate of getSelection().toString() then
         * push BR_EV_SELECTION through h2g. */
        return true;
    case BR_CMD_PASTE:
        /* TODO(M6): script.evaluate of document.execCommand('insertText',
         * false, <text>) at the focused element. */
        return true;

    default:
        fprintf(stderr, "[Cmd] unknown type=0x%x len=%u\n", type, len);
        return false;
    }
}

}  // namespace browser
