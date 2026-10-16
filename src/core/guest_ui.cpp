/*
 * guest_ui.cpp - the guest's user interface as JSON, read from its memory.
 *
 * For /api/ui: every window front to back with its rectangle in screen
 * coordinates, its controls (title, rectangle, value, enabled; popup menus
 * with their items) and, for dialogs, its items (buttons, checkboxes,
 * static and edit text with their text). Lets a recipe click "Install" by
 * name instead of by pixel. Read-only: nothing here writes guest memory.
 *
 * Layouts (Inside Macintosh: Toolbox Essentials):
 *   WindowRecord  port +0 (GrafPort 108 / CGrafPort), portRect +16,
 *                 windowKind +108, visible +110, hilited +111,
 *                 titleHandle +134, controlList +140, nextWindow +144
 *   GrafPort      portBits.bounds +8;  CGrafPort: portPixMap +2 (handle),
 *                 portVersion +6 (top bits 0xC000), PixMap.bounds +6
 *   DialogRecord  WindowRecord + items +156 (handle to the item list)
 *   ControlRecord nextControl +0, contrlRect +8, contrlVis +16,
 *                 contrlHilite +17, value +18, min +20, max +22,
 *                 contrlData +28, contrlTitle +40 (Str255)
 *   Item list     count-1 (int16), then per item: handle (4), rect (8),
 *                 type (1), data length (1), data (padded to even)
 *   MenuInfo      menuID +0, menuData +14 (title Str255, then per item:
 *                 Str255 name, icon, key, mark, style; ends with 0)
 *   MenuList      (0x0A1C) lastMenu +0, then 6-byte entries (handle, left);
 *                 after them the hierarchical part: lastHMenu, menuTitleSave,
 *                 6-byte entries. A popup menu is in the hierarchical part
 *                 while it is open (PopUpMenuSelect).
 *
 * Hidden controls are moved far off screen by some apps (Installer VISE):
 * anything left of x = 8192 counts as on screen.
 */

#include <cstdint>
#include <cstdio>
#include <string>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "mac_roman.h"
#include "guest_ui.h"

namespace {

bool valid(uint32_t p, uint32_t n)
{
    return p >= 0x400 && p < RAMSize && n <= RAMSize - p;
}

uint32_t deref(uint32_t h)
{
    if (!valid(h, 4)) return 0;
    uint32_t p = ReadMacInt32(h) & 0x7FFFFFFF;   /* clear a 24-bit master pointer's flags */
    if (!valid(p, 1)) p = ReadMacInt32(h) & 0x00FFFFFF;
    return valid(p, 1) ? p : 0;
}

std::string pstring(uint32_t p, int max = 255)
{
    if (!valid(p, 1)) return "";
    int n = ReadMacInt8(p);
    if (n > max || !valid(p, 1 + n)) return "";
    std::string s;
    for (int i = 0; i < n; i++) s += static_cast<char>(ReadMacInt8(p + 1 + i));
    return s;
}

std::string bytes(uint32_t p, int n)
{
    if (n <= 0 || !valid(p, n)) return "";
    std::string s;
    for (int i = 0; i < n; i++) s += static_cast<char>(ReadMacInt8(p + i));
    return s;
}

void str(std::string &out, const std::string &mac)
{
    std::string u = mac_roman_to_utf8(mac);
    out += '"';
    for (unsigned char c : u) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\r' || c == '\n') out += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += c;
    }
    out += '"';
}

struct Rect { int16_t top, left, bottom, right; };

Rect rect_at(uint32_t p)
{
    return { (int16_t)ReadMacInt16(p), (int16_t)ReadMacInt16(p + 2),
             (int16_t)ReadMacInt16(p + 4), (int16_t)ReadMacInt16(p + 6) };
}

/* [left, top, right, bottom] in screen coordinates. */
void rect(std::string &out, Rect r, int dx, int dy)
{
    char b[64];
    snprintf(b, sizeof b, "[%d,%d,%d,%d]", r.left + dx, r.top + dy, r.right + dx, r.bottom + dy);
    out += b;
}

/* Screen position of a window's local (0,0): minus its port's bounds origin. */
void origin(uint32_t wp, int &dx, int &dy)
{
    uint32_t bounds = wp + 8;
    if ((ReadMacInt16(wp + 6) & 0xC000) == 0xC000) {        /* CGrafPort */
        uint32_t pm = deref(ReadMacInt32(wp + 2));
        if (pm) bounds = pm + 6;
    }
    dx = -(int16_t)ReadMacInt16(bounds + 2);
    dy = -(int16_t)ReadMacInt16(bounds);
}

