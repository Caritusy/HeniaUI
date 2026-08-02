#include "henia/ui/platform/win32/Win32Clipboard.h"

#include "henia/ui/text/Utf8.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>

namespace henia::ui {
namespace {

class ClipboardScope final {
public:
    explicit ClipboardScope(HWND owner) noexcept : mOpen(OpenClipboard(owner) != FALSE) {}
    ~ClipboardScope() { if (mOpen) static_cast<void>(CloseClipboard()); }
    [[nodiscard]] bool open() const noexcept { return mOpen; }

private:
    bool mOpen = false;
};

[[nodiscard]] std::wstring toUtf16(std::string_view text) {
    const std::string valid = validUtf8(text) ? std::string(text) : sanitizeUtf8(text);
    if (valid.empty()) return {};
    if (valid.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int units = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        valid.data(),
        static_cast<int>(valid.size()),
        nullptr,
        0);
    if (units <= 0) return {};
    std::wstring result(static_cast<std::size_t>(units), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            valid.data(),
            static_cast<int>(valid.size()),
            result.data(),
            units) != units) {
        return {};
    }
    return result;
}

[[nodiscard]] std::optional<std::string> toUtf8(std::wstring_view text) {
    if (text.empty()) return std::string{};
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const int bytes = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (bytes <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            bytes,
            nullptr,
            nullptr) != bytes) {
        return std::nullopt;
    }
    return result;
}

} // namespace

Win32Clipboard::Win32Clipboard(HWND ownerValue) noexcept : mOwner(ownerValue) {}

void Win32Clipboard::setOwner(HWND ownerValue) noexcept { mOwner = ownerValue; }
HWND Win32Clipboard::owner() const noexcept { return mOwner; }

bool Win32Clipboard::writeText(std::string_view text) {
    const std::wstring utf16 = toUtf16(text);
    if (!text.empty() && utf16.empty()) return false;
    ClipboardScope clipboard(mOwner);
    if (!clipboard.open() || EmptyClipboard() == FALSE) return false;

    const std::size_t bytes = (utf16.size() + 1U) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) return false;
    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        static_cast<void>(GlobalFree(memory));
        return false;
    }
    std::memcpy(destination, utf16.c_str(), bytes);
    static_cast<void>(GlobalUnlock(memory));
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        static_cast<void>(GlobalFree(memory));
        return false;
    }
    return true;
}

std::optional<std::string> Win32Clipboard::readText() {
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE) return std::nullopt;
    ClipboardScope clipboard(mOwner);
    if (!clipboard.open()) return std::nullopt;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == nullptr) return std::nullopt;
    const auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
    if (text == nullptr) return std::nullopt;
    const SIZE_T bytes = GlobalSize(handle);
    const std::size_t capacity = static_cast<std::size_t>(bytes) / sizeof(wchar_t);
    const auto terminator = std::find(text, text + capacity, L'\0');
    const std::wstring_view view(text, static_cast<std::size_t>(terminator - text));
    const std::optional<std::string> result = toUtf8(view);
    static_cast<void>(GlobalUnlock(handle));
    return result;
}

} // namespace henia::ui
