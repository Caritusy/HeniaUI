#include "henia/ui/widget/controls/KeyBindingEditor.h"

#include <algorithm>

namespace henia::ui {

std::string_view keyCodeName(KeyCode key) noexcept {
    switch (key) {
        case KeyCode::Unknown: return "Unbound";
        case KeyCode::Backspace: return "Backspace";
        case KeyCode::Delete: return "Delete";
        case KeyCode::Left: return "Left";
        case KeyCode::Right: return "Right";
        case KeyCode::Up: return "Up";
        case KeyCode::Down: return "Down";
        case KeyCode::Home: return "Home";
        case KeyCode::End: return "End";
        case KeyCode::Enter: return "Enter";
        case KeyCode::Escape: return "Escape";
        case KeyCode::Tab: return "Tab";
        case KeyCode::Space: return "Space";
        case KeyCode::Insert: return "Insert";
        case KeyCode::PageUp: return "Page Up";
        case KeyCode::PageDown: return "Page Down";
        case KeyCode::Digit0: return "0"; case KeyCode::Digit1: return "1";
        case KeyCode::Digit2: return "2"; case KeyCode::Digit3: return "3";
        case KeyCode::Digit4: return "4"; case KeyCode::Digit5: return "5";
        case KeyCode::Digit6: return "6"; case KeyCode::Digit7: return "7";
        case KeyCode::Digit8: return "8"; case KeyCode::Digit9: return "9";
        case KeyCode::A: return "A"; case KeyCode::B: return "B";
        case KeyCode::C: return "C"; case KeyCode::D: return "D";
        case KeyCode::E: return "E"; case KeyCode::F: return "F";
        case KeyCode::G: return "G"; case KeyCode::H: return "H";
        case KeyCode::I: return "I"; case KeyCode::J: return "J";
        case KeyCode::K: return "K"; case KeyCode::L: return "L";
        case KeyCode::M: return "M"; case KeyCode::N: return "N";
        case KeyCode::O: return "O"; case KeyCode::P: return "P";
        case KeyCode::Q: return "Q"; case KeyCode::R: return "R";
        case KeyCode::S: return "S"; case KeyCode::T: return "T";
        case KeyCode::U: return "U"; case KeyCode::V: return "V";
        case KeyCode::W: return "W"; case KeyCode::X: return "X";
        case KeyCode::Y: return "Y"; case KeyCode::Z: return "Z";
        case KeyCode::F1: return "F1"; case KeyCode::F2: return "F2";
        case KeyCode::F3: return "F3"; case KeyCode::F4: return "F4";
        case KeyCode::F5: return "F5"; case KeyCode::F6: return "F6";
        case KeyCode::F7: return "F7"; case KeyCode::F8: return "F8";
        case KeyCode::F9: return "F9"; case KeyCode::F10: return "F10";
        case KeyCode::F11: return "F11"; case KeyCode::F12: return "F12";
        case KeyCode::Shift: return "Shift";
        case KeyCode::Control: return "Control";
        case KeyCode::Alt: return "Alt";
        case KeyCode::CapsLock: return "Caps Lock";
        case KeyCode::NumLock: return "Num Lock";
        case KeyCode::ScrollLock: return "Scroll Lock";
        case KeyCode::PrintScreen: return "Print Screen";
        case KeyCode::Pause: return "Pause";
        case KeyCode::LeftSuper: return "Left Super";
        case KeyCode::RightSuper: return "Right Super";
        case KeyCode::Menu: return "Menu";
        case KeyCode::Semicolon: return ";";
        case KeyCode::Equal: return "=";
        case KeyCode::Comma: return ",";
        case KeyCode::Minus: return "-";
        case KeyCode::Period: return ".";
        case KeyCode::Slash: return "/";
        case KeyCode::Backtick: return "`";
        case KeyCode::LeftBracket: return "[";
        case KeyCode::Backslash: return "\\";
        case KeyCode::RightBracket: return "]";
        case KeyCode::Apostrophe: return "'";
        case KeyCode::IntlBackslash: return "Intl \\";
        case KeyCode::Numpad0: return "Numpad 0";
        case KeyCode::Numpad1: return "Numpad 1";
        case KeyCode::Numpad2: return "Numpad 2";
        case KeyCode::Numpad3: return "Numpad 3";
        case KeyCode::Numpad4: return "Numpad 4";
        case KeyCode::Numpad5: return "Numpad 5";
        case KeyCode::Numpad6: return "Numpad 6";
        case KeyCode::Numpad7: return "Numpad 7";
        case KeyCode::Numpad8: return "Numpad 8";
        case KeyCode::Numpad9: return "Numpad 9";
        case KeyCode::NumpadMultiply: return "Numpad *";
        case KeyCode::NumpadAdd: return "Numpad +";
        case KeyCode::NumpadSeparator: return "Numpad Separator";
        case KeyCode::NumpadSubtract: return "Numpad -";
        case KeyCode::NumpadDecimal: return "Numpad .";
        case KeyCode::NumpadDivide: return "Numpad /";
        case KeyCode::NumpadEnter: return "Numpad Enter";
    }
    return "Unknown";
}

KeyBindingEditor::KeyBindingEditor(KeyCode binding, KeyBindingEditorStyle style) noexcept
    : Widget(WidgetKind::KeyBindingEditor), mStyle(style), mBinding(binding) {}

void KeyBindingEditor::setBinding(KeyCode binding) noexcept {
    if (mBinding == binding) return;
    mBinding = binding;
    markPaintDirty();
}
KeyCode KeyBindingEditor::binding() const noexcept { return mBinding; }
bool KeyBindingEditor::capturing() const noexcept { return mCapturing; }
void KeyBindingEditor::setStyle(KeyBindingEditorStyle style) noexcept {
    mStyle = style;
    markLayoutDirty();
}
void KeyBindingEditor::setOnBindingChanged(Callback<KeyCode> callback) noexcept {
    mOnBindingChanged = callback;
}
bool KeyBindingEditor::acceptsPointerInput() const noexcept { return true; }
bool KeyBindingEditor::acceptsKeyboardFocus() const noexcept { return true; }
bool KeyBindingEditor::wantsTabKey() const noexcept { return mCapturing; }

bool KeyBindingEditor::handleInput(const InputEvent& event) {
    if (event.kind == InputEventKind::FocusLost) {
        if (mCapturing) { mCapturing = false; markPaintDirty(); }
        return true;
    }
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) return true;
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary && contains(event.position)) {
        beginCapture();
        return true;
    }
    if (event.kind != InputEventKind::KeyDown || !focused()) return false;
    if (!mCapturing) {
        if (event.key == KeyCode::Enter || event.key == KeyCode::Space) {
            beginCapture();
            return true;
        }
        return false;
    }
    if (event.key == KeyCode::Escape) {
        mCapturing = false;
        markPaintDirty();
        return true;
    }
    if (event.key == KeyCode::Backspace || event.key == KeyCode::Delete) {
        finishCapture(KeyCode::Unknown, true);
        return true;
    }
    if (event.key == KeyCode::Unknown) return false;
    finishCapture(event.key, true);
    return true;
}

