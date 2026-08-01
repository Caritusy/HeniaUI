#include "henia/ui/Frame.h"
#include "henia/ui/backend/opengl/OpenGlRenderer.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/text/TextLayout.h"

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

void drawInterface(
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

[[nodiscard]] bool isHeadless() noexcept {
    return std::wstring_view(GetCommandLineW()).find(L"--headless") != std::wstring_view::npos;
}

[[nodiscard]] bool wantsSnapshot() noexcept {
    return std::wstring_view(GetCommandLineW()).find(L"--snapshot") != std::wstring_view::npos;
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
    Frame frame;
    frame.reserve(32768, 256);

    OpenGlRenderer renderer;
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

        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glClearColor(0.012F, 0.018F, 0.029F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        Canvas& canvas = frame.begin();
        drawInterface(canvas, text, font, static_cast<float>(width), static_cast<float>(height), time);
        if (!renderer.render(frame.finish(), width, height)) {
            result = 4;
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

    renderer.shutdown();
    return result;
}
