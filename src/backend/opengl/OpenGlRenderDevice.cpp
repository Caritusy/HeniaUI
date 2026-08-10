#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"
#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"

#include "../FixedError.h"
#include "../ProfileTimeline.h"
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
constexpr GLenum kElementArrayBuffer = 0x8893;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kDynamicDraw = 0x88E8;
constexpr GLenum kStaticDraw = 0x88E4;
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

constexpr std::size_t kFaceIndexCount = 36;
constexpr std::size_t kEdgeIndexCount = 72;
constexpr auto kBoxIndices = [] {
    std::array<std::uint16_t, kFaceIndexCount + kEdgeIndexCount> result{};
    constexpr std::array<std::uint16_t, 6> pattern{0, 1, 2, 2, 3, 0};
    for (std::uint16_t face = 0; face < 6; ++face) {
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            result[static_cast<std::size_t>(face) * pattern.size() + index] =
                static_cast<std::uint16_t>(48U + face * 4U + pattern[index]);
        }
    }
    for (std::uint16_t edge = 0; edge < 12; ++edge) {
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            result[kFaceIndexCount + static_cast<std::size_t>(edge) * pattern.size() + index] =
                static_cast<std::uint16_t>(edge * 4U + pattern[index]);
        }
    }
    return result;
}();

struct PrimitiveSelection final {
    std::size_t firstIndex{};
    std::size_t indexCount{};
    std::size_t vertexCount{};
};

[[nodiscard]] PrimitiveSelection selectPrimitives(
    bool anyFill,
    bool anyOutline) noexcept {
    if (anyFill && anyOutline) {
        return {0, kBoxIndices.size(), 72};
    }
    if (anyFill) {
        return {0, kFaceIndexCount, 24};
    }
    return {kFaceIndexCount, anyOutline ? kEdgeIndexCount : 0U, anyOutline ? 48U : 0U};
}

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
using DrawElementsInstancedFn = void(APIENTRYP)(GLenum, GLsizei, GLenum, const void*, GLsizei);
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
    DrawElementsInstancedFn drawElementsInstanced = nullptr;
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
        && load(gl.drawElementsInstanced, "glDrawElementsInstanced")
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
layout(location = 4) in uvec3 instanceReserved;

uniform mat4 viewProjection;
uniform vec2 viewportSize;
uniform float motionScale;
uniform int zeroToOneDepth;

flat out vec4 lineColor;
noperspective out float edgeAcross;
noperspective out float edgeAlong;
flat out float segmentLength;
flat out float halfWidth;
flat out float hueOffset;
flat out uint effects;
flat out float validEdge;
flat out uint primitiveKind;

const ivec2 edges[12] = ivec2[12](
    ivec2(0, 1), ivec2(2, 3), ivec2(0, 2), ivec2(1, 3),
    ivec2(4, 5), ivec2(6, 7), ivec2(4, 6), ivec2(5, 7),
    ivec2(0, 4), ivec2(1, 5), ivec2(2, 6), ivec2(3, 7));
const uint edgeFaces[12] = uint[12](
    17u, 33u, 5u, 9u,
    18u, 34u, 6u, 10u,
    20u, 24u, 36u, 40u);
const vec2 quad[4] = vec2[4](
    vec2(0.0, -1.0), vec2(1.0, -1.0),
    vec2(1.0, 1.0), vec2(0.0, 1.0));
const ivec4 faces[6] = ivec4[6](
    ivec4(0, 2, 3, 1), ivec4(4, 5, 7, 6),
    ivec4(0, 4, 6, 2), ivec4(1, 3, 7, 5),
    ivec4(0, 1, 5, 4), ivec4(2, 6, 7, 3));

vec3 corner(int code) {
    return vec3(float(code & 1), float((code >> 1) & 1), float((code >> 2) & 1));
}

float unpackMotionComponent(uint value) {
    uint bits = ((value >> 20u) << 31u)
        | (((value >> 12u) & 0xffu) << 23u)
        | ((value & 0xfffu) << 11u);
    return uintBitsToFloat(bits);
}

