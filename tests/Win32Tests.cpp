#include "henia/ui/Frame.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/platform/win32/Win32InputAdapter.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/controls/Panel.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
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
    [[nodiscard]] bool wantsTabKey() const noexcept override { return true; }

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
            case InputEventKind::CompositionStart:
                ++compositionStarts;
                return true;
            case InputEventKind::CompositionUpdate:
                composition.assign(event.textUtf8);
                return true;
            case InputEventKind::CompositionCommit:
                composition.assign(event.textUtf8);
                ++compositionCommits;
                return true;
            case InputEventKind::CompositionCancel:
                ++compositionCancels;
                return true;
            case InputEventKind::KeyDown:
                lastKey = event.key;
                return true;
            case InputEventKind::FocusLost:
                ++focusLostCalls;
                return true;
            default:
                return false;
        }
    }

    std::vector<char32_t> text;
    std::string composition;
    int focusLostCalls = 0;
    int compositionStarts = 0;
    int compositionCommits = 0;
    int compositionCancels = 0;
    KeyCode lastKey = KeyCode::Unknown;
    bool throwOnPointerUp = false;
};

class DpiProbe final {
public:
    explicit DpiProbe(UiDocument& document) noexcept : mDocument(&document) {}

    void changed(const Win32DpiChange& change) {
        last = change;
        ++calls;
        const Vec2 scale = change.scale();
        static_cast<void>(mDocument->setCoordinateSpace(makeUiCoordinateSpace(
            {200.0F, 100.0F},
            {200.0F * scale.x, 100.0F * scale.y},
            static_cast<std::uint32_t>(200.0F * scale.x),
            static_cast<std::uint32_t>(100.0F * scale.y),
            scale.x)));
    }

