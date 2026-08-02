#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"
#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"

#include "../FixedError.h"
#include "OpenGlFailure.h"
#include "OpenGlUploadRing.h"

#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY*
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace henia::gfx {
namespace {

using henia::detail::assignGlFailure;

using GlChar = char;
using GlSize = std::ptrdiff_t;
using GlIntPtr = std::ptrdiff_t;
struct GlSyncObject;
using GlSync = GlSyncObject*;

constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kDynamicDraw = 0x88E8;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kVertexArrayBinding = 0x85B5;
constexpr GLenum kMapWriteBit = 0x0002;
constexpr GLenum kMapUnsynchronizedBit = 0x0020;
constexpr GLenum kSyncGpuCommandsComplete = 0x9117;
constexpr GLenum kAlreadySignaled = 0x911A;
constexpr GLenum kTimeoutExpired = 0x911B;
constexpr GLenum kConditionSatisfied = 0x911C;
constexpr GLbitfield kSyncFlushCommandsBit = 0x00000001;
constexpr GLenum kBlendSourceRgb = 0x80C9;
constexpr GLenum kBlendDestinationRgb = 0x80C8;
constexpr GLenum kBlendSourceAlpha = 0x80CB;
constexpr GLenum kBlendDestinationAlpha = 0x80CA;
constexpr GLenum kBlendEquationRgb = 0x8009;
constexpr GLenum kBlendEquationAlpha = 0x883D;
constexpr GLenum kFunctionAdd = 0x8006;
constexpr GLenum kFramebufferSrgb = 0x8DB9;
constexpr GLenum kRasterizerDiscard = 0x8C89;
constexpr GLenum kMultisample = 0x809D;
constexpr GLenum kSampleAlphaToCoverage = 0x809E;
constexpr GLenum kSampleAlphaToOne = 0x809F;
constexpr GLenum kSampleCoverage = 0x80A0;

static_assert(std::is_standard_layout_v<BoxInstance>);
static_assert(sizeof(BoxInstance) == 64);
static_assert(offsetof(BoxInstance, lineWidth) == 12);
static_assert(offsetof(BoxInstance, maximum) == 16);
static_assert(offsetof(BoxInstance, color) == 32);
static_assert(offsetof(BoxInstance, effects) == 48);

using CreateShaderFn = GLuint(APIENTRYP)(GLenum);
using ShaderSourceFn = void(APIENTRYP)(GLuint, GLsizei, const GlChar* const*, const GLint*);
using CompileShaderFn = void(APIENTRYP)(GLuint);
using GetShaderIvFn = void(APIENTRYP)(GLuint, GLenum, GLint*);
using GetShaderInfoLogFn = void(APIENTRYP)(GLuint, GLsizei, GLsizei*, GlChar*);
using DeleteShaderFn = void(APIENTRYP)(GLuint);
using CreateProgramFn = GLuint(APIENTRYP)();
using AttachShaderFn = void(APIENTRYP)(GLuint, GLuint);
using LinkProgramFn = void(APIENTRYP)(GLuint);
using GetProgramIvFn = void(APIENTRYP)(GLuint, GLenum, GLint*);
using GetProgramInfoLogFn = void(APIENTRYP)(GLuint, GLsizei, GLsizei*, GlChar*);
using DeleteProgramFn = void(APIENTRYP)(GLuint);
using UseProgramFn = void(APIENTRYP)(GLuint);
using GenVertexArraysFn = void(APIENTRYP)(GLsizei, GLuint*);
using BindVertexArrayFn = void(APIENTRYP)(GLuint);
using DeleteVertexArraysFn = void(APIENTRYP)(GLsizei, const GLuint*);
using GenBuffersFn = void(APIENTRYP)(GLsizei, GLuint*);
using BindBufferFn = void(APIENTRYP)(GLenum, GLuint);
using BufferDataFn = void(APIENTRYP)(GLenum, GlSize, const void*, GLenum);
using MapBufferRangeFn = void*(APIENTRYP)(GLenum, GlIntPtr, GlSize, GLbitfield);
using UnmapBufferFn = GLboolean(APIENTRYP)(GLenum);
using DeleteBuffersFn = void(APIENTRYP)(GLsizei, const GLuint*);
using FenceSyncFn = GlSync(APIENTRYP)(GLenum, GLbitfield);
using ClientWaitSyncFn = GLenum(APIENTRYP)(GlSync, GLbitfield, std::uint64_t);
using DeleteSyncFn = void(APIENTRYP)(GlSync);
using EnableVertexAttribArrayFn = void(APIENTRYP)(GLuint);
using VertexAttribPointerFn = void(APIENTRYP)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using VertexAttribIPointerFn = void(APIENTRYP)(GLuint, GLint, GLenum, GLsizei, const void*);
using VertexAttribDivisorFn = void(APIENTRYP)(GLuint, GLuint);
using GetUniformLocationFn = GLint(APIENTRYP)(GLuint, const GlChar*);
using UniformMatrix4fvFn = void(APIENTRYP)(GLint, GLsizei, GLboolean, const GLfloat*);
using Uniform2fFn = void(APIENTRYP)(GLint, GLfloat, GLfloat);
using Uniform1fFn = void(APIENTRYP)(GLint, GLfloat);
using Uniform1iFn = void(APIENTRYP)(GLint, GLint);
using DrawArraysInstancedFn = void(APIENTRYP)(GLenum, GLint, GLsizei, GLsizei);
using BlendFuncSeparateFn = void(APIENTRYP)(GLenum, GLenum, GLenum, GLenum);
using BlendEquationSeparateFn = void(APIENTRYP)(GLenum, GLenum);
using IsProgramFn = GLboolean(APIENTRYP)(GLuint);
using GetBooleanIndexedFn = void(APIENTRYP)(GLenum, GLuint, GLboolean*);
using ColorMaskIndexedFn = void(APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);

struct GlFunctions final {
    CreateShaderFn createShader = nullptr;
    ShaderSourceFn shaderSource = nullptr;
    CompileShaderFn compileShader = nullptr;
    GetShaderIvFn getShaderIv = nullptr;
    GetShaderInfoLogFn getShaderInfoLog = nullptr;
    DeleteShaderFn deleteShader = nullptr;
    CreateProgramFn createProgram = nullptr;
    AttachShaderFn attachShader = nullptr;
    LinkProgramFn linkProgram = nullptr;
    GetProgramIvFn getProgramIv = nullptr;
    GetProgramInfoLogFn getProgramInfoLog = nullptr;
    DeleteProgramFn deleteProgram = nullptr;
    UseProgramFn useProgram = nullptr;
    GenVertexArraysFn genVertexArrays = nullptr;
    BindVertexArrayFn bindVertexArray = nullptr;
    DeleteVertexArraysFn deleteVertexArrays = nullptr;
    GenBuffersFn genBuffers = nullptr;
    BindBufferFn bindBuffer = nullptr;
    BufferDataFn bufferData = nullptr;
    MapBufferRangeFn mapBufferRange = nullptr;
    UnmapBufferFn unmapBuffer = nullptr;
    DeleteBuffersFn deleteBuffers = nullptr;
    FenceSyncFn fenceSync = nullptr;
    ClientWaitSyncFn clientWaitSync = nullptr;
    DeleteSyncFn deleteSync = nullptr;
    EnableVertexAttribArrayFn enableVertexAttribArray = nullptr;
    VertexAttribPointerFn vertexAttribPointer = nullptr;
    VertexAttribIPointerFn vertexAttribIPointer = nullptr;
    VertexAttribDivisorFn vertexAttribDivisor = nullptr;
    GetUniformLocationFn getUniformLocation = nullptr;
    UniformMatrix4fvFn uniformMatrix4fv = nullptr;
    Uniform2fFn uniform2f = nullptr;
    Uniform1fFn uniform1f = nullptr;
    Uniform1iFn uniform1i = nullptr;
    DrawArraysInstancedFn drawArraysInstanced = nullptr;
    BlendFuncSeparateFn blendFuncSeparate = nullptr;
    BlendEquationSeparateFn blendEquationSeparate = nullptr;
    IsProgramFn isProgram = nullptr;
    GetBooleanIndexedFn getBooleanIndexed = nullptr;
    ColorMaskIndexedFn colorMaskIndexed = nullptr;
};

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

template <typename Function>
[[nodiscard]] bool load(Function& output, const char* name) noexcept {
    output = reinterpret_cast<Function>(loadOpenGlFunction(name));
    return output != nullptr;
}

[[nodiscard]] bool loadFunctions(GlFunctions& gl) noexcept {
    return load(gl.createShader, "glCreateShader") && load(gl.shaderSource, "glShaderSource")
        && load(gl.compileShader, "glCompileShader") && load(gl.getShaderIv, "glGetShaderiv")
        && load(gl.getShaderInfoLog, "glGetShaderInfoLog") && load(gl.deleteShader, "glDeleteShader")
        && load(gl.createProgram, "glCreateProgram") && load(gl.attachShader, "glAttachShader")
        && load(gl.linkProgram, "glLinkProgram") && load(gl.getProgramIv, "glGetProgramiv")
        && load(gl.getProgramInfoLog, "glGetProgramInfoLog") && load(gl.deleteProgram, "glDeleteProgram")
        && load(gl.useProgram, "glUseProgram") && load(gl.genVertexArrays, "glGenVertexArrays")
        && load(gl.bindVertexArray, "glBindVertexArray") && load(gl.deleteVertexArrays, "glDeleteVertexArrays")
        && load(gl.genBuffers, "glGenBuffers") && load(gl.bindBuffer, "glBindBuffer")
        && load(gl.bufferData, "glBufferData") && load(gl.mapBufferRange, "glMapBufferRange")
        && load(gl.unmapBuffer, "glUnmapBuffer") && load(gl.deleteBuffers, "glDeleteBuffers")
        && load(gl.fenceSync, "glFenceSync") && load(gl.clientWaitSync, "glClientWaitSync")
        && load(gl.deleteSync, "glDeleteSync")
        && load(gl.enableVertexAttribArray, "glEnableVertexAttribArray")
        && load(gl.vertexAttribPointer, "glVertexAttribPointer")
        && load(gl.vertexAttribIPointer, "glVertexAttribIPointer")
        && load(gl.vertexAttribDivisor, "glVertexAttribDivisor")
        && load(gl.getUniformLocation, "glGetUniformLocation")
        && load(gl.uniformMatrix4fv, "glUniformMatrix4fv") && load(gl.uniform2f, "glUniform2f")
        && load(gl.uniform1f, "glUniform1f") && load(gl.uniform1i, "glUniform1i")
        && load(gl.drawArraysInstanced, "glDrawArraysInstanced")
        && load(gl.blendFuncSeparate, "glBlendFuncSeparate")
        && load(gl.blendEquationSeparate, "glBlendEquationSeparate")
        && load(gl.isProgram, "glIsProgram")
        && load(gl.getBooleanIndexed, "glGetBooleani_v")
        && load(gl.colorMaskIndexed, "glColorMaski");
}

[[nodiscard]] GLenum consumeOperationErrors() noexcept {
    const GLenum first = glGetError();
    if (first != GL_NO_ERROR) {
        while (glGetError() != GL_NO_ERROR) {}
    }
    return first;
}

constexpr const char* kVertexShaderSource = R"glsl(
#version 330 core
layout(location = 0) in vec4 instanceMinimumAndWidth;
layout(location = 1) in vec4 instanceMaximumAndHue;
layout(location = 2) in vec4 instanceColor;
layout(location = 3) in uint instanceEffects;

uniform mat4 viewProjection;
uniform vec2 viewportSize;
uniform int zeroToOneDepth;

out vec4 lineColor;
out float edgeDistance;
out float halfWidth;
out float hueOffset;
flat out uint effects;
flat out float validEdge;

const ivec2 edges[12] = ivec2[12](
    ivec2(0, 1), ivec2(2, 3), ivec2(0, 2), ivec2(1, 3),
    ivec2(4, 5), ivec2(6, 7), ivec2(4, 6), ivec2(5, 7),
    ivec2(0, 4), ivec2(1, 5), ivec2(2, 6), ivec2(3, 7));
const vec2 quad[6] = vec2[6](
    vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(0.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

vec3 corner(int code) {
    return vec3(float(code & 1), float((code >> 1) & 1), float((code >> 2) & 1));
}

void main() {
    int edgeIndex = gl_VertexID / 6;
    vec2 vertex = quad[gl_VertexID % 6];
    vec3 minimumValue = instanceMinimumAndWidth.xyz;
    vec3 maximumValue = instanceMaximumAndHue.xyz;
    vec3 start = mix(minimumValue, maximumValue, corner(edges[edgeIndex].x));
    vec3 finish = mix(minimumValue, maximumValue, corner(edges[edgeIndex].y));
    vec4 startClip = viewProjection * vec4(start, 1.0);
    vec4 finishClip = viewProjection * vec4(finish, 1.0);
    validEdge = startClip.w > 0.0001 && finishClip.w > 0.0001 ? 1.0 : 0.0;

    vec2 startNdc = startClip.xy / max(startClip.w, 0.0001);
    vec2 finishNdc = finishClip.xy / max(finishClip.w, 0.0001);
    vec2 directionPixels = (finishNdc - startNdc) * viewportSize * 0.5;
    vec2 normalPixels = length(directionPixels) > 0.0001
        ? normalize(vec2(-directionPixels.y, directionPixels.x))
        : vec2(0.0, 1.0);
    halfWidth = max(instanceMinimumAndWidth.w, 0.5) * 0.5;
    float expandedWidth = halfWidth + 1.25;
    vec2 offsetNdc = normalPixels * expandedWidth * 2.0 / viewportSize * vertex.y;
    vec4 endpoint = mix(startClip, finishClip, vertex.x);
    endpoint.xy += offsetNdc * endpoint.w;
    if (zeroToOneDepth != 0) {
        endpoint.z = endpoint.z * 2.0 - endpoint.w;
    }
    gl_Position = validEdge > 0.5 ? endpoint : vec4(2.0, 2.0, 2.0, 1.0);
    edgeDistance = vertex.y * expandedWidth;
    lineColor = instanceColor;
    hueOffset = instanceMaximumAndHue.w;
    effects = instanceEffects;
}
)glsl";

constexpr const char* kFragmentShaderSource = R"glsl(
#version 330 core
in vec4 lineColor;
in float edgeDistance;
in float halfWidth;
in float hueOffset;
flat in uint effects;
flat in float validEdge;
uniform float timeSeconds;
out vec4 outputColor;

vec3 hue(float value) {
    vec3 shifted = abs(fract(value + vec3(0.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return clamp(shifted - 1.0, 0.0, 1.0);
}

void main() {
    if (validEdge < 0.5) discard;
    float antiAlias = max(fwidth(edgeDistance), 0.75);
    float coverage = 1.0 - smoothstep(halfWidth - antiAlias, halfWidth + antiAlias, abs(edgeDistance));
    vec4 color = lineColor;
    if ((effects & 1u) != 0u) {
        color.rgb *= hue(fract(timeSeconds * 0.08 + hueOffset));
    }
    color.a *= coverage;
    if (color.a <= 0.001) discard;
    outputColor = vec4(color.rgb * color.a, color.a);
}
)glsl";

[[nodiscard]] GLuint compileShader(
    const GlFunctions& gl,
    GLenum type,
    const char* source,
    henia::detail::FixedError& error) noexcept {
    const GLuint shader = gl.createShader(type);
    if (const GLenum glError = consumeOperationErrors(); shader == 0 || glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx shader creation failed", glError, "shaderType", type);
        return 0;
    }
    gl.shaderSource(shader, 1, &source, nullptr);
    gl.compileShader(shader);
    GLint compiled = GL_FALSE;
    gl.getShaderIv(shader, kCompileStatus, &compiled);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx shader compilation failed", glError, "shader", shader);
        gl.deleteShader(shader);
        return 0;
    }
    if (compiled == GL_TRUE) {
        return shader;
    }
    GLint length = 0;
    gl.getShaderIv(shader, kInfoLogLength, &length);
    std::array<char, henia::detail::FixedError::kCapacity> log{};
    const GLsizei logCapacity = static_cast<GLsizei>(log.size() - 1U);
    GLsizei written = 0;
    gl.getShaderInfoLog(shader, std::min(length, logCapacity), &written, log.data());
    error.assign(log.data(), static_cast<std::size_t>(std::max(written, 0)));
    gl.deleteShader(shader);
    return 0;
}

[[nodiscard]] GLenum compareFunction(CompareOp operation) noexcept {
    switch (operation) {
        case CompareOp::Never: return GL_NEVER;
        case CompareOp::Less: return GL_LESS;
        case CompareOp::LessEqual: return GL_LEQUAL;
        case CompareOp::Equal: return GL_EQUAL;
        case CompareOp::GreaterEqual: return GL_GEQUAL;
        case CompareOp::Greater: return GL_GREATER;
        case CompareOp::Always: return GL_ALWAYS;
    }
    return GL_LEQUAL;
}

struct GlState final {
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint sourceRgb = 0;
    GLint destinationRgb = 0;
    GLint sourceAlpha = 0;
    GLint destinationAlpha = 0;
    GLint equationRgb = 0;
    GLint equationAlpha = 0;
    GLint depthFunction = 0;
    std::array<GLint, 2> polygonMode{};
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    std::array<GLboolean, 4> colorMask{};
    std::array<GLdouble, 2> depthRange{};
    GLboolean depthWrite = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
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

[[nodiscard]] std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

} // namespace

struct OpenGlRenderDevice::Implementation final {
    struct UploadSlot final {
        GLuint buffer = 0;
        GlSync fence = nullptr;
    };

    GlFunctions gl{};
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLint viewProjectionLocation = -1;
    GLint viewportLocation = -1;
    GLint timeLocation = -1;
    GLint depthRangeLocation = -1;
    std::size_t capacity = 0;
    std::vector<UploadSlot> uploadSlots;
    henia::detail::OpenGlUploadRing uploadRing;
    OpenGlGfxStatistics statistics{};
    henia::detail::FixedError error;
    HGLRC ownerContext = nullptr;
    bool ready = false;

    [[nodiscard]] bool initialize(
        std::size_t requestedCapacity,
        std::size_t requestedUploadSlots);
    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable) noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    [[nodiscard]] henia::detail::UploadFenceStatus pollUploadSlot(std::size_t index) noexcept;
    [[nodiscard]] bool fenceUploadSlot(std::size_t index) noexcept;
    void configureAttributes() const noexcept;
    [[nodiscard]] GlState captureState() const noexcept;
    [[nodiscard]] bool restoreState(const GlState& state) noexcept;
    [[nodiscard]] bool validateOwnerContext(const char* operation) noexcept;
    void discardHostErrors() noexcept;
};

bool OpenGlRenderDevice::Implementation::initialize(
    std::size_t requestedCapacity,
    std::size_t requestedUploadSlots) {
    if (ready) return validateOwnerContext("initialize");
    std::size_t instanceBytes = 0;
    const HGLRC currentContext = wglGetCurrentContext();
    if (currentContext == nullptr || requestedCapacity == 0 || requestedUploadSlots == 0
        || requestedCapacity > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || requestedUploadSlots > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || !checkedMultiply(requestedCapacity, sizeof(BoxInstance), instanceBytes)
        || instanceBytes > static_cast<std::size_t>(std::numeric_limits<GlSize>::max())) {
        error = "OpenGL gfx configuration has an invalid box/upload capacity";
        return false;
    }
    ownerContext = currentContext;
    uploadSlots.resize(requestedUploadSlots);
    uploadRing.reset(requestedUploadSlots);
    if (!loadFunctions(gl)) {
        error = "OpenGL 3.3 gfx entry points are unavailable";
        return false;
    }
    discardHostErrors();
    const GLuint vertexShader = compileShader(gl, kVertexShader, kVertexShaderSource, error);
    if (vertexShader == 0) return false;
    const GLuint fragmentShader = compileShader(gl, kFragmentShader, kFragmentShaderSource, error);
    if (fragmentShader == 0) {
        gl.deleteShader(vertexShader);
        return false;
    }
    program = gl.createProgram();
    if (const GLenum glError = consumeOperationErrors(); program == 0 || glError != GL_NO_ERROR) {
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        assignGlFailure(error, "OpenGL gfx program creation failed", glError, "program", program);
        program = 0;
        return false;
    }
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx program link submission failed", glError, "program", program);
        return false;
    }
    GLint linked = GL_FALSE;
    gl.getProgramIv(program, kLinkStatus, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        gl.getProgramIv(program, kInfoLogLength, &length);
        std::array<char, henia::detail::FixedError::kCapacity> log{};
        const GLsizei logCapacity = static_cast<GLsizei>(log.size() - 1U);
        GLsizei written = 0;
        gl.getProgramInfoLog(program, std::min(length, logCapacity), &written, log.data());
        error.assign(log.data(), static_cast<std::size_t>(std::max(written, 0)));
        return false;
    }
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx program link-status query failed", glError, "program", program);
        return false;
    }
    viewProjectionLocation = gl.getUniformLocation(program, "viewProjection");
    viewportLocation = gl.getUniformLocation(program, "viewportSize");
    timeLocation = gl.getUniformLocation(program, "timeSeconds");
    depthRangeLocation = gl.getUniformLocation(program, "zeroToOneDepth");
    const GLenum uniformError = consumeOperationErrors();
    if (uniformError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx uniform lookup failed", uniformError, "program", program);
        return false;
    }
    if (viewProjectionLocation < 0 || viewportLocation < 0 || timeLocation < 0 || depthRangeLocation < 0) {
        error = "OpenGL gfx shader uniforms are unavailable";
        return false;
    }
    gl.genVertexArrays(1, &vertexArray);
    if (const GLenum glError = consumeOperationErrors(); vertexArray == 0 || glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx vertex-array creation failed", glError, "object", vertexArray);
        return false;
    }
    for (std::size_t index = 0; index < uploadSlots.size(); ++index) {
        UploadSlot& slot = uploadSlots[index];
        gl.genBuffers(1, &slot.buffer);
        if (const GLenum glError = consumeOperationErrors(); slot.buffer == 0 || glError != GL_NO_ERROR) {
            assignGlFailure(error, "OpenGL gfx instance-buffer creation failed", glError, "uploadSlot", index);
            return false;
        }
    }
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(kVertexArrayBinding, &previousVertexArray);
    glGetIntegerv(kArrayBufferBinding, &previousArrayBuffer);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx initialization-state capture failed", glError, "object", 0);
        return false;
    }
    gl.bindVertexArray(vertexArray);
    const auto restoreInitializationState = [&]() noexcept {
        gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(previousArrayBuffer));
        gl.bindVertexArray(static_cast<GLuint>(previousVertexArray));
    };
    for (std::size_t index = 0; index < uploadSlots.size(); ++index) {
        const UploadSlot& slot = uploadSlots[index];
        gl.bindBuffer(kArrayBuffer, slot.buffer);
        gl.bufferData(
            kArrayBuffer,
            static_cast<GlSize>(instanceBytes),
            nullptr,
            kDynamicDraw);
        if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
            restoreInitializationState();
            assignGlFailure(
                error,
                "OpenGL gfx instance-buffer storage allocation failed",
                glError,
                "uploadSlot",
                index);
            return false;
        }
    }
    gl.bindBuffer(kArrayBuffer, uploadSlots.front().buffer);
    configureAttributes();
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        restoreInitializationState();
        assignGlFailure(error, "OpenGL gfx vertex-layout setup failed", glError, "object", vertexArray);
        return false;
    }
    restoreInitializationState();
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx initialization-state restoration failed", glError, "object", 0);
        return false;
    }
    capacity = requestedCapacity;
    ready = true;
    error.clear();
    return true;
}

