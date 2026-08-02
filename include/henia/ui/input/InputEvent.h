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
    A,
    C,
    V,
    X,
    Y,
    Z,
};

struct InputEvent final {
    InputEventKind kind = InputEventKind::PointerMove;
    Vec2 position{};
    PointerButton button = PointerButton::None;
    float scrollX = 0.0F;
    float scrollY = 0.0F;
    KeyCode key = KeyCode::Unknown;
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
