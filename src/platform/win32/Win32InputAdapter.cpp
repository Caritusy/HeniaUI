#include "henia/ui/platform/win32/Win32InputAdapter.h"

#include <windowsx.h>

namespace henia::ui {
namespace {

[[nodiscard]] PointerButton pointerButton(UINT message) noexcept {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            return PointerButton::Primary;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return PointerButton::Secondary;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return PointerButton::Middle;
        default:
            return PointerButton::None;
    }
}

} // namespace

Win32InputAdapter::Win32InputAdapter(UiDocument& document) noexcept : mDocument(&document) {}

bool Win32InputAdapter::handleMessage(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
            static_cast<void>(TrackMouseEvent(&tracking));
            return mDocument->dispatch(makePointerEvent(
                InputEventKind::PointerMove, window, message, wParam, lParam));
        }
        case WM_MOUSELEAVE:
            return mDocument->dispatch({
                .kind = InputEventKind::PointerMove,
                .position = {-1.0F, -1.0F},
            });
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            return mDocument->dispatch(makePointerEvent(
                InputEventKind::PointerDown, window, message, wParam, lParam));
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            return mDocument->dispatch(makePointerEvent(
                InputEventKind::PointerUp, window, message, wParam, lParam));
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return mDocument->dispatch(makePointerEvent(
                InputEventKind::PointerScroll, window, message, wParam, lParam));
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            return mDocument->dispatch(makeKeyEvent(InputEventKind::KeyDown, wParam, lParam));
        case WM_KEYUP:
        case WM_SYSKEYUP:
            return mDocument->dispatch(makeKeyEvent(InputEventKind::KeyUp, wParam, lParam));
        case WM_CHAR: {
            const char16_t unit = static_cast<char16_t>(wParam);
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                mHighSurrogate = unit;
                return true;
            }
            char32_t codepoint = unit;
            if (unit >= 0xDC00 && unit <= 0xDFFF && mHighSurrogate != u'\0') {
                codepoint = 0x10000U
                    + ((static_cast<char32_t>(mHighSurrogate) - 0xD800U) << 10U)
                    + (static_cast<char32_t>(unit) - 0xDC00U);
            }
            mHighSurrogate = u'\0';
            InputEvent event{.kind = InputEventKind::TextInput, .text = codepoint};
            addModifiers(event);
            return mDocument->dispatch(event);
        }
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR) {
                return true;
            }
            return mDocument->dispatch({
                .kind = InputEventKind::TextInput,
                .text = static_cast<char32_t>(wParam),
            });
        case WM_KILLFOCUS:
            mHighSurrogate = u'\0';
            return mDocument->dispatch({.kind = InputEventKind::FocusLost});
        default:
            return false;
    }
}

InputEvent Win32InputAdapter::makePointerEvent(
    InputEventKind kind, HWND window, UINT message, WPARAM wParam, LPARAM lParam) const noexcept {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        static_cast<void>(ScreenToClient(window, &point));
    }
    InputEvent event{
        .kind = kind,
        .position = {static_cast<float>(point.x), static_cast<float>(point.y)},
        .button = pointerButton(message),
    };
    if (message == WM_MOUSEWHEEL) {
        event.scrollY = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
    } else if (message == WM_MOUSEHWHEEL) {
        event.scrollX = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
    }
    addModifiers(event);
    return event;
}

InputEvent Win32InputAdapter::makeKeyEvent(InputEventKind kind, WPARAM wParam, LPARAM lParam) const noexcept {
    InputEvent event{
        .kind = kind,
        .key = translateKey(wParam),
        .repeated = (static_cast<unsigned long long>(lParam) & (1ULL << 30U)) != 0,
    };
    addModifiers(event);
    return event;
}

KeyCode Win32InputAdapter::translateKey(WPARAM key) noexcept {
    switch (key) {
        case VK_BACK: return KeyCode::Backspace;
        case VK_DELETE: return KeyCode::Delete;
        case VK_LEFT: return KeyCode::Left;
        case VK_RIGHT: return KeyCode::Right;
        case VK_UP: return KeyCode::Up;
        case VK_DOWN: return KeyCode::Down;
        case VK_HOME: return KeyCode::Home;
        case VK_END: return KeyCode::End;
        case VK_RETURN: return KeyCode::Enter;
        case VK_ESCAPE: return KeyCode::Escape;
        case VK_TAB: return KeyCode::Tab;
        default: return KeyCode::Unknown;
    }
}

void Win32InputAdapter::addModifiers(InputEvent& event) noexcept {
    event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    event.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
}

} // namespace henia::ui