    Win32DpiChange last{};
    std::uint32_t calls = 0;

private:
    UiDocument* mDocument = nullptr;
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
    probe.setLayoutParameters({.width = 100.0F, .height = 100.0F});
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());

    Win32InputAdapter adapter(document);
    NativeTestWindow native;
    const HWND window = native.get();

    if (!document.setCoordinateSpace(makeUiCoordinateSpace(
            {200.0F, 100.0F}, {400.0F, 200.0F}, 300, 150, 1.5F))) {
        fail("Unable to configure independent Win32 input and framebuffer transforms");
    }
    if (!adapter.handleMessage(
            window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(150, 100))
        || !probe.pressed()) {
        fail("Win32 physical pointer coordinates were not converted to logical units");
    }
    if (!adapter.handleMessage(window, WM_LBUTTONUP, 0, MAKELPARAM(150, 100))) {
        fail("Transformed Win32 pointer release was not handled");
    }

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
    struct KeyMapping final {
        WPARAM virtualKey = 0;
        LPARAM flags = 0;
        KeyCode expected = KeyCode::Unknown;
    };
    constexpr auto kExtendedKey = static_cast<LPARAM>(1ULL << 24U);
    constexpr std::array keyMappings{
        KeyMapping{VK_BACK, 0, KeyCode::Backspace},
        KeyMapping{VK_DELETE, 0, KeyCode::Delete},
        KeyMapping{VK_INSERT, 0, KeyCode::Insert},
        KeyMapping{VK_HOME, 0, KeyCode::Home},
        KeyMapping{VK_END, 0, KeyCode::End},
        KeyMapping{VK_LEFT, 0, KeyCode::Left},
        KeyMapping{VK_RIGHT, 0, KeyCode::Right},
        KeyMapping{VK_UP, 0, KeyCode::Up},
        KeyMapping{VK_DOWN, 0, KeyCode::Down},
        KeyMapping{VK_RETURN, 0, KeyCode::Enter},
        KeyMapping{VK_ESCAPE, 0, KeyCode::Escape},
        KeyMapping{VK_TAB, 0, KeyCode::Tab},
        KeyMapping{VK_PRIOR, 0, KeyCode::PageUp},
        KeyMapping{VK_NEXT, 0, KeyCode::PageDown},
        KeyMapping{VK_F5, 0, KeyCode::F5},
        KeyMapping{VK_SPACE, 0, KeyCode::Space},
        KeyMapping{VK_SHIFT, 0, KeyCode::Shift},
        KeyMapping{VK_LSHIFT, 0, KeyCode::Shift},
        KeyMapping{VK_RSHIFT, 0, KeyCode::Shift},
        KeyMapping{VK_CONTROL, 0, KeyCode::Control},
        KeyMapping{VK_LCONTROL, 0, KeyCode::Control},
        KeyMapping{VK_RCONTROL, 0, KeyCode::Control},
        KeyMapping{VK_MENU, 0, KeyCode::Alt},
        KeyMapping{VK_LMENU, 0, KeyCode::Alt},
        KeyMapping{VK_RMENU, 0, KeyCode::Alt},
        KeyMapping{VK_CAPITAL, 0, KeyCode::CapsLock},
        KeyMapping{VK_NUMLOCK, 0, KeyCode::NumLock},
        KeyMapping{VK_SCROLL, 0, KeyCode::ScrollLock},
        KeyMapping{VK_SNAPSHOT, 0, KeyCode::PrintScreen},
        KeyMapping{VK_PAUSE, 0, KeyCode::Pause},
        KeyMapping{VK_LWIN, 0, KeyCode::LeftSuper},
        KeyMapping{VK_RWIN, 0, KeyCode::RightSuper},
        KeyMapping{VK_APPS, 0, KeyCode::Menu},
        KeyMapping{VK_OEM_1, 0, KeyCode::Semicolon},
        KeyMapping{VK_OEM_PLUS, 0, KeyCode::Equal},
        KeyMapping{VK_OEM_COMMA, 0, KeyCode::Comma},
        KeyMapping{VK_OEM_MINUS, 0, KeyCode::Minus},
        KeyMapping{VK_OEM_PERIOD, 0, KeyCode::Period},
        KeyMapping{VK_OEM_2, 0, KeyCode::Slash},
        KeyMapping{VK_OEM_3, 0, KeyCode::Backtick},
        KeyMapping{VK_OEM_4, 0, KeyCode::LeftBracket},
        KeyMapping{VK_OEM_5, 0, KeyCode::Backslash},
        KeyMapping{VK_OEM_6, 0, KeyCode::RightBracket},
        KeyMapping{VK_OEM_7, 0, KeyCode::Apostrophe},
        KeyMapping{VK_OEM_102, 0, KeyCode::IntlBackslash},
        KeyMapping{VK_NUMPAD0, 0, KeyCode::Numpad0},
        KeyMapping{VK_NUMPAD5, 0, KeyCode::Numpad5},
        KeyMapping{VK_NUMPAD9, 0, KeyCode::Numpad9},
        KeyMapping{VK_MULTIPLY, 0, KeyCode::NumpadMultiply},
        KeyMapping{VK_ADD, 0, KeyCode::NumpadAdd},
        KeyMapping{VK_SEPARATOR, 0, KeyCode::NumpadSeparator},
        KeyMapping{VK_SUBTRACT, 0, KeyCode::NumpadSubtract},
        KeyMapping{VK_DECIMAL, 0, KeyCode::NumpadDecimal},
        KeyMapping{VK_DIVIDE, 0, KeyCode::NumpadDivide},
        KeyMapping{VK_RETURN, kExtendedKey, KeyCode::NumpadEnter},
    };
    for (const KeyMapping& mapping : keyMappings) {
        if (!adapter.handleMessage(
                window, WM_KEYDOWN, mapping.virtualKey, mapping.flags)
            || probe.lastKey != mapping.expected) {
            fail("Standard Win32 keyboard mapping did not reach the focused widget");
        }
    }
    const auto verifyContiguousKeys = [&](WPARAM firstVirtualKey, KeyCode firstKey, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            const auto virtualKey = firstVirtualKey + static_cast<WPARAM>(index);
            const auto expected = static_cast<KeyCode>(
                static_cast<std::uint16_t>(firstKey) + static_cast<std::uint16_t>(index));
            if (!adapter.handleMessage(window, WM_KEYDOWN, virtualKey, 0)
                || probe.lastKey != expected) {
                fail("A contiguous standard Win32 key range was not mapped");
            }
        }
    };
    verifyContiguousKeys('0', KeyCode::Digit0, 10);
    verifyContiguousKeys('A', KeyCode::A, 26);
    verifyContiguousKeys(VK_F1, KeyCode::F1, 12);
    verifyContiguousKeys(VK_NUMPAD0, KeyCode::Numpad0, 10);

    probe.text.clear();
    constexpr std::array controlCharacters{
        WPARAM{0x01},
        WPARAM{0x08},
        WPARAM{0x09},
        WPARAM{0x0D},
        WPARAM{0x1B},
        WPARAM{0x7F},
        WPARAM{0x85},
    };
    for (WPARAM control : controlCharacters) {
        if (!adapter.handleMessage(window, WM_CHAR, control, 0)) {
            fail("A duplicate WM_CHAR control code was not consumed");
        }
    }
    if (!adapter.handleMessage(window, WM_UNICHAR, 0x9F, 0)
        || !probe.text.empty()) {
        fail("Win32 control codes leaked into committed text input");
    }
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

    if (!adapter.handleMessage(window, WM_IME_STARTCOMPOSITION, 0, 0)
        || !adapter.handleMessage(window, WM_IME_ENDCOMPOSITION, 0, 0)
        || probe.compositionStarts != 1 || probe.compositionCancels != 1) {
        fail("Win32 IME lifecycle did not reach the focused widget");
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

    DpiProbe dpiProbe(document);
    adapter.setOnDpiChanged(
        Callback<const Win32DpiChange&>::bind<DpiProbe, &DpiProbe::changed>(dpiProbe));
    RECT suggested{10, 20, 410, 220};
    constexpr std::array dpis{120U, 144U, 192U};
    for (std::uint32_t dpi : dpis) {
        if (adapter.handleMessage(
                window,
                WM_DPICHANGED,
                MAKEWPARAM(dpi, dpi),
                reinterpret_cast<LPARAM>(&suggested))) {
            fail("WM_DPICHANGED was consumed instead of remaining host-owned");
        }
    }
    const Win32DpiChange& dpiState = adapter.dpiState();
    if (dpiProbe.calls != dpis.size() || dpiState.revision != dpis.size()
        || dpiState.dpiX != 192 || dpiState.dpiY != 192
        || !dpiState.hasSuggestedWindowRect
        || dpiState.suggestedWindowRect.left != suggested.left
        || document.coordinateSpace().dpiScale != 2.0F
        || document.coordinateSpace().render.framebufferWidth != 400) {
        fail("Win32 DPI notifications did not update per-window host state synchronously");
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

    Win32FontScaleCache scaleCache(
        textures,
        fonts,
        {
            .family = L"Segoe UI",
            .logicalPixelHeight = 18.0F,
            .atlasWidth = 256,
            .atlasHeight = 128,
            .ranges = ranges,
        });
    const FontHandle font100 = scaleCache.selectForDpi(96);
    const FontHandle font125 = scaleCache.selectForDpi(120);
    const FontHandle font150 = scaleCache.selectForDpi(144);
    const FontHandle font200 = scaleCache.selectForDpi(192);
    const FontHandle font125Again = scaleCache.selectForDpi(120);
    const Win32FontScaleCacheStatistics scaleStatistics = scaleCache.statistics();
    if (!font100.valid() || !font125.valid() || !font150.valid() || !font200.valid()
        || font125Again != font125 || scaleStatistics.variants != 4
        || scaleStatistics.cacheHits != 1 || scaleStatistics.cacheMisses != 4
        || std::abs(fonts.find(font100)->pixelSize() - 18.0F) > 0.0001F
        || std::abs(fonts.find(font125)->pixelSize() - 18.0F) > 0.0001F
        || std::abs(fonts.find(font150)->pixelSize() - 18.0F) > 0.0001F
        || std::abs(fonts.find(font200)->pixelSize() - 18.0F) > 0.0001F) {
        std::cerr << "Win32 scaled font variants churned or exposed physical metrics\n";
        return EXIT_FAILURE;
    }

    DynamicGlyphAtlas dynamic(textures, fonts, font, {
        .pageWidth = 64,
        .pageHeight = 64,
        .padding = 1,
        .maximumPages = 2,
    });
    constexpr std::array dynamicCodepoints{U'\u00E9'};
    if (!Win32FontLoader::appendGlyphs(
            dynamic,
            {.family = L"Segoe UI", .pixelHeight = 24},
            dynamicCodepoints)
        || fonts.find(font)->glyph(U'\u00E9') == nullptr
        || dynamic.statistics().glyphsAdded != 1) {
        std::cerr << "Win32 dynamic glyph atlas growth failed\n";
        return EXIT_FAILURE;
    }

    TextRunCache cache(fonts);
    cache.reserve(16, 64);
    TextPainter painter(cache);
    const TextMetrics metrics = painter.measure(font, 18.0F, "HeniaUI 0123456789");
    const TextMetrics dynamicMetrics = painter.measure(font, 18.0F, "caf\xC3\xA9");
    if (metrics.width <= 0.0F || metrics.height <= 0.0F
        || dynamicMetrics.width <= 0.0F) {
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
