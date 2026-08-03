#include "henia/ui/Frame.h"
#include "henia/ui/backend/opengl/OpenGlRenderer.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/resource/TextureStore.h"
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
#include <cwchar>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace henia::ui;

constexpr wchar_t kWindowClass[] = L"HeniaUIEffectsExample";
constexpr wchar_t kWindowTitle[] = L"HeniaUI Effects Gallery - ESC to close";
constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 920;
constexpr float kPi = 3.14159265358979323846F;

using CreateContextAttributesFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using SwapIntervalFn = BOOL(WINAPI*)(int);

[[nodiscard]] bool commandLineContains(std::wstring_view option) noexcept {
    return std::wstring_view(GetCommandLineW()).find(option) != std::wstring_view::npos;
}

LRESULT CALLBACK windowProcedure(
    HWND window,
    UINT message,
    WPARAM wordParameter,
    LPARAM longParameter) {
    if (message == WM_CLOSE || (message == WM_KEYDOWN && wordParameter == VK_ESCAPE)) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto* limits = reinterpret_cast<MINMAXINFO*>(longParameter);
        limits->ptMinTrackSize = {1050, 720};
        return 0;
    }
    if (message == WM_DPICHANGED && longParameter != 0) {
        const RECT& suggested = *reinterpret_cast<const RECT*>(longParameter);
        SetWindowPos(
            window,
            nullptr,
            suggested.left,
            suggested.top,
            suggested.right - suggested.left,
            suggested.bottom - suggested.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
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
        if (window != nullptr) DestroyWindow(window);
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
        if (RegisterClassExW(&windowClass) == 0) return false;

        RECT area{0, 0, kInitialWidth, kInitialHeight};
        AdjustWindowRect(&area, WS_OVERLAPPEDWINDOW, FALSE);
        window = CreateWindowExW(
            0,
            kWindowClass,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            area.right - area.left,
            area.bottom - area.top,
            nullptr,
            nullptr,
            instance,
            nullptr);
        if (window == nullptr) return false;

        deviceContext = GetDC(window);
        if (deviceContext == nullptr) return false;
        PIXELFORMATDESCRIPTOR pixelFormat{};
        pixelFormat.nSize = sizeof(pixelFormat);
        pixelFormat.nVersion = 1;
        pixelFormat.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pixelFormat.iPixelType = PFD_TYPE_RGBA;
        pixelFormat.cColorBits = 32;
        pixelFormat.cAlphaBits = 8;
        pixelFormat.cDepthBits = 24;
        const int selected = ChoosePixelFormat(deviceContext, &pixelFormat);
        if (selected == 0 || !SetPixelFormat(deviceContext, selected, &pixelFormat)) return false;

        HGLRC context = wglCreateContext(deviceContext);
        if (context == nullptr || !wglMakeCurrent(deviceContext, context)) return false;
        const auto createContext = reinterpret_cast<CreateContextAttributesFn>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        if (createContext != nullptr) {
            constexpr int kContextMajor = 0x2091;
            constexpr int kContextMinor = 0x2092;
            constexpr int kContextProfileMask = 0x9126;
            constexpr int kContextCoreProfile = 0x00000001;
            constexpr std::array attributes{
                kContextMajor, 3,
                kContextMinor, 3,
                kContextProfileMask, kContextCoreProfile,
                0,
            };
            HGLRC modern = createContext(deviceContext, nullptr, attributes.data());
            if (modern != nullptr) {
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(context);
                context = modern;
                if (!wglMakeCurrent(deviceContext, context)) {
                    wglDeleteContext(context);
                    return false;
                }
            }
        }
        renderContext = context;

        const auto swapInterval = reinterpret_cast<SwapIntervalFn>(
            wglGetProcAddress("wglSwapIntervalEXT"));
        if (swapInterval != nullptr) static_cast<void>(swapInterval(1));
        ShowWindow(window, hidden ? SW_HIDE : SW_SHOW);
        UpdateWindow(window);
        return true;
    }
};

