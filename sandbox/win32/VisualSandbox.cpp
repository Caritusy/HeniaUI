#include "henia/ui/Frame.h"
#include "henia/ui/backend/opengl/OpenGlRenderer.h"
#include "henia/gfx/Math.h"
#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/platform/win32/Win32InputAdapter.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/UiDocument.h"
#include "henia/ui/widget/controls/Button.h"
#include "henia/ui/widget/controls/Label.h"
#include "henia/ui/widget/controls/NumericInput.h"
#include "henia/ui/widget/controls/Panel.h"

#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using namespace henia::ui;

constexpr wchar_t kWindowClass[] = L"HeniaUIVisualSandbox";
constexpr int kInitialWidth = 1180;
constexpr int kInitialHeight = 760;

using CreateContextAttributesFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using SwapIntervalFn = BOOL(WINAPI*)(int);

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_KEYDOWN && wordParameter == VK_ESCAPE) {
        DestroyWindow(window);
        return 0;
    }
    auto* input = reinterpret_cast<Win32InputAdapter*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (input != nullptr && input->handleMessage(window, message, wordParameter, longParameter)) {
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
            L"HeniaUI - Native OpenGL Sandbox",
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
        pixelFormat.cDepthBits = 24;
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

        const auto swapInterval = reinterpret_cast<SwapIntervalFn>(wglGetProcAddress("wglSwapIntervalEXT"));
        if (swapInterval != nullptr) {
            swapInterval(1);
        }

        ShowWindow(window, hidden ? SW_HIDE : SW_SHOW);
        UpdateWindow(window);
        return true;
    }
};

[[maybe_unused]] void drawInterface(
    Canvas& canvas,
    TextPainter& text,
    FontHandle font,
    float width,
    float height,
    float time) {
    const Color background{0.018F, 0.027F, 0.043F, 1.0F};
    const Color surface{0.032F, 0.047F, 0.071F, 0.98F};
    const Color surfaceRaised{0.046F, 0.064F, 0.092F, 1.0F};
    const Color border{0.12F, 0.20F, 0.28F, 1.0F};
    const Color accent{0.10F, 0.72F, 0.91F, 1.0F};
    const Color textPrimary{0.90F, 0.95F, 0.98F, 1.0F};
    const Color textMuted{0.48F, 0.59F, 0.67F, 1.0F};

    canvas.fillRect({{0.0F, 0.0F}, {width, height}}, background);

    const Rect shell{{28.0F, 28.0F}, {width - 28.0F, height - 28.0F}};
    canvas.fillRect(shell, surface, 18.0F);
    canvas.strokeRect(shell, border, 18.0F, 1.0F);

    const Rect sidebar{{28.0F, 28.0F}, {270.0F, height - 28.0F}};
    canvas.fillRect(sidebar, {0.025F, 0.037F, 0.057F, 1.0F}, 18.0F);
    canvas.line({270.0F, 50.0F}, {270.0F, height - 50.0F}, border, 1.0F);
    text.draw(canvas, font, 26.0F, {58.0F, 60.0F}, textPrimary, "HeniaUI");
    text.draw(canvas, font, 13.0F, {59.0F, 96.0F}, textMuted, "Native rendering workspace");

    constexpr std::array<std::string_view, 5> navigation{
        "Overview", "Components", "Typography", "Performance", "Settings"};
    for (std::size_t index = 0; index < navigation.size(); ++index) {
        const float y = 145.0F + static_cast<float>(index) * 48.0F;
        if (index == 0) {
            canvas.fillRect({{46.0F, y - 11.0F}, {252.0F, y + 25.0F}}, {0.07F, 0.20F, 0.27F, 1.0F}, 8.0F);
            canvas.fillRect({{46.0F, y - 2.0F}, {49.0F, y + 16.0F}}, accent, 1.5F);
        }
        text.draw(
            canvas,
            font,
            15.0F,
            {66.0F, y},
            index == 0 ? textPrimary : textMuted,
            navigation[index]);
    }

    const float contentLeft = 310.0F;
    text.draw(canvas, font, 28.0F, {contentLeft, 62.0F}, textPrimary, "Rendering overview");
    text.draw(
        canvas,
        font,
        14.0F,
        {contentLeft, 101.0F},
        textMuted,
        "One ordered display list. One native UI pipeline. No ImGui dependency.");

    const float available = width - contentLeft - 58.0F;
    const float cardGap = 16.0F;
    const float cardWidth = (available - cardGap * 2.0F) / 3.0F;
    constexpr std::array<std::string_view, 3> cardTitles{"Draw batches", "UI instances", "Frame storage"};
    constexpr std::array<std::string_view, 3> cardValues{"1", "4,178", "Stable"};
    constexpr std::array<std::string_view, 3> cardNotes{
        "shapes + glyph atlas", "ordered and merged", "zero growth after warm-up"};
    for (std::size_t index = 0; index < cardTitles.size(); ++index) {
        const float left = contentLeft + static_cast<float>(index) * (cardWidth + cardGap);
        const Rect card{{left, 145.0F}, {left + cardWidth, 275.0F}};
        canvas.fillRect(card, surfaceRaised, 12.0F);
        canvas.strokeRect(card, border, 12.0F, 1.0F);
        text.draw(canvas, font, 13.0F, {left + 18.0F, 166.0F}, textMuted, cardTitles[index]);
        text.draw(canvas, font, 30.0F, {left + 18.0F, 196.0F}, textPrimary, cardValues[index]);
        text.draw(canvas, font, 12.0F, {left + 18.0F, 242.0F}, textMuted, cardNotes[index]);
    }

    const Rect chart{{contentLeft, 302.0F}, {contentLeft + available, height - 72.0F}};
    canvas.fillRect(chart, surfaceRaised, 12.0F);
    canvas.strokeRect(chart, border, 12.0F, 1.0F);
    text.draw(canvas, font, 16.0F, {contentLeft + 20.0F, 325.0F}, textPrimary, "Frame pacing");
    text.draw(canvas, font, 12.0F, {contentLeft + 20.0F, 352.0F}, textMuted, "Analytic shapes and cached text runs");

    const float chartLeft = contentLeft + 22.0F;
    const float chartRight = contentLeft + available - 22.0F;
    const float chartTop = 395.0F;
    const float chartBottom = height - 100.0F;
    for (int line = 0; line <= 4; ++line) {
        const float y = chartTop + (chartBottom - chartTop) * static_cast<float>(line) / 4.0F;
        canvas.line({chartLeft, y}, {chartRight, y}, {0.10F, 0.15F, 0.21F, 1.0F}, 1.0F);
    }

    std::array<Vec2, 96> samples{};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const float phase = static_cast<float>(index) * 0.19F + time * 1.2F;
        const float normalized = 0.54F + std::sin(phase) * 0.12F + std::sin(phase * 0.37F) * 0.08F;
        samples[index] = {
            chartLeft + (chartRight - chartLeft) * static_cast<float>(index) / static_cast<float>(samples.size() - 1),
            chartBottom - (chartBottom - chartTop) * normalized,
        };
    }
    canvas.polyline(samples, accent, 2.0F, false);
}

