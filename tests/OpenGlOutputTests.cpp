#include "VisualRegression.h"

#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"
#include "henia/ui/backend/opengl/OpenGlRenderer.h"

#include <Windows.h>
#include <gl/GL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ActiveTextureFn = void(APIENTRY*)(GLenum);
using BindSamplerFn = void(APIENTRY*)(GLuint, GLuint);
using GenSamplersFn = void(APIENTRY*)(GLsizei, GLuint*);
using DeleteSamplersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using BlendEquationSeparateFn = void(APIENTRY*)(GLenum, GLenum);
using BlendFuncSeparateFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLenum);
using CreateShaderFn = GLuint(APIENTRY*)(GLenum);
using ShaderSourceFn = void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
using CompileShaderFn = void(APIENTRY*)(GLuint);
using CreateProgramFn = GLuint(APIENTRY*)();
using AttachShaderFn = void(APIENTRY*)(GLuint, GLuint);
using LinkProgramFn = void(APIENTRY*)(GLuint);
using DeleteShaderFn = void(APIENTRY*)(GLuint);
using DeleteProgramFn = void(APIENTRY*)(GLuint);
using UseProgramFn = void(APIENTRY*)(GLuint);
using GenBuffersFn = void(APIENTRY*)(GLsizei, GLuint*);
using BindBufferFn = void(APIENTRY*)(GLenum, GLuint);
using BufferDataFn = void(APIENTRY*)(GLenum, std::ptrdiff_t, const void*, GLenum);
using DeleteBuffersFn = void(APIENTRY*)(GLsizei, const GLuint*);
using GenVertexArraysFn = void(APIENTRY*)(GLsizei, GLuint*);
using BindVertexArrayFn = void(APIENTRY*)(GLuint);
using DeleteVertexArraysFn = void(APIENTRY*)(GLsizei, const GLuint*);

constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kActiveTexture = 0x84E0;
constexpr GLenum kSamplerBinding = 0x8919;
constexpr GLenum kBlendSourceRgb = 0x80C9;
constexpr GLenum kBlendDestinationRgb = 0x80C8;
constexpr GLenum kBlendSourceAlpha = 0x80CB;
constexpr GLenum kBlendDestinationAlpha = 0x80CA;
constexpr GLenum kBlendEquationRgb = 0x8009;
constexpr GLenum kBlendEquationAlpha = 0x883D;
constexpr GLenum kFunctionAdd = 0x8006;
constexpr GLenum kFunctionSubtract = 0x800A;
constexpr GLenum kFunctionReverseSubtract = 0x800B;
constexpr GLenum kFramebufferSrgb = 0x8DB9;
constexpr GLenum kRasterizerDiscard = 0x8C89;
constexpr GLenum kMultisample = 0x809D;
constexpr GLenum kSampleAlphaToCoverage = 0x809E;
constexpr GLenum kSampleAlphaToOne = 0x809F;
constexpr GLenum kSampleCoverage = 0x80A0;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kPixelUnpackBuffer = 0x88EC;
constexpr GLenum kPixelUnpackBufferBinding = 0x88EF;
constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kVertexArrayBinding = 0x85B5;
constexpr GLenum kStreamDraw = 0x88E0;
constexpr GLenum kUnpackRowLength = 0x0CF2;
constexpr GLenum kStencilBackFunction = 0x8800;
constexpr GLenum kStencilBackFail = 0x8801;
constexpr GLenum kStencilBackDepthFail = 0x8802;
constexpr GLenum kStencilBackDepthPass = 0x8803;
constexpr GLenum kStencilBackReference = 0x8CA3;
constexpr GLenum kStencilBackValueMask = 0x8CA4;
constexpr GLenum kStencilBackWriteMask = 0x8CA5;

[[nodiscard]] void* loadOpenGlFunction(const char* name) noexcept {
    void* function = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (function == nullptr || function == reinterpret_cast<void*>(1)
        || function == reinterpret_cast<void*>(2) || function == reinterpret_cast<void*>(3)
        || function == reinterpret_cast<void*>(-1)) {
        const HMODULE module = GetModuleHandleW(L"opengl32.dll");
        function = module == nullptr ? nullptr : reinterpret_cast<void*>(GetProcAddress(module, name));
    }
    return function;
}

struct TestFunctions final {
    ActiveTextureFn activeTexture = nullptr;
    BindSamplerFn bindSampler = nullptr;
    GenSamplersFn genSamplers = nullptr;
    DeleteSamplersFn deleteSamplers = nullptr;
    BlendEquationSeparateFn blendEquationSeparate = nullptr;
    BlendFuncSeparateFn blendFuncSeparate = nullptr;
    CreateShaderFn createShader = nullptr;
    ShaderSourceFn shaderSource = nullptr;
    CompileShaderFn compileShader = nullptr;
    CreateProgramFn createProgram = nullptr;
    AttachShaderFn attachShader = nullptr;
    LinkProgramFn linkProgram = nullptr;
    DeleteShaderFn deleteShader = nullptr;
    DeleteProgramFn deleteProgram = nullptr;
    UseProgramFn useProgram = nullptr;
    GenBuffersFn genBuffers = nullptr;
    BindBufferFn bindBuffer = nullptr;
    BufferDataFn bufferData = nullptr;
    DeleteBuffersFn deleteBuffers = nullptr;
    GenVertexArraysFn genVertexArrays = nullptr;
    BindVertexArrayFn bindVertexArray = nullptr;
    DeleteVertexArraysFn deleteVertexArrays = nullptr;

    [[nodiscard]] bool load() noexcept {
#define HENIAUI_LOAD_TEST_GL(member, name) \
        member = reinterpret_cast<decltype(member)>(loadOpenGlFunction(name)); \
        if (member == nullptr) return false
        HENIAUI_LOAD_TEST_GL(activeTexture, "glActiveTexture");
        HENIAUI_LOAD_TEST_GL(bindSampler, "glBindSampler");
        HENIAUI_LOAD_TEST_GL(genSamplers, "glGenSamplers");
        HENIAUI_LOAD_TEST_GL(deleteSamplers, "glDeleteSamplers");
        HENIAUI_LOAD_TEST_GL(blendEquationSeparate, "glBlendEquationSeparate");
        HENIAUI_LOAD_TEST_GL(blendFuncSeparate, "glBlendFuncSeparate");
        HENIAUI_LOAD_TEST_GL(createShader, "glCreateShader");
        HENIAUI_LOAD_TEST_GL(shaderSource, "glShaderSource");
        HENIAUI_LOAD_TEST_GL(compileShader, "glCompileShader");
        HENIAUI_LOAD_TEST_GL(createProgram, "glCreateProgram");
        HENIAUI_LOAD_TEST_GL(attachShader, "glAttachShader");
        HENIAUI_LOAD_TEST_GL(linkProgram, "glLinkProgram");
        HENIAUI_LOAD_TEST_GL(deleteShader, "glDeleteShader");
        HENIAUI_LOAD_TEST_GL(deleteProgram, "glDeleteProgram");
        HENIAUI_LOAD_TEST_GL(useProgram, "glUseProgram");
        HENIAUI_LOAD_TEST_GL(genBuffers, "glGenBuffers");
        HENIAUI_LOAD_TEST_GL(bindBuffer, "glBindBuffer");
        HENIAUI_LOAD_TEST_GL(bufferData, "glBufferData");
        HENIAUI_LOAD_TEST_GL(deleteBuffers, "glDeleteBuffers");
        HENIAUI_LOAD_TEST_GL(genVertexArrays, "glGenVertexArrays");
        HENIAUI_LOAD_TEST_GL(bindVertexArray, "glBindVertexArray");
        HENIAUI_LOAD_TEST_GL(deleteVertexArrays, "glDeleteVertexArrays");
#undef HENIAUI_LOAD_TEST_GL
        return true;
    }
};