[[nodiscard]] TextureHandle createImageTexture(TextureStore& textures) {
    constexpr std::uint32_t size = 64;
    std::vector<std::byte> pixels(size * size * 4U);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const bool checker = ((x / 8U) + (y / 8U)) % 2U == 0U;
            const float horizontal = static_cast<float>(x) / static_cast<float>(size - 1U);
            const float vertical = static_cast<float>(y) / static_cast<float>(size - 1U);
            const std::size_t offset = static_cast<std::size_t>(y * size + x) * 4U;
            pixels[offset] = static_cast<std::byte>(checker ? 48U + static_cast<unsigned>(160.0F * horizontal) : 18U);
            pixels[offset + 1U] = static_cast<std::byte>(checker ? 60U : 36U + static_cast<unsigned>(180.0F * vertical));
            pixels[offset + 2U] = static_cast<std::byte>(checker ? 220U : 150U);
            pixels[offset + 3U] = std::byte{0xFF};
        }
    }
    return textures.create(TextureFormat::Rgba8, size, size, size * 4U, pixels);
}

[[nodiscard]] TextureHandle createNinePatchTexture(TextureStore& textures) {
    constexpr std::uint32_t size = 8;
    std::array<std::byte, size * size * 4U> pixels{};
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const bool outer = x == 0 || y == 0 || x == size - 1U || y == size - 1U;
            const bool inner = x == 1 || y == 1 || x == size - 2U || y == size - 2U;
            const std::array<std::uint8_t, 4> color = outer
                ? std::array<std::uint8_t, 4>{90, 225, 255, 255}
                : (inner
                    ? std::array<std::uint8_t, 4>{30, 105, 150, 255}
                    : std::array<std::uint8_t, 4>{16, 34, 58, 255});
            const std::size_t offset = static_cast<std::size_t>(y * size + x) * 4U;
            for (std::size_t channel = 0; channel < color.size(); ++channel) {
                pixels[offset + channel] = static_cast<std::byte>(color[channel]);
            }
        }
    }
    return textures.create(TextureFormat::Rgba8, size, size, size * 4U, pixels);
}

[[nodiscard]] TextureHandle createSdfTexture(TextureStore& textures) {
    constexpr std::uint32_t size = 64;
    std::array<std::byte, size * size> pixels{};
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float px = (static_cast<float>(x) + 0.5F) / static_cast<float>(size) * 2.0F - 1.0F;
            const float py = (static_cast<float>(y) + 0.5F) / static_cast<float>(size) * 2.0F - 1.0F;
            const float diamond = std::abs(px) + std::abs(py) - 0.70F;
            const float circularCutout = 0.20F - std::hypot(px, py);
            const float signedDistance = std::max(diamond, circularCutout);
            const float encoded = std::clamp(0.5F - signedDistance * 2.5F, 0.0F, 1.0F);
            pixels[static_cast<std::size_t>(y * size + x)] = static_cast<std::byte>(
                static_cast<unsigned>(std::lround(encoded * 255.0F)));
        }
    }
    return textures.create(TextureFormat::Alpha8, size, size, size, pixels);
}

void drawCard(
    Canvas& canvas,
    TextPainter& text,
    FontHandle font,
    Rect card,
    std::string_view title,
    std::string_view subtitle) {
    canvas.roundedShadow(card, {0.0F, 0.0F, 0.0F, 0.40F}, 18.0F, 7.0F, {0.0F, 6.0F});
    canvas.fillRect(card, {0.030F, 0.047F, 0.075F, 0.97F}, 18.0F);
    canvas.roundedOutline(card, {0.12F, 0.23F, 0.34F, 0.90F}, 18.0F, 1.0F);
    text.draw(canvas, font, 17.0F, {card.min.x + 18.0F, card.min.y + 17.0F},
        {0.91F, 0.96F, 1.0F, 1.0F}, title);
    text.draw(canvas, font, 11.5F, {card.min.x + 18.0F, card.min.y + 42.0F},
        {0.46F, 0.60F, 0.70F, 1.0F}, subtitle);
}

