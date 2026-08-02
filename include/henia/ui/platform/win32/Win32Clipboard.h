#pragma once

#include "henia/ui/text/TextEditor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace henia::ui {

// Synchronous CF_UNICODETEXT bridge. The host owns the HWND and may pass null
// when clipboard ownership need not be associated with a window.
class Win32Clipboard final : public TextClipboard {
public:
    explicit Win32Clipboard(HWND owner = nullptr) noexcept;

    void setOwner(HWND owner) noexcept;
    [[nodiscard]] HWND owner() const noexcept;
    [[nodiscard]] bool writeText(std::string_view text) override;
    [[nodiscard]] std::optional<std::string> readText() override;

private:
    HWND mOwner = nullptr;
};

} // namespace henia::ui
