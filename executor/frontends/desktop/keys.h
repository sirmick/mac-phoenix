#pragma once
/*
 * keys.h - Qt key events to classic Mac (ADB) key codes, by the physical
 * key, as the tab's keys.js does from KeyboardEvent.code: the keyboard
 * layout is the Mac's to apply (its KCHR), not the host's.
 *
 *   macOS        nativeVirtualKey(): Apple's virtual key codes, the ADB
 *                codes but for the arrows and the right-hand modifiers
 *   Linux        nativeScanCode() - 8: evdev codes (xcb and Wayland)
 *   Windows      nativeScanCode(): set 1, extended keys with 0x100
 *   otherwise    (offscreen, a scan code of 0) Qt::Key, US layout
 *
 * Ctrl, Alt and Meta are Command, Option and Control as the
 * input.command_key setting says (keys.js MODIFIER_PRESETS); on macOS the
 * keys are the Mac's own.
 */
#include <QString>

class QKeyEvent;

namespace keys {

/* "meta", "ctrl" or "alt": the key that is Command */
void set_command_key(const QString &which);

/* The Mac key code, or -1: not a key the Mac has. */
int mac_keycode(const QKeyEvent *e);

constexpr int MAC_COMMAND = 0x37, MAC_SHIFT = 0x38, MAC_OPTION = 0x3A, MAC_CONTROL = 0x36;

}  // namespace keys
