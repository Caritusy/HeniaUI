#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "henia/ui/backend/opengl/OpenGlRenderer.h"
#include "henia/ui/platform/win32/Win32AsyncFont.h"
#include "henia/ui/platform/win32/Win32Clipboard.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/platform/win32/Win32InputAdapter.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/UiDocument.h"
#include "henia/ui/widget/controls/Button.h"
#include "henia/ui/widget/controls/ColorPicker.h"
#include "henia/ui/widget/controls/ComboBox.h"
#include "henia/ui/widget/controls/KeyBindingEditor.h"
#include "henia/ui/widget/controls/Label.h"
#include "henia/ui/widget/controls/ListView.h"
#include "henia/ui/widget/controls/NumericInput.h"
#include "henia/ui/widget/controls/Panel.h"
#include "henia/ui/widget/controls/PopupLayer.h"
#include "henia/ui/widget/controls/ScrollContainer.h"
#include "henia/ui/widget/controls/Slider.h"
#include "henia/ui/widget/controls/TabBar.h"
#include "henia/ui/widget/controls/TextInput.h"
#include "henia/ui/widget/controls/Toggle.h"
#include "henia/ui/widget/controls/Tooltip.h"
#include "henia/ui/widget/controls/TreeView.h"

#include <Windows.h>
#include <gl/GL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace henia::ui;

constexpr wchar_t kWindowClass[] = L"HeniaUIWidgetGallery";
constexpr int kInitialWidth = 1560;
constexpr int kInitialHeight = 980;

constexpr Color kCanvas{0.012F, 0.019F, 0.030F, 1.0F};
constexpr Color kSurface{0.026F, 0.039F, 0.058F, 1.0F};
constexpr Color kSurfaceRaised{0.036F, 0.054F, 0.078F, 1.0F};
constexpr Color kBorder{0.11F, 0.19F, 0.27F, 1.0F};
constexpr Color kAccent{0.10F, 0.72F, 0.91F, 1.0F};
constexpr Color kText{0.90F, 0.95F, 0.98F, 1.0F};
constexpr Color kMuted{0.48F, 0.59F, 0.67F, 1.0F};

constexpr std::string_view kMultilingualSample =
    "English: asynchronous glyph atlas | "
    "\u4E2D\u6587: \u5F02\u6B65\u5B57\u4F53\u70D8\u7119 | "
    "\u65E5\u672C\u8A9E: \u975E\u540C\u671F\u30D5\u30A9\u30F3\u30C8 | "
    "\uD55C\uAD6D\uC5B4: \uBE44\uB3D9\uAE30 \uAE00\uAF34 | "
    "Caf\u00E9 \u03A9 \u221E \u2605 \U0001F525";

using CreateContextAttributesFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using SwapIntervalFn = BOOL(WINAPI*)(int);

LRESULT CALLBACK windowProcedure(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter) {
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    auto* input = reinterpret_cast<Win32InputAdapter*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_DESTROY) {
        if (input != nullptr) {
            static_cast<void>(input->handleMessage(
                window, message, wordParameter, longParameter));
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_KEYDOWN && wordParameter == VK_ESCAPE) {
        DestroyWindow(window);
        return 0;
    }
    if (input != nullptr
        && input->handleMessage(window, message, wordParameter, longParameter)) {
        return 0;
    }
    if (message == WM_DPICHANGED && input != nullptr) {
        return 0;
    }
    return DefWindowProcW(window, message, wordParameter, longParameter);
}

struct NativeWindow final {
    HWND window = nullptr;
    HDC deviceContext = nullptr;
    HGLRC renderContext = nullptr;

