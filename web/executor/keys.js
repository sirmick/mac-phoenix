// W3C KeyboardEvent.code -> classic Mac ADB key code; the table is
// client/client.js's, with Ctrl/Alt/Meta at its defaults (Control,
// Option, Command).
'use strict';

const EVENT_CODE_TO_MAC = Object.freeze({
    // Letters
    KeyA: 0x00, KeyB: 0x0B, KeyC: 0x08, KeyD: 0x02, KeyE: 0x0E,
    KeyF: 0x03, KeyG: 0x05, KeyH: 0x04, KeyI: 0x22, KeyJ: 0x26,
    KeyK: 0x28, KeyL: 0x25, KeyM: 0x2E, KeyN: 0x2D, KeyO: 0x1F,
    KeyP: 0x23, KeyQ: 0x0C, KeyR: 0x0F, KeyS: 0x01, KeyT: 0x11,
    KeyU: 0x20, KeyV: 0x09, KeyW: 0x0D, KeyX: 0x07, KeyY: 0x10,
    KeyZ: 0x06,
    // Digits
    Digit0: 0x1D, Digit1: 0x12, Digit2: 0x13, Digit3: 0x14, Digit4: 0x15,
    Digit5: 0x17, Digit6: 0x16, Digit7: 0x1A, Digit8: 0x1C, Digit9: 0x19,
    // Punctuation
    Backquote: 0x32, Minus: 0x1B, Equal: 0x18,
    BracketLeft: 0x21, BracketRight: 0x1E, Backslash: 0x2A,
    Semicolon: 0x29, Quote: 0x27, Comma: 0x2B, Period: 0x2F, Slash: 0x2C,
    IntlBackslash: 0x0A, IntlYen: 0x5D, IntlRo: 0x5E,
    // Whitespace & control
    Space: 0x31, Tab: 0x30, Enter: 0x24, Escape: 0x35,
    Backspace: 0x33, Delete: 0x75, CapsLock: 0x39,
    // Shift is the only modifier in this static table — it's never user-
    // remapped (Mac Shift IS the only sensible target). Ctrl / Alt / Meta
    // are owned by `modifierOverride` (built from keyboardConfig) so the
    // user can change them in Settings → Keyboard at runtime.
    ShiftLeft: 0x38, ShiftRight: 0x38,
    // Editing & navigation
    Insert: 0x72,                                     // Mac Help
    Home: 0x73, End: 0x77, PageUp: 0x74, PageDown: 0x79,
    ArrowLeft: 0x3B, ArrowRight: 0x3C,
    ArrowDown: 0x3D, ArrowUp: 0x3E,
    // Function row (full Apple Extended Keyboard II range)
    F1: 0x7A, F2: 0x78, F3: 0x63, F4: 0x76, F5: 0x60,
    F6: 0x61, F7: 0x62, F8: 0x64, F9: 0x65, F10: 0x6D,
    F11: 0x67, F12: 0x6F, F13: 0x69, F14: 0x6B, F15: 0x71,
    // PC keys positionally aligned with Mac F13/F14/F15
    PrintScreen: 0x69, ScrollLock: 0x6B, Pause: 0x71,
    // Numpad
    NumLock: 0x47,                                    // Mac Clear
    NumpadDivide: 0x4B, NumpadMultiply: 0x43,
    NumpadSubtract: 0x4E, NumpadAdd: 0x45,
    NumpadEnter: 0x4C, NumpadDecimal: 0x41, NumpadEqual: 0x51,
    Numpad0: 0x52, Numpad1: 0x53, Numpad2: 0x54, Numpad3: 0x55,
    Numpad4: 0x56, Numpad5: 0x57, Numpad6: 0x58, Numpad7: 0x59,
    Numpad8: 0x5B, Numpad9: 0x5C,
});

const MODIFIERS = Object.freeze({
    ControlLeft: 0x36, ControlRight: 0x36,
    AltLeft: 0x3A, AltRight: 0x3A,
    MetaLeft: 0x37, MetaRight: 0x37,
});

function macKeycodeForEvent(e) {
    if (e.code in MODIFIERS)
        return MODIFIERS[e.code];
    if (e.code in EVENT_CODE_TO_MAC)
        return EVENT_CODE_TO_MAC[e.code];
    return null;
}
