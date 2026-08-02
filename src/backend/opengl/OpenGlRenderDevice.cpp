#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"

#include "../FixedError.h"

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

namespace henia::gfx {
namespace {

using GlChar = char;
using GlSize = std::ptrdiff_t;
using GlIntPtr = std::ptrdiff_t;

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
constexpr GLenum kMapInvalidateRangeBit = 0x0004;
constexpr GLenum kMapInvalidateBufferBit = 0x0008;
constexpr GLenum kMapUnsynchronizedBit = 0x0020;
constexpr GLenum kBlendSourceRgb = 0x80C9;
constexpr GLenum kBlendDestinationRgb = 0x80C8;
constexpr GLenum kBlendSourceAlpha = 0x80CB;
constexpr GLenum kBlendDestinationAlpha = 0x80CA;

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
        && load(gl.enableVertexAttribArray, "glEnableVertexAttribArray")
        && load(gl.vertexAttribPointer, "glVertexAttribPointer")
        && load(gl.vertexAttribIPointer, "glVertexAttribIPointer")
        && load(gl.vertexAttribDivisor, "glVertexAttribDivisor")
        && load(gl.getUniformLocation, "glGetUniformLocation")
        && load(gl.uniformMatrix4fv, "glUniformMatrix4fv") && load(gl.uniform2f, "glUniform2f")
        && load(gl.uniform1f, "glUniform1f") && load(gl.uniform1i, "glUniform1i")
        && load(gl.drawArraysInstanced, "glDrawArraysInstanced")
        && load(gl.blendFuncSeparate, "glBlendFuncSeparate");
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
    if (shader == 0) {
        error = "OpenGL failed to create a gfx shader";
        return 0;
    }
    gl.shaderSource(shader, 1, &source, nullptr);
    gl.compileShader(shader);
    GLint compiled = GL_FALSE;
    gl.getShaderIv(shader, kCompileStatus, &compiled);
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
    GLint depthFunction = 0;
    GLboolean depthWrite = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
};

[[nodiscard]] std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

} // namespace

struct OpenGlRenderDevice::Implementation final {
    GlFunctions gl{};
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint instanceBuffer = 0;
    GLint viewProjectionLocation = -1;
    GLint viewportLocation = -1;
    GLint timeLocation = -1;
    GLint depthRangeLocation = -1;
    std::size_t capacity = 0;
    std::uint64_t uploadedIdentity = 0;
    std::uint64_t uploadedRevision = 0;
    OpenGlGfxStatistics statistics{};
    henia::detail::FixedError error;
    bool ready = false;

    [[nodiscard]] bool initialize(std::size_t requestedCapacity) noexcept;
    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] GlState captureState() const noexcept;
    void restoreState(const GlState& state) const noexcept;
};