    ~NativeWindow() {
        if (renderContext != nullptr) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(renderContext);
        }
        if (window != nullptr && deviceContext != nullptr) {
            ReleaseDC(window, deviceContext);
        }
        if (window != nullptr) {
            DestroyWindow(window);
        }
        UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    }

    [[nodiscard]] bool create(bool hidden) noexcept {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0) {
            return false;
        }

        RECT area{0, 0, kInitialWidth, kInitialHeight};
        AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
        window = CreateWindowExW(
            0,
            kWindowClass,
            L"HeniaUI - Complete Widget Gallery",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            area.right - area.left,
            area.bottom - area.top,
            nullptr,
            nullptr,
            instance,
            nullptr);
        if (window == nullptr) {
            return false;
        }
        deviceContext = GetDC(window);
        if (deviceContext == nullptr) {
            return false;
        }

        PIXELFORMATDESCRIPTOR pixelFormat{};
        pixelFormat.nSize = sizeof(pixelFormat);
        pixelFormat.nVersion = 1;
        pixelFormat.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pixelFormat.iPixelType = PFD_TYPE_RGBA;
        pixelFormat.cColorBits = 32;
        pixelFormat.cAlphaBits = 8;
        const int selected = ChoosePixelFormat(deviceContext, &pixelFormat);
        if (selected == 0 || !SetPixelFormat(deviceContext, selected, &pixelFormat)) {
            return false;
        }

        HGLRC legacy = wglCreateContext(deviceContext);
        if (legacy == nullptr || !wglMakeCurrent(deviceContext, legacy)) {
            return false;
        }
        const auto createContext = reinterpret_cast<CreateContextAttributesFn>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        if (createContext != nullptr) {
            constexpr int kContextMajor = 0x2091;
            constexpr int kContextMinor = 0x2092;
            constexpr int kContextProfileMask = 0x9126;
            constexpr int kContextCoreProfile = 0x00000001;
            constexpr std::array attributes{
                kContextMajor,
                3,
                kContextMinor,
                3,
                kContextProfileMask,
                kContextCoreProfile,
                0,
            };
            HGLRC modern = createContext(deviceContext, nullptr, attributes.data());
            if (modern != nullptr) {
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(legacy);
                legacy = modern;
                if (!wglMakeCurrent(deviceContext, legacy)) {
                    wglDeleteContext(legacy);
                    return false;
                }
            }
        }
        renderContext = legacy;

        const auto swapInterval = reinterpret_cast<SwapIntervalFn>(
            wglGetProcAddress("wglSwapIntervalEXT"));
        if (swapInterval != nullptr) {
            swapInterval(1);
        }
        ShowWindow(window, hidden ? SW_HIDE : SW_SHOW);
        UpdateWindow(window);
        return true;
    }
};

struct WindowInputAttachment final {
    WindowInputAttachment(HWND windowValue, Win32InputAdapter& input) noexcept
        : window(windowValue) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&input));
    }

    ~WindowInputAttachment() {
        if (window != nullptr && IsWindow(window)) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
    }

    WindowInputAttachment(const WindowInputAttachment&) = delete;
    WindowInputAttachment& operator=(const WindowInputAttachment&) = delete;

    HWND window = nullptr;
};

[[nodiscard]] bool commandLineContains(std::wstring_view option) noexcept {
    return std::wstring_view(GetCommandLineW()).find(option) != std::wstring_view::npos;
}

[[nodiscard]] bool saveSnapshot(std::uint32_t width, std::uint32_t height) {
    constexpr GLenum kBgra = 0x80E1;
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0,
        0,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        kBgra,
        GL_UNSIGNED_BYTE,
        pixels.data());

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(width);
    infoHeader.biHeight = static_cast<LONG>(height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(pixels.size());
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + infoHeader.biSizeImage;

    HANDLE file = CreateFileW(
        L"HeniaUIWidgetGallery.bmp",
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool result = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr)
        && written == sizeof(fileHeader)
        && WriteFile(file, &infoHeader, sizeof(infoHeader), &written, nullptr)
        && written == sizeof(infoHeader)
        && WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &written, nullptr)
        && written == pixels.size();
    CloseHandle(file);
    return result;
}

[[nodiscard]] Theme galleryTheme(FontHandle font) noexcept {
    Theme theme;
    theme.canvas = kCanvas;
    theme.panelBackground = {0.0F, 0.0F, 0.0F, 0.0F};
    theme.panelBorder = {0.0F, 0.0F, 0.0F, 0.0F};
    theme.surface = kSurface;
    theme.surfaceRaised = kSurfaceRaised;
    theme.surfaceHover = {0.055F, 0.085F, 0.12F, 1.0F};
    theme.surfacePressed = {0.07F, 0.13F, 0.17F, 1.0F};
    theme.border = kBorder;
    theme.accent = kAccent;
    theme.textPrimary = kText;
    theme.textMuted = kMuted;
    theme.font = font;
    theme.fontSize = 13.5F;
    theme.cornerRadius = 7.0F;
    theme.controlHeight = 36.0F;
    theme.panelGap = 10.0F;
    return theme;
}