bool OpenGlRenderDevice::Implementation::render(
    const InstanceBatch& batch,
    const ViewParameters& view,
    bool depthAttachmentAvailable) noexcept {
    ++statistics.frames;
    statistics.profile.cpuBuildNanoseconds = batch.cpuBuildNanoseconds();
    const std::span<const BoxInstance> boxes = batch.boxes();
    if (!ready) {
        ++statistics.rejectedFrames;
        error = "OpenGL gfx renderer is not initialized";
        return false;
    }
    if (!validateOwnerContext("render")) {
        ++statistics.rejectedFrames;
        return false;
    }
    if (const std::string_view issue = validate(view); !issue.empty()) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error.assign(issue.data(), issue.size());
        return false;
    }
    if (const std::string_view issue = validate(batch.depthState()); !issue.empty()) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error.assign(issue.data(), issue.size());
        return false;
    }
    if (boxes.size() > capacity
        || boxes.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "OpenGL gfx instance count exceeds boxCapacity";
        return false;
    }
    for (const BoxInstance& box : boxes) {
        if (const std::string_view issue = validate(box); !issue.empty()) {
            ++statistics.rejectedFrames;
            ++statistics.invalidInputFrames;
            error.assign(issue.data(), issue.size());
            return false;
        }
    }

    const bool partialRequested = batch.revision() > 1 && batch.dirtyCount() > 0
        && batch.dirtyOffset() <= boxes.size()
        && batch.dirtyCount() <= boxes.size() - batch.dirtyOffset();
    const henia::detail::UploadSelection upload = uploadRing.select(
        batch.identity(),
        batch.revision(),
        partialRequested,
        [this](std::size_t index) noexcept { return pollUploadSlot(index); });
    if (upload.kind == henia::detail::UploadSelectionKind::Exhausted) {
        ++statistics.uploadSlotExhaustions;
        ++statistics.rejectedFrames;
        error = "OpenGL gfx upload ring has no fence-safe slot";
        return false;
    }
    UploadSlot& uploadSlot = uploadSlots[upload.slot];

    discardHostErrors();
    const GlState state = captureState();
    if (consumeOperationErrors() != GL_NO_ERROR) {
        ++statistics.rejectedFrames;
        error = "OpenGL gfx state capture failed";
        return false;
    }
    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, uploadSlot.buffer);
    configureAttributes();
    bool partialUpload = false;
    std::size_t uploadedBytes = 0;
    if (upload.requiresUpload()) {
        const auto uploadStarted = std::chrono::steady_clock::now();
        const bool partial = upload.kind == henia::detail::UploadSelectionKind::Partial;
        const std::size_t offset = partial ? batch.dirtyOffset() : 0;
        const std::size_t count = partial ? batch.dirtyCount() : boxes.size();
        if (count > 0) {
            std::size_t offsetBytes = 0;
            std::size_t countBytes = 0;
            if (!checkedMultiply(offset, sizeof(BoxInstance), offsetBytes)
                || !checkedMultiply(count, sizeof(BoxInstance), countBytes)
                || offsetBytes > static_cast<std::size_t>(std::numeric_limits<GlIntPtr>::max())
                || countBytes > static_cast<std::size_t>(std::numeric_limits<GlSize>::max())) {
                const bool restored = restoreState(state);
                ++statistics.rejectedFrames;
                ++statistics.capacityRejectedFrames;
                if (restored) error = "OpenGL gfx upload byte range exceeds GLintptr/GLsizeiptr";
                return false;
            }
            constexpr GLbitfield flags = kMapWriteBit | kMapUnsynchronizedBit;
            void* destination = gl.mapBufferRange(
                kArrayBuffer,
                static_cast<GlIntPtr>(offsetBytes),
                static_cast<GlSize>(countBytes),
                flags);
            if (destination == nullptr) {
                const bool restored = restoreState(state);
                ++statistics.rejectedFrames;
                if (restored) error = "OpenGL failed to map the gfx instance buffer";
                return false;
            }
            std::memcpy(destination, boxes.data() + offset, countBytes);
            if (gl.unmapBuffer(kArrayBuffer) != GL_TRUE) {
                uploadRing.invalidate(upload.slot);
                const bool restored = restoreState(state);
                ++statistics.rejectedFrames;
                if (restored) error = "OpenGL reported a corrupted gfx instance buffer";
                return false;
            }
        }
        uploadRing.markUploaded(upload.slot, batch.identity(), batch.revision());
        partialUpload = partial;
        static_cast<void>(checkedMultiply(count, sizeof(BoxInstance), uploadedBytes));
        statistics.profile.cpuUploadNanoseconds = elapsedNanoseconds(uploadStarted);
    } else {
        statistics.profile.cpuUploadNanoseconds = 0;
    }
    if (consumeOperationErrors() != GL_NO_ERROR) {
        if (upload.requiresUpload()) {
            uploadRing.invalidate(upload.slot);
        }
        const bool restored = restoreState(state);
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx instance upload failed";
        return false;
    }
    if (upload.requiresUpload()) {
        if (partialUpload) {
            ++statistics.partialInstanceUploads;
        } else {
            ++statistics.fullInstanceUploads;
            if (partialRequested) {
                ++statistics.fullUploadFallbacks;
            }
        }
        statistics.uploadedInstanceBytes += uploadedBytes;
    }

    const auto submitStarted = std::chrono::steady_clock::now();
    gl.useProgram(program);
    gl.uniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, view.viewProjection.values.data());
    gl.uniform2f(viewportLocation, view.viewport.x, view.viewport.y);
    gl.uniform1f(timeLocation, view.timeSeconds);
    gl.uniform1i(depthRangeLocation, view.clipDepthRange == ClipDepthRange::ZeroToOne ? 1 : 0);
    ++statistics.viewUpdates;
    if (consumeOperationErrors() != GL_NO_ERROR) {
        const bool restored = restoreState(state);
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx view update failed";
        return false;
    }

    const GLsizei viewportWidth = static_cast<GLsizei>(std::ceil(view.viewport.x));
    const GLsizei viewportHeight = static_cast<GLsizei>(std::ceil(view.viewport.y));
    glViewport(0, 0, viewportWidth, viewportHeight);
    glScissor(0, 0, viewportWidth, viewportHeight);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(kFramebufferSrgb);
    glDisable(kRasterizerDiscard);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_DITHER);
    glEnable(kMultisample);
    glDisable(kSampleAlphaToCoverage);
    glDisable(kSampleAlphaToOne);
    glDisable(kSampleCoverage);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.colorMaskIndexed(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthRange(0.0, 1.0);
    glEnable(GL_BLEND);
    gl.blendEquationSeparate(kFunctionAdd, kFunctionAdd);
    gl.blendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (consumeOperationErrors() != GL_NO_ERROR) {
        const bool restored = restoreState(state);
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx blend state failed";
        return false;
    }
    DepthState depth = batch.depthState();
    if (depth.enabled && !depthAttachmentAvailable) {
        depth.enabled = false;
        ++statistics.depthFallbacks;
    }
    if (depth.enabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(depth.enabled && depth.writeEnabled ? GL_TRUE : GL_FALSE);
    glDepthFunc(compareFunction(depth.compare));
    if (consumeOperationErrors() != GL_NO_ERROR) {
        const bool restored = restoreState(state);
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx pipeline state failed";
        return false;
    }
    if (!boxes.empty()) {
        gl.drawArraysInstanced(GL_TRIANGLES, 0, 72, static_cast<GLsizei>(boxes.size()));
        ++statistics.drawCalls;
        statistics.submittedInstances += boxes.size();
    }
    const GLenum drawError = consumeOperationErrors();
    const bool fenced = boxes.empty() || fenceUploadSlot(upload.slot);
    if (drawError != GL_NO_ERROR) {
        const bool restored = restoreState(state);
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx instanced draw failed";
        return false;
    }
    if (!fenced) {
        static_cast<void>(restoreState(state));
        ++statistics.rejectedFrames;
        return false;
    }
    statistics.profile.cpuDrawSubmitNanoseconds = elapsedNanoseconds(submitStarted);
    if (!restoreState(state)) {
        ++statistics.rejectedFrames;
        return false;
    }
    error.clear();
    return true;
}

henia::detail::UploadFenceStatus OpenGlRenderDevice::Implementation::pollUploadSlot(
    std::size_t index) noexcept {
    UploadSlot& slot = uploadSlots[index];
    if (slot.fence == nullptr) {
        return henia::detail::UploadFenceStatus::Signaled;
    }
    const GLenum result = gl.clientWaitSync(slot.fence, kSyncFlushCommandsBit, 0);
    if (result == kAlreadySignaled || result == kConditionSatisfied) {
        gl.deleteSync(slot.fence);
        slot.fence = nullptr;
        return henia::detail::UploadFenceStatus::Signaled;
    }
    if (result == kTimeoutExpired) {
        return henia::detail::UploadFenceStatus::Busy;
    }
    gl.deleteSync(slot.fence);
    slot.fence = nullptr;
    ++statistics.uploadFenceFailures;
    return henia::detail::UploadFenceStatus::Failed;
}

bool OpenGlRenderDevice::Implementation::fenceUploadSlot(std::size_t index) noexcept {
    UploadSlot& slot = uploadSlots[index];
    GlSync fence = gl.fenceSync(kSyncGpuCommandsComplete, 0);
    if (fence == nullptr) {
        if (slot.fence != nullptr) {
            gl.deleteSync(slot.fence);
            slot.fence = nullptr;
        }
        uploadRing.markFenceFailed(index);
        ++statistics.uploadFenceFailures;
        error = "OpenGL failed to fence the submitted gfx upload slot";
        return false;
    }
    if (slot.fence != nullptr) {
        gl.deleteSync(slot.fence);
    }
    slot.fence = fence;
    uploadRing.markSubmitted(index);
    return true;
}

void OpenGlRenderDevice::Implementation::configureAttributes() const noexcept {
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(BoxInstance));
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(32));
    gl.enableVertexAttribArray(3);
    gl.vertexAttribIPointer(3, 1, GL_UNSIGNED_INT, stride, reinterpret_cast<const void*>(48));
    for (GLuint attribute = 0; attribute < 4; ++attribute) {
        gl.vertexAttribDivisor(attribute, 1);
    }
}