bool OpenGlRenderDevice::Implementation::initialize(std::size_t requestedCapacity) noexcept {
    if (ready) return true;
    if (wglGetCurrentContext() == nullptr || requestedCapacity == 0
        || requestedCapacity > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        error = "OpenGL 3.3 context and a non-zero box capacity are required";
        return false;
    }
    if (!loadFunctions(gl)) {
        error = "OpenGL 3.3 gfx entry points are unavailable";
        return false;
    }
    const GLuint vertexShader = compileShader(gl, kVertexShader, kVertexShaderSource, error);
    if (vertexShader == 0) return false;
    const GLuint fragmentShader = compileShader(gl, kFragmentShader, kFragmentShaderSource, error);
    if (fragmentShader == 0) {
        gl.deleteShader(vertexShader);
        return false;
    }
    program = gl.createProgram();
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
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
        shutdown();
        return false;
    }
    viewProjectionLocation = gl.getUniformLocation(program, "viewProjection");
    viewportLocation = gl.getUniformLocation(program, "viewportSize");
    timeLocation = gl.getUniformLocation(program, "timeSeconds");
    depthRangeLocation = gl.getUniformLocation(program, "zeroToOneDepth");
    if (viewProjectionLocation < 0 || viewportLocation < 0 || timeLocation < 0 || depthRangeLocation < 0) {
        error = "OpenGL gfx shader uniforms are unavailable";
        shutdown();
        return false;
    }
    gl.genVertexArrays(1, &vertexArray);
    gl.genBuffers(1, &instanceBuffer);
    if (vertexArray == 0 || instanceBuffer == 0) {
        error = "OpenGL failed to allocate the gfx instance buffer";
        shutdown();
        return false;
    }
    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, instanceBuffer);
    gl.bufferData(kArrayBuffer, static_cast<GlSize>(requestedCapacity * sizeof(BoxInstance)), nullptr, kDynamicDraw);
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(BoxInstance));
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(32));
    gl.enableVertexAttribArray(3);
    gl.vertexAttribIPointer(3, 1, GL_UNSIGNED_INT, stride, reinterpret_cast<const void*>(48));
    for (GLuint attribute = 0; attribute < 4; ++attribute) gl.vertexAttribDivisor(attribute, 1);
    gl.bindBuffer(kArrayBuffer, 0);
    gl.bindVertexArray(0);
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
    if (!ready || wglGetCurrentContext() == nullptr || view.viewport.x <= 0.0F || view.viewport.y <= 0.0F
        || boxes.size() > capacity || boxes.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        ++statistics.rejectedFrames;
        error = "OpenGL gfx render rejected an invalid context, view, or instance count";
        return false;
    }

    const GlState state = captureState();
    if (glGetError() != GL_NO_ERROR) {
        ++statistics.rejectedFrames;
        error = "OpenGL gfx state capture failed";
        return false;
    }
    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, instanceBuffer);
    if (uploadedIdentity != batch.identity() || uploadedRevision != batch.revision()) {
        const auto uploadStarted = std::chrono::steady_clock::now();
        const bool partial = uploadedIdentity == batch.identity()
            && uploadedRevision + 1 == batch.revision()
            && batch.dirtyCount() > 0
            && batch.dirtyOffset() + batch.dirtyCount() <= boxes.size();
        const std::size_t offset = partial ? batch.dirtyOffset() : 0;
        const std::size_t count = partial ? batch.dirtyCount() : boxes.size();
        if (count > 0) {
            const GLbitfield flags = kMapWriteBit | kMapUnsynchronizedBit
                | (partial ? kMapInvalidateRangeBit : kMapInvalidateBufferBit);
            void* destination = gl.mapBufferRange(
                kArrayBuffer,
                static_cast<GlIntPtr>(offset * sizeof(BoxInstance)),
                static_cast<GlSize>(count * sizeof(BoxInstance)),
                flags);
            if (destination == nullptr) {
                restoreState(state);
                ++statistics.rejectedFrames;
                error = "OpenGL failed to map the gfx instance buffer";
                return false;
            }
            std::memcpy(destination, boxes.data() + offset, count * sizeof(BoxInstance));
            if (gl.unmapBuffer(kArrayBuffer) != GL_TRUE) {
                restoreState(state);
                ++statistics.rejectedFrames;
                error = "OpenGL reported a corrupted gfx instance buffer";
                return false;
            }
        }
        uploadedIdentity = batch.identity();
        uploadedRevision = batch.revision();
        if (partial) ++statistics.partialInstanceUploads;
        else ++statistics.fullInstanceUploads;
        statistics.profile.cpuUploadNanoseconds = elapsedNanoseconds(uploadStarted);
    } else {
        statistics.profile.cpuUploadNanoseconds = 0;
    }
    if (glGetError() != GL_NO_ERROR) {
        restoreState(state);
        ++statistics.rejectedFrames;
        error = "OpenGL gfx instance upload failed";
        return false;
    }

    const auto submitStarted = std::chrono::steady_clock::now();
    gl.useProgram(program);
    gl.uniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, view.viewProjection.values.data());
    gl.uniform2f(viewportLocation, view.viewport.x, view.viewport.y);
    gl.uniform1f(timeLocation, view.timeSeconds);
    gl.uniform1i(depthRangeLocation, view.clipDepthRange == ClipDepthRange::ZeroToOne ? 1 : 0);
    ++statistics.viewUpdates;
    if (glGetError() != GL_NO_ERROR) {
        restoreState(state);
        ++statistics.rejectedFrames;
        error = "OpenGL gfx view update failed";
        return false;
    }

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    gl.blendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (glGetError() != GL_NO_ERROR) {
        restoreState(state);
        ++statistics.rejectedFrames;
        error = "OpenGL gfx blend state failed";
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
    if (glGetError() != GL_NO_ERROR) {
        restoreState(state);
        ++statistics.rejectedFrames;
        error = "OpenGL gfx pipeline state failed";
        return false;
    }
    if (!boxes.empty()) {
        gl.drawArraysInstanced(GL_TRIANGLES, 0, 72, static_cast<GLsizei>(boxes.size()));
        ++statistics.drawCalls;
        statistics.submittedInstances += boxes.size();
    }
    if (glGetError() != GL_NO_ERROR) {
        restoreState(state);
        ++statistics.rejectedFrames;
        error = "OpenGL gfx instanced draw failed";
        return false;
    }
    statistics.profile.cpuDrawSubmitNanoseconds = elapsedNanoseconds(submitStarted);
    restoreState(state);
    if (glGetError() != GL_NO_ERROR) {
        ++statistics.rejectedFrames;
        error = "OpenGL gfx state restore failed";
        return false;
    }
    error.clear();
    return true;
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
    glGetIntegerv(GL_DEPTH_FUNC, &state.depthFunction);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWrite);
    state.blend = glIsEnabled(GL_BLEND);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    return state;
}