[[nodiscard]] PanelStyle cardStyle() noexcept {
    return {
        .background = kSurface,
        .border = kBorder,
        .borderWidth = 1.0F,
        .radius = 11.0F,
        .padding = Insets{16.0F, 14.0F, 16.0F, 16.0F},
        .gap = 9.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    };
}

[[nodiscard]] Panel& addCard(
    Panel& column,
    FontHandle font,
    std::string_view title,
    std::string_view note) {
    Panel& card = column.emplaceChild<Panel>(cardStyle());
    card.emplaceChild<Label>(std::string(title), LabelStyle{font, 17.0F, kText});
    card.emplaceChild<Label>(std::string(note), LabelStyle{font, 11.5F, kMuted});
    return card;
}

struct GalleryState final {
    void setStatus(std::string textValue) {
        statusText = std::move(textValue);
        if (status != nullptr) {
            status->setText(statusText);
        }
    }

    void primaryClicked() {
        ++clickCount;
        setStatus("Primary button clicked " + std::to_string(clickCount) + " time(s)");
    }
    void checkboxChanged(bool value) {
        checkbox = value;
        setStatus(value ? "Checkbox enabled" : "Checkbox disabled");
    }
    void toggleChanged(bool value) {
        toggle = value;
        setStatus(value ? "Toggle switched on" : "Toggle switched off");
    }
    void sliderChanged(double value) {
        slider = value;
        setStatus(numberStatus("Slider", value));
    }
    void numberChanged(double value) {
        number = value;
        setStatus(numberStatus("Numeric input", value));
    }
    void textChanged(std::string_view value) {
        text.assign(value);
        setStatus("Text input updated (" + std::to_string(value.size()) + " UTF-8 bytes)");
    }
    void comboChanged(std::size_t value) {
        combo = value;
        setStatus("ComboBox selected item " + std::to_string(value + 1U));
    }
    void tabChanged(std::size_t value) {
        tab = value;
        setStatus("TabBar selected tab " + std::to_string(value + 1U));
    }
    void listChanged(std::size_t value) {
        list = value;
        setStatus("ListView selected row " + std::to_string(value + 1U));
    }
    void treeChanged(std::size_t value) {
        tree = value;
        setStatus("TreeView selected node " + std::to_string(value + 1U));
    }
    void treeExpansionChanged(std::size_t value, bool expanded) {
        setStatus(
            "Tree node " + std::to_string(value + 1U)
            + (expanded ? " expanded" : " collapsed"));
    }
    void colorChanged(Color value) {
        color = value;
        setStatus(numberStatus("Color red channel", value.red));
    }
    void bindingChanged(KeyCode value) {
        binding = value;
        setStatus("Key binding changed to " + std::string(keyCodeName(value)));
    }
    void openPopup() {
        if (popup != nullptr) {
            popup->setOpen(true);
            setStatus("Modal PopupLayer opened");
        }
    }
    void closePopup() {
        if (popup != nullptr) {
            popup->setOpen(false);
            setStatus("Modal PopupLayer closed");
        }
    }
    void popupDismissed() { setStatus("Popup dismissed from backdrop"); }

    [[nodiscard]] static std::string numberStatus(
        std::string_view prefix,
        double value) {
        std::array<char, 96> buffer{};
        const int written = std::snprintf(
            buffer.data(),
            buffer.size(),
            "%.*s: %.2f",
            static_cast<int>(prefix.size()),
            prefix.data(),
            value);
        if (written <= 0) {
            return std::string(prefix);
        }
        return std::string(
            buffer.data(),
            std::min(static_cast<std::size_t>(written), buffer.size() - 1U));
    }

    PopupLayer* popup = nullptr;
    Label* status = nullptr;
    std::string statusText = "Ready - use mouse, wheel, Tab, arrows, Enter and Space";
    std::string text = "Editable UTF-8: \u4E2D\u6587 / \u65E5\u672C\u8A9E / \uD55C\uAD6D\uC5B4";
    Color color{0.12F, 0.64F, 0.92F, 1.0F};
    KeyCode binding = KeyCode::F9;
    double slider = 0.62;
    double number = 42.5;
    std::size_t combo = 1;
    std::size_t tab = 0;
    std::size_t list = 2;
    std::size_t tree = 1;
    std::size_t clickCount = 0;
    bool checkbox = true;
    bool toggle = false;
};