GlState OpenGlRenderDevice::Implementation::captureState() const noexcept {
    GlState state{};
    glGetIntegerv(kCurrentProgram, &state.program);
    glGetIntegerv(kVertexArrayBinding, &state.vertexArray);
    glGetIntegerv(kArrayBufferBinding, &state.arrayBuffer);
    glGetIntegerv(kBlendSourceRgb, &state.sourceRgb);
    glGetIntegerv(kBlendDestinationRgb, &state.destinationRgb);
    glGetIntegerv(kBlendSourceAlpha, &state.sourceAlpha);
    glGetIntegerv(kBlendDestinationAlpha, &state.destinationAlpha);
    glGetIntegerv(kBlendEquationRgb, &state.equationRgb);
    glGetIntegerv(kBlendEquationAlpha, &state.equationAlpha);
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode.data());
    glGetIntegerv(GL_VIEWPORT, state.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor.data());
    gl.getBooleanIndexed(GL_COLOR_WRITEMASK, 0, state.colorMask.data());
    glGetDoublev(GL_DEPTH_RANGE, state.depthRange.data());
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWrite);
    state.blend = glIsEnabled(GL_BLEND);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
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

bool OpenGlRenderDevice::Implementation::restoreState(const GlState& state) noexcept {
    const GLuint savedProgram = static_cast<GLuint>(state.program);
    gl.useProgram(savedProgram == 0 || gl.isProgram(savedProgram) == GL_TRUE ? savedProgram : 0);
    gl.bindVertexArray(static_cast<GLuint>(state.vertexArray));
    gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(state.arrayBuffer));
    gl.blendFuncSeparate(
        static_cast<GLenum>(state.sourceRgb), static_cast<GLenum>(state.destinationRgb),
        static_cast<GLenum>(state.sourceAlpha), static_cast<GLenum>(state.destinationAlpha));
    gl.blendEquationSeparate(
        static_cast<GLenum>(state.equationRgb),
        static_cast<GLenum>(state.equationAlpha));
    glDepthFunc(static_cast<GLenum>(state.depthFunction));
    glDepthMask(state.depthWrite);
    glPolygonMode(GL_FRONT, static_cast<GLenum>(state.polygonMode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(state.polygonMode[1]));
    gl.colorMaskIndexed(
        0, state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
    glDepthRange(state.depthRange[0], state.depthRange[1]);
    state.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    state.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    state.cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    state.scissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    state.stencilTest ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
    state.framebufferSrgb ? glEnable(kFramebufferSrgb) : glDisable(kFramebufferSrgb);
    state.rasterizerDiscard ? glEnable(kRasterizerDiscard) : glDisable(kRasterizerDiscard);
    state.colorLogicOp ? glEnable(GL_COLOR_LOGIC_OP) : glDisable(GL_COLOR_LOGIC_OP);
    state.dither ? glEnable(GL_DITHER) : glDisable(GL_DITHER);
    state.multisample ? glEnable(kMultisample) : glDisable(kMultisample);
    state.sampleAlphaToCoverage ? glEnable(kSampleAlphaToCoverage) : glDisable(kSampleAlphaToCoverage);
    state.sampleAlphaToOne ? glEnable(kSampleAlphaToOne) : glDisable(kSampleAlphaToOne);
    state.sampleCoverage ? glEnable(kSampleCoverage) : glDisable(kSampleCoverage);
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]);
    if (consumeOperationErrors() != GL_NO_ERROR) {
        ++statistics.stateRestoreFailures;
        error = "OpenGL gfx state restoration failed";
        return false;
    }
    return true;
}