vec3 motionDelta() {
    if ((instanceEffects & 4u) == 0u) {
        return vec3(
            uintBitsToFloat(instanceReserved.x),
            uintBitsToFloat(instanceReserved.y),
            uintBitsToFloat(instanceReserved.z));
    }
    uint low = instanceReserved.y;
    uint high = instanceReserved.z;
    return vec3(
        unpackMotionComponent(low & 0x1fffffu),
        unpackMotionComponent((low >> 21u) | ((high & 0x3ffu) << 11u)),
        unpackMotionComponent((high >> 10u) & 0x1fffffu));
}

bool finiteClip(vec4 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

float planeDistance(vec4 point, int plane, bool zeroToOne) {
    if (plane == 0) return point.w + point.x;
    if (plane == 1) return point.w - point.x;
    if (plane == 2) return point.w + point.y;
    if (plane == 3) return point.w - point.y;
    if (plane == 4) return zeroToOne ? point.z : point.w + point.z;
    if (plane == 5) return point.w - point.z;
    return point.w - 0.0001;
}

bool clipAgainstPlane(
    inout vec4 startClip,
    inout vec4 finishClip,
    int plane,
    bool zeroToOne) {
    float startDistance = planeDistance(startClip, plane, zeroToOne);
    float finishDistance = planeDistance(finishClip, plane, zeroToOne);
    if (isnan(startDistance) || isinf(startDistance)
        || isnan(finishDistance) || isinf(finishDistance)) {
        return false;
    }
    bool startInside = startDistance >= 0.0;
    bool finishInside = finishDistance >= 0.0;
    if (!startInside && !finishInside) return false;
    if (startInside && finishInside) return true;

    float denominator = startDistance - finishDistance;
    if (abs(denominator) <= 1e-20 || isnan(denominator) || isinf(denominator)) {
        return false;
    }
    float amount = clamp(startDistance / denominator, 0.0, 1.0);
    if (isnan(amount) || isinf(amount)) return false;
    vec4 clipped = mix(startClip, finishClip, amount);
    if (!finiteClip(clipped)) return false;
    if (startInside) {
        finishClip = clipped;
    } else {
        startClip = clipped;
    }
    return true;
}

bool clipSegment(inout vec4 startClip, inout vec4 finishClip, bool zeroToOne) {
    if (!finiteClip(startClip) || !finiteClip(finishClip)
        || !clipAgainstPlane(startClip, finishClip, 6, zeroToOne)) {
        return false;
    }
    for (int plane = 0; plane < 6; ++plane) {
        if (!clipAgainstPlane(startClip, finishClip, plane, zeroToOne)) {
            return false;
        }
    }
    return finiteClip(startClip) && finiteClip(finishClip)
        && startClip.w >= 0.0001 && finishClip.w >= 0.0001;
}

void main() {
    vec3 motionOffset = (instanceEffects & 2u) != 0u
        ? motionDelta() * motionScale
        : vec3(0.0);
    vec3 minimumValue = instanceMinimumAndWidth.xyz + motionOffset;
    vec3 maximumValue = instanceMaximumAndHue.xyz + motionOffset;
    lineColor = instanceColor;
    hueOffset = instanceMaximumAndHue.w;
    effects = instanceEffects;
    primitiveKind = gl_VertexID >= 48 ? 1u : 0u;
    edgeAcross = 0.0;
    edgeAlong = 0.0;
    segmentLength = 0.0;
    halfWidth = max(instanceMinimumAndWidth.w, 0.5) * 0.5;
    validEdge = 0.0;
    bool zeroToOne = zeroToOneDepth != 0;
    uint faceMask = (instanceEffects & 64u) != 0u
        ? (instanceEffects >> 16u) & 63u
        : 63u;

    if (primitiveKind != 0u) {
        int faceVertex = gl_VertexID - 48;
        int face = faceVertex / 4;
        int localVertex = faceVertex % 4;
        int cornerCode = faces[face][localVertex];
        vec3 position = mix(minimumValue, maximumValue, corner(cornerCode));
        vec4 clipPosition = viewProjection * vec4(position, 1.0);
        validEdge = (instanceEffects & 16u) != 0u
            && (faceMask & (1u << uint(face))) != 0u
            && finiteClip(clipPosition)
            ? 1.0 : 0.0;
        if (validEdge < 0.5) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        if (zeroToOne) {
            clipPosition.z = clipPosition.z * 2.0 - clipPosition.w;
        }
        gl_Position = clipPosition;
        return;
    }

    int edgeIndex = gl_VertexID / 4;
    vec2 vertex = quad[gl_VertexID % 4];
    vec3 start = mix(minimumValue, maximumValue, corner(edges[edgeIndex].x));
    vec3 finish = mix(minimumValue, maximumValue, corner(edges[edgeIndex].y));
    vec4 startClip = viewProjection * vec4(start, 1.0);
    vec4 finishClip = viewProjection * vec4(finish, 1.0);
    validEdge = (instanceEffects & 32u) == 0u
        && (faceMask & edgeFaces[edgeIndex]) != 0u
        && clipSegment(startClip, finishClip, zeroToOne) ? 1.0 : 0.0;

    float fringe = 1.25;
    float expandedWidth = halfWidth + fringe;
    if (validEdge < 0.5) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec2 startNdc = startClip.xy / startClip.w;
    vec2 finishNdc = finishClip.xy / finishClip.w;
    vec2 directionPixels = (finishNdc - startNdc) * viewportSize * 0.5;
    segmentLength = length(directionPixels);
    vec2 direction = segmentLength > 0.0001
        ? directionPixels / segmentLength
        : vec2(1.0, 0.0);
    vec2 normalPixels = vec2(-direction.y, direction.x);
    bool finishVertex = vertex.x > 0.5;
    edgeAlong = finishVertex ? segmentLength + fringe : -fringe;
    edgeAcross = vertex.y * expandedWidth;
    float capOffset = finishVertex ? fringe : -fringe;
    vec2 offsetPixels = direction * capOffset
        + normalPixels * expandedWidth * vertex.y;
    vec2 offsetNdc = offsetPixels * 2.0 / viewportSize;
    vec4 endpoint = finishVertex ? finishClip : startClip;
    endpoint.xy += offsetNdc * endpoint.w;
    if (zeroToOne) {
        endpoint.z = endpoint.z * 2.0 - endpoint.w;
    }
    gl_Position = endpoint;
}
)glsl";

