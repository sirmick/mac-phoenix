/*
 * keys.cpp - Qt key events to Mac key codes (keys.h). The Mac codes are
 * keys.js's (EVENT_CODE_TO_MAC), each physical key's.
 */
#include "keys.h"

#include <QGuiApplication>
#include <QKeyEvent>

#include <unordered_map>

namespace keys {

namespace {

enum class Mod { None, Ctrl, Alt, Meta };

/* Command, Option, Control for Ctrl, Alt and Meta (keys.js presets) */
int mod_ctrl = MAC_CONTROL, mod_alt = MAC_OPTION, mod_meta = MAC_COMMAND;

int modifier(Mod m)
{
    switch(m)
    {
        case Mod::Ctrl: return mod_ctrl;
        case Mod::Alt: return mod_alt;
        case Mod::Meta: return mod_meta;
        default: return -1;
    }
}

/* evdev (linux/input-event-codes.h) key codes, which are the PC's set 1
   scan codes for 1..88 too: the Mac's. */
const std::unordered_map<int, int> &evdev()
{
    static const std::unordered_map<int, int> m = {
        { 1, 0x35 },                                                      // Escape
        { 2, 0x12 }, { 3, 0x13 }, { 4, 0x14 }, { 5, 0x15 }, { 6, 0x17 },  // 1..5
        { 7, 0x16 }, { 8, 0x1A }, { 9, 0x1C }, { 10, 0x19 }, { 11, 0x1D },// 6..0
        { 12, 0x1B }, { 13, 0x18 }, { 14, 0x33 }, { 15, 0x30 },           // - = Backspace Tab
        { 16, 0x0C }, { 17, 0x0D }, { 18, 0x0E }, { 19, 0x0F }, { 20, 0x11 }, // Q W E R T
        { 21, 0x10 }, { 22, 0x20 }, { 23, 0x22 }, { 24, 0x1F }, { 25, 0x23 }, // Y U I O P
        { 26, 0x21 }, { 27, 0x1E }, { 28, 0x24 },                          // [ ] Enter
        { 30, 0x00 }, { 31, 0x01 }, { 32, 0x02 }, { 33, 0x03 }, { 34, 0x05 }, // A S D F G
        { 35, 0x04 }, { 36, 0x26 }, { 37, 0x28 }, { 38, 0x25 },            // H J K L
        { 39, 0x29 }, { 40, 0x27 }, { 41, 0x32 }, { 42, 0x38 }, { 43, 0x2A }, // ; ' ` LShift backslash
        { 44, 0x06 }, { 45, 0x07 }, { 46, 0x08 }, { 47, 0x09 }, { 48, 0x0B }, // Z X C V B
        { 49, 0x2D }, { 50, 0x2E }, { 51, 0x2B }, { 52, 0x2F }, { 53, 0x2C }, // N M , . /
        { 54, 0x38 }, { 55, 0x43 }, { 57, 0x31 }, { 58, 0x39 },            // RShift KP* Space CapsLock
        { 59, 0x7A }, { 60, 0x78 }, { 61, 0x63 }, { 62, 0x76 }, { 63, 0x60 }, // F1..F5
        { 64, 0x61 }, { 65, 0x62 }, { 66, 0x64 }, { 67, 0x65 }, { 68, 0x6D }, // F6..F10
        { 69, 0x47 }, { 70, 0x6B },                                        // NumLock (Clear) ScrollLock (F14)
        { 71, 0x59 }, { 72, 0x5B }, { 73, 0x5C }, { 74, 0x4E },            // KP7 KP8 KP9 KP-
        { 75, 0x56 }, { 76, 0x57 }, { 77, 0x58 }, { 78, 0x45 },            // KP4 KP5 KP6 KP+
        { 79, 0x53 }, { 80, 0x54 }, { 81, 0x55 }, { 82, 0x52 }, { 83, 0x41 }, // KP1 KP2 KP3 KP0 KP.
        { 86, 0x0A }, { 87, 0x67 }, { 88, 0x6F }, { 89, 0x5E },            // IntlBackslash F11 F12 Ro
        { 96, 0x4C }, { 98, 0x4B }, { 99, 0x69 },                          // KPEnter KP/ PrintScreen (F13)
        { 102, 0x73 }, { 103, 0x3E }, { 104, 0x74 }, { 105, 0x3B },        // Home Up PageUp Left
        { 106, 0x3C }, { 107, 0x77 }, { 108, 0x3D }, { 109, 0x79 },        // Right End Down PageDown
        { 110, 0x72 }, { 111, 0x75 }, { 117, 0x51 }, { 119, 0x71 },        // Insert (Help) Delete KP= Pause (F15)
        { 124, 0x5D },                                                     // Yen
        { 183, 0x69 }, { 184, 0x6B }, { 185, 0x71 },                       // F13 F14 F15
    };
    return m;
}

Mod evdev_modifier(int code)
{
    switch(code)
    {
        case 29: case 97: return Mod::Ctrl;
        case 56: case 100: return Mod::Alt;
        case 125: case 126: return Mod::Meta;
        default: return Mod::None;
    }
}

/* Windows: set 1, the E0-prefixed keys with 0x100 (Qt's nativeScanCode) */
int windows_keycode(int sc)
{
    switch(sc)
    {
        case 0x11D: return modifier(Mod::Ctrl);
        case 0x138: return modifier(Mod::Alt);
        case 0x15B: case 0x15C: return modifier(Mod::Meta);
        case 0x11C: return 0x4C;  // KPEnter
        case 0x135: return 0x4B;  // KP/
        case 0x137: return 0x69;  // PrintScreen
        case 0x147: return 0x73;  // Home
        case 0x148: return 0x3E;  // Up
        case 0x149: return 0x74;  // PageUp
        case 0x14B: return 0x3B;  // Left
        case 0x14D: return 0x3C;  // Right
        case 0x14F: return 0x77;  // End
        case 0x150: return 0x3D;  // Down
        case 0x151: return 0x79;  // PageDown
        case 0x152: return 0x72;  // Insert
        case 0x153: return 0x75;  // Delete
        case 0x1D: return modifier(Mod::Ctrl);
        case 0x38: return modifier(Mod::Alt);
    }
    auto it = evdev().find(sc);
    return it == evdev().end() ? -1 : it->second;
}

/* macOS: virtual key codes are the ADB codes but for these */
int macos_keycode(int vk)
{
    switch(vk)
    {
        case 0x36: return MAC_COMMAND;  // right Command
        case 0x3C: return MAC_SHIFT;    // right Shift
        case 0x3D: return MAC_OPTION;   // right Option
        case 0x3B: case 0x3E: return MAC_CONTROL;
        case 0x7B: return 0x3B;         // arrows
        case 0x7C: return 0x3C;
        case 0x7D: return 0x3D;
        case 0x7E: return 0x3E;
    }
    return vk < 0x80 ? vk : -1;
}

/* No scan code (offscreen, a platform plugin without them): by Qt::Key,
   as a US keyboard has them. */
int qt_keycode(const QKeyEvent *e)
{
    static const std::unordered_map<int, int> m = {
        { Qt::Key_A, 0x00 }, { Qt::Key_B, 0x0B }, { Qt::Key_C, 0x08 }, { Qt::Key_D, 0x02 },
        { Qt::Key_E, 0x0E }, { Qt::Key_F, 0x03 }, { Qt::Key_G, 0x05 }, { Qt::Key_H, 0x04 },
        { Qt::Key_I, 0x22 }, { Qt::Key_J, 0x26 }, { Qt::Key_K, 0x28 }, { Qt::Key_L, 0x25 },
        { Qt::Key_M, 0x2E }, { Qt::Key_N, 0x2D }, { Qt::Key_O, 0x1F }, { Qt::Key_P, 0x23 },
        { Qt::Key_Q, 0x0C }, { Qt::Key_R, 0x0F }, { Qt::Key_S, 0x01 }, { Qt::Key_T, 0x11 },
        { Qt::Key_U, 0x20 }, { Qt::Key_V, 0x09 }, { Qt::Key_W, 0x0D }, { Qt::Key_X, 0x07 },
        { Qt::Key_Y, 0x10 }, { Qt::Key_Z, 0x06 },
        { Qt::Key_0, 0x1D }, { Qt::Key_1, 0x12 }, { Qt::Key_2, 0x13 }, { Qt::Key_3, 0x14 },
        { Qt::Key_4, 0x15 }, { Qt::Key_5, 0x17 }, { Qt::Key_6, 0x16 }, { Qt::Key_7, 0x1A },
        { Qt::Key_8, 0x1C }, { Qt::Key_9, 0x19 },
        { Qt::Key_Minus, 0x1B }, { Qt::Key_Equal, 0x18 }, { Qt::Key_BracketLeft, 0x21 },
        { Qt::Key_BracketRight, 0x1E }, { Qt::Key_Backslash, 0x2A }, { Qt::Key_Semicolon, 0x29 },
        { Qt::Key_Apostrophe, 0x27 }, { Qt::Key_Comma, 0x2B }, { Qt::Key_Period, 0x2F },
        { Qt::Key_Slash, 0x2C }, { Qt::Key_QuoteLeft, 0x32 },
        { Qt::Key_Space, 0x31 }, { Qt::Key_Tab, 0x30 }, { Qt::Key_Return, 0x24 },
        { Qt::Key_Enter, 0x4C }, { Qt::Key_Escape, 0x35 }, { Qt::Key_Backspace, 0x33 },
        { Qt::Key_Delete, 0x75 }, { Qt::Key_CapsLock, 0x39 }, { Qt::Key_Shift, MAC_SHIFT },
        { Qt::Key_Insert, 0x72 }, { Qt::Key_Home, 0x73 }, { Qt::Key_End, 0x77 },
        { Qt::Key_PageUp, 0x74 }, { Qt::Key_PageDown, 0x79 },
        { Qt::Key_Left, 0x3B }, { Qt::Key_Right, 0x3C }, { Qt::Key_Down, 0x3D }, { Qt::Key_Up, 0x3E },
        { Qt::Key_F1, 0x7A }, { Qt::Key_F2, 0x78 }, { Qt::Key_F3, 0x63 }, { Qt::Key_F4, 0x76 },
        { Qt::Key_F5, 0x60 }, { Qt::Key_F6, 0x61 }, { Qt::Key_F7, 0x62 }, { Qt::Key_F8, 0x64 },
        { Qt::Key_F9, 0x65 }, { Qt::Key_F10, 0x6D }, { Qt::Key_F11, 0x67 }, { Qt::Key_F12, 0x6F },
    };
    switch(e->key())
    {
        case Qt::Key_Control: return modifier(Mod::Ctrl);
        case Qt::Key_Alt: return modifier(Mod::Alt);
        case Qt::Key_Meta: return modifier(Mod::Meta);
    }
    auto it = m.find(e->key());
    return it == m.end() ? -1 : it->second;
}

}

void set_command_key(const QString &which)
{
    if(which == "ctrl")
        mod_ctrl = MAC_COMMAND, mod_alt = MAC_OPTION, mod_meta = MAC_CONTROL;
    else if(which == "alt")
        mod_ctrl = MAC_CONTROL, mod_alt = MAC_COMMAND, mod_meta = MAC_OPTION;
    else
        mod_ctrl = MAC_CONTROL, mod_alt = MAC_OPTION, mod_meta = MAC_COMMAND;
}

int mac_keycode(const QKeyEvent *e)
{
#if defined(Q_OS_MACOS)
    if(e->nativeVirtualKey())
        return macos_keycode((int)e->nativeVirtualKey());
#elif defined(Q_OS_WIN)
    if(e->nativeScanCode())
        return windows_keycode((int)e->nativeScanCode());
#else
    static const bool evdev_codes = QGuiApplication::platformName() == "xcb"
                                    || QGuiApplication::platformName().startsWith("wayland");
    if(evdev_codes && e->nativeScanCode() > 8)
    {
        int code = (int)e->nativeScanCode() - 8;
        Mod m = evdev_modifier(code);
        if(m != Mod::None)
            return modifier(m);
        auto it = evdev().find(code);
        return it == evdev().end() ? -1 : it->second;
    }
#endif
    (void)macos_keycode;
    (void)windows_keycode;
    return qt_keycode(e);
}

}  // namespace keys
