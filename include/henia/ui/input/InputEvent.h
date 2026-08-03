#pragma once

#include "henia/ui/Types.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace henia::ui {

enum class InputEventKind : std::uint8_t {
    PointerMove,
    PointerDown,
    PointerUp,
    PointerScroll,
    KeyDown,
    KeyUp,
    TextInput,
    CompositionStart,
    CompositionUpdate,
    CompositionCommit,
    CompositionCancel,
    FocusLost,
    PointerCancel,
};

enum class PointerButton : std::uint8_t {
    None,
    Primary,
    Secondary,
    Middle,
};

enum class KeyCode : std::uint16_t {
    Unknown,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Enter,
    Escape,
    Tab,
    Space,
    Insert,
    PageUp,
    PageDown,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Shift,
    Control,
    Alt,
    CapsLock,
    NumLock,
    ScrollLock,
    PrintScreen,
    Pause,
    LeftSuper,
    RightSuper,
    Menu,
    Semicolon,
    Equal,
    Comma,
    Minus,
    Period,
    Slash,
    Backtick,
    LeftBracket,
    Backslash,
    RightBracket,
    Apostrophe,
    IntlBackslash,
    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    NumpadMultiply,
    NumpadAdd,
    NumpadSeparator,
    NumpadSubtract,
    NumpadDecimal,
    NumpadDivide,
    NumpadEnter,
};

struct InputEvent final {
    InputEventKind kind = InputEventKind::PointerMove;
    Vec2 position{};
    PointerButton button = PointerButton::None;
    float scrollX = 0.0F;
    float scrollY = 0.0F;
    KeyCode key = KeyCode::Unknown;
    // TextInput carries committed Unicode text, not editing/navigation control
    // characters. Backspace, Tab, Enter, Insert, and similar keys use `key`.
    char32_t text = U'\0';
    // Synchronous UTF-8 payload for committed multi-codepoint input or IME
    // preedit text. The adapter-owned view is valid only during dispatch.
    std::string_view textUtf8{};
    std::size_t compositionSelectionStart = 0;
    std::size_t compositionSelectionLength = 0;
    bool repeated = false;
    bool shift = false;
    bool control = false;
    bool alt = false;
};

} // namespace henia::ui
