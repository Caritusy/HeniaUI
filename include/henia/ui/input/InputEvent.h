#pragma once

#include "henia/ui/Types.h"

#include <cstdint>

namespace henia::ui {

enum class InputEventKind : std::uint8_t {
    PointerMove,
    PointerDown,
    PointerUp,
    PointerScroll,
    KeyDown,
    KeyUp,
    TextInput,
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
};

struct InputEvent final {
    InputEventKind kind = InputEventKind::PointerMove;
    Vec2 position{};
    PointerButton button = PointerButton::None;
    float scrollX = 0.0F;
    float scrollY = 0.0F;
    KeyCode key = KeyCode::Unknown;
    char32_t text = U'\0';
    bool repeated = false;
    bool shift = false;
    bool control = false;
    bool alt = false;
};

} // namespace henia::ui