void drawGallery(
    Canvas& canvas,
    TextPainter& text,
    FontHandle font,
    TextureHandle imageTexture,
    TextureHandle panelTexture,
    TextureHandle sdfTexture,
    float width,
    float height,
    float time) {
    const Color accent{0.10F, 0.76F, 1.0F, 1.0F};
    const Color magenta{0.93F, 0.22F, 0.68F, 1.0F};
    const Color lime{0.36F, 0.92F, 0.55F, 1.0F};
    const Color amber{1.0F, 0.68F, 0.18F, 1.0F};
    const float phase = std::fmod(time * 0.18F, 1.0F);

    canvas.gradientRect(
        {{0.0F, 0.0F}, {width, height}},
        {0.008F, 0.014F, 0.027F, 1.0F},
        {0.025F, 0.055F, 0.082F, 1.0F},
        {0.8F, 1.0F});
    canvas.roundedGlow(
        {{width * 0.16F, -60.0F}, {width * 0.42F, 34.0F}},
        {0.04F, 0.58F, 1.0F, 0.18F}, 30.0F, 34.0F);
    canvas.roundedGlow(
        {{width * 0.68F, 0.0F}, {width * 0.88F, 24.0F}},
        {0.85F, 0.12F, 0.62F, 0.14F}, 18.0F, 40.0F);
    text.draw(canvas, font, 28.0F, {28.0F, 20.0F},
        {0.92F, 0.97F, 1.0F, 1.0F}, "HeniaUI / Effects Gallery");
    text.draw(canvas, font, 13.0F, {29.0F, 59.0F},
        {0.48F, 0.64F, 0.75F, 1.0F},
        "Every current 2D primitive and shader effect - one ordered native OpenGL packet");
    text.draw(canvas, font, 12.0F, {width - 235.0F, 30.0F},
        {0.45F, 0.58F, 0.68F, 1.0F}, "Animated / resize-safe / ESC closes");

    constexpr float margin = 24.0F;
    constexpr float gap = 16.0F;
    constexpr float gridTop = 92.0F;
    const float cardWidth = (width - margin * 2.0F - gap * 2.0F) / 3.0F;
    const float cardHeight = (height - gridTop - margin - gap * 2.0F) / 3.0F;
    const auto cardAt = [cardWidth, cardHeight, margin, gap, gridTop](int column, int row) noexcept {
        const float left = margin + static_cast<float>(column) * (cardWidth + gap);
        const float top = gridTop + static_cast<float>(row) * (cardHeight + gap);
        return Rect{{left, top}, {left + cardWidth, top + cardHeight}};
    };

    Rect card = cardAt(0, 0);
    drawCard(canvas, text, font, card, "Core analytic shapes", "fill / circle / ellipse / arc / capsule");
    const float coreY = card.min.y + cardHeight * 0.66F;
    canvas.circle({card.min.x + cardWidth * 0.14F, coreY}, cardHeight * 0.115F, accent);
    canvas.ellipse(
        {{card.min.x + cardWidth * 0.25F, coreY - cardHeight * 0.10F},
         {card.min.x + cardWidth * 0.47F, coreY + cardHeight * 0.10F}}, magenta);
    canvas.capsule(
        {{card.min.x + cardWidth * 0.52F, coreY - cardHeight * 0.085F},
         {card.min.x + cardWidth * 0.76F, coreY + cardHeight * 0.085F}}, lime);
    canvas.arc(
        {{card.min.x + cardWidth * 0.80F, coreY - cardHeight * 0.12F},
         {card.min.x + cardWidth * 0.96F, coreY + cardHeight * 0.12F}},
        -kPi * 0.5F, kPi * 1.55F, amber, 4.0F);

    card = cardAt(1, 0);
    drawCard(canvas, text, font, card, "Gradients and tint", "static + host-phased gradient / overlay tint");
    const Rect staticGradient{
        {card.min.x + 18.0F, card.min.y + cardHeight * 0.38F},
        {card.max.x - 18.0F, card.min.y + cardHeight * 0.55F}};
    canvas.gradientRect(staticGradient, accent, magenta, {1.0F, 0.25F}, 9.0F);
    const Rect movingGradient{
        {card.min.x + 18.0F, card.min.y + cardHeight * 0.64F},
        {card.max.x - 18.0F, card.min.y + cardHeight * 0.82F}};
    canvas.animatedGradientRect(
        movingGradient, lime, accent, {1.0F, 0.0F}, phase, 9.0F);
    canvas.tintRect(
        {{movingGradient.min.x + cardWidth * 0.52F, movingGradient.min.y}, movingGradient.max},
        {0.38F, 0.04F, 0.56F, 0.42F}, 9.0F);

    card = cardAt(2, 0);
    drawCard(canvas, text, font, card, "Glow and soft shadow", "bounded analytic falloff / no blur target");
    const Rect shadowShape{
        {card.min.x + cardWidth * 0.10F, card.min.y + cardHeight * 0.48F},
        {card.min.x + cardWidth * 0.43F, card.min.y + cardHeight * 0.77F}};
    canvas.roundedShadow(shadowShape, {0.0F, 0.0F, 0.0F, 0.75F}, 18.0F, 9.0F, {8.0F, 10.0F});
    canvas.fillRect(shadowShape, {0.11F, 0.19F, 0.30F, 1.0F}, 18.0F);
    const Rect glowShape{
        {card.min.x + cardWidth * 0.61F, card.min.y + cardHeight * 0.48F},
        {card.min.x + cardWidth * 0.90F, card.min.y + cardHeight * 0.77F}};
    canvas.roundedGlow(glowShape, {0.10F, 0.70F, 1.0F, 0.62F}, 18.0F, 10.0F);
    canvas.fillRect(glowShape, {0.04F, 0.18F, 0.28F, 1.0F}, 18.0F);
    canvas.roundedOutline(glowShape, accent, 18.0F, 2.0F);

    card = cardAt(0, 1);
    drawCard(canvas, text, font, card, "Borders and outlines", "tight stroke / one-quad outline / corner radii");
    const float borderTop = card.min.y + cardHeight * 0.43F;
    const float borderBottom = card.min.y + cardHeight * 0.80F;
    const float borderWidth = cardWidth * 0.25F;
    Rect borderShape{{card.min.x + cardWidth * 0.07F, borderTop},
        {card.min.x + cardWidth * 0.07F + borderWidth, borderBottom}};
    canvas.strokeRect(borderShape, accent, 16.0F, 3.0F);
    borderShape = {{card.min.x + cardWidth * 0.38F, borderTop},
        {card.min.x + cardWidth * 0.38F + borderWidth, borderBottom}};
    canvas.roundedOutline(borderShape, magenta, 16.0F, 3.0F);
    borderShape = {{card.min.x + cardWidth * 0.69F, borderTop},
        {card.min.x + cardWidth * 0.94F, borderBottom}};
    canvas.border(borderShape, lime, {4.0F, 22.0F, 7.0F, 16.0F}, 3.0F);

    card = cardAt(1, 1);
    drawCard(canvas, text, font, card, "Line caps and joins", "butt / square / round + bevel / round joins");
    const float lineLeft = card.min.x + 24.0F;
    const float lineRight = card.min.x + cardWidth * 0.43F;
    constexpr std::array caps{LineCap::Butt, LineCap::Square, LineCap::Round};
    constexpr std::array capColors{Color{0.10F, 0.76F, 1.0F, 1.0F},
        Color{0.93F, 0.22F, 0.68F, 1.0F}, Color{0.36F, 0.92F, 0.55F, 1.0F}};
    for (std::size_t index = 0; index < caps.size(); ++index) {
        const float y = card.min.y + cardHeight * (0.44F + static_cast<float>(index) * 0.16F);
        canvas.line({lineLeft, y}, {lineRight, y}, capColors[index], 7.0F, caps[index]);
    }
    const std::array bevel{
        Vec2{card.min.x + cardWidth * 0.55F, card.min.y + cardHeight * 0.75F},
        Vec2{card.min.x + cardWidth * 0.72F, card.min.y + cardHeight * 0.43F},
        Vec2{card.min.x + cardWidth * 0.89F, card.min.y + cardHeight * 0.75F},
    };
    canvas.polyline(bevel, amber, 7.0F, false, LineCap::Butt, LineJoin::Bevel);
    std::array<Vec2, 3> round = bevel;
    for (Vec2& point : round) point.y += cardHeight * 0.10F;
    canvas.polyline(round, accent, 4.0F, false, LineCap::Round, LineJoin::Round);

    card = cardAt(2, 1);
    drawCard(canvas, text, font, card, "Texture effects", "image tint / nine-patch / red-channel SDF");
    const float textureTop = card.min.y + cardHeight * 0.42F;
    const float textureBottom = card.min.y + cardHeight * 0.82F;
    canvas.image(imageTexture,
        {{card.min.x + cardWidth * 0.06F, textureTop},
         {card.min.x + cardWidth * 0.31F, textureBottom}},
        {0.75F, 0.90F, 1.0F, 1.0F});
    canvas.ninePatch(panelTexture,
        {{card.min.x + cardWidth * 0.38F, textureTop},
         {card.min.x + cardWidth * 0.65F, textureBottom}},
        {{0.0F, 0.0F}, {1.0F, 1.0F}}, 15.0F, 0.25F);
    const Rect sdfBounds{
        {card.min.x + cardWidth * 0.73F, textureTop},
        {card.min.x + cardWidth * 0.94F, textureBottom}};
    canvas.roundedGlow(sdfBounds, {0.80F, 0.18F, 0.75F, 0.45F}, 12.0F, 7.0F);
    canvas.sdfIcon(sdfTexture, sdfBounds, {{0.0F, 0.0F}, {1.0F, 1.0F}},
        {1.0F, 0.45F, 0.90F, 1.0F}, 0.5F, 0.055F);

    card = cardAt(0, 2);
    drawCard(canvas, text, font, card, "Ordered effect stack", "shadow -> glow -> animated gradient -> outline");
    const Rect stackBounds{
        {card.min.x + 32.0F, card.min.y + cardHeight * 0.43F},
        {card.max.x - 32.0F, card.min.y + cardHeight * 0.80F}};
    const std::array layers{
        EffectLayer{.kind = EffectLayerKind::SoftShadow,
            .color = {0.0F, 0.0F, 0.0F, 0.72F}, .vector = {7.0F, 9.0F}, .amount = 8.0F},
        EffectLayer{.kind = EffectLayerKind::Glow,
            .color = {0.10F, 0.66F, 1.0F, 0.38F}, .amount = 9.0F},
        EffectLayer{.kind = EffectLayerKind::AnimatedGradient,
            .color = {0.06F, 0.55F, 0.96F, 1.0F},
            .secondaryColor = {0.94F, 0.18F, 0.64F, 1.0F},
            .vector = {1.0F, 0.2F}, .phase = phase},
        EffectLayer{.kind = EffectLayerKind::Outline,
            .color = {0.88F, 0.96F, 1.0F, 0.92F}, .amount = 2.0F},
        EffectLayer{.kind = EffectLayerKind::Tint,
            .color = {1.0F, 0.0F, 0.0F, 1.0F}, .enabled = false},
    };
    canvas.effectRect(stackBounds, 18.0F, layers);

    card = cardAt(1, 2);
    drawCard(canvas, text, font, card, "Masking and blend", "rectangular clip / premultiplied + additive");
    const Rect maskBounds{
        {card.min.x + 22.0F, card.min.y + cardHeight * 0.40F},
        {card.min.x + cardWidth * 0.49F, card.min.y + cardHeight * 0.84F}};
    canvas.fillRect(maskBounds, {0.015F, 0.025F, 0.045F, 1.0F}, 12.0F);
    {
        Canvas::ClipScope clip = canvas.scopedClip(
            {{maskBounds.min.x + 10.0F, maskBounds.min.y + 10.0F},
             {maskBounds.max.x - 10.0F, maskBounds.max.y - 10.0F}});
        canvas.animatedGradientRect(
            {{maskBounds.min.x - 28.0F, maskBounds.min.y + 4.0F},
             {maskBounds.max.x + 30.0F, maskBounds.max.y - 4.0F}},
            magenta, accent, {1.0F, 0.4F}, phase, 30.0F);
        canvas.circle(
            {(maskBounds.min.x + maskBounds.max.x) * 0.5F, maskBounds.max.y - 2.0F},
            cardHeight * 0.22F, {1.0F, 0.76F, 0.18F, 0.82F});
    }
    canvas.setBlendMode(BlendMode::Additive);
    const Vec2 blendCenter{card.min.x + cardWidth * 0.75F, card.min.y + cardHeight * 0.64F};
    canvas.circle({blendCenter.x - 24.0F, blendCenter.y}, cardHeight * 0.13F,
        {0.08F, 0.55F, 1.0F, 0.58F});
    canvas.circle({blendCenter.x + 20.0F, blendCenter.y}, cardHeight * 0.13F,
        {1.0F, 0.12F, 0.52F, 0.58F});
    canvas.circle({blendCenter.x, blendCenter.y - 25.0F}, cardHeight * 0.13F,
        {0.30F, 1.0F, 0.42F, 0.48F});
    canvas.setBlendMode(BlendMode::PremultipliedAlpha);

    card = cardAt(2, 2);
    drawCard(canvas, text, font, card, "Text and shared batching", "glyph atlas + every variant in ordered paint");
    text.draw(canvas, font, 34.0F,
        {card.min.x + 24.0F, card.min.y + cardHeight * 0.42F},
        {0.92F, 0.97F, 1.0F, 1.0F}, "HeniaUI");
    text.draw(canvas, font, 14.0F,
        {card.min.x + 25.0F, card.min.y + cardHeight * 0.64F},
        accent, "88 B command / 60 B instance / 5 attributes");
    text.draw(canvas, font, 12.0F,
        {card.min.x + 25.0F, card.min.y + cardHeight * 0.76F},
        {0.51F, 0.65F, 0.75F, 1.0F}, "No ImGui. No full-screen post-process.");
}