struct SandboxControls final {
    void lineWidthChanged(double value) noexcept { lineWidth = value; }
    void toggleAnimation() noexcept { animationPaused = !animationPaused; }

    double lineWidth = 1.75;
    bool animationPaused = false;
};

[[nodiscard]] std::vector<henia::gfx::BoxInstance> createBoxField(float lineWidth) {
    using namespace henia::gfx;
    constexpr int columns = 24;
    constexpr int rows = 10;
    constexpr int layers = 24;
    constexpr float spacing = 1.35F;
    std::vector<BoxInstance> boxes;
    boxes.reserve(static_cast<std::size_t>(columns * rows * layers));
    for (int z = 0; z < layers; ++z) {
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                const float left = (static_cast<float>(x) - static_cast<float>(columns - 1) * 0.5F) * spacing;
                const float bottom = (static_cast<float>(y) - static_cast<float>(rows - 1) * 0.5F) * spacing;
                const float front = (static_cast<float>(z) - static_cast<float>(layers - 1) * 0.5F) * spacing;
                const std::size_t index = boxes.size();
                boxes.push_back({
                    .minimum = {left - 0.40F, bottom - 0.40F, front - 0.40F},
                    .lineWidth = lineWidth,
                    .maximum = {left + 0.40F, bottom + 0.40F, front + 0.40F},
                    .hueOffset = static_cast<float>(index % 997U) / 997.0F,
                    .color = {0.88F, 0.94F, 1.0F, 0.34F},
                    .effects = BoxEffect::HueCycle,
                });
            }
        }
    }
    return boxes;
}

