/*
 *  cmd.cpp — guest command dispatcher. See cmd.h.
 */
#include "cmd.h"
#include "bidi.h"
#include "qtwebengine_browser.h"
#include "window_resize.h"
#include "shm.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
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
        if (qt_dispatch_nav(url)) return true;
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
        if (qt_dispatch_reload()) return true;
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->reload(&err)) {
                log_remote_err("reload", err);
                send_status(BR_STATUS_ERROR, "");
            }
        });
        return true;
    case BR_CMD_BACK:
        if (qt_dispatch_back()) return true;
        dispatch_async([](BidiClient* c) {
            std::string err;
            if (!c->go_back(&err)) {
                log_remote_err("back", err);
                send_status(BR_STATUS_READY, "");
            }
        });
        return true;
    case BR_CMD_FORWARD:
        if (qt_dispatch_forward()) return true;
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
        if (qt_dispatch_click(x, y, btn, count)) return true;
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
        if (qt_dispatch_mouse_move(x, y)) return true;
        if (!b) return true;
        std::string err;
        if (!b->mouse_move(x, y, &err))
            log_remote_err("mouse_move", err);
        return true;
    }
    case BR_CMD_MOUSE_OUT:
        if (qt_dispatch_mouse_out()) return true;
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
         *     (form submission, input deletion).
         *  2. Printable character — text wins. Send the UTF-8 bytes
         *     verbatim through BiDi's type() helper.
         *
         * Modifier handling: Mac sends `mods` with shiftKey/cmdKey/
         * optionKey/controlKey bits. We pass shift through (so
         * Shift+ArrowRight extends selection), and skip cmd/ctrl
         * because those are already intercepted by Mac's menu bar
         * and never reach send_key. */
        if (len < 5) return false;
        uint16_t vk       = be16(payload + 0);
        uint16_t mods     = be16(payload + 2);
        uint8_t  text_len = payload[4];
        if (5u + text_len > len) return false;
        if (qt_dispatch_key_down(vk, mods, payload + 5, text_len)) return true;
        if (!b) return true;
        /* Mac modifier bits — see Events.h. We only honour shift here;
         * cmd/ctrl/option don't reach this command (cmd → menu bar,
         * ctrl is rare on Mac, option produces special characters
         * already pre-cooked into `text`). */
        constexpr uint16_t kMacShift = 0x0200;
        unsigned w3c_mods = 0;
        if (mods & kMacShift) w3c_mods |= BidiClient::kModShift;

        std::string err;
        const char* w3c = mac_vk_to_w3c_key(vk);
        if (w3c[0] != '\0') {
            bool ok = w3c_mods
                ? b->send_key_with_mods(w3c, w3c_mods, &err)
                : b->type(w3c, &err);
            if (!ok) log_remote_err("type-special", err);
        } else if (text_len > 0) {
            std::string text((const char*)payload + 5, text_len);
            /* Modifier+printable: route through send_key_with_mods so
             * Shift+letter-still-as-letter (Mac pre-cooked it as 'A'
             * not 'a') reaches the page with shift held — matters
             * for word-by-word selection extension on Shift+Right. */
            bool ok = w3c_mods
                ? b->send_key_with_mods(text, w3c_mods, &err)
                : b->type(text, &err);
            if (!ok) log_remote_err("type", err);
        }
        return true;
    }
    case BR_CMD_KEY_UP: {
        if (len < 2) return false;
        uint16_t vk = be16(payload + 0);
        if (qt_dispatch_key_up(vk)) return true;
        /* Per-character keyDown+keyUp is already done in BR_CMD_KEY_DOWN
         * (BiDi's input.performActions emits both for us). KEY_UP from
         * the guest is currently a no-op; revisit when we add held
         * modifiers. */
        return true;
    }

    case BR_CMD_SCROLL: {
        if (len < 8) return false;
        int32_t dx = be32(payload + 0);
        int32_t dy = be32(payload + 4);
        if (qt_dispatch_scroll(dx, dy)) return true;
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
        if (qt_dispatch_resize(w, h)) return true;
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

    case BR_CMD_STOP:
        if (qt_dispatch_stop()) return true;
        /* Toolbar Stop button — cancel the current load. BiDi has no
         * top-level stop call; window.stop() is the JS equivalent
         * and works for all in-flight resources. */
        dispatch_async([](BidiClient* c) {
            std::string err;
            (void)c->evaluate("window.stop()", &err);
        });
        return true;

    case BR_CMD_ZOOM_IN:
        if (qt_dispatch_zoom_in()) return true;
        goto bidi_zoom;
    case BR_CMD_ZOOM_OUT:
        if (qt_dispatch_zoom_out()) return true;
        goto bidi_zoom;
    case BR_CMD_ZOOM_RESET:
        if (qt_dispatch_zoom_reset()) return true;
        goto bidi_zoom;
    bidi_zoom: {
        /* Page zoom via Firefox's full-page zoom (browser.zoom). We
         * can't reach the privileged ZoomManager from BiDi script
         * context, but `document.documentElement.style.zoom` (CSS
         * zoom — supported in Firefox 126+ as an explicitly-shipped
         * non-standard prop, identical to Blink) gives us full-page
         * scaling that reflows content. Levels mirror the standard
         * Firefox steps: 50/67/80/90/100/110/125/150/175/200%. */
        const char* op = (type == BR_CMD_ZOOM_IN)    ? "in"
                        : (type == BR_CMD_ZOOM_OUT)  ? "out"
                                                     : "reset";
        std::string js =
            "(()=>{const steps=[0.5,0.67,0.8,0.9,1.0,1.1,1.25,1.5,1.75,2.0];"
            "const e=document.documentElement;"
            "const cur=parseFloat(e.style.zoom||'1.0')||1.0;"
            "let i=steps.findIndex(s=>Math.abs(s-cur)<0.005);"
            "if(i<0){i=steps.findIndex(s=>s>=cur);if(i<0)i=steps.length-1;}"
            "let next=cur;"
            "if('" + std::string(op) + "'==='in')   next=steps[Math.min(steps.length-1,i+1)];"
            "if('" + std::string(op) + "'==='out')  next=steps[Math.max(0,i-1)];"
            "if('" + std::string(op) + "'==='reset')next=1.0;"
            "e.style.zoom=String(next);"
            "return next;})()";
        dispatch_async([js](BidiClient* c) {
            std::string err;
            std::string r = c->evaluate(js, &err);
            if (r.empty()) log_remote_err("zoom", err);
            else {
                QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(r));
                if (!doc.isNull()) {
                    double z = doc.object().value("result").toObject().value("value").toDouble();
                    fprintf(stderr, "[Cmd] zoom → %.0f%%\n", z * 100.0);
                }
            }
        });
        return true;
    }

    case BR_CMD_SELECT_ALL:
        if (qt_dispatch_select_all()) return true;
        /* Cmd+A in MacBrowser web context. JS path covers the three
         * common cases: <input>/<textarea> via .select(), generic
         * editable + page text via document.execCommand('selectAll'),
         * or a window.getSelection / Range.selectNodeContents fallback
         * if execCommand returns false. */
        dispatch_async([](BidiClient* c) {
            std::string err;
            std::string js =
                "(()=>{const el=document.activeElement;"
                "const t=(el&&el.tagName||'').toLowerCase();"
                "if(t==='input'||t==='textarea'){"
                "  if(typeof el.select==='function'){el.select();return true;}"
                "}"
                "if(document.execCommand('selectAll',false,null))return true;"
                "const sel=window.getSelection();"
                "if(sel){"
                "  const r=document.createRange();"
                "  r.selectNodeContents(document.body);"
                "  sel.removeAllRanges();sel.addRange(r);return true;"
                "}"
                "return false;})()";
            std::string r = c->evaluate(js, &err);
            if (r.empty()) log_remote_err("select_all", err);
        });
        return true;

    case BR_CMD_GET_SELECTION:
        if (qt_dispatch_get_selection()) return true;
        /* Cmd+C in MacBrowser: ask Firefox what's currently selected,
         * round-trip the text back through h2g as BR_EV_SELECTION.
         * Mac side puts that into TEScrap so a subsequent Cmd+V in
         * any app sees the web-copied text. evaluate runs sync on
         * the BiDi worker thread; we dispatch async to keep the
         * guest unblocked. */
        dispatch_async([](BidiClient* c) {
            std::string err;
            std::string r = c->evaluate(
                "(window.getSelection&&window.getSelection().toString())||''",
                &err);
            if (r.empty()) {
                log_remote_err("get_selection", err);
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(r));
            if (doc.isNull()) return;
            std::string text = doc.object().value("result").toObject().value("value").toString().toStdString();
            uint16_t n = (uint16_t)std::min(text.size(), (size_t)4096);
            std::vector<uint8_t> buf(2 + n);
            buf[0] = (uint8_t)(n >> 8);
            buf[1] = (uint8_t)(n & 0xFF);
            if (n) memcpy(buf.data() + 2, text.data(), n);
            send_event(BR_EV_SELECTION, buf.data(), (uint16_t)buf.size());
        });
        return true;
    case BR_CMD_PASTE: {
        /* Cmd+V in MacBrowser: drop Mac scrap text at the focused
         * element. Three paths, in order of preference:
         *   1. <input>/<textarea> — patch .value around the current
         *      selection and fire `input`/`change` events so React-
         *      like frameworks notice. setRangeText is the modern
         *      idiomatic API; it fires input automatically.
         *   2. contentEditable element — execCommand('insertText'),
         *      which is the path rich-text editors expect.
         *   3. nothing focused — silently no-op (Firefox would just
         *      eat a stray Ctrl-V too).
         *
         * execCommand alone broke for plain <input>: Firefox's modern
         * legacy-execCommand path won't insert text into <input> when
         * called from BiDi script context (no user gesture). */
        if (len < 2) return false;
        uint16_t n = be16(payload + 0);
        if ((uint16_t)(2 + n) > len) return false;
        std::string text((const char*)payload + 2, n);
        fprintf(stderr, "[Cmd] BR_CMD_PASTE %u bytes: \"%.*s%s\"\n",
                (unsigned)n, (int)std::min(n, (uint16_t)40),
                text.c_str(), n > 40 ? "…" : "");
        if (qt_dispatch_paste(text)) return true;
        dispatch_async([text](BidiClient* c) {
            // JSON-encode the string for use as a JS literal. QJsonDocument
            // only serializes objects/arrays so wrap in a single-element
            // array and strip the brackets.
            QByteArray wrapped = QJsonDocument(QJsonArray{
                QJsonValue(QString::fromStdString(text))
            }).toJson(QJsonDocument::Compact);
            std::string lit(wrapped.constData() + 1, wrapped.size() - 2);
            std::string js =
                "(()=>{const t=" + lit + ";"
                "const el=document.activeElement;"
                "if(!el)return false;"
                "const tag=(el.tagName||'').toLowerCase();"
                "if(tag==='input'||tag==='textarea'){"
                "  const s=el.selectionStart||0,e=el.selectionEnd||0;"
                "  if(typeof el.setRangeText==='function'){"
                "    el.setRangeText(t,s,e,'end');"
                "    el.dispatchEvent(new Event('input',{bubbles:true}));"
                "    el.dispatchEvent(new Event('change',{bubbles:true}));"
                "    return true;"
                "  }"
                "  el.value=el.value.slice(0,s)+t+el.value.slice(e);"
                "  el.selectionStart=el.selectionEnd=s+t.length;"
                "  el.dispatchEvent(new Event('input',{bubbles:true}));"
                "  return true;"
                "}"
                "if(el.isContentEditable){"
                "  document.execCommand('insertText',false,t);"
                "  return true;"
                "}"
                "return false;})()";
            std::string err;
            std::string r = c->evaluate(js, &err);
            if (r.empty()) log_remote_err("paste", err);
        });
        return true;
    }

    default:
        fprintf(stderr, "[Cmd] unknown type=0x%x len=%u\n", type, len);
        return false;
    }
}

}  // namespace browser