Vec2 KeyBindingEditor::onMeasure(TextPainter&, Constraints) {
    return {mStyle.width, mStyle.height};
}

void KeyBindingEditor::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    const Color background = mCapturing ? mStyle.capture
        : (hovered() ? mStyle.hover : mStyle.background);
    canvas.fillRect(frame(), background, mStyle.radius);
    canvas.strokeRect(frame(), focused() ? mStyle.focus : mStyle.border,
        mStyle.radius, 1.0F);
    const std::string_view label = mCapturing ? std::string_view("Press a key…")
                                             : keyCodeName(mBinding);
    const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, label);
    text.draw(canvas, mStyle.font, mStyle.fontSize,
        {frame().min.x + std::max((frame().width() - metrics.width) * 0.5F, 0.0F),
         frame().min.y + std::max((frame().height() - metrics.height) * 0.5F, 0.0F)},
        mBinding == KeyCode::Unknown && !mCapturing ? mStyle.muted : mStyle.text, label);
}

void KeyBindingEditor::beginCapture() noexcept {
    if (mCapturing) return;
    mCapturing = true;
    markPaintDirty();
}

void KeyBindingEditor::finishCapture(KeyCode key, bool notify) {
    const bool changed = mBinding != key;
    mBinding = key;
    mCapturing = false;
    markPaintDirty();
    if (changed && notify) mOnBindingChanged(mBinding);
}

} // namespace henia::ui
