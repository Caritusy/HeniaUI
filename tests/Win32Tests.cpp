#include "henia/ui/Frame.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/platform/win32/Win32InputAdapter.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/controls/Panel.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace henia::ui;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

class InputProbe final : public Widget {
public:
    [[nodiscard]] bool acceptsPointerInput() const noexcept override { return true; }
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override { return true; }

    [[nodiscard]] bool handleInput(const InputEvent& event) override {
        switch (event.kind) {
            case InputEventKind::PointerDown:
                return event.button != PointerButton::None;
            case InputEventKind::PointerUp:
                if (throwOnPointerUp) {
                    throw std::runtime_error("pointer up callback");
                }
                return event.button != PointerButton::None;
            case InputEventKind::TextInput:
                text.push_back(event.text);
                return true;
            case InputEventKind::FocusLost:
                ++focusLostCalls;
                return true;
            default:
                return false;
        }
    }

    std::vector<char32_t> text;
    int focusLostCalls = 0;
    bool throwOnPointerUp = false;
};

class NativeTestWindow final {
public:
    NativeTestWindow() {
        mWindow = CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_POPUP,
            0,
            0,
            200,
            100,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (mWindow == nullptr) {
            fail("Unable to create the Win32 input test window");
        }
    }

    ~NativeTestWindow() {
        if (mWindow != nullptr) {
            if (GetCapture() == mWindow) {
                static_cast<void>(ReleaseCapture());
            }
            DestroyWindow(mWindow);
        }
    }

    NativeTestWindow(const NativeTestWindow&) = delete;
    NativeTestWindow& operator=(const NativeTestWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept { return mWindow; }

private:
    HWND mWindow = nullptr;
};

void pointerDown(Win32InputAdapter& adapter, HWND window, UINT message, WPARAM state) {
    if (!adapter.handleMessage(window, message, state, MAKELPARAM(10, 10))) {
        fail("Win32 pointer down was not handled by the input probe");
    }
}

void pointerUp(Win32InputAdapter& adapter, HWND window, UINT message, WPARAM state) {
    if (!adapter.handleMessage(window, message, state, MAKELPARAM(10, 10))) {
        fail("Win32 pointer up was not handled by the input probe");
    }
}

void focusProbe(Win32InputAdapter& adapter, HWND window) {
    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    pointerUp(adapter, window, WM_LBUTTONUP, 0);
}

void verifyWin32InputAdapter(TextPainter& painter) {
    UiDocument document(painter);
    document.reserve(64, 8);
    document.setViewport({200.0F, 100.0F});
    auto root = std::make_unique<Panel>();
    InputProbe& probe = root->emplaceChild<InputProbe>();
    probe.setLayoutParameters({.width = 200.0F, .height = 100.0F});
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());

    Win32InputAdapter adapter(document);
    NativeTestWindow native;
    const HWND window = native.get();

    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    if (GetCapture() != window || !probe.pressed() || !probe.focused()) {
        fail("Handled pointer down did not acquire native capture");
    }
    pointerDown(adapter, window, WM_RBUTTONDOWN, MK_LBUTTON | MK_RBUTTON);
    pointerUp(adapter, window, WM_LBUTTONUP, MK_RBUTTON);
    if (GetCapture() != window) {
        fail("Native capture was released before every pressed button was released");
    }
    pointerUp(adapter, window, WM_RBUTTONUP, 0);
    if (GetCapture() != nullptr || probe.pressed() || !probe.focused()) {
        fail("Final button release did not release capture while preserving focus");
    }

    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    HWND other = CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (other == nullptr) {
        fail("Unable to create the alternate capture window");
    }
    static_cast<void>(SetCapture(other));
    const bool captureChangedConsumed = adapter.handleMessage(
        window,
        WM_CAPTURECHANGED,
        0,
        reinterpret_cast<LPARAM>(other));
    if (captureChangedConsumed || GetCapture() != other || probe.pressed() || !probe.focused()
        || probe.focusLostCalls != 0) {
        fail("WM_CAPTURECHANGED did not cancel pointer interaction cleanly");
    }
    static_cast<void>(ReleaseCapture());
    DestroyWindow(other);
    if (adapter.handleMessage(window, WM_LBUTTONUP, 0, MAKELPARAM(10, 10))) {
        fail("A release after capture loss was forwarded as a valid pointer up");
    }

    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    if (adapter.handleMessage(window, WM_CANCELMODE, 0, 0)
        || GetCapture() != nullptr || probe.pressed() || !probe.focused()) {
        fail("WM_CANCELMODE was consumed or left pointer capture active");
    }

    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    probe.throwOnPointerUp = true;
    try {
        static_cast<void>(adapter.handleMessage(
            window, WM_LBUTTONUP, 0, MAKELPARAM(10, 10)));
        fail("A pointer-up callback exception was swallowed");
    } catch (const std::runtime_error&) {
    }
    probe.throwOnPointerUp = false;
    if (GetCapture() != nullptr || probe.pressed() || probe.focused()) {
        fail("A pointer callback exception left native or document capture active");
    }