[[nodiscard]] std::unique_ptr<Panel> createScrollableContent(
    FontHandle font,
    GalleryState& state,
    Win32Clipboard& clipboard) {
    auto content = std::make_unique<Panel>(PanelStyle{
        .background = kCanvas,
        .padding = {28.0F, 22.0F, 28.0F, 26.0F},
        .gap = 16.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });

    Panel& heading = content->emplaceChild<Panel>(PanelStyle{
        .gap = 5.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    heading.emplaceChild<Label>(
        "HeniaUI / Complete Widget Gallery", LabelStyle{font, 27.0F, kText});
    heading.emplaceChild<Label>(
        "Every retained control in one native OpenGL demo - no ImGui dependency",
        LabelStyle{font, 12.5F, kMuted});

    Panel& grid = content->emplaceChild<Panel>(PanelStyle{
        .gap = 14.0F,
        .direction = LayoutDirection::Row,
        .stretchCrossAxis = false,
    });

    Panel& first = grid.emplaceChild<Panel>(PanelStyle{
        .gap = 14.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    first.setLayoutParameters({.flexGrow = 1.0F});

    Panel& basics = addCard(
        first, font, "Basics", "Label / Button / Checkbox / Toggle / Panel");
    Button& primary = basics.emplaceChild<Button>("Primary action");
    primary.setOnClick(
        Callback<>::bind<GalleryState, &GalleryState::primaryClicked>(state));
    Button& disabled = basics.emplaceChild<Button>(
        "Disabled action",
        ButtonStyle{
            .textColor = Color{0.35F, 0.42F, 0.49F, 1.0F},
            .background = Color{0.024F, 0.034F, 0.049F, 1.0F},
            .hover = Color{0.024F, 0.034F, 0.049F, 1.0F},
            .pressed = Color{0.024F, 0.034F, 0.049F, 1.0F},
            .border = Color{0.08F, 0.12F, 0.17F, 1.0F},
        });
    disabled.setEnabled(false);
    Panel& toggles = basics.emplaceChild<Panel>(PanelStyle{
        .gap = 18.0F,
        .direction = LayoutDirection::Row,
        .stretchCrossAxis = true,
    });
    Checkbox& checkbox = toggles.emplaceChild<Checkbox>(
        "Checkbox", state.checkbox, ToggleStyle{.font = font});
    checkbox.setLayoutParameters({.flexGrow = 1.0F});
    checkbox.setOnChanged(
        Callback<bool>::bind<GalleryState, &GalleryState::checkboxChanged>(state));
    Toggle& toggle = toggles.emplaceChild<Toggle>(
        "Toggle", state.toggle, ToggleStyle{.font = font});
    toggle.setLayoutParameters({.flexGrow = 1.0F});
    toggle.setOnChanged(
        Callback<bool>::bind<GalleryState, &GalleryState::toggleChanged>(state));

    Panel& textCard = addCard(
        first, font, "Text editing", "Single-line, multiline, selection and clipboard");
    TextInput& singleLine = textCard.emplaceChild<TextInput>(
        state.text, TextInputStyle{.font = font, .controlWidth = 270.0F});
    singleLine.setPlaceholder("Type here...");
    singleLine.setClipboard(&clipboard);
    singleLine.setOnTextChanged(
        Callback<std::string_view>::bind<GalleryState, &GalleryState::textChanged>(state));
    TextInput& multiline = textCard.emplaceChild<TextInput>(
        "Multiline editor\nCtrl+C / Ctrl+V supported",
        TextInputStyle{
            .font = font,
            .controlWidth = 270.0F,
            .controlHeight = 76.0F,
            .multiline = true,
        });
    multiline.setClipboard(&clipboard);

    Panel& multilingual = addCard(
        first, font, "Multilingual text", "On-demand DirectWrite glyphs / bounded main-thread commit");
    multilingual.emplaceChild<Label>(
        "English: HeniaUI async atlas", LabelStyle{font, 13.0F, kText});
    multilingual.emplaceChild<Label>(
        "\u4E2D\u6587: \u5F02\u6B65\u5B57\u4F53\u70D8\u7119",
        LabelStyle{font, 13.0F, kText});
    multilingual.emplaceChild<Label>(
        "\u65E5\u672C\u8A9E: \u975E\u540C\u671F\u30D5\u30A9\u30F3\u30C8",
        LabelStyle{font, 13.0F, kText});
    multilingual.emplaceChild<Label>(
        "\uD55C\uAD6D\uC5B4: \uBE44\uB3D9\uAE30 \uAE00\uAF34",
        LabelStyle{font, 13.0F, kText});
    multilingual.emplaceChild<Label>(
        "Caf\u00E9 / \u03A9 / \u221E / \u2605 / \U0001F525",
        LabelStyle{font, 13.0F, kMuted});

    Panel& values = addCard(
        first, font, "Values", "Slider and keyboard-editable NumericInput");
    Slider& slider = values.emplaceChild<Slider>(
        state.slider, 0.0, 1.0, 0.01, SliderStyle{.width = 270.0F});
    slider.setOnValueChanged(
        Callback<double>::bind<GalleryState, &GalleryState::sliderChanged>(state));
    NumericInput& numeric = values.emplaceChild<NumericInput>(
        state.number,
        NumericInputStyle{
            .font = font,
            .fontSize = 13.5F,
            .controlWidth = 270.0F,
            .controlHeight = 36.0F,
            .stepButtonWidth = 40.0F,
        });
    numeric.setRange(-100.0, 100.0);
    numeric.setStep(0.5);
    numeric.setPrecision(1);
    numeric.setOnValueChanged(
        Callback<double>::bind<GalleryState, &GalleryState::numberChanged>(state));

    Panel& second = grid.emplaceChild<Panel>(PanelStyle{
        .gap = 14.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    second.setLayoutParameters({.flexGrow = 1.0F});

    Panel& selection = addCard(
        second, font, "Selection", "ComboBox and TabBar keyboard navigation");
    ComboBox& combo = selection.emplaceChild<ComboBox>(
        std::vector<std::string>{"OpenGL", "Direct3D 12", "Host-defined"},
        state.combo,
        ComboBoxStyle{.font = font, .width = 270.0F});
    combo.setOnSelectionChanged(
        Callback<std::size_t>::bind<GalleryState, &GalleryState::comboChanged>(state));
    TabBar& tabs = selection.emplaceChild<TabBar>(
        std::vector<std::string>{"Preview", "Metrics", "Theme"},
        state.tab,
        TabBarStyle{.font = font, .width = 270.0F});
    tabs.setOnSelectionChanged(
        Callback<std::size_t>::bind<GalleryState, &GalleryState::tabChanged>(state));

    std::vector<std::string> rows;
    rows.reserve(250);
    for (std::size_t index = 0; index < 250; ++index) {
        rows.push_back("Virtual row " + std::to_string(index + 1U));
    }
    Panel& listCard = addCard(
        second, font, "ListView", "250 rows, clipped painting and selection reveal");
    ListView& list = listCard.emplaceChild<ListView>(
        std::move(rows),
        ListViewStyle{
            .font = font,
            .width = 280.0F,
            .height = 166.0F,
            .rowHeight = 27.0F,
        });
    list.setSelectedIndex(state.list);
    list.setScrollOffset(0.0F);
    list.setOnSelectionChanged(
        Callback<std::size_t>::bind<GalleryState, &GalleryState::listChanged>(state));

    auto scrollingContent = std::make_unique<Panel>(PanelStyle{
        .padding = {10.0F, 8.0F, 10.0F, 8.0F},
        .gap = 7.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    for (std::size_t index = 0; index < 14; ++index) {
        scrollingContent->emplaceChild<Label>(
            "Scrollable content line " + std::to_string(index + 1U),
            LabelStyle{font, 12.5F, index % 2U == 0 ? kText : kMuted});
    }
    Panel& scrollCard = addCard(
        second, font, "ScrollContainer", "Wheel, arrows, PageUp/PageDown, Home/End");
    scrollCard.emplaceChild<ScrollContainer>(
        std::move(scrollingContent),
        ScrollContainerStyle{
            .background = {0.018F, 0.028F, 0.043F, 1.0F},
            .width = 280.0F,
            .height = 126.0F,
        });

    Panel& third = grid.emplaceChild<Panel>(PanelStyle{
        .gap = 14.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    third.setLayoutParameters({.flexGrow = 1.0F});

    Panel& treeCard = addCard(
        third, font, "TreeView", "Disclosure, hierarchy, scrolling and keyboard traversal");
    TreeView& tree = treeCard.emplaceChild<TreeView>(
        std::vector<TreeViewNode>{
            {"HeniaUI", kTreeRoot, true},
            {"Core", 0, true},
            {"Canvas", 1, true},
            {"RenderPacket", 1, true},
            {"Widgets", 0, true},
            {"Inputs", 4, true},
            {"Data views", 4, true},
            {"Backends", 0, true},
            {"OpenGL", 7, true},
            {"Direct3D 12", 7, true},
        },
        TreeViewStyle{
            .font = font,
            .width = 280.0F,
            .height = 174.0F,
            .rowHeight = 27.0F,
        });
    tree.setSelectedIndex(state.tree);
    tree.setScrollOffset(0.0F);
    tree.setOnSelectionChanged(
        Callback<std::size_t>::bind<GalleryState, &GalleryState::treeChanged>(state));
    tree.setOnExpansionChanged(
        Callback<std::size_t, bool>::bind<
            GalleryState, &GalleryState::treeExpansionChanged>(state));

    Panel& colorCard = addCard(
        third, font, "Color and key input", "ColorPicker plus KeyBindingEditor capture mode");
    ColorPicker& color = colorCard.emplaceChild<ColorPicker>(
        state.color, ColorPickerStyle{.width = 260.0F, .height = 152.0F});
    color.setOnColorChanged(
        Callback<Color>::bind<GalleryState, &GalleryState::colorChanged>(state));
    KeyBindingEditor& binding = colorCard.emplaceChild<KeyBindingEditor>(
        state.binding, KeyBindingEditorStyle{.font = font, .width = 260.0F});
    binding.setOnBindingChanged(
        Callback<KeyCode>::bind<GalleryState, &GalleryState::bindingChanged>(state));

    Panel& overlays = addCard(
        third, font, "Overlays", "Passive Tooltip and modal PopupLayer");
    overlays.emplaceChild<Tooltip>(
        "Tooltip preview - host controls hover delay", TooltipStyle{.font = font});
    Button& popupButton = overlays.emplaceChild<Button>("Open modal popup");
    popupButton.setOnClick(
        Callback<>::bind<GalleryState, &GalleryState::openPopup>(state));

    Panel& statusPanel = content->emplaceChild<Panel>(PanelStyle{
        .background = Color{0.020F, 0.033F, 0.050F, 1.0F},
        .border = kBorder,
        .borderWidth = 1.0F,
        .radius = 8.0F,
        .padding = Insets{13.0F, 9.0F, 13.0F, 9.0F},
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    state.status = &statusPanel.emplaceChild<Label>(
        state.statusText, LabelStyle{font, 12.0F, kMuted});
    return content;
}

[[nodiscard]] std::unique_ptr<PopupLayer> createGallery(
    FontHandle font,
    GalleryState& state,
    Win32Clipboard& clipboard) {
    auto scroll = std::make_unique<ScrollContainer>(
        createScrollableContent(font, state, clipboard),
        ScrollContainerStyle{
            .background = kCanvas,
            .border = {0.0F, 0.0F, 0.0F, 0.0F},
            .width = static_cast<float>(kInitialWidth),
            .height = static_cast<float>(kInitialHeight),
            .wheelStep = 52.0F,
            .scrollbarWidth = 7.0F,
            .radius = 0.0F,
        });

    auto popup = std::make_unique<Panel>(PanelStyle{
        .background = Color{0.028F, 0.044F, 0.066F, 1.0F},
        .border = kAccent,
        .borderWidth = 1.0F,
        .radius = 13.0F,
        .padding = Insets{24.0F, 22.0F, 24.0F, 24.0F},
        .gap = 12.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    popup->emplaceChild<Label>(
        "PopupLayer / Modal dialog", LabelStyle{font, 22.0F, kText});
    popup->emplaceChild<Label>(
        "This subtree owns its backdrop, focus boundary and paint order.",
        LabelStyle{font, 12.5F, kMuted});
    popup->emplaceChild<Checkbox>(
        "Remember this choice", true, ToggleStyle{.font = font});
    Button& close = popup->emplaceChild<Button>("Close popup");
    close.setOnClick(Callback<>::bind<GalleryState, &GalleryState::closePopup>(state));

    auto layer = std::make_unique<PopupLayer>(
        std::move(scroll),
        std::move(popup),
        Rect{{510.0F, 255.0F}, {1050.0F, 565.0F}},
        PopupLayerStyle{.backdrop = {0.0F, 0.0F, 0.0F, 0.64F}});
    state.popup = layer.get();
    layer->setOnDismissed(
        Callback<>::bind<GalleryState, &GalleryState::popupDismissed>(state));
    return layer;
}

class GalleryDpiHost final {
public:
    GalleryDpiHost(
        HWND window,
        UiDocument& document,
        TextureStore& textures,
        OpenGlRenderer& renderer,
        Win32FontScaleCache& fonts,
        Win32Clipboard& clipboard,
        GalleryState& state,
        FontHandle initialFont) noexcept
        : mWindow(window),
          mDocument(&document),
          mTextures(&textures),
          mRenderer(&renderer),
          mFonts(&fonts),
          mClipboard(&clipboard),
          mState(&state),
          mFont(initialFont) {}

    void changed(const Win32DpiChange& change) {
        if (change.hasSuggestedWindowRect) {
            const RECT& area = change.suggestedWindowRect;
            SetWindowPos(
                mWindow,
                nullptr,
                area.left,
                area.top,
                area.right - area.left,
                area.bottom - area.top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        const FontHandle selected = mFonts->selectForDpi(change.dpiY);
        if (!selected.valid() || !mRenderer->synchronizeTextures(*mTextures)) {
            mFailed = true;
            return;
        }
        if (selected != mFont) {
            mFont = selected;
            mDocument->setTheme(galleryTheme(mFont));
            mDocument->setRoot(createGallery(mFont, *mState, *mClipboard));
        }
        updateCoordinateSpace(change.dpiX, change.dpiY);
    }

    void updateCoordinateSpace(std::uint32_t dpiX, std::uint32_t dpiY) {
        RECT client{};
        if (!GetClientRect(mWindow, &client)) {
            mFailed = true;
            return;
        }
        const std::uint32_t width = static_cast<std::uint32_t>(
            std::max(client.right - client.left, 1L));
        const std::uint32_t height = static_cast<std::uint32_t>(
            std::max(client.bottom - client.top, 1L));
        const Vec2 inputExtent{static_cast<float>(width), static_cast<float>(height)};
        const Vec2 logicalViewport{
            inputExtent.x * 96.0F / static_cast<float>(std::max(dpiX, 1U)),
            inputExtent.y * 96.0F / static_cast<float>(std::max(dpiY, 1U)),
        };
        if (!mDocument->setCoordinateSpace(makeUiCoordinateSpace(
                logicalViewport,
                inputExtent,
                width,
                height,
                static_cast<float>(std::max(dpiY, 1U)) / 96.0F))) {
            mFailed = true;
        }
    }

    [[nodiscard]] bool failed() const noexcept { return mFailed; }

private:
    HWND mWindow = nullptr;
    UiDocument* mDocument = nullptr;
    TextureStore* mTextures = nullptr;
    OpenGlRenderer* mRenderer = nullptr;
    Win32FontScaleCache* mFonts = nullptr;
    Win32Clipboard* mClipboard = nullptr;
    GalleryState* mState = nullptr;
    FontHandle mFont{};
    bool mFailed = false;
};

void showRendererError(HWND window, const wchar_t* title, std::string_view error) {
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, error.data(), static_cast<int>(error.size()), nullptr, 0);
    std::wstring message(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    if (length > 0) {
        MultiByteToWideChar(
            CP_UTF8,
            0,
            error.data(),
            static_cast<int>(error.size()),
            message.data(),
            length);
    }
    MessageBoxW(window, message.c_str(), title, MB_ICONERROR);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (commandLineContains(L"--help")) {
        return 0;
    }
    static_cast<void>(SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    const bool headless = commandLineContains(L"--headless");
    const bool snapshot = commandLineContains(L"--snapshot");

    NativeWindow native;
    if (!native.create(headless)) {
        MessageBoxW(
            nullptr,
            L"Unable to create the widget gallery window.",
            L"HeniaUI",
            MB_ICONERROR);
        return 1;
    }

    TextureStore textures;
    FontStore fonts;
    constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
    Win32FontScaleCache fontScaleCache(textures, fonts, {
        .family = L"Segoe UI",
        .logicalPixelHeight = 32.0F,
        .atlasWidth = 1024,
        .atlasHeight = 512,
        .ranges = ranges,
    });
    const std::uint32_t initialDpi = std::max(GetDpiForWindow(native.window), 1U);
    const FontHandle font = fontScaleCache.selectForDpi(initialDpi);
    if (!font.valid()) {
        MessageBoxW(
            native.window,
            L"Unable to build the Segoe UI atlas.",
            L"HeniaUI",
            MB_ICONERROR);
        return 2;
    }

    Win32AsyncFontSet asyncFonts(textures, fonts, {
        .primaryFont = font,
        .logicalPixelHeight = 32.0F,
        .dpiScale = static_cast<float>(initialDpi) / 96.0F,
        .initialAtlasWidth = 1024,
        .initialAtlasHeight = 512,
        .dynamicAtlas = {
            .pageWidth = 1024,
            .pageHeight = 1024,
            .padding = 1,
            .maximumPages = 8,
        },
        .preallocatedPagesPerFace = 1,
    });
    if (!asyncFonts.valid()) {
        showRendererError(native.window, L"HeniaUI multilingual fonts", asyncFonts.lastError());
        return 2;
    }
    static_cast<void>(asyncFonts.requestUtf8(kMultilingualSample));

    TextRunCache textCache(fonts);
    textCache.reserve(2048, 512);
    TextPainter text(textCache);
    text.setFallbackFonts(asyncFonts.fontChain(Win32FontLocale::SimplifiedChinese));
    text.setGlyphRequestBackend(&asyncFonts);
    Win32Clipboard clipboard(native.window);
    GalleryState state;
    UiDocument document(text, galleryTheme(font));
    document.reserve(16384, 65536, 256);
    document.setRoot(createGallery(font, state, clipboard));
    Win32InputAdapter input(document);
    [[maybe_unused]] WindowInputAttachment inputAttachment(native.window, input);

    OpenGlRenderer renderer;
    if (!renderer.initialize(65536) || !renderer.synchronizeTextures(textures)) {
        showRendererError(
            native.window,
            L"HeniaUI OpenGL initialization",
            renderer.lastError());
        return 3;
    }

    GalleryDpiHost dpiHost(
        native.window,
        document,
        textures,
        renderer,
        fontScaleCache,
        clipboard,
        state,
        font);
    input.setOnDpiChanged(
        Callback<const Win32DpiChange&>::bind<
            GalleryDpiHost, &GalleryDpiHost::changed>(dpiHost));
    dpiHost.updateCoordinateSpace(initialDpi, initialDpi);

    MSG message{};
    bool running = true;
    int result = 0;
    int headlessFrames = 0;
    bool snapshotSaved = false;
    while (running) {
        const auto frameStarted = std::chrono::steady_clock::now();
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }
        if (dpiHost.failed()) {
            result = 6;
            break;
        }

        if (asyncFonts.commitReady(32) != 0) {
            if (!renderer.synchronizeTextures(textures)) {
                result = 7;
                break;
            }
            document.invalidateTypography();
        }

        RECT client{};
        GetClientRect(native.window, &client);
        const std::uint32_t width = static_cast<std::uint32_t>(
            std::max(client.right - client.left, 1L));
        const std::uint32_t height = static_cast<std::uint32_t>(
            std::max(client.bottom - client.top, 1L));
        const std::uint32_t currentDpi = std::max(GetDpiForWindow(native.window), 1U);
        dpiHost.updateCoordinateSpace(currentDpi, currentDpi);
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glClearColor(kCanvas.red, kCanvas.green, kCanvas.blue, kCanvas.alpha);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!renderer.render(document.compose(), document.coordinateSpace().render)) {
            result = 4;
            break;
        }
        const bool fontsSettled = asyncFonts.idle();
        const bool captureSnapshot = snapshot && !snapshotSaved
            && (!headless || (headlessFrames >= 2 && fontsSettled));
        if (captureSnapshot) {
            glFinish();
            if (!saveSnapshot(width, height)) {
                result = 5;
                break;
            }
            snapshotSaved = true;
        }
        if (!headless) {
            SwapBuffers(native.deviceContext);
        }

        if (!headless) {
            constexpr auto minimumFrameTime = std::chrono::microseconds(6945);
            std::this_thread::sleep_until(frameStarted + minimumFrameTime);
        }
        if (headless && ++headlessFrames >= 240) {
            result = fontsSettled ? result : 8;
            break;
        }
        if (headless && headlessFrames >= 3 && fontsSettled) {
            break;
        }
    }

    if (!renderer.shutdown()) {
        return 7;
    }
    return result;
}
