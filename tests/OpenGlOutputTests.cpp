#include "VisualRegression.h"

#include "henia/ui/backend/opengl/OpenGlRenderer.h"

#include <Windows.h>
#include <gl/GL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string_view>
#include <vector>

namespace {

using ActiveTextureFn = void(APIENTRY*)(GLenum);

struct GlState final {
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    GLint activeTexture = 0;
    std::array<GLint, 8> textures{};
    GLint sourceRgb = 0;
    GLint destinationRgb = 0;
    GLint sourceAlpha = 0;
    GLint destinationAlpha = 0;
    GLboolean blend = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
};

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

class HiddenOpenGlContext final {
public:
    HiddenOpenGlContext() {
        WNDCLASSW windowClass{};
        windowClass.style = CS_OWNDC;
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"HeniaUIOpenGlOutputTest";
        mClass = RegisterClassW(&windowClass);
        if (mClass == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return;
        }
        mWindow = CreateWindowExW(
            0,
            windowClass.lpszClassName,
            L"HeniaUI hidden OpenGL output test",
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            static_cast<int>(henia::test::kVisualWidth),
            static_cast<int>(henia::test::kVisualHeight),
            nullptr,
            nullptr,
            windowClass.hInstance,
            nullptr);
        if (mWindow == nullptr) {
            return;
        }
        mDeviceContext = GetDC(mWindow);
        PIXELFORMATDESCRIPTOR descriptor{};
        descriptor.nSize = sizeof(descriptor);
        descriptor.nVersion = 1;
        descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        descriptor.iPixelType = PFD_TYPE_RGBA;
        descriptor.cColorBits = 32;
        descriptor.cAlphaBits = 8;
        descriptor.cDepthBits = 24;
        descriptor.iLayerType = PFD_MAIN_PLANE;
        const int format = ChoosePixelFormat(mDeviceContext, &descriptor);
        if (format == 0 || SetPixelFormat(mDeviceContext, format, &descriptor) == FALSE) {
            return;
        }
        mContext = wglCreateContext(mDeviceContext);
        if (mContext == nullptr || wglMakeCurrent(mDeviceContext, mContext) == FALSE) {
            return;
        }
        mActiveTexture = reinterpret_cast<ActiveTextureFn>(wglGetProcAddress("glActiveTexture"));
    }

    ~HiddenOpenGlContext() {
        if (wglGetCurrentContext() == mContext) {
            static_cast<void>(wglMakeCurrent(nullptr, nullptr));
        }
        if (mContext != nullptr) {
            static_cast<void>(wglDeleteContext(mContext));
        }
        if (mWindow != nullptr && mDeviceContext != nullptr) {
            static_cast<void>(ReleaseDC(mWindow, mDeviceContext));
        }
        if (mWindow != nullptr) {
            static_cast<void>(DestroyWindow(mWindow));
        }
    }

    [[nodiscard]] bool ready() const noexcept {
        return mContext != nullptr && mActiveTexture != nullptr;
    }

    [[nodiscard]] ActiveTextureFn activeTexture() const noexcept { return mActiveTexture; }

private:
    ATOM mClass = 0;
    HWND mWindow = nullptr;
    HDC mDeviceContext = nullptr;
    HGLRC mContext = nullptr;
    ActiveTextureFn mActiveTexture = nullptr;
};

[[nodiscard]] GlState captureState(ActiveTextureFn activeTexture) {
    constexpr GLenum kActiveTexture = 0x84E0;
    constexpr GLenum kTexture0 = 0x84C0;
    constexpr GLenum kBlendSourceRgb = 0x80C9;
    constexpr GLenum kBlendDestinationRgb = 0x80C8;
    constexpr GLenum kBlendSourceAlpha = 0x80CB;
    constexpr GLenum kBlendDestinationAlpha = 0x80CA;
    GlState state{};
    glGetIntegerv(GL_VIEWPORT, state.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor.data());
    glGetIntegerv(kActiveTexture, &state.activeTexture);
    for (std::uint32_t slot = 0; slot < state.textures.size(); ++slot) {
        activeTexture(kTexture0 + slot);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[slot]);
    }
    activeTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(kBlendSourceRgb, &state.sourceRgb);
    glGetIntegerv(kBlendDestinationRgb, &state.destinationRgb);
    glGetIntegerv(kBlendSourceAlpha, &state.sourceAlpha);
    glGetIntegerv(kBlendDestinationAlpha, &state.destinationAlpha);
    state.blend = glIsEnabled(GL_BLEND);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    return state;
}

[[nodiscard]] bool sameState(const GlState& left, const GlState& right) noexcept {
    return left.viewport == right.viewport && left.scissor == right.scissor
        && left.activeTexture == right.activeTexture && left.textures == right.textures
        && left.sourceRgb == right.sourceRgb && left.destinationRgb == right.destinationRgb
        && left.sourceAlpha == right.sourceAlpha
        && left.destinationAlpha == right.destinationAlpha && left.blend == right.blend
        && left.scissorTest == right.scissorTest && left.depthTest == right.depthTest
        && left.cullFace == right.cullFace;
}

} // namespace

