/*
 *  cmd.cpp — guest command dispatcher. See cmd.h.
 */
#include "cmd.h"
#include "bidi.h"
#include "window_resize.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <algorithm>
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

/* Push a BR_EV_STATUS so the guest's "Loading…" indicator clears.
 * Used when a BiDi navigate-style call fails — Firefox won't emit
 * browsingContext.load for malformed URLs or DNS errors, so without
 * this the URL bar's status label sticks on LOADING forever. */
void send_status(uint8_t code, const std::string& url)
{
    uint8_t buf[2 + 250];
    buf[0] = code;
    size_t n = std::min(url.size(), (size_t)250);
    buf[1] = (uint8_t)n;
    if (n) memcpy(buf + 2, url.data(), n);
    send_event(BR_EV_STATUS, buf, (uint16_t)(2 + n));
}

/* Map a Mac virtual key code to the W3C-WebDriver "normalized key
 * value" — single Unicode code point in the U+E000 private-use
 * range, as defined by the W3C WebDriver spec. Returns empty on
 * "no special-key meaning, fall through to the typed character".
 *
 * The Mac sends us {vk, mods, text}. text is post-KCHR (e.g. \r for
 * Return, \b/\x7F for Delete). Sending the raw control byte through
 * BiDi *sometimes* triggers the right key event in Firefox — but
 * not for forms (Google's search input ignores a bare \r keyDown
 * because no W3C "Enter" key event fires). The W3C codepoints below
 * are how Selenium / Puppeteer / etc. drive these keys reliably.
 *
 * Only mapping non-text keys here. Letters / digits / punctuation
 * stay routed via the UTF-8 text byte (still works correctly). */
const char* mac_vk_to_w3c_key(uint16_t vk)
{
    switch (vk) {
    /* Editing */
    case 0x33: return "\xEE\x80\x83";   /* Delete (Mac's main Delete = Backspace)  U+E003 */
    case 0x75: return "\xEE\x80\x97";   /* Forward Delete  U+E017 */
    /* Whitespace / dispatch */
    case 0x24: return "\xEE\x80\x86";   /* Return  U+E006 */
    case 0x4C: return "\xEE\x80\x87";   /* Enter (numpad)  U+E007 */
    case 0x30: return "\xEE\x80\x84";   /* Tab  U+E004 */
    case 0x35: return "\xEE\x80\x8C";   /* Escape  U+E00C */
    /* Arrows */
    case 0x7B: return "\xEE\x80\x92";   /* Left  U+E012 */
    case 0x7C: return "\xEE\x80\x94";   /* Right  U+E014 */
    case 0x7E: return "\xEE\x80\x93";   /* Up  U+E013 */
    case 0x7D: return "\xEE\x80\x95";   /* Down  U+E015 */
    /* Navigation cluster */
    case 0x73: return "\xEE\x80\x91";   /* Home  U+E011 */
    case 0x77: return "\xEE\x80\x90";   /* End  U+E010 */
    case 0x74: return "\xEE\x80\x8E";   /* PageUp  U+E00E */
    case 0x79: return "\xEE\x80\x8F";   /* PageDown  U+E00F */
    /* F-keys (only F1..F4 mapped — extend if needed) */
    case 0x7A: return "\xEE\x80\xB1";   /* F1  U+E031 */
    case 0x78: return "\xEE\x80\xB2";   /* F2  U+E032 */
    case 0x63: return "\xEE\x80\xB3";   /* F3  U+E033 */
    case 0x76: return "\xEE\x80\xB4";   /* F4  U+E034 */
    default:   return "";
    }
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
            if (!c->navigate(url, &err)) {
                log_remote_err("navigate", err);
                send_status(BR_STATUS_ERROR, url);
            }
        });
        return true;
    }
    case BR_CMD_RELOAD:
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->reload(&err)) {
                log_remote_err("reload", err);
                send_status(BR_STATUS_ERROR, "");
            }
        });
        return true;
    case BR_CMD_BACK:
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->go_back(&err)) {
                log_remote_err("back", err);
                send_status(BR_STATUS_READY, "");
            }
        });
        return true;
    case BR_CMD_FORWARD:
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->go_forward(&err)) {
                log_remote_err("forward", err);
                send_status(BR_STATUS_READY, "");
            }
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
         * Two paths:
         *  1. Special key (Return / Delete / Tab / Esc / Arrows / etc.)
         *     — vk wins. Send the W3C-WebDriver private-use codepoint
         *     so Firefox sees a proper "Enter" / "Backspace" key event
         *     (form submission, input deletion). Without this, a bare
         *     \r or \b reaches Firefox as a literal character which
         *     <input> handlers ignore.
         *  2. Printable character — text wins. Send the UTF-8 bytes
         *     verbatim through BiDi's type() helper.
         *
         * Mods are still ignored; held-modifier keystrokes (e.g.
         * Shift-A) are pre-cooked by Mac's Event Manager into 'A',
         * which is what the page sees. Held Cmd/Ctrl combos beyond
         * Mac-side shortcuts (Cmd-C / Cmd-V) are M6+ work. */
        if (len < 5) return false;
        uint16_t vk       = be16(payload + 0);
        uint8_t  text_len = payload[4];
        if (5u + text_len > len) return false;
        if (!b) return true;
        std::string err;
        const char* w3c = mac_vk_to_w3c_key(vk);
        if (w3c[0] != '\0') {
            if (!b->type(w3c, &err)) log_remote_err("type-special", err);
        } else if (text_len > 0) {
            std::string text((const char*)payload + 5, text_len);
            if (!b->type(text, &err)) log_remote_err("type", err);
        }
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
        /* Resize the Firefox top-level *inside* the fixed Xvfb root
         * (instead of resizing the X server's root via RandR — Xvfb
         * mode-list bookkeeping fights us on that path). Three steps:
         *   1. xcb_configure_window on the Firefox window — actual
         *      paintable area shrinks/grows and Firefox triggers a
         *      reflow + repaint.
         *   2. bidi.setViewport so the BiDi layout viewport agrees
         *      (matters for media queries, body sizing, getBoundingClientRect).
         *   3. publish fb.width/height into BrowserShm so the guest
         *      and the host pipeline both read the new crop region.
         *      Pipeline keeps the next BR_FENCE_RELEASE + fb.seq bump
         *      (after fresh pixels arrive), so the guest only sees
         *      the new dims paired with new pixels.
         */
        int disp = g_display.load(std::memory_order_acquire);
        bool win_ok = false;
        if (disp >= 0) win_ok = resize_firefox_window(disp, w, h);
        bool bidi_ok = false;
        std::string err;
        if (b) {
            bidi_ok = b->set_viewport(w, h, &err);
            if (!bidi_ok) log_remote_err("setViewport", err);
        }
        if (BrowserShm* s = browser::shm_get()) {
            br_u16_store(&s->fb.width,  w);
            br_u16_store(&s->fb.height, h);
        }
        fprintf(stderr,
                "[Cmd] BR_CMD_RESIZE guest_pixels=%ux%u  "
                "ff_window=%s  bidi.setViewport=%s\n",
                w, h,
                (disp >= 0) ? (win_ok ? "ok" : "FAIL") : "skip",
                b ? (bidi_ok ? "ok" : "FAIL") : "skip");
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