    focusProbe(adapter, window);
    probe.text.clear();
    if (!adapter.handleMessage(window, WM_CHAR, 0xD83D, 0)
        || !adapter.handleMessage(window, WM_CHAR, 0xDE00, 0)
        || probe.text != std::vector<char32_t>{U'\U0001F600'}) {
        fail("A valid UTF-16 surrogate pair was not combined");
    }
    probe.text.clear();
    if (!adapter.handleMessage(window, WM_CHAR, 0xDC00, 0)
        || probe.text != std::vector<char32_t>{U'\uFFFD'}) {
        fail("An isolated low surrogate was not replaced");
    }
    probe.text.clear();
    static_cast<void>(adapter.handleMessage(window, WM_CHAR, 0xD83D, 0));
    if (!adapter.handleMessage(window, WM_CHAR, L'A', 0)
        || probe.text != std::vector<char32_t>{U'\uFFFD', U'A'}) {
        fail("An isolated high surrogate was not replaced before the next character");
    }
    probe.text.clear();
    if (!adapter.handleMessage(window, WM_UNICHAR, UNICODE_NOCHAR, 0)
        || !probe.text.empty()) {
        fail("WM_UNICHAR capability probing is incorrect");
    }
    if (!adapter.handleMessage(window, WM_UNICHAR, 0x110000, 0)
        || !adapter.handleMessage(window, WM_UNICHAR, 0xD800, 0)
        || !adapter.handleMessage(window, WM_UNICHAR, 0x1F642, 0)
        || probe.text != std::vector<char32_t>{U'\uFFFD', U'\uFFFD', U'\U0001F642'}) {
        fail("WM_UNICHAR scalar validation is incorrect");
    }

    static_cast<void>(adapter.handleMessage(window, WM_CHAR, 0xD83D, 0));
    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    const int focusLostBefore = probe.focusLostCalls;
    if (adapter.handleMessage(window, WM_KILLFOCUS, 0, 0)
        || GetCapture() != nullptr || probe.focused() || probe.pressed()
        || probe.focusLostCalls != focusLostBefore + 1) {
        fail("WM_KILLFOCUS did not cancel interaction and focus exactly once");
    }
    focusProbe(adapter, window);
    probe.text.clear();
    if (!adapter.handleMessage(window, WM_CHAR, 0xDC00, 0)
        || probe.text != std::vector<char32_t>{U'\uFFFD'}) {
        fail("Focus loss did not discard a pending UTF-16 high surrogate");
    }

    pointerDown(adapter, window, WM_LBUTTONDOWN, MK_LBUTTON);
    const int destroyFocusLostBefore = probe.focusLostCalls;
    if (adapter.handleMessage(window, WM_DESTROY, 0, 0)
        || GetCapture() != nullptr || probe.pressed() || probe.focused()
        || probe.focusLostCalls != destroyFocusLostBefore + 1
        || adapter.handleMessage(window, WM_NCDESTROY, 0, 0)
        || probe.focusLostCalls != destroyFocusLostBefore + 1) {
        fail("Window destruction did not cancel capture and focus exactly once");
    }
}

} // namespace

int main() {
    using namespace henia::ui;

    TextureStore textures;
    FontStore fonts;
    constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
    const FontHandle font = Win32FontLoader::load(
        textures,
        fonts,
        {.family = L"Segoe UI", .pixelHeight = 24, .atlasWidth = 512, .atlasHeight = 256, .ranges = ranges});
    if (!font.valid() || textures.size() != 1 || fonts.size() != 1) {
        std::cerr << "Win32 font atlas construction failed\n";
        return EXIT_FAILURE;
    }

    const TextureView atlas = textures.view(fonts.find(font)->atlas());
    if (atlas.format != TextureFormat::Alpha8 || atlas.width != 512 || atlas.height != 256
        || atlas.pixels.empty()) {
        std::cerr << "Win32 font atlas texture is invalid\n";
        return EXIT_FAILURE;
    }

    TextRunCache cache(fonts);
    cache.reserve(16, 64);
    TextPainter painter(cache);
    const TextMetrics metrics = painter.measure(font, 18.0F, "HeniaUI 0123456789");
    if (metrics.width <= 0.0F || metrics.height <= 0.0F) {
        std::cerr << "Win32 font text metrics are invalid\n";
        return EXIT_FAILURE;
    }

    Frame frame;
    frame.reserve(128, 8);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{0.0F, 0.0F}, {220.0F, 40.0F}}, {}, 6.0F);
    painter.draw(canvas, font, 18.0F, {8.0F, 8.0F}, {}, "HeniaUI 0123456789");
    const RenderPacket packet = frame.finish();
    if (packet.batches().size() != 1 || packet.instances().size() <= 1) {
        std::cerr << "Win32 text did not merge with the UI batch\n";
        return EXIT_FAILURE;
    }

    verifyWin32InputAdapter(painter);

    std::cout << "HeniaUI Win32 font and input tests passed\n";
    return EXIT_SUCCESS;
}