/* A menu's items: [{"text", "enabled", "mark"}]. */
void menu_items(uint32_t menu, std::string &out)
{
    uint32_t enable = ReadMacInt32(menu + 10);
    uint32_t p = menu + 14;
    p += 1 + ReadMacInt8(p);                                   /* the menu title */
    out += "[";
    for (int i = 0; i < 64 && valid(p, 1); i++) {
        int n = ReadMacInt8(p);
        if (n == 0 || !valid(p, n + 5)) break;
        if (i) out += ',';
        out += "{\"text\":";
        str(out, bytes(p + 1, n));
        char b[64];
        bool on = (enable & 1) && (i >= 31 || (enable >> (i + 1)) & 1);
        snprintf(b, sizeof b, ",\"enabled\":%s,\"mark\":%d}", on ? "true" : "false", ReadMacInt8(p + 1 + n + 2));
        out += b;
        p += 1 + n + 4;
    }
    out += "]";
}

/* Menus in the menu list: the menu bar's, then hierarchical and open popups. */
void menus(std::string &out)
{
    out += ",\"menus\":[";
    uint32_t ml = deref(valid(0x0A1C, 4) ? ReadMacInt32(0x0A1C) : 0);
    if (!ml) { out += "]"; return; }
    int last = (int16_t)ReadMacInt16(ml);
    int count = 0;
    auto one = [&](uint32_t mh, bool popup) {
        uint32_t m = deref(mh);
        if (!m || !valid(m, 16)) return;
        if (count++) out += ',';
        out += "{\"title\":";
        str(out, pstring(m + 14));
        char b[64];
        snprintf(b, sizeof b, ",\"id\":%d,\"hierarchical\":%s,\"items\":", (int16_t)ReadMacInt16(m),
                 popup ? "true" : "false");
        out += b;
        menu_items(m, out);
        out += "}";
    };
    for (int off = 6; off <= last && off < 600; off += 6)
        one(ReadMacInt32(ml + off), false);
    uint32_t h = ml + last + 6;                                /* the hierarchical part */
    if (valid(h, 6)) {
        int hlast = (int16_t)ReadMacInt16(h);
        for (int off = 6; off <= hlast && off < 600; off += 6)
            one(ReadMacInt32(h + off), true);
    }
    out += "]";
}

/* A popup control's menu items, if its data is PopupPrivateData. */
bool popup_items(uint32_t ctl, std::string &out)
{
    uint32_t data = deref(ReadMacInt32(ctl + 28));
    if (!data || !valid(data, 6)) return false;
    uint32_t mh = ReadMacInt32(data);
    int16_t mid = ReadMacInt16(data + 4);
    uint32_t menu = deref(mh);
    if (!menu || !valid(menu, 16) || (int16_t)ReadMacInt16(menu) != mid) return false;
    uint32_t p = menu + 14;
    (void)p;
    out += ",\"menu\":";
    menu_items(menu, out);
    return true;
}

void controls(uint32_t wp, int dx, int dy, std::string &out)
{
    out += ",\"controls\":[";
    uint32_t ch = ReadMacInt32(wp + 140);
    for (int i = 0; ch && i < 200; i++) {
        uint32_t c = deref(ch);
        if (!c || !valid(c, 41)) break;
        if (i) out += ',';
        out += "{\"title\":";
        str(out, pstring(c + 40));
        out += ",\"rect\":";
        rect(out, rect_at(c + 8), dx, dy);
        char b[128];
        Rect r = rect_at(c + 8);
        snprintf(b, sizeof b, ",\"visible\":%s,\"enabled\":%s,\"value\":%d,\"min\":%d,\"max\":%d",
                 ReadMacInt8(c + 16) && r.left + dx < 8192 ? "true" : "false", ReadMacInt8(c + 17) == 255 ? "false" : "true",
                 (int16_t)ReadMacInt16(c + 18), (int16_t)ReadMacInt16(c + 20), (int16_t)ReadMacInt16(c + 22));
        out += b;
        popup_items(c, out);
        out += '}';
        ch = ReadMacInt32(c);
    }
    out += ']';
}