bool OpenGlRenderDevice::Implementation::shutdown() noexcept {
    const bool hadOwner = ownerContext != nullptr;
    if (hadOwner && wglGetCurrentContext() != ownerContext) {
        ++statistics.wrongContextCalls;
        error = "OpenGL gfx shutdown requires the initialize() context; resources were preserved";
        return false;
    }
    if (hadOwner) {
        discardHostErrors();
        for (UploadSlot& slot : uploadSlots) {
            if (slot.fence != nullptr && gl.deleteSync != nullptr) {
                gl.deleteSync(slot.fence);
                slot.fence = nullptr;
            }
            if (slot.buffer != 0 && gl.deleteBuffers != nullptr) {
                gl.deleteBuffers(1, &slot.buffer);
            }
        }
        if (vertexArray != 0 && gl.deleteVertexArrays != nullptr) gl.deleteVertexArrays(1, &vertexArray);
        if (program != 0 && gl.deleteProgram != nullptr) gl.deleteProgram(program);
    }
    program = 0;
    vertexArray = 0;
    uploadSlots.clear();
    uploadRing.clear();
    capacity = 0;
    ownerContext = nullptr;
    ready = false;
    if (hadOwner && consumeOperationErrors() != GL_NO_ERROR) {
        error = "OpenGL gfx resource destruction generated an error";
        return false;
    }
    error.clear();
    return true;
}