constexpr const char* kFragmentShaderSource = R"glsl(
#version 330 core
flat in vec4 lineColor;
noperspective in float edgeAcross;
noperspective in float edgeAlong;
flat in float segmentLength;
flat in float halfWidth;
flat in float hueOffset;
flat in uint effects;
flat in float validEdge;
flat in uint primitiveKind;
uniform float timeSeconds;
out vec4 outputColor;

vec3 hue(float value) {
    vec3 shifted = abs(fract(value + vec3(0.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return clamp(shifted - 1.0, 0.0, 1.0);
}

void main() {
    if (validEdge < 0.5) discard;
    vec4 color = lineColor;
    if ((effects & 1u) != 0u) {
        color.rgb *= hue(fract(timeSeconds * 0.08 + hueOffset));
    }
    if (primitiveKind != 0u) {
        color.a *= float((effects >> 8u) & 255u) / 255.0;
        if (color.a <= 0.001) discard;
        outputColor = vec4(color.rgb * color.a, color.a);
        return;
    }
    vec2 centered = vec2(edgeAlong - segmentLength * 0.5, edgeAcross);
    vec2 outside = abs(centered) - vec2(segmentLength * 0.5, halfWidth);
    float distanceToEdge = length(max(outside, vec2(0.0)))
        + min(max(outside.x, outside.y), 0.0);
    float antiAlias = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
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
    GLuint indexBuffer = 0;
    GLint viewProjectionLocation = -1;
    GLint viewportLocation = -1;
    GLint timeLocation = -1;
    GLint motionScaleLocation = -1;
    GLint depthRangeLocation = -1;
    std::size_t capacity = 0;
    std::vector<UploadSlot> uploadSlots;
    henia::detail::OpenGlUploadRing uploadRing;
    VisibilityList visibilityList;
    henia::detail::ProfileTimeline profileTimeline;
    OpenGlGfxStatistics statistics{};
    henia::detail::FixedError error;
    HGLRC ownerContext = nullptr;
    OpenGlStatePolicy statePolicy = OpenGlStatePolicy::Preserve;
    bool ready = false;

    [[nodiscard]] bool initialize(
        std::size_t requestedCapacity,
        std::size_t requestedUploadSlots,
        OpenGlStatePolicy requestedStatePolicy);
    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable,
        VisibilityOptions visibility) noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    void abandon() noexcept;
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
    std::size_t requestedUploadSlots,
    OpenGlStatePolicy requestedStatePolicy) {
    if (ready) {
        if (!validateOwnerContext("initialize")) {
            ++statistics.lifecycleRejections;
            return false;
        }
        if (requestedCapacity != capacity || requestedUploadSlots != uploadSlots.size()
            || requestedStatePolicy != statePolicy) {
            ++statistics.lifecycleRejections;
            error = "OpenGL gfx renderer is already initialized with a different configuration";
            return false;
        }
        error.clear();
        return true;
    }
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
    statePolicy = requestedStatePolicy;
    statistics = {};
    profileTimeline.reset();
    uploadSlots.resize(requestedUploadSlots);
    uploadRing.reset(requestedUploadSlots);
    if (!visibilityList.reserve(requestedCapacity)) {
        error.assign(visibilityList.lastError().data(), visibilityList.lastError().size());
        return false;
    }
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
    motionScaleLocation = gl.getUniformLocation(program, "motionScale");
    depthRangeLocation = gl.getUniformLocation(program, "zeroToOneDepth");
    const GLenum uniformError = consumeOperationErrors();
    if (uniformError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx uniform lookup failed", uniformError, "program", program);
        return false;
    }
    if (viewProjectionLocation < 0 || viewportLocation < 0 || timeLocation < 0
        || motionScaleLocation < 0 || depthRangeLocation < 0) {
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
    const bool preserveState = statePolicy == OpenGlStatePolicy::Preserve;
    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    if (preserveState) {
        glGetIntegerv(kVertexArrayBinding, &previousVertexArray);
        glGetIntegerv(kArrayBufferBinding, &previousArrayBuffer);
        if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
            assignGlFailure(error, "OpenGL gfx initialization-state capture failed", glError, "object", 0);
            return false;
        }
    }
    gl.genBuffers(1, &indexBuffer);
    if (const GLenum glError = consumeOperationErrors(); indexBuffer == 0 || glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL gfx index-buffer creation failed", glError, "object", indexBuffer);
        return false;
    }
    gl.bindVertexArray(vertexArray);
    const auto restoreInitializationState = [&]() noexcept {
        if (!preserveState) return;
        gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(previousArrayBuffer));
        gl.bindVertexArray(static_cast<GLuint>(previousVertexArray));
    };
    gl.bindBuffer(kElementArrayBuffer, indexBuffer);
    gl.bufferData(
        kElementArrayBuffer,
        static_cast<GlSize>(sizeof(kBoxIndices)),
        kBoxIndices.data(),
        kStaticDraw);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        restoreInitializationState();
        assignGlFailure(error, "OpenGL gfx index-buffer storage allocation failed", glError, "object", indexBuffer);
        return false;
    }
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
    if (preserveState) {
        if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
            assignGlFailure(error, "OpenGL gfx initialization-state restoration failed", glError, "object", 0);
            return false;
        }
    }
    capacity = requestedCapacity;
    ready = true;
    error.clear();
    return true;
}

