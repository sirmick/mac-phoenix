/*
 *  cmd.cpp — guest command dispatcher. See cmd.h.
 *
 *  Every BR_CMD_* type routes to a corresponding qt_dispatch_* free
 *  function in qtwebengine_browser.cpp. The qt_dispatch_* layer
 *  marshals to the GUI thread and posts QMouseEvent / QKeyEvent /
 *  QWheelEvent / QWebEnginePage::triggerAction or runJavaScript as
 *  appropriate.
 */
#include "cmd.h"
#include "qtwebengine_browser.h"

#define BR_HOST 1
#include "MacBrowser.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace browser {

namespace {

// Read big-endian fields from the guest payload. Guest is m68k, so
// every multi-byte int in the ring arrives BE.
uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
int32_t  be32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                     (uint32_t)p[2] <<  8 | (uint32_t)p[3]);
}

}  // namespace

bool cmd_dispatch(uint16_t type, const uint8_t* payload, uint16_t len)
{
    switch (type) {
    case BR_CMD_NAV: {
        std::string url((const char*)payload, len);
        qt_dispatch_nav(url);
        return true;
    }
    case BR_CMD_RELOAD:
        qt_dispatch_reload();
        return true;
    case BR_CMD_BACK:
        qt_dispatch_back();
        return true;
    case BR_CMD_FORWARD:
        qt_dispatch_forward();
        return true;
    case BR_CMD_STOP:
        qt_dispatch_stop();
        return true;

    case BR_CMD_CLICK: {
        if (len < 10) return false;
        int32_t  x = be32(payload + 0);
        int32_t  y = be32(payload + 4);
        uint8_t  btn = payload[8];
        uint8_t  count = payload[9];
        qt_dispatch_click(x, y, btn, count);
        return true;
    }
    case BR_CMD_MOUSE_MOVE: {
        if (len < 8) return false;
        int32_t x = be32(payload + 0);
        int32_t y = be32(payload + 4);
        qt_dispatch_mouse_move(x, y);
        return true;
    }
    case BR_CMD_MOUSE_OUT:
        qt_dispatch_mouse_out();
        return true;

    case BR_CMD_KEY_DOWN: {
        // u16 vk, u16 mods, u8 text_len, u8 text[]
        // Two paths inside qt_dispatch_key_down:
        //   1. Special key (Return/Delete/Tab/Esc/Arrows/etc.) — vk
        //      maps to a Qt::Key code, text is ignored.
        //   2. Printable character — text wins, key code = first
        //      char's Unicode.
        // Modifier handling: only kMacShift flows through (cmd reaches
        // the menu bar before this, option produces pre-cooked text).
        if (len < 5) return false;
        uint16_t vk       = be16(payload + 0);
        uint16_t mods     = be16(payload + 2);
        uint8_t  text_len = payload[4];
        if (5u + text_len > len) return false;
        qt_dispatch_key_down(vk, mods, payload + 5, text_len);
        return true;
    }
    case BR_CMD_KEY_UP: {
        if (len < 2) return false;
        uint16_t vk = be16(payload + 0);
        qt_dispatch_key_up(vk);
        return true;
    }

    case BR_CMD_SCROLL: {
        if (len < 8) return false;
        int32_t dx = be32(payload + 0);
        int32_t dy = be32(payload + 4);
        qt_dispatch_scroll(dx, dy);
        return true;
    }
    case BR_CMD_RESIZE: {
        if (len < 4) return false;
        uint16_t w = be16(payload + 0);
        uint16_t h = be16(payload + 2);
        qt_dispatch_resize(w, h);
        return true;
    }

    case BR_CMD_ZOOM_IN:    qt_dispatch_zoom_in();    return true;
    case BR_CMD_ZOOM_OUT:   qt_dispatch_zoom_out();   return true;
    case BR_CMD_ZOOM_RESET: qt_dispatch_zoom_reset(); return true;

    case BR_CMD_SELECT_ALL:    qt_dispatch_select_all();    return true;
    case BR_CMD_GET_SELECTION: qt_dispatch_get_selection(); return true;
    case BR_CMD_PASTE: {
        if (len < 2) return false;
        uint16_t n = be16(payload + 0);
        if ((uint16_t)(2 + n) > len) return false;
        std::string text((const char*)payload + 2, n);
        fprintf(stderr, "[Cmd] BR_CMD_PASTE %u bytes\n", (unsigned)n);
        qt_dispatch_paste(text);
        return true;
    }

    default:
        fprintf(stderr, "[Cmd] unknown type=0x%x len=%u\n", type, len);
        return false;
    }
}

}  // namespace browser