void OpenGlRenderDevice::Implementation::restoreState(const GlState& state) const noexcept {
    gl.useProgram(static_cast<GLuint>(state.program));
    gl.bindVertexArray(static_cast<GLuint>(state.vertexArray));
    gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(state.arrayBuffer));
    gl.blendFuncSeparate(
        static_cast<GLenum>(state.sourceRgb), static_cast<GLenum>(state.destinationRgb),
        static_cast<GLenum>(state.sourceAlpha), static_cast<GLenum>(state.destinationAlpha));
    glDepthFunc(static_cast<GLenum>(state.depthFunction));
    glDepthMask(state.depthWrite);
    state.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    state.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    state.cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void OpenGlRenderDevice::Implementation::shutdown() noexcept {
    if (wglGetCurrentContext() != nullptr) {
        if (instanceBuffer != 0 && gl.deleteBuffers != nullptr) gl.deleteBuffers(1, &instanceBuffer);
        if (vertexArray != 0 && gl.deleteVertexArrays != nullptr) gl.deleteVertexArrays(1, &vertexArray);
        if (program != 0 && gl.deleteProgram != nullptr) gl.deleteProgram(program);
    }
    program = 0;
    vertexArray = 0;
    instanceBuffer = 0;
    capacity = 0;
    uploadedIdentity = 0;
    uploadedRevision = 0;
    ready = false;
}

OpenGlRenderDevice::OpenGlRenderDevice() : mImplementation(std::make_unique<Implementation>()) {}
OpenGlRenderDevice::~OpenGlRenderDevice() { shutdown(); }
bool OpenGlRenderDevice::initialize(std::size_t capacityValue) noexcept { return mImplementation->initialize(capacityValue); }
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
void OpenGlRenderDevice::shutdown() noexcept { if (mImplementation != nullptr) mImplementation->shutdown(); }
bool OpenGlRenderDevice::initialized() const noexcept { return mImplementation->ready; }
std::size_t OpenGlRenderDevice::boxCapacity() const noexcept { return mImplementation->capacity; }
OpenGlGfxStatistics OpenGlRenderDevice::statistics() const noexcept { return mImplementation->statistics; }
std::string_view OpenGlRenderDevice::lastError() const noexcept { return mImplementation->error.view(); }

} // namespace henia::gfx