bool OpenGlRenderDevice::Implementation::render(
    const InstanceBatch& batch,
    const ViewParameters& view,
    bool depthAttachmentAvailable,
    VisibilityOptions visibility) noexcept {
    const std::uint64_t frameAttemptId = ++statistics.frameAttempts;
    const BoxInstanceView sourceBoxes = batch.boxes();
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
    if (const std::string_view issue = validate(visibility); !issue.empty()) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error.assign(issue.data(), issue.size());
        return false;
    }
    if (sourceBoxes.size() > capacity
        || sourceBoxes.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "OpenGL gfx instance count exceeds boxCapacity";
        return false;
    }
    const bool cpuCulling = usesCpuVisibility(visibility, sourceBoxes.size());
    if (!cpuCulling) {
        for (std::size_t pageIndex = 0; pageIndex < batch.boxPageCount(); ++pageIndex) {
            for (const BoxInstance& box : batch.boxPage(pageIndex)) {
                if (const std::string_view issue = validate(box); !issue.empty()) {
                    ++statistics.rejectedFrames;
                    ++statistics.invalidInputFrames;
                    error.assign(issue.data(), issue.size());
                    return false;
                }
            }
        }
    }

    std::span<const BoxInstance> culledBoxes;
    VisibilityStatistics visibilityStatistics{};
    if (cpuCulling) {
        if (!visibilityList.update(batch, view, visibility)) {
            ++statistics.rejectedFrames;
            ++statistics.invalidInputFrames;
            const std::string_view issue = visibilityList.lastError();
            error.assign(issue.data(), issue.size());
            return false;
        }
        culledBoxes = visibilityList.boxes();
        visibilityStatistics = visibilityList.statistics();
    }
    const std::size_t submittedCount = cpuCulling ? culledBoxes.size() : sourceBoxes.size();
    bool anyFill = false;
    bool anyOutline = false;
    const auto inspectBox = [&](const BoxInstance& box) noexcept {
        anyFill = anyFill || box.fillEnabled();
        anyOutline = anyOutline || box.outlineEnabled();
    };
    if (cpuCulling) {
        for (const BoxInstance& box : culledBoxes) inspectBox(box);
    } else {
        for (const BoxInstance& box : sourceBoxes) inspectBox(box);
    }
    const PrimitiveSelection primitives = selectPrimitives(anyFill, anyOutline);
    const std::uint64_t producerIdentity = cpuCulling
        ? visibilityList.identity() : batch.identity();
    const std::uint64_t producerRevision = cpuCulling
        ? visibilityList.revision() : batch.revision();

    const std::span<const DirtyRange> dirtyRanges = batch.dirtyRanges();
    bool dirtyRangesValid = !dirtyRanges.empty();
    std::size_t previousEnd = 0;
    for (const DirtyRange range : dirtyRanges) {
        const bool valid = range.count > 0 && range.offset >= previousEnd
            && range.offset <= sourceBoxes.size()
            && range.count <= sourceBoxes.size() - range.offset;
        if (!valid) {
            dirtyRangesValid = false;
            break;
        }
        previousEnd = range.offset + range.count;
    }
    const bool partialRequested = !cpuCulling && batch.revision() > 1
        && !batch.requiresFullUpload() && dirtyRangesValid;
    const henia::detail::UploadSelection upload = uploadRing.select(
        producerIdentity,
        producerRevision,
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
    const bool preserveState = statePolicy == OpenGlStatePolicy::Preserve;
    GlState state{};
    if (preserveState) {
        state = captureState();
        if (consumeOperationErrors() != GL_NO_ERROR) {
            ++statistics.rejectedFrames;
            error = "OpenGL gfx state capture failed";
            return false;
        }
    }
    const auto restoreCapturedState = [&]() noexcept {
        return !preserveState || restoreState(state);
    };
    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, uploadSlot.buffer);
    configureAttributes();
    bool partialUpload = false;
    std::size_t uploadedBytes = 0;
    std::uint64_t cpuUploadNanoseconds = 0;
    if (upload.requiresUpload()) {
        const auto uploadStarted = std::chrono::steady_clock::now();
        const bool partial = upload.kind == henia::detail::UploadSelectionKind::Partial;
        const auto uploadRange = [&](DirtyRange range) noexcept {
            std::size_t offsetBytes = 0;
            std::size_t countBytes = 0;
            if (!checkedMultiply(range.offset, sizeof(BoxInstance), offsetBytes)
                || !checkedMultiply(range.count, sizeof(BoxInstance), countBytes)
                || offsetBytes > static_cast<std::size_t>(std::numeric_limits<GlIntPtr>::max())
                || countBytes > static_cast<std::size_t>(std::numeric_limits<GlSize>::max())) {
                return 1;
            }
            constexpr GLbitfield flags = kMapWriteBit | kMapUnsynchronizedBit;
            void* destination = gl.mapBufferRange(
                kArrayBuffer,
                static_cast<GlIntPtr>(offsetBytes),
                static_cast<GlSize>(countBytes),
                flags);
            if (destination == nullptr) return 2;
            std::size_t sourceOffset = range.offset;
            std::size_t destinationOffset = 0;
            std::size_t remaining = range.count;
            if (cpuCulling) {
                std::memcpy(destination, culledBoxes.data() + range.offset, countBytes);
                remaining = 0;
            }
            while (remaining > 0) {
                const std::span<const BoxInstance> page = batch.boxPage(
                    sourceOffset / InstanceBatch::kBoxesPerPage);
                const std::size_t localOffset = sourceOffset % InstanceBatch::kBoxesPerPage;
                const std::size_t pageCount = std::min(remaining, page.size() - localOffset);
                std::memcpy(
                    static_cast<std::byte*>(destination)
                        + destinationOffset * sizeof(BoxInstance),
                    page.data() + localOffset,
                    pageCount * sizeof(BoxInstance));
                sourceOffset += pageCount;
                destinationOffset += pageCount;
                remaining -= pageCount;
            }
            if (gl.unmapBuffer(kArrayBuffer) != GL_TRUE) {
                return 3;
            }
            uploadedBytes += countBytes;
            return 0;
        };
        int uploadFailure = 0;
        if (partial) {
            for (const DirtyRange range : dirtyRanges) {
                uploadFailure = uploadRange(range);
                if (uploadFailure != 0) break;
            }
        } else if (submittedCount != 0) {
            uploadFailure = uploadRange({0, submittedCount});
        }
        if (uploadFailure != 0) {
            uploadRing.invalidate(upload.slot);
            const bool restored = restoreCapturedState();
            ++statistics.rejectedFrames;
            if (uploadFailure == 1) ++statistics.capacityRejectedFrames;
            if (restored) {
                if (uploadFailure == 1) {
                    error = "OpenGL gfx upload byte range exceeds GLintptr/GLsizeiptr";
                } else if (uploadFailure == 2) {
                    error = "OpenGL failed to map the gfx instance buffer";
                } else {
                    error = "OpenGL reported a corrupted gfx instance buffer";
                }
            }
            return false;
        }
        uploadRing.markUploaded(upload.slot, producerIdentity, producerRevision);
        partialUpload = partial;
        cpuUploadNanoseconds = elapsedNanoseconds(uploadStarted);
    }
    if (consumeOperationErrors() != GL_NO_ERROR) {
        if (upload.requiresUpload()) {
            uploadRing.invalidate(upload.slot);
        }
        const bool restored = restoreCapturedState();
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx instance upload failed";
        return false;
    }
    if (upload.requiresUpload()) {
        if (submittedCount == 0) {
            ++statistics.zeroWorkInstanceRevisions;
        } else if (partialUpload) {
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
    gl.uniform1f(motionScaleLocation, view.motionScale);
    gl.uniform1i(depthRangeLocation, view.clipDepthRange == ClipDepthRange::ZeroToOne ? 1 : 0);
    ++statistics.viewUpdates;
    if (consumeOperationErrors() != GL_NO_ERROR) {
        const bool restored = restoreCapturedState();
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
        const bool restored = restoreCapturedState();
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
        const bool restored = restoreCapturedState();
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx pipeline state failed";
        return false;
    }
    if (submittedCount != 0 && primitives.indexCount != 0) {
        gl.drawElementsInstanced(
            GL_TRIANGLES,
            static_cast<GLsizei>(primitives.indexCount),
            GL_UNSIGNED_SHORT,
            reinterpret_cast<const void*>(primitives.firstIndex * sizeof(std::uint16_t)),
            static_cast<GLsizei>(submittedCount));
        ++statistics.drawCalls;
        statistics.submittedInstances += submittedCount;
        statistics.generatedVertices += submittedCount * primitives.vertexCount;
        statistics.submittedIndices += submittedCount * primitives.indexCount;
    }
    const GLenum drawError = consumeOperationErrors();
    const bool fenced = submittedCount == 0 || primitives.indexCount == 0
        || fenceUploadSlot(upload.slot);
    if (drawError != GL_NO_ERROR) {
        const bool restored = restoreCapturedState();
        ++statistics.rejectedFrames;
        if (restored) error = "OpenGL gfx instanced draw failed";
        return false;
    }
    if (!fenced) {
        static_cast<void>(restoreCapturedState());
        ++statistics.rejectedFrames;
        return false;
    }
    const std::uint64_t cpuDrawSubmitNanoseconds = elapsedNanoseconds(submitStarted);
    if (!restoreCapturedState()) {
        ++statistics.rejectedFrames;
        return false;
    }
    ++statistics.successfulFrames;
    if (!preserveState) {
        ++statistics.dedicatedContextFrames;
    }
    if (cpuCulling) {
        ++statistics.cpuCulledFrames;
        statistics.visibilitySourceInstances += visibilityStatistics.sourceInstances;
        statistics.visibilityRejectedInstances += visibilityStatistics.frustumRejectedInstances
            + visibilityStatistics.applicationMaskRejectedInstances
            + visibilityStatistics.projectedSizeRejectedInstances;
        statistics.visibilityChunkTests += visibilityStatistics.chunkTests;
        statistics.visibilityChunkRejectedInstances += visibilityStatistics.chunkRejectedInstances;
        statistics.visibilityResultReuses += visibilityStatistics.resultReused ? 1U : 0U;
        statistics.visibilityCullingNanoseconds += visibilityStatistics.cullingNanoseconds;
    } else {
        ++statistics.directVisibilityFrames;
    }
    const InstanceUploadKind uploadKind = !upload.requiresUpload()
        ? InstanceUploadKind::None
        : submittedCount == 0
            ? InstanceUploadKind::ZeroWorkRevision
            : partialUpload
                ? InstanceUploadKind::DirtyRanges
                : InstanceUploadKind::Full;
    static_cast<void>(profileTimeline.complete({
        .frameAttemptId = frameAttemptId,
        .producerIdentity = producerIdentity,
        .producerRevision = producerRevision,
        .producerBuildNanoseconds = batch.cpuBuildNanoseconds(),
        .cpuUploadNanoseconds = cpuUploadNanoseconds,
        .cpuDrawSubmitNanoseconds = cpuDrawSubmitNanoseconds,
        .uploadedInstanceBytes = uploadedBytes,
        .uploadRangeCount = uploadKind == InstanceUploadKind::DirtyRanges
            ? static_cast<std::uint32_t>(dirtyRanges.size())
            : uploadKind == InstanceUploadKind::Full ? 1U : 0U,
        .submissionSlot = static_cast<std::uint32_t>(upload.slot),
        .uploadKind = uploadKind,
    }));
    statistics.profile = profileTimeline.profile();
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
    gl.enableVertexAttribArray(4);
    gl.vertexAttribIPointer(4, 3, GL_UNSIGNED_INT, stride, reinterpret_cast<const void*>(52));
    for (GLuint attribute = 0; attribute < 5; ++attribute) {
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
    GLenum firstError = GL_NO_ERROR;
    const char* failureCategory = nullptr;
    const auto captureRestoreError = [&](const char* category) noexcept {
        const GLenum current = consumeOperationErrors();
        if (firstError == GL_NO_ERROR && current != GL_NO_ERROR) {
            firstError = current;
            failureCategory = category;
        }
    };
    const GLuint savedProgram = static_cast<GLuint>(state.program);
    gl.useProgram(savedProgram == 0 || gl.isProgram(savedProgram) == GL_TRUE ? savedProgram : 0);
    gl.bindVertexArray(static_cast<GLuint>(state.vertexArray));
    gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(state.arrayBuffer));
    captureRestoreError("OpenGL gfx object binding restoration failed");
    gl.blendFuncSeparate(
        static_cast<GLenum>(state.sourceRgb), static_cast<GLenum>(state.destinationRgb),
        static_cast<GLenum>(state.sourceAlpha), static_cast<GLenum>(state.destinationAlpha));
    gl.blendEquationSeparate(
        static_cast<GLenum>(state.equationRgb),
        static_cast<GLenum>(state.equationAlpha));
    captureRestoreError("OpenGL gfx blend state restoration failed");
    glDepthFunc(static_cast<GLenum>(state.depthFunction));
    glDepthMask(state.depthWrite);
    if (state.polygonMode[0] == state.polygonMode[1]) {
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(state.polygonMode[0]));
    } else {
        glPolygonMode(GL_FRONT, static_cast<GLenum>(state.polygonMode[0]));
        glPolygonMode(GL_BACK, static_cast<GLenum>(state.polygonMode[1]));
    }
    gl.colorMaskIndexed(
        0, state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
    glDepthRange(state.depthRange[0], state.depthRange[1]);
    captureRestoreError("OpenGL gfx raster/depth state restoration failed");
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
    captureRestoreError("OpenGL gfx capability restoration failed");
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]);
    captureRestoreError("OpenGL gfx viewport/scissor restoration failed");
    if (firstError != GL_NO_ERROR) {
        ++statistics.stateRestoreFailures;
        assignGlFailure(
            error,
            failureCategory,
            firstError,
            "context",
            reinterpret_cast<std::uintptr_t>(ownerContext));
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
        if (indexBuffer != 0 && gl.deleteBuffers != nullptr) gl.deleteBuffers(1, &indexBuffer);
        if (vertexArray != 0 && gl.deleteVertexArrays != nullptr) gl.deleteVertexArrays(1, &vertexArray);
        if (program != 0 && gl.deleteProgram != nullptr) gl.deleteProgram(program);
    }
    program = 0;
    vertexArray = 0;
    indexBuffer = 0;
    viewProjectionLocation = -1;
    viewportLocation = -1;
    timeLocation = -1;
    motionScaleLocation = -1;
    depthRangeLocation = -1;
    uploadSlots.clear();
    uploadRing.clear();
    capacity = 0;
    ownerContext = nullptr;
    statePolicy = OpenGlStatePolicy::Preserve;
    ready = false;
    if (hadOwner && consumeOperationErrors() != GL_NO_ERROR) {
        error = "OpenGL gfx resource destruction generated an error";
        return false;
    }
    error.clear();
    return true;
}