[[nodiscard]] std::unique_ptr<Panel> createOverlay(
    FontHandle font,
    SandboxControls& controls) {
    auto root = std::make_unique<Panel>(PanelStyle{
        .padding = {28.0F, 28.0F, 28.0F, 28.0F},
        .gap = 0.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = false,
    });
    Panel& card = root->emplaceChild<Panel>(PanelStyle{
        .background = {0.025F, 0.037F, 0.057F, 0.93F},
        .border = {0.12F, 0.20F, 0.28F, 1.0F},
        .borderWidth = 1.0F,
        .radius = 14.0F,
        .padding = {20.0F, 18.0F, 20.0F, 20.0F},
        .gap = 11.0F,
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = false,
    });
    card.setLayoutParameters({.width = 390.0F, .height = 292.0F});
    card.emplaceChild<Label>(
        "HeniaUI / GPU instance field",
        LabelStyle{font, 22.0F, {0.90F, 0.95F, 0.98F, 1.0F}});
    card.emplaceChild<Label>(
        "5,760 boxes  /  1 draw call  /  zero CPU tessellation",
        LabelStyle{font, 12.5F, {0.48F, 0.59F, 0.67F, 1.0F}});
    card.emplaceChild<Label>(
        "Camera and hue animate through frame constants.\nStatic instances stay resident on the GPU.",
        LabelStyle{font, 13.0F, {0.64F, 0.73F, 0.79F, 1.0F}});
    NumericInput& width = card.emplaceChild<NumericInput>(
        controls.lineWidth,
        NumericInputStyle{
            .font = font,
            .fontSize = 14.0F,
            .controlWidth = 190.0F,
            .controlHeight = 38.0F,
            .stepButtonWidth = 40.0F,
        });
    width.setRange(0.5, 6.0);
    width.setStep(0.25);
    width.setPrecision(2);
    width.setOnValueChanged(
        Callback<double>::bind<SandboxControls, &SandboxControls::lineWidthChanged>(controls));
    Button& toggle = card.emplaceChild<Button>(
        "Pause / resume camera",
        ButtonStyle{.font = font, .fontSize = 14.0F});
    toggle.setLayoutParameters({.width = 190.0F, .height = 38.0F});
    toggle.setOnClick(Callback<>::bind<SandboxControls, &SandboxControls::toggleAnimation>(controls));
    return root;
}

[[nodiscard]] bool isHeadless() noexcept {
    return std::wstring_view(GetCommandLineW()).find(L"--headless") != std::wstring_view::npos;
}

[[nodiscard]] bool wantsSnapshot() noexcept {
    return std::wstring_view(GetCommandLineW()).find(L"--snapshot") != std::wstring_view::npos;
}