struct GlState final {
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    GLint activeTexture = 0;
    std::array<GLint, 8> textures{};
    std::array<GLint, 8> samplers{};
    GLint sourceRgb = 0;
    GLint destinationRgb = 0;
    GLint sourceAlpha = 0;
    GLint destinationAlpha = 0;
    GLint equationRgb = 0;
    GLint equationAlpha = 0;
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint depthFunction = 0;
    GLboolean depthWrite = GL_FALSE;
    std::array<GLint, 2> polygonMode{};
    std::array<GLboolean, 4> colorMask{};
    std::array<GLdouble, 2> depthRange{};
    std::array<GLint, 7> stencilFront{};
    std::array<GLint, 7> stencilBack{};
    GLboolean blend = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    GLboolean framebufferSrgb = GL_FALSE;
    GLboolean rasterizerDiscard = GL_FALSE;
    GLboolean colorLogicOp = GL_FALSE;
    GLboolean dither = GL_FALSE;
    GLboolean multisample = GL_FALSE;
    GLboolean sampleAlphaToCoverage = GL_FALSE;
    GLboolean sampleAlphaToOne = GL_FALSE;
    GLboolean sampleCoverage = GL_FALSE;
};

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

[[nodiscard]] std::vector<henia::test::Rgba8> readCurrentPixels() {
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
    return topDown;
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

    [[nodiscard]] bool makeCurrent() const noexcept {
        return mContext != nullptr && wglMakeCurrent(mDeviceContext, mContext) != FALSE;
    }

    [[nodiscard]] static bool clearCurrent() noexcept {
        return wglMakeCurrent(nullptr, nullptr) != FALSE;
    }

    [[nodiscard]] HGLRC handle() const noexcept { return mContext; }

private:
    ATOM mClass = 0;
    HWND mWindow = nullptr;
    HDC mDeviceContext = nullptr;
    HGLRC mContext = nullptr;
    ActiveTextureFn mActiveTexture = nullptr;
};

[[nodiscard]] GlState captureState(const TestFunctions& gl) {
    GlState state{};
    glGetIntegerv(GL_VIEWPORT, state.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor.data());
    glGetIntegerv(kActiveTexture, &state.activeTexture);
    for (std::uint32_t slot = 0; slot < state.textures.size(); ++slot) {
        gl.activeTexture(kTexture0 + slot);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textures[slot]);
        glGetIntegerv(kSamplerBinding, &state.samplers[slot]);
    }
    gl.activeTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(kBlendSourceRgb, &state.sourceRgb);
    glGetIntegerv(kBlendDestinationRgb, &state.destinationRgb);
    glGetIntegerv(kBlendSourceAlpha, &state.sourceAlpha);
    glGetIntegerv(kBlendDestinationAlpha, &state.destinationAlpha);
    glGetIntegerv(kBlendEquationRgb, &state.equationRgb);
    glGetIntegerv(kBlendEquationAlpha, &state.equationAlpha);
    glGetIntegerv(kCurrentProgram, &state.program);
    glGetIntegerv(kVertexArrayBinding, &state.vertexArray);
    glGetIntegerv(kArrayBufferBinding, &state.arrayBuffer);
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode.data());
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWrite);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask.data());
    glGetDoublev(GL_DEPTH_RANGE, state.depthRange.data());
    glGetIntegerv(GL_STENCIL_FUNC, &state.stencilFront[0]);
    glGetIntegerv(GL_STENCIL_REF, &state.stencilFront[1]);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &state.stencilFront[2]);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &state.stencilFront[3]);
    glGetIntegerv(GL_STENCIL_FAIL, &state.stencilFront[4]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state.stencilFront[5]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state.stencilFront[6]);
    glGetIntegerv(kStencilBackFunction, &state.stencilBack[0]);
    glGetIntegerv(kStencilBackReference, &state.stencilBack[1]);
    glGetIntegerv(kStencilBackValueMask, &state.stencilBack[2]);
    glGetIntegerv(kStencilBackWriteMask, &state.stencilBack[3]);
    glGetIntegerv(kStencilBackFail, &state.stencilBack[4]);
    glGetIntegerv(kStencilBackDepthFail, &state.stencilBack[5]);
    glGetIntegerv(kStencilBackDepthPass, &state.stencilBack[6]);
    state.blend = glIsEnabled(GL_BLEND);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    state.framebufferSrgb = glIsEnabled(kFramebufferSrgb);
    state.rasterizerDiscard = glIsEnabled(kRasterizerDiscard);
    state.colorLogicOp = glIsEnabled(GL_COLOR_LOGIC_OP);
    state.dither = glIsEnabled(GL_DITHER);
    state.multisample = glIsEnabled(kMultisample);
    state.sampleAlphaToCoverage = glIsEnabled(kSampleAlphaToCoverage);
    state.sampleAlphaToOne = glIsEnabled(kSampleAlphaToOne);
    state.sampleCoverage = glIsEnabled(kSampleCoverage);
    return state;
}

