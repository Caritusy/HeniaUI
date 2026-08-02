#include "henia/ui/platform/win32/Win32InputAdapter.h"

#include <windowsx.h>

namespace henia::ui {
namespace {

constexpr char32_t kReplacementCharacter = U'\uFFFD';

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
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
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
        case WM_MBUTTONDOWN: {
            const PressedButtonMask mask = buttonMask(message);
            mPressedButtons = static_cast<PressedButtonMask>(mPressedButtons | mask);
            bool handled = false;
            try {
                handled = mDocument->dispatch(makePointerEvent(
                    InputEventKind::PointerDown, window, message, wParam, lParam));
            } catch (...) {
                mPressedButtons = 0;
                releaseNativeCapture();
                throw;
            }
            if (handled && !acquireNativeCapture(window)) {
                mPressedButtons = 0;
                static_cast<void>(mDocument->dispatch({.kind = InputEventKind::PointerCancel}));
            }
            return handled;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            const PressedButtonMask mask = buttonMask(message);
            if ((mPressedButtons & mask) == 0) {
                return false;
            }
            bool handled = false;
            try {
                handled = mDocument->dispatch(makePointerEvent(
                    InputEventKind::PointerUp, window, message, wParam, lParam));
            } catch (...) {
                mPressedButtons = 0;
                releaseNativeCapture();
                throw;
            }
            mPressedButtons = static_cast<PressedButtonMask>(mPressedButtons & ~mask);
            if (mPressedButtons == 0) {
                releaseNativeCapture();
            }
            return handled;
        }
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
        case WM_CHAR:
            return handleUtf16Unit(static_cast<char16_t>(wParam));
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR) {
                return true;
            }
            return handleUnicodeScalar(static_cast<std::uint64_t>(wParam));
        case WM_CAPTURECHANGED: {
            if (mReleasingNativeCapture
                || reinterpret_cast<HWND>(lParam) == window) {
                return false;
            }
            mCaptureWindow = nullptr;
            mPressedButtons = 0;
            static_cast<void>(mDocument->dispatch({.kind = InputEventKind::PointerCancel}));
            return false;
        }
        case WM_CANCELMODE:
            cancelInteraction(false);
            return false;
        case WM_KILLFOCUS:
            mHighSurrogate = u'\0';
            cancelInteraction(true);
            return false;
        case WM_DESTROY:
        case WM_NCDESTROY:
            mHighSurrogate = u'\0';
            cancelInteraction(true);
            return false;
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

bool Win32InputAdapter::handleUtf16Unit(char16_t unit) {
    const bool highSurrogate = unit >= 0xD800 && unit <= 0xDBFF;
    const bool lowSurrogate = unit >= 0xDC00 && unit <= 0xDFFF;
    if (highSurrogate) {
        const bool hadUnpairedHigh = mHighSurrogate != u'\0';
        mHighSurrogate = unit;
        if (hadUnpairedHigh) {
            static_cast<void>(dispatchText(kReplacementCharacter));
        }
        return true;
    }

    if (lowSurrogate) {
        if (mHighSurrogate == u'\0') {
            return dispatchText(kReplacementCharacter);
        }
        const char16_t high = mHighSurrogate;
        mHighSurrogate = u'\0';
        const char32_t scalar = 0x10000U
            + ((static_cast<char32_t>(high) - 0xD800U) << 10U)
            + (static_cast<char32_t>(unit) - 0xDC00U);
        return dispatchText(scalar);
    }

    bool handled = false;
    if (mHighSurrogate != u'\0') {
        mHighSurrogate = u'\0';
        handled = dispatchText(kReplacementCharacter);
    }
    return dispatchText(static_cast<char32_t>(unit)) || handled;
}

bool Win32InputAdapter::handleUnicodeScalar(std::uint64_t value) {
    bool handled = false;
    if (mHighSurrogate != u'\0') {
        mHighSurrogate = u'\0';
        handled = dispatchText(kReplacementCharacter);
    }
    const char32_t scalar = validUnicodeScalar(value)
        ? static_cast<char32_t>(value)
        : kReplacementCharacter;
    return dispatchText(scalar) || handled;
}

bool Win32InputAdapter::dispatchText(char32_t value) {
    InputEvent event{.kind = InputEventKind::TextInput, .text = value};
    addModifiers(event);
    return mDocument->dispatch(event);
}

bool Win32InputAdapter::acquireNativeCapture(HWND window) noexcept {
    if (GetCapture() != window) {
        static_cast<void>(SetCapture(window));
    }
    if (GetCapture() != window) {
        mCaptureWindow = nullptr;
        return false;
    }
    mCaptureWindow = window;
    return true;
}

void Win32InputAdapter::releaseNativeCapture() noexcept {
    const HWND captureWindow = mCaptureWindow;
    mCaptureWindow = nullptr;
    if (captureWindow == nullptr || GetCapture() != captureWindow) {
        return;
    }
    mReleasingNativeCapture = true;
    const bool released = ReleaseCapture() != FALSE;
    mReleasingNativeCapture = false;
    if (!released && GetCapture() == captureWindow) {
        mCaptureWindow = captureWindow;
    }
}

void Win32InputAdapter::cancelInteraction(bool loseFocus) {
    mPressedButtons = 0;
    releaseNativeCapture();
    static_cast<void>(mDocument->dispatch({
        .kind = loseFocus ? InputEventKind::FocusLost : InputEventKind::PointerCancel,
    }));
}

Win32InputAdapter::PressedButtonMask Win32InputAdapter::buttonMask(UINT message) noexcept {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            return 1U << 0U;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            return 1U << 1U;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            return 1U << 2U;
        default:
            return 0;
    }
}

bool Win32InputAdapter::validUnicodeScalar(std::uint64_t value) noexcept {
    return value <= 0x10FFFFU && !(value >= 0xD800U && value <= 0xDFFFU);
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