[[nodiscard]] bool saveSnapshot(std::uint32_t width, std::uint32_t height) {
    constexpr GLenum kBgra = 0x80E1;
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
        kBgra, GL_UNSIGNED_BYTE, pixels.data());

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
    HANDLE file = CreateFileW(L"HeniaUIEffectsExample.bmp", GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool saved = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr)
        && written == sizeof(fileHeader)
        && WriteFile(file, &infoHeader, sizeof(infoHeader), &written, nullptr)
        && written == sizeof(infoHeader)
        && WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &written, nullptr)
        && written == pixels.size();
    CloseHandle(file);
    return saved;
}

[[nodiscard]] int showRendererError(HWND window, std::string_view error, int result) {
    HANDLE diagnostic = CreateFileW(
        L"HeniaUIEffectsExample.error.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (diagnostic != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        static_cast<void>(WriteFile(
            diagnostic, error.data(), static_cast<DWORD>(error.size()), &written, nullptr));
        CloseHandle(diagnostic);
    }
    if (commandLineContains(L"--headless")) return result;

    const int length = MultiByteToWideChar(
        CP_UTF8, 0, error.data(), static_cast<int>(error.size()), nullptr, 0);
    std::wstring message(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()),
            message.data(), length);
    }
    MessageBoxW(window, message.c_str(), L"HeniaUI Effects Example", MB_ICONERROR);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (commandLineContains(L"--help")) return 0;
    static_cast<void>(SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    const bool headless = commandLineContains(L"--headless");
    const bool snapshot = commandLineContains(L"--snapshot");
    NativeWindow native;
    if (!native.create(headless)) {
        MessageBoxW(nullptr, L"Unable to create the OpenGL effects window.",
            L"HeniaUI Effects Example", MB_ICONERROR);
        return 1;
    }

    TextureStore textures;
    FontStore fonts;
    constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
    Win32FontScaleCache fontScaleCache(textures, fonts, {
        .family = L"Segoe UI",
        .logicalPixelHeight = 36.0F,
        .atlasWidth = 1024,
        .atlasHeight = 512,
        .ranges = ranges,
    });
    std::uint32_t selectedDpi = GetDpiForWindow(native.window);
    FontHandle font = fontScaleCache.selectForDpi(selectedDpi);
    const TextureHandle imageTexture = createImageTexture(textures);
    const TextureHandle panelTexture = createNinePatchTexture(textures);
    const TextureHandle sdfTexture = createSdfTexture(textures);
    if (!font.valid() || !imageTexture.valid() || !panelTexture.valid() || !sdfTexture.valid()) {
        MessageBoxW(native.window, L"Unable to create gallery font or textures.",
            L"HeniaUI Effects Example", MB_ICONERROR);
        return 2;
    }

    TextRunCache textCache(fonts);
    textCache.reserve(192, 96);
    TextPainter text(textCache);
    Frame frame;
    frame.reserve(2048, 4096, 128, CapacityPolicy::Grow);
    frame.setFragmentAreaTracking(true);
    OpenGlRenderer renderer;
    if (!renderer.initialize(4096, 16, 3) || !renderer.synchronizeTextures(textures)) {
        return showRendererError(native.window, renderer.lastError(), 3);
    }

    const auto started = std::chrono::steady_clock::now();
    MSG message{};
    int result = 0;
    int headlessFrames = 0;
    bool running = true;
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
        if (!running) break;

        RECT client{};
        GetClientRect(native.window, &client);
        const std::uint32_t width = static_cast<std::uint32_t>(
            std::max(client.right - client.left, 1L));
        const std::uint32_t height = static_cast<std::uint32_t>(
            std::max(client.bottom - client.top, 1L));
        const std::uint32_t dpi = std::max(GetDpiForWindow(native.window), 1U);
        const float dpiScale = static_cast<float>(dpi) / 96.0F;
        if (dpi != selectedDpi) {
            font = fontScaleCache.selectForDpi(dpi);
            if (!font.valid() || !renderer.synchronizeTextures(textures)) {
                result = showRendererError(
                    native.window, "Unable to select or upload the DPI-scaled font atlas", 8);
                break;
            }
            selectedDpi = dpi;
        }
        const float logicalWidth = static_cast<float>(width) / dpiScale;
        const float logicalHeight = static_cast<float>(height) / dpiScale;
        const float time = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - started).count();

        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glClearColor(0.008F, 0.014F, 0.027F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        Canvas& canvas = frame.begin();
        drawGallery(canvas, text, font, imageTexture, panelTexture, sdfTexture,
            logicalWidth, logicalHeight, time);
        const RenderPacket packet = frame.finish();
        if (!packet) {
            result = showRendererError(
                native.window, "Frame packet build rejected the gallery command stream", 4);
            break;
        }
        if (!renderer.render(
                packet,
                {
                    .framebufferWidth = width,
                    .framebufferHeight = height,
                    .logicalToFramebuffer = {.scale = {dpiScale, dpiScale}},
                })) {
            result = showRendererError(native.window, renderer.lastError(), 4);
            break;
        }
        if (glGetError() != GL_NO_ERROR) {
            result = 5;
            break;
        }
        if (snapshot && headlessFrames == 0 && !saveSnapshot(width, height)) {
            result = 6;
            break;
        }
        SwapBuffers(native.deviceContext);

        const PacketStatistics& statistics = packet.statistics();
        wchar_t title[256]{};
        swprintf_s(title, L"HeniaUI Effects Gallery | %u%% DPI | %llu instances | %llu effects | %llu batches | ESC closes",
            dpi * 100U / 96U,
            static_cast<unsigned long long>(statistics.instances),
            static_cast<unsigned long long>(statistics.effectInstances),
            static_cast<unsigned long long>(statistics.batches));
        SetWindowTextW(native.window, title);

        if (!headless) {
            constexpr auto minimumFrameTime = std::chrono::microseconds(8333);
            std::this_thread::sleep_until(frameStarted + minimumFrameTime);
        } else if (++headlessFrames >= 3) {
            break;
        }
    }

    if (!renderer.shutdown()) return 7;
    return result;
}