[[nodiscard]] bool sameState(const GlState& left, const GlState& right) noexcept {
    return left.viewport == right.viewport && left.scissor == right.scissor
        && left.activeTexture == right.activeTexture && left.textures == right.textures
        && left.samplers == right.samplers
        && left.sourceRgb == right.sourceRgb && left.destinationRgb == right.destinationRgb
        && left.sourceAlpha == right.sourceAlpha
        && left.destinationAlpha == right.destinationAlpha && left.blend == right.blend
        && left.scissorTest == right.scissorTest && left.depthTest == right.depthTest
        && left.cullFace == right.cullFace && left.equationRgb == right.equationRgb
        && left.equationAlpha == right.equationAlpha && left.program == right.program
        && left.vertexArray == right.vertexArray && left.arrayBuffer == right.arrayBuffer
        && left.depthFunction == right.depthFunction && left.depthWrite == right.depthWrite
        && left.polygonMode == right.polygonMode && left.colorMask == right.colorMask
        && left.depthRange == right.depthRange && left.stencilFront == right.stencilFront
        && left.stencilBack == right.stencilBack
        && left.stencilTest == right.stencilTest
        && left.framebufferSrgb == right.framebufferSrgb
        && left.rasterizerDiscard == right.rasterizerDiscard
        && left.colorLogicOp == right.colorLogicOp && left.dither == right.dither
        && left.multisample == right.multisample
        && left.sampleAlphaToCoverage == right.sampleAlphaToCoverage
        && left.sampleAlphaToOne == right.sampleAlphaToOne
        && left.sampleCoverage == right.sampleCoverage;
}

void drainErrors() noexcept {
    while (glGetError() != GL_NO_ERROR) {}
}

[[nodiscard]] GLuint createLinkedHostProgram(const TestFunctions& gl) {
    constexpr const char* vertexSource = R"glsl(
#version 330 core
void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)glsl";
    constexpr const char* fragmentSource = R"glsl(
#version 330 core
out vec4 outputColor;
void main() { outputColor = vec4(1.0); }
)glsl";
    const GLuint vertex = gl.createShader(kVertexShader);
    const GLuint fragment = gl.createShader(kFragmentShader);
    gl.shaderSource(vertex, 1, &vertexSource, nullptr);
    gl.shaderSource(fragment, 1, &fragmentSource, nullptr);
    gl.compileShader(vertex);
    gl.compileShader(fragment);
    const GLuint program = gl.createProgram();
    gl.attachShader(program, vertex);
    gl.attachShader(program, fragment);
    gl.linkProgram(program);
    gl.deleteShader(vertex);
    gl.deleteShader(fragment);
    return program;
}