void OpenGlRenderDevice::Implementation::abandon() noexcept {
    const bool hadOwner = ownerContext != nullptr;
    program = 0;
    vertexArray = 0;
    indexBuffer = 0;
    viewProjectionLocation = -1;
    viewportLocation = -1;
    timeLocation = -1;
    motionScaleLocation = -1;
    depthRangeLocation = -1;
    uploadSlots.clear();
    uploadRing.clear();
    capacity = 0;
    ownerContext = nullptr;
    statePolicy = OpenGlStatePolicy::Preserve;
    ready = false;
    if (hadOwner) {
        ++statistics.abandonedContexts;
    }
    error.clear();
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
    std::size_t uploadSlotCountValue,
    OpenGlStatePolicy statePolicyValue) noexcept {
    try {
        const bool wasReady = mImplementation->ready;
        const bool initialized = mImplementation->initialize(
            capacityValue,
            uploadSlotCountValue,
            statePolicyValue);
        if (!initialized && !wasReady) {
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
    bool depthAttachmentAvailable,
    VisibilityOptions visibility) noexcept {
    return mImplementation->render(batch, view, depthAttachmentAvailable, visibility);
}
bool OpenGlRenderDevice::reportGpuTime(
    std::uint64_t sampleId,
    std::uint64_t nanoseconds) noexcept {
    if (!mImplementation->ready) return false;
    const bool reported = mImplementation->profileTimeline.reportGpuTime(sampleId, nanoseconds);
    mImplementation->statistics.profile = mImplementation->profileTimeline.profile();
    return reported;
}
bool OpenGlRenderDevice::shutdown() noexcept {
    return mImplementation == nullptr || mImplementation->shutdown();
}
void OpenGlRenderDevice::abandon() noexcept {
    if (mImplementation != nullptr) mImplementation->abandon();
}
bool OpenGlRenderDevice::initialized() const noexcept { return mImplementation->ready; }
std::size_t OpenGlRenderDevice::boxCapacity() const noexcept { return mImplementation->capacity; }
std::size_t OpenGlRenderDevice::uploadSlotCount() const noexcept {
    return mImplementation->uploadSlots.size();
}
OpenGlGfxStatistics OpenGlRenderDevice::statistics() const noexcept { return mImplementation->statistics; }
std::string_view OpenGlRenderDevice::lastError() const noexcept { return mImplementation->error.view(); }

} // namespace henia::gfx