const char *item_kind(int type)
{
    switch (type & 0x7F) {
    case 0:  return "user";
    case 4:  return "button";
    case 5:  return "checkbox";
    case 6:  return "radio";
    case 7:  return "control";
    case 8:  return "text";
    case 16: return "edit";
    case 32: return "icon";
    case 64: return "picture";
    default: return "other";
    }
}

void dialog_items(uint32_t wp, int dx, int dy, std::string &out)
{
    uint32_t list = deref(ReadMacInt32(wp + 156));
    if (!list || !valid(list, 2)) return;
    int count = (int16_t)ReadMacInt16(list) + 1;
    if (count <= 0 || count > 256) return;
    out += ",\"items\":[";
    uint32_t p = list + 2;
    for (int i = 0; i < count && valid(p, 14); i++) {
        uint32_t h = ReadMacInt32(p);
        Rect r = rect_at(p + 4);
        int type = ReadMacInt8(p + 12);
        int len = ReadMacInt8(p + 13);
        if (i) out += ',';
        char b[96];
        snprintf(b, sizeof b, "{\"n\":%d,\"type\":\"%s\",\"visible\":%s,\"enabled\":%s,\"rect\":", i + 1,
                 item_kind(type), r.left + dx < 8192 ? "true" : "false", (type & 0x80) ? "false" : "true");
        out += b;
        rect(out, r, dx, dy);
        int t = type & 0x7F;
        if (t >= 4 && t <= 7) {                 /* a control: its title, value */
            uint32_t c = deref(h);
            std::string title = c && valid(c, 41) ? pstring(c + 40) : bytes(p + 14, len);
            out += ",\"text\":";
            str(out, title);
            if (c && valid(c, 24)) {
                snprintf(b, sizeof b, ",\"value\":%d,\"enabled\":%s", (int16_t)ReadMacInt16(c + 18),
                         ReadMacInt8(c + 17) == 255 ? "false" : "true");
                out += b;
            }
        } else if (t == 8 || t == 16) {         /* text: the item's own copy */
            out += ",\"text\":";
            str(out, bytes(p + 14, len));
        }
        out += '}';
        p += 14 + len + (len & 1);
    }
    out += ']';
}

void windows(uint32_t wp, int depth, int &n, bool &has_front, std::string &out)
{
    for (int i = 0; wp && valid(wp, 160) && i < 32 && n < 64; i++) {
        int16_t kind = ReadMacInt16(wp + 108);
        if (kind == -32767) {                    /* a Layer Manager layer: its windows */
            if (depth < 3) windows(ReadMacInt32(wp + 140), depth + 1, n, has_front, out);
            wp = ReadMacInt32(wp + 144);
            continue;
        }
        if (n++) out += ',';
        int dx, dy;
        origin(wp, dx, dy);
        bool visible = ReadMacInt8(wp + 110), hilited = ReadMacInt8(wp + 111);
        if (visible && hilited) has_front = true;
        out += "{\"title\":";
        str(out, pstring(deref(ReadMacInt32(wp + 134))));
        char b[112];
        snprintf(b, sizeof b, ",\"kind\":%d,\"visible\":%s,\"front\":%s,\"rect\":", kind,
                 visible ? "true" : "false", visible && hilited ? "true" : "false");
        out += b;
        rect(out, rect_at(wp + 16), dx, dy);
        controls(wp, dx, dy, out);
        if (kind == 2)                                   /* dialogKind */
            dialog_items(wp, dx, dy, out);
        out += '}';
        wp = ReadMacInt32(wp + 144);
    }
}

}  // namespace

/* The current process's windows (System 7's WindowList is per process: the
 * Process Manager switches it with the running application). *has_front:
 * the list holds the highlighted window, i.e. it is the front application's. */
std::string guest_ui_json(bool *has_front)
{
    char app[33] = "";
    int n = valid(0x910, 32) ? ReadMacInt8(0x910) : 0;     /* CurApName */
    for (int i = 0; i < n && i < 31; i++) app[i] = static_cast<char>(ReadMacInt8(0x911 + i));
    app[n < 31 ? n : 31] = 0;
    std::string out = "{\"app\":";
    str(out, app);
    out += ",\"windows\":[";
    bool front = false;
    int count = 0;
    windows(valid(0x9D6, 4) ? ReadMacInt32(0x9D6) : 0, 0, count, front, out);
    out += "]";
    menus(out);
    out += "}";
    if (has_front) *has_front = front;
    return out;
}