[[nodiscard]] bool wantsUiOnly() noexcept {
    return std::wstring_view(GetCommandLineW()).find(L"--ui-only") != std::wstring_view::npos;
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
        L"HeniaUIVisualSandbox.bmp",
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

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const bool headless = isHeadless();
    const bool snapshot = wantsSnapshot();
    const bool uiOnly = wantsUiOnly();
    NativeWindow native;
    if (!native.create(headless)) {
        MessageBoxW(nullptr, L"Unable to create the OpenGL sandbox window.", L"HeniaUI", MB_ICONERROR);
        return 1;
    }

    TextureStore textures;
    FontStore fonts;
    constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
    const FontHandle font = Win32FontLoader::load(
        textures,
        fonts,
        {.family = L"Segoe UI", .pixelHeight = 32, .atlasWidth = 1024, .atlasHeight = 512, .ranges = ranges});
    if (!font.valid()) {
        MessageBoxW(nullptr, L"Unable to build the Segoe UI atlas.", L"HeniaUI", MB_ICONERROR);
        return 2;
    }

    TextRunCache textCache(fonts);
    textCache.reserve(128, 64);
    TextPainter text(textCache);
    SandboxControls controls;
    UiDocument document(text);
    document.reserve(4096, 128);
    document.setRoot(createOverlay(font, controls));
    Win32InputAdapter input(document);
    SetWindowLongPtrW(native.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&input));

    OpenGlRenderer renderer;
    henia::gfx::ShapeBatch3D boxBuilder;
    std::vector<henia::gfx::BoxInstance> boxes = createBoxField(static_cast<float>(controls.lineWidth));
    boxBuilder.replaceBoxes(boxes);
    boxBuilder.setDepthState({
        .enabled = true,
        .writeEnabled = true,
        .compare = henia::gfx::CompareOp::LessEqual,
    });
    henia::gfx::InstanceBatch boxBatch = boxBuilder.snapshot();
    henia::gfx::OpenGlRenderDevice gfxRenderer;
    if (!gfxRenderer.initialize(16384)) {
        const std::string_view error = gfxRenderer.lastError();
        const int length = MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), nullptr, 0);
        std::wstring message(static_cast<std::size_t>(std::max(length, 0)), L'\0');
        if (length > 0) {
            MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), message.data(), length);
        }
        MessageBoxW(native.window, message.c_str(), L"HeniaUI gfx initialization", MB_ICONERROR);
        return 6;
    }
    // Initialize the compositing UI pipeline last so it owns the final context
    // texture/vertex setup before the first host frame begins.
    if (!renderer.initialize(32768) || !renderer.synchronizeTextures(textures)) {
        const std::string_view error = renderer.lastError();
        const int length = MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), nullptr, 0);
        std::wstring message(static_cast<std::size_t>(std::max(length, 0)), L'\0');
        if (length > 0) {
            MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), message.data(), length);
        }
        MessageBoxW(native.window, message.c_str(), L"HeniaUI OpenGL initialization", MB_ICONERROR);
        return 3;
    }

    const auto started = std::chrono::steady_clock::now();
    MSG message{};
    bool running = true;
    int headlessFrames = 0;
    int result = 0;
    while (running) {
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

        RECT client{};
        GetClientRect(native.window, &client);
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(client.right - client.left, 1L));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(client.bottom - client.top, 1L));
        const float time = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();

        const float requestedLineWidth = static_cast<float>(controls.lineWidth);
        if (!boxes.empty() && boxes.front().lineWidth != requestedLineWidth) {
            for (henia::gfx::BoxInstance& box : boxes) {
                box.lineWidth = requestedLineWidth;
            }
            boxBuilder.replaceBoxes(boxes);
            boxBatch = boxBuilder.snapshot();
        }

        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glClearColor(0.012F, 0.018F, 0.029F, 1.0F);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const float sceneTime = controls.animationPaused ? 0.0F : time;
        const float orbit = sceneTime * 0.18F;
        const henia::gfx::Vec3 eye{
            std::sin(orbit) * 48.0F,
            23.0F + std::sin(orbit * 0.7F) * 4.0F,
            std::cos(orbit) * 48.0F,
        };
        const henia::gfx::Mat4 viewMatrix = henia::gfx::lookAt(eye, {}, {0.0F, 1.0F, 0.0F});
        const henia::gfx::Mat4 projection = henia::gfx::perspective(
            1.05F,
            static_cast<float>(width) / static_cast<float>(height),
            0.1F,
            160.0F);
        const henia::gfx::ViewParameters view{
            .viewProjection = henia::gfx::multiply(projection, viewMatrix),
            .viewport = {static_cast<float>(width), static_cast<float>(height)},
            .timeSeconds = sceneTime,
            .clipDepthRange = henia::gfx::ClipDepthRange::ZeroToOne,
        };
        while (glGetError() != GL_NO_ERROR) {}
        if (!uiOnly && !gfxRenderer.render(boxBatch, view, true)) {
            const std::string_view error = gfxRenderer.lastError();
            result = error.find("capture") != std::string_view::npos ? 20
                : (error.find("upload") != std::string_view::npos ? 21
                : (error.find("view") != std::string_view::npos ? 22
                : (error.find("blend") != std::string_view::npos ? 26
                : (error.find("pipeline") != std::string_view::npos ? 23
                : (error.find("draw") != std::string_view::npos ? 24
                : (error.find("restore") != std::string_view::npos ? 25 : 7))))));
            break;
        }
        const GLenum gfxError = glGetError();
        if (!uiOnly && gfxError != GL_NO_ERROR) {
            result = gfxError == GL_INVALID_ENUM ? 10
                : (gfxError == GL_INVALID_VALUE ? 11
                : (gfxError == GL_INVALID_OPERATION ? 12
                : (gfxError == GL_OUT_OF_MEMORY ? 13 : 14)));
            break;
        }

        document.setViewport({static_cast<float>(width), static_cast<float>(height)});
        if (!renderer.render(document.compose(), width, height)) {
            result = 4;
            break;
        }
        const GLenum uiError = glGetError();
        if (uiError != GL_NO_ERROR) {
            result = uiError == GL_INVALID_ENUM ? 30
                : (uiError == GL_INVALID_VALUE ? 31
                : (uiError == GL_INVALID_OPERATION ? 32
                : (uiError == GL_OUT_OF_MEMORY ? 33 : 34)));
            break;
        }
        if (snapshot && headlessFrames == 0 && !saveSnapshot(width, height)) {
            result = 5;
            break;
        }
        SwapBuffers(native.deviceContext);

        if (headless && ++headlessFrames >= 3) {
            break;
        }
    }

    SetWindowLongPtrW(native.window, GWLP_USERDATA, 0);
    gfxRenderer.shutdown();
    renderer.shutdown();
    if (result == 0 && headless) {
        const henia::gfx::OpenGlGfxStatistics statistics = gfxRenderer.statistics();
        if (!uiOnly && (statistics.fullInstanceUploads != 1 || statistics.partialInstanceUploads != 0
            || statistics.drawCalls != static_cast<std::uint64_t>(headlessFrames))) {
            return 8;
        }
    }
    return result;
}