void setHostState(
    const TestFunctions& gl,
    const std::array<GLuint, 8>& textures,
    const std::array<GLuint, 8>& samplers,
    std::mt19937& random) {
    std::uniform_int_distribution<int> coordinate(0, 23);
    std::uniform_int_distribution<int> extent(32, 128);
    std::uniform_int_distribution<int> toggle(0, 1);
    std::uniform_int_distribution<int> textureUnit(0, 7);
    constexpr std::array blendFactors{GL_ONE, GL_ZERO, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
    constexpr std::array blendEquations{kFunctionAdd, kFunctionSubtract, kFunctionReverseSubtract};
    constexpr std::array polygonModes{GL_FILL, GL_LINE, GL_POINT};
    constexpr std::array depthFunctions{GL_NEVER, GL_LESS, GL_LEQUAL, GL_ALWAYS};
    std::uniform_int_distribution<std::size_t> blendFactor(0, blendFactors.size() - 1U);
    std::uniform_int_distribution<std::size_t> blendEquation(0, blendEquations.size() - 1U);
    std::uniform_int_distribution<std::size_t> polygonMode(0, polygonModes.size() - 1U);
    std::uniform_int_distribution<std::size_t> depthFunction(0, depthFunctions.size() - 1U);

    for (std::uint32_t slot = 0; slot < textures.size(); ++slot) {
        gl.activeTexture(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, textures[slot]);
        gl.bindSampler(slot, samplers[slot]);
    }
    gl.activeTexture(kTexture0 + static_cast<GLenum>(textureUnit(random)));
    glViewport(coordinate(random), coordinate(random), extent(random), extent(random));
    glScissor(coordinate(random), coordinate(random), extent(random), extent(random));
    (toggle(random) != 0 ? glEnable : glDisable)(GL_BLEND);
    gl.blendFuncSeparate(
        blendFactors[blendFactor(random)], blendFactors[blendFactor(random)],
        blendFactors[blendFactor(random)], blendFactors[blendFactor(random)]);
    gl.blendEquationSeparate(
        blendEquations[blendEquation(random)], blendEquations[blendEquation(random)]);
    (toggle(random) != 0 ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (toggle(random) != 0 ? glEnable : glDisable)(GL_DEPTH_TEST);
    (toggle(random) != 0 ? glEnable : glDisable)(GL_CULL_FACE);
    (toggle(random) != 0 ? glEnable : glDisable)(GL_STENCIL_TEST);
    (toggle(random) != 0 ? glEnable : glDisable)(kFramebufferSrgb);
    (toggle(random) != 0 ? glEnable : glDisable)(kRasterizerDiscard);
    (toggle(random) != 0 ? glEnable : glDisable)(GL_COLOR_LOGIC_OP);
    (toggle(random) != 0 ? glEnable : glDisable)(GL_DITHER);
    (toggle(random) != 0 ? glEnable : glDisable)(kMultisample);
    (toggle(random) != 0 ? glEnable : glDisable)(kSampleAlphaToCoverage);
    (toggle(random) != 0 ? glEnable : glDisable)(kSampleAlphaToOne);
    (toggle(random) != 0 ? glEnable : glDisable)(kSampleCoverage);
    glDepthFunc(depthFunctions[depthFunction(random)]);
    glDepthMask(toggle(random) != 0 ? GL_TRUE : GL_FALSE);
    glDepthRange(toggle(random) != 0 ? 0.125 : 0.25, toggle(random) != 0 ? 0.75 : 0.875);
    glPolygonMode(GL_FRONT, polygonModes[polygonMode(random)]);
    glPolygonMode(GL_BACK, polygonModes[polygonMode(random)]);
    glColorMask(
        toggle(random) != 0 ? GL_TRUE : GL_FALSE,
        toggle(random) != 0 ? GL_TRUE : GL_FALSE,
        toggle(random) != 0 ? GL_TRUE : GL_FALSE,
        toggle(random) != 0 ? GL_TRUE : GL_FALSE);
    glStencilFunc(GL_NEVER, 3, 0x3FU);
    glStencilMask(0x55U);
    glStencilOp(GL_REPLACE, GL_INCR, GL_DECR);
}

} // namespace

int main() {
    using namespace henia::ui;

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
    TestFunctions testGl;
    if (!testGl.load()) {
        std::cout << "OpenGL 3.3 test entry points are unavailable; output test skipped\n";
        return 77;
    }

    GLuint hostVertexArray = 0;
    GLuint hostArrayBuffer = 0;
    testGl.genVertexArrays(1, &hostVertexArray);
    testGl.genBuffers(1, &hostArrayBuffer);
    testGl.bindVertexArray(hostVertexArray);
    testGl.bindBuffer(kArrayBuffer, hostArrayBuffer);
    testGl.bufferData(kArrayBuffer, 64, nullptr, kStreamDraw);
    glEnable(0xFFFFFFFFU);

    TextureStore textures;
    Frame frame;
    const RenderPacket packet = henia::test::buildUiVisualScene(textures, frame);
    OpenGlRenderer oversizedRenderer;
    if (oversizedRenderer.initialize(std::numeric_limits<std::size_t>::max(), 1, 1)
        || oversizedRenderer.initialized()
        || oversizedRenderer.statistics().initializationFailures != 1) {
        fail("OpenGL renderer accepted an overflowing instance capacity");
    }
    OpenGlRenderer renderer;
    if (!renderer.initialize(64, 8, 3) || !renderer.synchronizeTextures(textures)) {
        std::cout << "OpenGL 3.3 renderer unavailable (" << version << "): "
                  << renderer.lastError() << "\n";
        return 77;
    }
    if (renderer.statistics().ignoredHostErrors == 0) {
        fail("OpenGL initialization attributed a pre-existing host error to resource setup");
    }
    GLint initializedVertexArray = 0;
    GLint initializedArrayBuffer = 0;
    glGetIntegerv(kVertexArrayBinding, &initializedVertexArray);
    glGetIntegerv(kArrayBufferBinding, &initializedArrayBuffer);
    if (initializedVertexArray != static_cast<GLint>(hostVertexArray)
        || initializedArrayBuffer != static_cast<GLint>(hostArrayBuffer)) {
        fail("OpenGL initialization did not preserve host buffer bindings");
    }
    if (renderer.render(packet, std::numeric_limits<std::uint32_t>::max(), 128)
        || renderer.lastError() != "viewportWidth/viewportHeight is outside the GLsizei range") {
        fail("OpenGL renderer accepted an out-of-range viewport");
    }
    const OpenGlRenderStatistics rejected = renderer.statistics();
    if (rejected.invalidInputFrames != 1 || rejected.capacityRejectedFrames != 0
        || rejected.drawCalls != 0 || rejected.instanceUploads != 0) {
        fail("OpenGL invalid-input rejection issued work or used capacity statistics");
    }

    Frame offscreenFrame;
    Canvas& offscreenCanvas = offscreenFrame.begin();
    {
        Canvas::ClipScope clip = offscreenCanvas.scopedClip(
            {{200.25F, 200.25F}, {220.75F, 220.75F}});
        if (!clip.active()) fail("OpenGL off-screen clip setup failed");
        offscreenCanvas.fillRect(
            {{202.0F, 202.0F}, {218.0F, 218.0F}},
            {1.0F, 1.0F, 1.0F, 1.0F});
    }
    const RenderPacket offscreenPacket = offscreenFrame.finish();
    if (offscreenPacket.instances().size() != 1 || offscreenPacket.batches().size() != 1) {
        fail("OpenGL off-screen scissor packet compiled unexpectedly");
    }
    const OpenGlRenderStatistics beforeOffscreen = renderer.statistics();
    if (!renderer.render(
            offscreenPacket,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight)) {
        fail("OpenGL rejected a valid fully off-screen scissor");
    }
    const OpenGlRenderStatistics afterOffscreen = renderer.statistics();
    if (afterOffscreen.successfulFrames != beforeOffscreen.successfulFrames + 1U
        || afterOffscreen.frameAttempts != beforeOffscreen.frameAttempts + 1U
        || afterOffscreen.drawCalls != beforeOffscreen.drawCalls
        || afterOffscreen.instanceUploads != beforeOffscreen.instanceUploads) {
        fail("OpenGL submitted or uploaded a fully off-screen scissor batch");
    }

    HiddenOpenGlContext wrongContext;
    if (!wrongContext.ready()) {
        fail("Unable to create the wrong-context validation context");
    }
    if (renderer.render(packet, 128, 128)
        || renderer.lastError() != "OpenGL UI call requires the exact context used by initialize()"
        || renderer.synchronizeTextures(textures) || renderer.shutdown() || !renderer.initialized()) {
        fail("OpenGL UI wrong-context calls were not rejected with resources preserved");
    }
    OpenGlRenderer secondaryRenderer;
    Frame secondaryFrame;
    Canvas& secondaryCanvas = secondaryFrame.begin();
    secondaryCanvas.fillRect({{8.0F, 8.0F}, {24.0F, 24.0F}}, {1.0F, 1.0F, 1.0F, 1.0F});
    const RenderPacket secondaryPacket = secondaryFrame.finish();
    if (!secondaryRenderer.initialize(4, 2, 1)
        || !secondaryRenderer.initialize(4, 2, 1)
        || secondaryRenderer.initialize(5, 2, 1)
        || secondaryRenderer.lastError()
            != "OpenGL renderer is already initialized with a different configuration"
        || !secondaryRenderer.initialized()
        || !secondaryRenderer.render(secondaryPacket, 32, 32)) {
        fail("OpenGL independent-context renderer lifecycle failed");
    }
    if (!context.makeCurrent()) {
        fail("Unable to restore the OpenGL owner context");
    }
    if (secondaryRenderer.render(secondaryPacket, 32, 32)
        || secondaryRenderer.lastError()
            != "OpenGL UI call requires the exact context used by initialize()") {
        fail("OpenGL renderer lost its independent owner-context identity");
    }
    if (!wrongContext.makeCurrent() || !secondaryRenderer.shutdown()
        || secondaryRenderer.initialized() || !context.makeCurrent()) {
        fail("OpenGL independent-context renderer shutdown failed");
    }

    TextureStore lifetimeTextures;
    const std::array<std::byte, 4> lifetimePixels{
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    const TextureHandle lifetimeOld = lifetimeTextures.create(
        TextureFormat::Rgba8, 1, 1, 4, lifetimePixels);
    Frame lifetimeOldFrame;
    lifetimeOldFrame.begin().image(
        lifetimeOld, {{0.0F, 0.0F}, {8.0F, 8.0F}});
    const RenderPacket lifetimeOldPacket = lifetimeOldFrame.finish();
    OpenGlRenderer lifetimeRenderer;
    if (!lifetimeRenderer.initialize(4, 2, 2)
        || !lifetimeRenderer.synchronizeTextures(lifetimeTextures)
        || !lifetimeRenderer.render(lifetimeOldPacket, 8, 8)
        || !lifetimeTextures.destroy(lifetimeOld)) {
        fail("OpenGL resource lifetime fixture failed");
    }
    const TextureHandle lifetimeReplacement = lifetimeTextures.create(
        TextureFormat::Rgba8, 1, 1, 4, lifetimePixels);
    if (lifetimeReplacement.value() != lifetimeOld.value()
        || lifetimeReplacement.generation() == lifetimeOld.generation()
        || !lifetimeRenderer.synchronizeTextures(lifetimeTextures)
        || lifetimeRenderer.render(lifetimeOldPacket, 8, 8)) {
        fail("OpenGL renderer accepted a stale packet after texture-slot reuse");
    }
    Frame lifetimeReplacementFrame;
    lifetimeReplacementFrame.begin().image(
        lifetimeReplacement, {{0.0F, 0.0F}, {8.0F, 8.0F}});
    if (!lifetimeRenderer.render(lifetimeReplacementFrame.finish(), 8, 8)) {
        fail("OpenGL renderer rejected the replacement texture generation");
    }

    GLuint borrowedTexture = 0;
    glGenTextures(1, &borrowedTexture);
    glBindTexture(GL_TEXTURE_2D, borrowedTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        1,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        lifetimePixels.data());
    const TextureHandle borrowedHandle = lifetimeTextures.createExternal(
        TextureFormat::Rgba8, 1, 1);
    Frame borrowedFrame;
    borrowedFrame.begin().image(borrowedHandle, {{0.0F, 0.0F}, {8.0F, 8.0F}});
    if (!lifetimeRenderer.bindExternalTexture(
            lifetimeTextures,
            borrowedHandle,
            borrowedTexture,
            OpenGlExternalTextureOwnership::Borrowed)) {
        std::cerr << lifetimeRenderer.lastError() << '\n';
        fail("OpenGL borrowed external texture binding failed");
    }
    glFinish();
    if (!lifetimeRenderer.render(borrowedFrame.finish(), 8, 8)
        || lifetimeRenderer.statistics().externalTextures != 1) {
        fail("OpenGL borrowed external texture was not renderable");
    }
    if (!lifetimeRenderer.shutdown() || glIsTexture(borrowedTexture) != GL_TRUE) {
        fail("OpenGL borrowed external texture ownership was not preserved");
    }
    glDeleteTextures(1, &borrowedTexture);

    TextureStore recoveryTextures;
    const TextureHandle recoveryHandle = recoveryTextures.create(
        TextureFormat::Rgba8,
        1,
        1,
        4,
        lifetimePixels,
        {.backingPolicy = TextureBackingPolicy::DiscardAfterUpload});
    if (!lifetimeRenderer.initialize(4, 1, 1)
        || !lifetimeRenderer.synchronizeTextures(recoveryTextures)
        || !recoveryTextures.discardCpuBacking(recoveryHandle)
        || !lifetimeRenderer.shutdown()
        || !lifetimeRenderer.initialize(4, 1, 1)
        || lifetimeRenderer.synchronizeTextures(recoveryTextures)
        || lifetimeRenderer.lastError()
            != "OpenGL texture CPU backing is unavailable; restore it before synchronization"
        || !recoveryTextures.restoreCpuBacking(recoveryHandle, 4, lifetimePixels)
        || !lifetimeRenderer.synchronizeTextures(recoveryTextures)
        || !lifetimeRenderer.shutdown()) {
        fail("OpenGL discarded CPU backing was not explicit and recoverable after recreation");
    }

    glViewport(0, 0, henia::test::kVisualWidth, henia::test::kVisualHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    std::array<GLuint, 8> hostTextures{};
    std::array<GLuint, 8> hostSamplers{};
    glGenTextures(static_cast<GLsizei>(hostTextures.size()), hostTextures.data());
    testGl.genSamplers(static_cast<GLsizei>(hostSamplers.size()), hostSamplers.data());
    for (std::uint32_t slot = 0; slot < hostTextures.size(); ++slot) {
        testGl.activeTexture(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, hostTextures[slot]);
        testGl.bindSampler(slot, hostSamplers[slot]);
    }
    GLuint hostUnpackBuffer = 0;
    testGl.genBuffers(1, &hostUnpackBuffer);
    testGl.bindBuffer(kPixelUnpackBuffer, hostUnpackBuffer);
    testGl.bufferData(kPixelUnpackBuffer, 64, nullptr, kStreamDraw);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(kUnpackRowLength, 17);
    const TextureView atlasView = textures.view(TextureHandle{1});
    const std::array<std::byte, 1> atlasPatch{atlasView.pixels[5]};
    if (!textures.updateRegion(atlasView.handle, {1, 1, 1, 1}, 1, atlasPatch)
        || !renderer.synchronizeTextures(textures)) {
        fail("OpenGL partial texture synchronization failed with a host unpack buffer");
    }
    GLint restoredUnpackBuffer = 0;
    GLint restoredUnpackAlignment = 0;
    GLint restoredUnpackRowLength = 0;
    glGetIntegerv(kPixelUnpackBufferBinding, &restoredUnpackBuffer);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &restoredUnpackAlignment);
    glGetIntegerv(kUnpackRowLength, &restoredUnpackRowLength);
    if (restoredUnpackBuffer != static_cast<GLint>(hostUnpackBuffer)
        || restoredUnpackAlignment != 8 || restoredUnpackRowLength != 17) {
        fail("OpenGL texture synchronization did not restore pixel-unpack state");
    }
    std::mt19937 random(0x0BADC0DE);
    setHostState(testGl, hostTextures, hostSamplers, random);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_NEVER, 7, 0xFFU);
    glEnable(kFramebufferSrgb);
    glEnable(kRasterizerDiscard);
    glEnable(GL_COLOR_LOGIC_OP);
    glEnable(kSampleAlphaToCoverage);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
    testGl.blendEquationSeparate(kFunctionReverseSubtract, kFunctionSubtract);
    const GlState before = captureState(testGl);

    glEnable(0xFFFFFFFFU);

    if (!renderer.render(
            packet,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    const TextureView inFlightView = textures.view(TextureHandle{1});
    const std::vector<std::byte> inFlightPixels(
        inFlightView.pixels.begin(), inFlightView.pixels.end());
    if (!textures.update(inFlightView.handle, inFlightView.rowPitch, inFlightPixels)
        || !renderer.synchronizeTextures(textures)) {
        fail("OpenGL could not transactionally replace an in-flight texture");
    }
    const GlState after = captureState(testGl);
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

    TextureStore contractTextures;
    Frame contractFrame;
    const RenderPacket contractPacket =
        henia::test::buildTextureContractScene(contractTextures, contractFrame);
    OpenGlRenderer contractRenderer;
    if (!contractRenderer.initialize(8, 4, 2)
        || !contractRenderer.synchronizeTextures(contractTextures)) {
        fail("OpenGL texture-contract renderer setup failed");
    }
    if (contractRenderer.render(
            contractPacket,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight,
            static_cast<RenderTargetColorSpace>(0xFFU))
        || contractRenderer.lastError() != "targetColorSpace is invalid") {
        fail("OpenGL renderer accepted an invalid target color space");
    }
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!contractRenderer.render(
            contractPacket,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight)) {
        std::cerr << contractRenderer.lastError() << '\n';
        fail("OpenGL texture-contract render failed");
    }
    glFinish();
    glReadPixels(
        0,
        0,
        henia::test::kVisualWidth,
        henia::test::kVisualHeight,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        bottomUp.data());
    for (std::uint32_t y = 0; y < henia::test::kVisualHeight; ++y) {
        const std::size_t source = static_cast<std::size_t>(
            henia::test::kVisualHeight - 1U - y) * henia::test::kVisualWidth;
        const std::size_t destination = static_cast<std::size_t>(y) * henia::test::kVisualWidth;
        std::copy_n(
            bottomUp.data() + source,
            henia::test::kVisualWidth,
            topDown.data() + destination);
    }
    if (!henia::test::matchesTextureContractGolden(
            topDown,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight)) {
        henia::test::writePpm(
            "opengl-texture-contract-actual.ppm",
            topDown,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight);
        fail("OpenGL texture contract exceeded the golden tolerance");
    }
    if (!contractRenderer.shutdown()) {
        fail("OpenGL texture-contract renderer shutdown failed");
    }

    for (int iteration = 0; iteration < 32; ++iteration) {
        setHostState(testGl, hostTextures, hostSamplers, random);
        const GlState randomizedBefore = captureState(testGl);
        if (!renderer.render(
                packet,
                henia::test::kVisualWidth,
                henia::test::kVisualHeight)) {
            fail("OpenGL randomized-state render failed");
        }
        if (!sameState(randomizedBefore, captureState(testGl))) {
            fail("OpenGL renderer did not restore randomized host state");
        }
        glFinish();
    }

    drainErrors();
    const GLuint pendingDeleteProgram = createLinkedHostProgram(testGl);
    testGl.useProgram(pendingDeleteProgram);
    testGl.deleteProgram(pendingDeleteProgram);
    if (!renderer.render(packet, henia::test::kVisualWidth, henia::test::kVisualHeight)) {
        fail("OpenGL UI failed with a pending-delete host program");
    }
    GLint restoredProgram = -1;
    glGetIntegerv(kCurrentProgram, &restoredProgram);
    if (restoredProgram != 0 || glGetError() != GL_NO_ERROR) {
        fail("OpenGL UI blindly rebound a deleted host program name");
    }
    glFinish();

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
    if (statistics.successfulFrames != 99 || statistics.frameAttempts != 101
        || statistics.instanceUploads != 65
        || statistics.textureUploads != 4
        || statistics.fullTextureUploads != 3 || statistics.partialTextureUploads != 1
        || statistics.uploadedTextureBytes != 97 || statistics.gpuTextureBytes != 80
        || statistics.uploadFenceFailures != 0 || statistics.rejectedFrames != 2
        || statistics.invalidInputFrames != 1 || statistics.capacityRejectedFrames != 0
        || statistics.wrongContextCalls != 3 || statistics.ignoredHostErrors == 0
        || statistics.stateRestoreFailures != 0 || statistics.initializationFailures != 0
        || statistics.textureSynchronizationFailures != 0
        || statistics.frameAttempts != statistics.successfulFrames + statistics.rejectedFrames
        || statistics.profile.cumulative.samples != statistics.successfulFrames
        || statistics.profile.latestSample.identity.sampleId == 0
        || statistics.profile.latestSample.gpuTimingAvailable) {
        fail("OpenGL multi-slot stress statistics are incorrect");
    }

    using namespace henia::gfx;
    OpenGlRenderDevice oversizedGfx;
    if (oversizedGfx.initialize(std::numeric_limits<std::size_t>::max(), 1)
        || oversizedGfx.initialized()
        || oversizedGfx.statistics().initializationFailures != 1) {
        fail("OpenGL gfx accepted an overflowing box capacity");
    }
    ShapeBatch3D shapes;
    if (shapes.addBox({}) == ShapeBatch3D::kInvalidIndex) {
        fail("OpenGL gfx validation setup rejected a valid box");
    }
    const InstanceBatch boxBatch = shapes.snapshot();
    OpenGlRenderDevice gfx;
    glEnable(0xFFFFFFFFU);
    if (!gfx.initialize(512, 1)) {
        fail("OpenGL gfx validation renderer did not initialize");
    }
    if (gfx.statistics().ignoredHostErrors == 0) {
        fail("OpenGL gfx initialization attributed a pre-existing host error to resource setup");
    }
    ViewParameters invalidView{.viewport = {128.0F, 128.0F}};
    invalidView.timeSeconds = std::numeric_limits<float>::quiet_NaN();
    if (gfx.render(boxBatch, invalidView)
        || gfx.lastError() != "view.timeSeconds") {
        fail("OpenGL gfx accepted a non-finite view");
    }
    const OpenGlGfxStatistics invalidGfxStatistics = gfx.statistics();
    if (invalidGfxStatistics.invalidInputFrames != 1
        || invalidGfxStatistics.capacityRejectedFrames != 0
        || invalidGfxStatistics.drawCalls != 0 || invalidGfxStatistics.fullInstanceUploads != 0) {
        fail("OpenGL gfx invalid-input rejection issued GPU work");
    }

    ViewParameters validView{.viewport = {128.0F, 128.0F}};
    glEnable(0xFFFFFFFFU);
    for (int iteration = 0; iteration < 100; ++iteration) {
        setHostState(testGl, hostTextures, hostSamplers, random);
        const GlState gfxBefore = captureState(testGl);
        if (!gfx.render(boxBatch, validView)) {
            std::cerr << gfx.lastError() << '\n';
            fail("OpenGL gfx randomized-state render failed");
        }
        if (!sameState(gfxBefore, captureState(testGl))) {
            fail("OpenGL gfx did not restore randomized host state");
        }
        glFinish();
    }
    drainErrors();
    const GLuint pendingDeleteGfxProgram = createLinkedHostProgram(testGl);
    testGl.useProgram(pendingDeleteGfxProgram);
    testGl.deleteProgram(pendingDeleteGfxProgram);
    if (!gfx.render(boxBatch, validView)) {
        fail("OpenGL gfx failed with a pending-delete host program");
    }
    glGetIntegerv(kCurrentProgram, &restoredProgram);
    if (restoredProgram != 0 || glGetError() != GL_NO_ERROR) {
        fail("OpenGL gfx blindly rebound a deleted host program name");
    }
    glFinish();
    if (!wrongContext.makeCurrent()) {
        fail("Unable to make the wrong OpenGL context current");
    }
    if (gfx.render(boxBatch, validView) || gfx.shutdown() || !gfx.initialized()) {
        fail("OpenGL gfx wrong-context calls were not rejected with resources preserved");
    }
    OpenGlRenderDevice secondaryGfx;
    if (!secondaryGfx.initialize(1, 1)
        || !secondaryGfx.initialize(1, 1)
        || secondaryGfx.initialize(2, 1)
        || secondaryGfx.lastError()
            != "OpenGL gfx renderer is already initialized with a different configuration"
        || !secondaryGfx.initialized()
        || !secondaryGfx.render(boxBatch, validView)) {
        fail("OpenGL independent-context gfx lifecycle failed");
    }
    if (!context.makeCurrent()) {
        fail("Unable to return to the OpenGL owner context");
    }
    if (secondaryGfx.render(boxBatch, validView)
        || secondaryGfx.lastError()
            != "OpenGL gfx call requires the exact context used by initialize()") {
        fail("OpenGL gfx renderer lost its independent owner-context identity");
    }
    if (!wrongContext.makeCurrent() || !secondaryGfx.shutdown()
        || secondaryGfx.initialized() || !context.makeCurrent()) {
        fail("OpenGL independent-context gfx shutdown failed");
    }

    for (std::size_t index = 1; index < 512; ++index) {
        if (shapes.addBox({}) == ShapeBatch3D::kInvalidIndex) {
            fail("OpenGL gfx paged expansion rejected a valid box");
        }
    }
    const InstanceBatch expandedBoxBatch = shapes.snapshot();
    if (!gfx.render(expandedBoxBatch, validView)) {
        fail("OpenGL gfx paged expansion render failed");
    }
    glFinish();
    BoxInstance sparseFirst = expandedBoxBatch.boxes()[0];
    BoxInstance sparseLast = expandedBoxBatch.boxes()[511];
    sparseFirst.hueOffset = 1.0F;
    sparseLast.hueOffset = 2.0F;
    static_cast<void>(shapes.updateBox(0, sparseFirst));
    static_cast<void>(shapes.updateBox(511, sparseLast));
    const InstanceBatch sparseBoxBatch = shapes.snapshot();
    if (sparseBoxBatch.dirtyRanges().size() != 2
        || !gfx.render(sparseBoxBatch, validView)) {
        fail("OpenGL gfx sparse paged render failed");
    }
    glFinish();

    const OpenGlGfxStatistics gfxStatistics = gfx.statistics();
    if (gfxStatistics.invalidInputFrames != 1 || gfxStatistics.capacityRejectedFrames != 0
        || gfxStatistics.drawCalls != 103 || gfxStatistics.fullInstanceUploads != 1
        || gfxStatistics.partialInstanceUploads != 2
        || gfxStatistics.uploadedInstanceBytes != 514U * sizeof(BoxInstance)
        || gfxStatistics.wrongContextCalls != 2 || gfxStatistics.ignoredHostErrors == 0
        || gfxStatistics.stateRestoreFailures != 0 || gfxStatistics.initializationFailures != 0
        || gfxStatistics.frameAttempts
            != gfxStatistics.successfulFrames + gfxStatistics.rejectedFrames
        || gfxStatistics.profile.cumulative.samples != gfxStatistics.successfulFrames
        || gfxStatistics.profile.cumulative.producerBuilds != 3
        || gfxStatistics.profile.latestSample.identity.producerIdentity
            != sparseBoxBatch.identity()
        || gfxStatistics.profile.latestSample.identity.producerRevision
            != sparseBoxBatch.revision()
        || gfxStatistics.profile.latestSample.uploadKind
            != henia::InstanceUploadKind::DirtyRanges
        || gfxStatistics.profile.latestSample.uploadRangeCount != 2
        || gfxStatistics.profile.latestSample.uploadedInstanceBytes
            != 2U * sizeof(BoxInstance)) {
        fail("OpenGL gfx isolation statistics are incorrect");
    }

    ShapeBatch3D directVisibilityShapes;
    const BoxInstance visibleBox{
        .minimum = {-0.5F, -0.5F, 0.25F},
        .lineWidth = 5.0F,
        .maximum = {0.5F, 0.5F, 0.75F},
        .hueOffset = 0.25F,
        .color = {0.2F, 0.8F, 0.4F, 0.85F},
        .effects = BoxEffect::HueCycle,
    };
    static_cast<void>(directVisibilityShapes.addBox(visibleBox));
    const InstanceBatch directVisibilityBatch = directVisibilityShapes.snapshot();
    glDisable(GL_SCISSOR_TEST);
    glDisable(kRasterizerDiscard);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!gfx.render(directVisibilityBatch, validView)) {
        fail("OpenGL direct visibility reference render failed");
    }
    glFinish();
    const std::vector<henia::test::Rgba8> directVisibilityPixels = readCurrentPixels();

    ShapeBatch3D culledVisibilityShapes;
    std::vector<BoxInstance> culledVisibilityBoxes(512, {
        .minimum = {4.0F, 4.0F, 0.25F},
        .lineWidth = 5.0F,
        .maximum = {4.5F, 4.5F, 0.75F},
        .color = {1.0F, 0.0F, 0.0F, 1.0F},
    });
    culledVisibilityBoxes[0] = visibleBox;
    if (!culledVisibilityShapes.replaceBoxes(culledVisibilityBoxes)) {
        fail("OpenGL culled visibility fixture was rejected");
    }
    const InstanceBatch culledVisibilityBatch = culledVisibilityShapes.snapshot();
    glClear(GL_COLOR_BUFFER_BIT);
    if (!gfx.render(
            culledVisibilityBatch,
            validView,
            false,
            {.mode = VisibilityMode::CpuFrustum})) {
        std::cerr << gfx.lastError() << '\n';
        fail("OpenGL CPU-culled render failed");
    }
    glFinish();
    const std::vector<henia::test::Rgba8> culledVisibilityPixels = readCurrentPixels();
    const OpenGlGfxStatistics culledGfxStatistics = gfx.statistics();
    const bool sameVisibilityPixels = culledVisibilityPixels.size() == directVisibilityPixels.size()
        && std::equal(
            culledVisibilityPixels.begin(),
            culledVisibilityPixels.end(),
            directVisibilityPixels.begin(),
            [](const henia::test::Rgba8& left, const henia::test::Rgba8& right) noexcept {
                return left.red == right.red && left.green == right.green
                    && left.blue == right.blue && left.alpha == right.alpha;
            });
    if (!sameVisibilityPixels
        || culledGfxStatistics.cpuCulledFrames != 1
        || culledGfxStatistics.visibilitySourceInstances != culledVisibilityBoxes.size()
        || culledGfxStatistics.visibilityRejectedInstances
            != culledVisibilityBoxes.size() - 1U
        || culledGfxStatistics.submittedInstances
            != gfxStatistics.submittedInstances + 2U) {
        std::cerr << "OpenGL visibility diagnostics: samePixels=" << sameVisibilityPixels
                  << " cpuFrames=" << culledGfxStatistics.cpuCulledFrames
                  << " source=" << culledGfxStatistics.visibilitySourceInstances
                  << " rejected=" << culledGfxStatistics.visibilityRejectedInstances
                  << " submittedDelta="
                  << (culledGfxStatistics.submittedInstances - gfxStatistics.submittedInstances)
                  << '\n';
        fail("OpenGL CPU visibility changed output or submitted offscreen instances");
    }

    OpenGlRenderDevice clipGfx;
    if (!clipGfx.initialize(1, 1)) {
        fail("OpenGL gfx clip-sweep renderer did not initialize");
    }
    const auto renderGfxFrame = [&](const InstanceBatch& batch, const ViewParameters& view) {
        glViewport(
            0,
            0,
            henia::test::kVisualWidth,
            henia::test::kVisualHeight);
        glDisable(GL_SCISSOR_TEST);
        glDisable(kRasterizerDiscard);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!clipGfx.render(batch, view)) {
            std::cerr << clipGfx.lastError() << '\n';
            fail("OpenGL gfx visual-regression render failed");
        }
        glFinish();
        return readCurrentPixels();
    };
    constexpr std::array depthRanges{
        ClipDepthRange::ZeroToOne,
        ClipDepthRange::MinusOneToOne,
    };
    for (const ClipDepthRange depthRange : depthRanges) {
        const std::string_view rangeName = depthRange == ClipDepthRange::ZeroToOne
            ? "zero-to-one"
            : "minus-one-to-one";
        for (const henia::test::GfxClipSweep& sweep : henia::test::kGfxClipSweeps) {
            for (const henia::test::GfxClipFrame& frameValue : sweep.frames) {
                const std::vector<henia::test::Rgba8> clipPixels = renderGfxFrame(
                    henia::test::gfxClipBatch(frameValue.box),
                    henia::test::gfxClipView(depthRange));
                if (!henia::test::matchesGfxClipFrame(clipPixels, frameValue.position)) {
                    const std::string filename = "opengl-gfx-clip-" + std::string(sweep.plane)
                        + '-' + std::string(rangeName) + "-actual.ppm";
                    henia::test::writePpm(
                        filename,
                        clipPixels,
                        henia::test::kVisualWidth,
                        henia::test::kVisualHeight);
                    std::cerr << "OpenGL gfx clip sweep failed at " << sweep.plane << " ("
                              << rangeName << "), visible pixels="
                              << henia::test::visibleGfxPixelCount(clipPixels) << '\n';
                    return EXIT_FAILURE;
                }
            }
        }
        const std::vector<henia::test::Rgba8> cameraPixels = renderGfxFrame(
            henia::test::gfxClipBatch(henia::test::kGfxCameraCrossingBox),
            henia::test::gfxClipView(depthRange));
        if (!henia::test::matchesGfxCameraCrossing(cameraPixels)) {
            const std::string filename = "opengl-gfx-camera-crossing-"
                + std::string(rangeName) + "-actual.ppm";
            henia::test::writePpm(
                filename,
                cameraPixels,
                henia::test::kVisualWidth,
                henia::test::kVisualHeight);
            std::cerr << "OpenGL camera-crossing edges were not shortened continuously ("
                      << rangeName << "), visible pixels="
                      << henia::test::visibleGfxPixelCount(cameraPixels) << '\n';
            return EXIT_FAILURE;
        }
    }
    for (const henia::test::GfxAaCase& aaCase : henia::test::kGfxAaCases) {
        const std::vector<henia::test::Rgba8> aaPixels = renderGfxFrame(
            henia::test::gfxAaBatch(aaCase),
            henia::test::gfxAaView());
        if (!henia::test::matchesGfxAaCase(aaPixels, aaCase)) {
            const std::string filename = "opengl-gfx-aa-" + std::string(aaCase.name)
                + "-actual.ppm";
            henia::test::writePpm(
                filename,
                aaPixels,
                henia::test::kVisualWidth,
                henia::test::kVisualHeight);
            std::cerr << "OpenGL gfx AA golden failed for " << aaCase.name << '\n';
            return EXIT_FAILURE;
        }
    }
    if (!clipGfx.shutdown()) {
        fail("OpenGL gfx clip-sweep renderer did not shut down");
    }

    for (std::uint32_t slot = 0; slot < hostTextures.size(); ++slot) {
        testGl.activeTexture(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, 0);
        testGl.bindSampler(slot, 0);
    }
    glDeleteTextures(static_cast<GLsizei>(hostTextures.size()), hostTextures.data());
    testGl.deleteSamplers(static_cast<GLsizei>(hostSamplers.size()), hostSamplers.data());
    testGl.bindBuffer(kPixelUnpackBuffer, 0);
    testGl.deleteBuffers(1, &hostUnpackBuffer);
    testGl.bindBuffer(kArrayBuffer, 0);
    testGl.bindVertexArray(0);
    testGl.deleteBuffers(1, &hostArrayBuffer);
    testGl.deleteVertexArrays(1, &hostVertexArray);

    if (!HiddenOpenGlContext::clearCurrent()) {
        fail("Unable to clear the current OpenGL context");
    }
    if (gfx.shutdown() || renderer.shutdown() || !gfx.initialized() || !renderer.initialized()) {
        fail("OpenGL no-context shutdown did not defer resource destruction");
    }
    if (!context.makeCurrent() || !gfx.shutdown() || !renderer.shutdown()
        || gfx.initialized() || renderer.initialized()) {
        fail("OpenGL deferred destruction did not complete on the owner context");
    }
    if (!renderer.initialize(64, 8, 3) || !gfx.initialize(1, 1)
        || !renderer.initialized() || !gfx.initialized()
        || !gfx.shutdown() || !renderer.shutdown()) {
        fail("OpenGL renderers did not recreate after orderly shutdown");
    }

    OpenGlRenderer abandonedRenderer;
    OpenGlRenderDevice abandonedGfx;
    {
        HiddenOpenGlContext lostContext;
        if (!lostContext.ready()
            || !abandonedRenderer.initialize(4, 2, 1)
            || !abandonedGfx.initialize(1, 1)) {
            fail("Unable to create OpenGL context-loss validation resources");
        }
    }
    abandonedRenderer.abandon();
    abandonedGfx.abandon();
    if (abandonedRenderer.initialized() || abandonedGfx.initialized()
        || abandonedRenderer.statistics().abandonedContexts != 1
        || abandonedGfx.statistics().abandonedContexts != 1
        || !context.makeCurrent()
        || !abandonedRenderer.initialize(4, 2, 1)
        || !abandonedGfx.initialize(1, 1)
        || !abandonedGfx.shutdown() || !abandonedRenderer.shutdown()) {
        fail("OpenGL context-loss abandon/recreation lifecycle failed");
    }
    std::cout << "HeniaUI OpenGL hidden-window output test passed (" << version << ")\n";
    return EXIT_SUCCESS;
}