int main() {
    using namespace henia::ui;
    constexpr GLenum kTexture0 = 0x84C0;

    HiddenOpenGlContext context;
    if (!context.ready()) {
        std::cout << "OpenGL 3.3 entry points are unavailable; output test skipped\n";
        return 77;
    }
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version == nullptr) {
        std::cout << "OpenGL version is unavailable; output test skipped\n";
        return 77;
    }

    TextureStore textures;
    Frame frame;
    const RenderPacket packet = henia::test::buildUiVisualScene(textures, frame);
    OpenGlRenderer renderer;
    if (!renderer.initialize(64, 8, 3) || !renderer.synchronizeTextures(textures)) {
        std::cout << "OpenGL 3.3 renderer unavailable (" << version << "): "
                  << renderer.lastError() << "\n";
        return 77;
    }

    glViewport(0, 0, henia::test::kVisualWidth, henia::test::kVisualHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    std::array<GLuint, 8> hostTextures{};
    glGenTextures(static_cast<GLsizei>(hostTextures.size()), hostTextures.data());
    for (std::uint32_t slot = 0; slot < hostTextures.size(); ++slot) {
        context.activeTexture()(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, hostTextures[slot]);
    }
    context.activeTexture()(kTexture0 + 3U);
    glViewport(3, 5, 97, 83);
    glScissor(7, 11, 53, 47);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    const GlState before = captureState(context.activeTexture());

    if (!renderer.render(
            packet,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    const GlState after = captureState(context.activeTexture());
    if (!sameState(before, after)) {
        fail("OpenGL renderer did not restore randomized host state");
    }

    glFinish();
    std::vector<henia::test::Rgba8> bottomUp(
        static_cast<std::size_t>(henia::test::kVisualWidth) * henia::test::kVisualHeight);
    glReadPixels(
        0,
        0,
        henia::test::kVisualWidth,
        henia::test::kVisualHeight,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        bottomUp.data());
    std::vector<henia::test::Rgba8> topDown(bottomUp.size());
    for (std::uint32_t y = 0; y < henia::test::kVisualHeight; ++y) {
        const std::size_t source = static_cast<std::size_t>(
            henia::test::kVisualHeight - 1U - y) * henia::test::kVisualWidth;
        const std::size_t destination = static_cast<std::size_t>(y) * henia::test::kVisualWidth;
        std::copy_n(
            bottomUp.data() + source,
            henia::test::kVisualWidth,
            topDown.data() + destination);
    }
    if (!henia::test::matchesUiGolden(
            topDown,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight)) {
        henia::test::writePpm(
            "opengl-ui-actual.ppm",
            topDown,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight);
        fail("OpenGL output exceeded the documented golden-image tolerance");
    }

    std::mt19937 random(0x0BADC0DE);
    std::uniform_int_distribution<int> coordinate(0, 23);
    std::uniform_int_distribution<int> extent(32, 128);
    std::uniform_int_distribution<int> toggle(0, 1);
    std::uniform_int_distribution<int> textureUnit(0, 7);
    constexpr std::array blendFactors{GL_ONE, GL_ZERO, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
    std::uniform_int_distribution<std::size_t> blendFactor(0, blendFactors.size() - 1U);
    for (int iteration = 0; iteration < 32; ++iteration) {
        context.activeTexture()(kTexture0 + static_cast<GLenum>(textureUnit(random)));
        glViewport(coordinate(random), coordinate(random), extent(random), extent(random));
        glScissor(coordinate(random), coordinate(random), extent(random), extent(random));
        (toggle(random) != 0 ? glEnable : glDisable)(GL_BLEND);
        glBlendFunc(blendFactors[blendFactor(random)], blendFactors[blendFactor(random)]);
        (toggle(random) != 0 ? glEnable : glDisable)(GL_SCISSOR_TEST);
        (toggle(random) != 0 ? glEnable : glDisable)(GL_DEPTH_TEST);
        (toggle(random) != 0 ? glEnable : glDisable)(GL_CULL_FACE);
        const GlState randomizedBefore = captureState(context.activeTexture());
        if (!renderer.render(
                packet,
                henia::test::kVisualWidth,
                henia::test::kVisualHeight)) {
            fail("OpenGL randomized-state render failed");
        }
        if (!sameState(randomizedBefore, captureState(context.activeTexture()))) {
            fail("OpenGL renderer did not restore randomized host state");
        }
        glFinish();
    }

    // Exercise all three upload slots with changing packets after every GPU
    // completion. This catches unsafe buffer reuse without relying on timing.
    for (int revision = 0; revision < 64; ++revision) {
        Canvas& canvas = frame.begin();
        const float inset = static_cast<float>(revision % 8);
        canvas.fillRect(
            {{8.0F + inset, 8.0F}, {120.0F, 120.0F - inset}},
            {0.25F, 0.55F, 0.90F, 1.0F},
            6.0F);
        const RenderPacket changed = frame.finish();
        if (!renderer.render(
                changed,
                henia::test::kVisualWidth,
                henia::test::kVisualHeight)) {
            fail("OpenGL multi-slot stress render failed");
        }
        glFinish();
    }
    const OpenGlRenderStatistics statistics = renderer.statistics();
    if (statistics.frames != 97 || statistics.instanceUploads != 65
        || statistics.uploadFenceFailures != 0 || statistics.rejectedFrames != 0) {
        fail("OpenGL multi-slot stress statistics are incorrect");
    }

    renderer.shutdown();
    for (std::uint32_t slot = 0; slot < hostTextures.size(); ++slot) {
        context.activeTexture()(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDeleteTextures(static_cast<GLsizei>(hostTextures.size()), hostTextures.data());
    std::cout << "HeniaUI OpenGL hidden-window output test passed (" << version << ")\n";
    return EXIT_SUCCESS;
}
