#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/UiDocument.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace henia::ui {

struct Win32DpiChange final {
    std::uint32_t dpiX = 96;
    std::uint32_t dpiY = 96;
    RECT suggestedWindowRect{};
    std::uint64_t revision = 0;
    bool hasSuggestedWindowRect = false;

    [[nodiscard]] constexpr Vec2 scale() const noexcept {
        return {
            static_cast<float>(dpiX) / 96.0F,
            static_cast<float>(dpiY) / 96.0F,
        };
    }
};

// Translates a host-owned HWND message stream into platform-neutral HeniaUI input.
// It never subclasses or owns the window and may therefore be used from an
// existing WndProc hook. One adapter instance tracks one HWND message stream.
class Win32InputAdapter final {
public:
    explicit Win32InputAdapter(UiDocument& document) noexcept;

    // Returns true only when translated input was handled, a duplicate
    // WM_CHAR control code was consumed, a UTF-16 high surrogate was buffered,
    // or WM_UNICHAR queried Unicode support. Capture,
    // IME composition is dispatched synchronously as UTF-8 start/update/
    // commit/cancel events. Focus, cancellation, and destruction notifications
    // are observed and return false so the host can continue normal processing.
    [[nodiscard]] bool handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void setOnDpiChanged(Callback<const Win32DpiChange&> callback) noexcept;
    [[nodiscard]] const Win32DpiChange& dpiState() const noexcept;

private:
    using PressedButtonMask = std::uint8_t;

    [[nodiscard]] InputEvent makePointerEvent(
        InputEventKind kind, HWND window, UINT message, WPARAM wParam, LPARAM lParam) const noexcept;
    [[nodiscard]] InputEvent makeKeyEvent(InputEventKind kind, WPARAM wParam, LPARAM lParam) const noexcept;
    [[nodiscard]] bool handleUtf16Unit(char16_t unit);
    [[nodiscard]] bool handleUnicodeScalar(std::uint64_t value);
    [[nodiscard]] bool handleImeComposition(HWND window, LPARAM flags);
    [[nodiscard]] bool dispatchComposition(
        InputEventKind kind,
        std::string_view text = {},
        std::size_t selectionStart = 0,
        std::size_t selectionLength = 0);
    void cancelComposition();
    [[nodiscard]] bool dispatchText(char32_t value);
    [[nodiscard]] bool acquireNativeCapture(HWND window) noexcept;
    void releaseNativeCapture() noexcept;
    void cancelInteraction(bool loseFocus);
    [[nodiscard]] static PressedButtonMask buttonMask(UINT message) noexcept;
    [[nodiscard]] static bool validUnicodeScalar(std::uint64_t value) noexcept;
    [[nodiscard]] static std::string utf8FromUtf16(std::u16string_view text);
    [[nodiscard]] static KeyCode translateKey(WPARAM key, LPARAM flags) noexcept;
    static void addModifiers(InputEvent& event) noexcept;

    UiDocument* mDocument = nullptr;
    HWND mCaptureWindow = nullptr;
    PressedButtonMask mPressedButtons = 0;
    char16_t mHighSurrogate = u'\0';
    std::u16string mCommittedImeUnits;
    std::size_t mCommittedImeOffset = 0;
    Callback<const Win32DpiChange&> mOnDpiChanged;
    Win32DpiChange mDpiState{};
    bool mImeActive = false;
    bool mReleasingNativeCapture = false;
};

} // namespace henia::ui
