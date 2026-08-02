#include "henia/ui/platform/win32/Win32InputAdapter.h"

#include "henia/ui/text/Utf8.h"

#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace henia::ui {
namespace {

constexpr char32_t kReplacementCharacter = U'\uFFFD';

template <typename Callback>
class ScopeExit final {
public:
    explicit ScopeExit(Callback callback) noexcept
        : mCallback(std::move(callback)) {}

    ~ScopeExit() {
        if (mActive) {
            mCallback();
        }
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    void dismiss() noexcept { mActive = false; }

private:
    Callback mCallback;
    bool mActive = true;
};

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
            ScopeExit failureCleanup([this]() noexcept {
                mPressedButtons = 0;
                releaseNativeCapture();
            });
            const bool handled = mDocument->dispatch(makePointerEvent(
                InputEventKind::PointerDown, window, message, wParam, lParam));
            failureCleanup.dismiss();
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
            bool dispatchCompleted = false;
            ScopeExit finishDispatch([this, mask, &dispatchCompleted]() noexcept {
                mPressedButtons = dispatchCompleted
                    ? static_cast<PressedButtonMask>(mPressedButtons & ~mask)
                    : 0;
                if (mPressedButtons == 0) {
                    releaseNativeCapture();
                }
            });
            const bool handled = mDocument->dispatch(makePointerEvent(
                InputEventKind::PointerUp, window, message, wParam, lParam));
            dispatchCompleted = true;
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
            if (mCommittedImeOffset < mCommittedImeUnits.size()) {
                const char16_t unit = static_cast<char16_t>(wParam);
                if (unit == mCommittedImeUnits[mCommittedImeOffset]) {
                    ++mCommittedImeOffset;
                    if (mCommittedImeOffset == mCommittedImeUnits.size()) {
                        mCommittedImeUnits.clear();
                        mCommittedImeOffset = 0;
                    }
                    return true;
                }
                mCommittedImeUnits.clear();
                mCommittedImeOffset = 0;
            }
            return handleUtf16Unit(static_cast<char16_t>(wParam));
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR) {
                return true;
            }
            return handleUnicodeScalar(static_cast<std::uint64_t>(wParam));
        case WM_IME_STARTCOMPOSITION:
            mImeActive = dispatchComposition(InputEventKind::CompositionStart);
            return mImeActive;
        case WM_IME_COMPOSITION:
            return handleImeComposition(window, lParam);
        case WM_IME_ENDCOMPOSITION: {
            const bool wasActive = mImeActive;
            cancelComposition();
            return wasActive;
        }
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
            cancelComposition();
            cancelInteraction(true);
            return false;
        case WM_DESTROY:
        case WM_NCDESTROY:
            mHighSurrogate = u'\0';
            cancelComposition();
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

bool Win32InputAdapter::handleImeComposition(HWND window, LPARAM flags) {
    HIMC context = ImmGetContext(window);
    if (context == nullptr) return false;
    ScopeExit release([window, context]() noexcept {
        static_cast<void>(ImmReleaseContext(window, context));
    });

    const auto read = [context](DWORD kind, std::u16string& output) {
        const LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
        if (bytes < 0 || (bytes % static_cast<LONG>(sizeof(char16_t))) != 0) return false;
        output.resize(static_cast<std::size_t>(bytes) / sizeof(char16_t));
        if (bytes == 0) return true;
        return ImmGetCompositionStringW(context, kind, output.data(), static_cast<DWORD>(bytes))
            == bytes;
    };

    bool handled = false;
    if ((flags & GCS_RESULTSTR) != 0) {
        std::u16string result;
        if (!read(GCS_RESULTSTR, result)) return false;
        const std::string utf8 = utf8FromUtf16(result);
        handled = dispatchComposition(InputEventKind::CompositionCommit, utf8);
        mImeActive = false;
        if (handled) {
            mCommittedImeUnits = std::move(result);
            mCommittedImeOffset = 0;
        }
    }
    if ((flags & GCS_COMPSTR) != 0) {
        std::u16string composition;
        if (!read(GCS_COMPSTR, composition)) return handled;
        const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
        const std::size_t unitCursor = cursor < 0
            ? 0
            : std::min<std::size_t>(static_cast<std::size_t>(cursor), composition.size());
        const std::string utf8 = utf8FromUtf16(composition);
        const std::string prefix = utf8FromUtf16(
            std::u16string_view(composition).substr(0, unitCursor));
        mImeActive = true;
        handled = dispatchComposition(
            InputEventKind::CompositionUpdate,
            utf8,
            prefix.size()) || handled;
    }
    return handled;
}

bool Win32InputAdapter::dispatchComposition(
    InputEventKind kind,
    std::string_view text,
    std::size_t selectionStart,
    std::size_t selectionLength) {
    InputEvent event{
        .kind = kind,
        .textUtf8 = text,
        .compositionSelectionStart = selectionStart,
        .compositionSelectionLength = selectionLength,
    };
    addModifiers(event);
    return mDocument->dispatch(event);
}

void Win32InputAdapter::cancelComposition() {
    if (mImeActive) {
        mImeActive = false;
        static_cast<void>(dispatchComposition(InputEventKind::CompositionCancel));
    }
    mCommittedImeUnits.clear();
    mCommittedImeOffset = 0;
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

std::string Win32InputAdapter::utf8FromUtf16(std::u16string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char16_t first = text[index];
        if (first >= 0xD800 && first <= 0xDBFF) {
            if (index + 1U < text.size()) {
                const char16_t second = text[index + 1U];
                if (second >= 0xDC00 && second <= 0xDFFF) {
                    const char32_t scalar = 0x10000U
                        + ((static_cast<char32_t>(first) - 0xD800U) << 10U)
                        + (static_cast<char32_t>(second) - 0xDC00U);
                    static_cast<void>(appendUtf8(result, scalar));
                    ++index;
                    continue;
                }
            }
            static_cast<void>(appendUtf8(result, kReplacementCharacter));
        } else if (first >= 0xDC00 && first <= 0xDFFF) {
            static_cast<void>(appendUtf8(result, kReplacementCharacter));
        } else {
            static_cast<void>(appendUtf8(result, static_cast<char32_t>(first)));
        }
    }
    return result;
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
        case VK_SPACE: return KeyCode::Space;
        case VK_INSERT: return KeyCode::Insert;
        case VK_PRIOR: return KeyCode::PageUp;
        case VK_NEXT: return KeyCode::PageDown;
        case '0': return KeyCode::Digit0;
        case '1': return KeyCode::Digit1;
        case '2': return KeyCode::Digit2;
        case '3': return KeyCode::Digit3;
        case '4': return KeyCode::Digit4;
        case '5': return KeyCode::Digit5;
        case '6': return KeyCode::Digit6;
        case '7': return KeyCode::Digit7;
        case '8': return KeyCode::Digit8;
        case '9': return KeyCode::Digit9;
        case 'A': return KeyCode::A;
        case 'B': return KeyCode::B;
        case 'C': return KeyCode::C;
        case 'D': return KeyCode::D;
        case 'E': return KeyCode::E;
        case 'F': return KeyCode::F;
        case 'G': return KeyCode::G;
        case 'H': return KeyCode::H;
        case 'I': return KeyCode::I;
        case 'J': return KeyCode::J;
        case 'K': return KeyCode::K;
        case 'L': return KeyCode::L;
        case 'M': return KeyCode::M;
        case 'N': return KeyCode::N;
        case 'O': return KeyCode::O;
        case 'P': return KeyCode::P;
        case 'Q': return KeyCode::Q;
        case 'R': return KeyCode::R;
        case 'S': return KeyCode::S;
        case 'T': return KeyCode::T;
        case 'U': return KeyCode::U;
        case 'V': return KeyCode::V;
        case 'W': return KeyCode::W;
        case 'X': return KeyCode::X;
        case 'Y': return KeyCode::Y;
        case 'Z': return KeyCode::Z;
        case VK_F1: return KeyCode::F1;
        case VK_F2: return KeyCode::F2;
        case VK_F3: return KeyCode::F3;
        case VK_F4: return KeyCode::F4;
        case VK_F5: return KeyCode::F5;
        case VK_F6: return KeyCode::F6;
        case VK_F7: return KeyCode::F7;
        case VK_F8: return KeyCode::F8;
        case VK_F9: return KeyCode::F9;
        case VK_F10: return KeyCode::F10;
        case VK_F11: return KeyCode::F11;
        case VK_F12: return KeyCode::F12;
        case VK_SHIFT: return KeyCode::Shift;
        case VK_CONTROL: return KeyCode::Control;
        case VK_MENU: return KeyCode::Alt;
        default: return KeyCode::Unknown;
    }
}

void Win32InputAdapter::addModifiers(InputEvent& event) noexcept {
    event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    event.control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
}

} // namespace henia::ui
