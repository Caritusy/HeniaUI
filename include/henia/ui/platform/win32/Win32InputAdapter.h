#pragma once

#include "henia/ui/widget/UiDocument.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdint>

namespace henia::ui {

// Translates a host-owned HWND message stream into platform-neutral HeniaUI input.
// It never subclasses or owns the window and may therefore be used from an
// existing WndProc hook. One adapter instance tracks one HWND message stream.
class Win32InputAdapter final {
public:
    explicit Win32InputAdapter(UiDocument& document) noexcept;

    // Returns true only when translated input was handled, a UTF-16 high
    // surrogate was buffered, or WM_UNICHAR queried Unicode support. Capture,
    // focus, cancellation, and destruction notifications are observed and
    // return false so the host can continue its normal window processing.
    [[nodiscard]] bool handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    using PressedButtonMask = std::uint8_t;

    [[nodiscard]] InputEvent makePointerEvent(
        InputEventKind kind, HWND window, UINT message, WPARAM wParam, LPARAM lParam) const noexcept;
    [[nodiscard]] InputEvent makeKeyEvent(InputEventKind kind, WPARAM wParam, LPARAM lParam) const noexcept;
    [[nodiscard]] bool handleUtf16Unit(char16_t unit);
    [[nodiscard]] bool handleUnicodeScalar(std::uint64_t value);
    [[nodiscard]] bool dispatchText(char32_t value);
    [[nodiscard]] bool acquireNativeCapture(HWND window) noexcept;
    void releaseNativeCapture() noexcept;
    void cancelInteraction(bool loseFocus);
    [[nodiscard]] static PressedButtonMask buttonMask(UINT message) noexcept;
    [[nodiscard]] static bool validUnicodeScalar(std::uint64_t value) noexcept;
    [[nodiscard]] static KeyCode translateKey(WPARAM key) noexcept;
    static void addModifiers(InputEvent& event) noexcept;

    UiDocument* mDocument = nullptr;
    HWND mCaptureWindow = nullptr;
    PressedButtonMask mPressedButtons = 0;
    char16_t mHighSurrogate = u'\0';
    bool mReleasingNativeCapture = false;
};

} // namespace henia::ui
