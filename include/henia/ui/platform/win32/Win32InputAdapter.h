#pragma once

#include "henia/ui/widget/UiDocument.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace henia::ui {

// Translates a host-owned HWND message stream into platform-neutral HeniaUI input.
// It never subclasses the window and may therefore be used from an existing WndProc hook.
class Win32InputAdapter final {
public:
    explicit Win32InputAdapter(UiDocument& document) noexcept;

    [[nodiscard]] bool handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    [[nodiscard]] InputEvent makePointerEvent(
        InputEventKind kind, HWND window, UINT message, WPARAM wParam, LPARAM lParam) const noexcept;
    [[nodiscard]] InputEvent makeKeyEvent(InputEventKind kind, WPARAM wParam, LPARAM lParam) const noexcept;
    [[nodiscard]] static KeyCode translateKey(WPARAM key) noexcept;
    static void addModifiers(InputEvent& event) noexcept;

    UiDocument* mDocument = nullptr;
    char16_t mHighSurrogate = u'\0';
};

} // namespace henia::ui