bool OpenGlRenderDevice::Implementation::validateOwnerContext(const char* operation) noexcept {
    static_cast<void>(operation);
    if (ownerContext != nullptr && wglGetCurrentContext() == ownerContext) {
        return true;
    }
    ++statistics.wrongContextCalls;
    error = "OpenGL gfx call requires the exact context used by initialize()";
    return false;
}

void OpenGlRenderDevice::Implementation::discardHostErrors() noexcept {
    std::uint64_t discarded = 0;
    while (discarded < 64 && glGetError() != GL_NO_ERROR) {
        ++discarded;
    }
    statistics.ignoredHostErrors += discarded;
}

OpenGlRenderDevice::OpenGlRenderDevice() : mImplementation(std::make_unique<Implementation>()) {}
OpenGlRenderDevice::~OpenGlRenderDevice() { static_cast<void>(shutdown()); }
bool OpenGlRenderDevice::initialize(
    std::size_t capacityValue,
    std::size_t uploadSlotCountValue) noexcept {
    try {
        const bool initialized = mImplementation->initialize(capacityValue, uploadSlotCountValue);
        if (!initialized) {
            ++mImplementation->statistics.initializationFailures;
            const henia::detail::FixedError diagnostic = mImplementation->error;
            static_cast<void>(mImplementation->shutdown());
            mImplementation->error = diagnostic;
        }
        return initialized;
    } catch (...) {
        ++mImplementation->statistics.initializationFailures;
        static_cast<void>(mImplementation->shutdown());
        mImplementation->error = "OpenGL gfx initialization exhausted upload-ring storage";
        return false;
    }
}
bool OpenGlRenderDevice::render(
    const InstanceBatch& batch,
    const ViewParameters& view,
    bool depthAttachmentAvailable) noexcept {
    return mImplementation->render(batch, view, depthAttachmentAvailable);
}
void OpenGlRenderDevice::reportGpuTime(std::uint64_t nanoseconds) noexcept {
    mImplementation->statistics.profile.gpuNanoseconds = nanoseconds;
    mImplementation->statistics.profile.gpuTimingAvailable = true;
}
bool OpenGlRenderDevice::shutdown() noexcept {
    return mImplementation == nullptr || mImplementation->shutdown();
}
bool OpenGlRenderDevice::initialized() const noexcept { return mImplementation->ready; }
std::size_t OpenGlRenderDevice::boxCapacity() const noexcept { return mImplementation->capacity; }
std::size_t OpenGlRenderDevice::uploadSlotCount() const noexcept {
    return mImplementation->uploadSlots.size();
}
OpenGlGfxStatistics OpenGlRenderDevice::statistics() const noexcept { return mImplementation->statistics; }
std::string_view OpenGlRenderDevice::lastError() const noexcept { return mImplementation->error.view(); }

} // namespace henia::gfx
