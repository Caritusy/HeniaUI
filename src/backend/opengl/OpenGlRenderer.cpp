#include "henia/ui/backend/opengl/OpenGlRenderer.h"
#include "henia/CheckedArithmetic.h"
#include "henia/ui/Validation.h"

#include "../FixedError.h"
#include "../ProfileTimeline.h"
#include "OpenGlFailure.h"
#include "OpenGlState.h"
#include "OpenGlTextureTransaction.h"
#include "OpenGlUploadRing.h"

#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY*
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace henia::ui {
namespace {

using henia::detail::assignGlFailure;

using GlChar = char;
using GlSize = std::ptrdiff_t;
using GlIntPtr = std::ptrdiff_t;
struct GlSyncObject;
using GlSync = GlSyncObject*;

constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kPixelUnpackBuffer = 0x88EC;
constexpr GLenum kPixelUnpackBufferBinding = 0x88EF;
constexpr GLenum kDynamicDraw = 0x88E8;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kInfoLogLength = 0x8B84;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kVertexArrayBinding = 0x85B5;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kActiveTexture = 0x84E0;
constexpr GLenum kTextureBinding2D = 0x8069;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kR8 = 0x8229;
constexpr GLenum kRed = 0x1903;
constexpr GLenum kRgba8 = 0x8058;
constexpr GLenum kSrgb8Alpha8 = 0x8C43;
constexpr GLenum kMapWriteBit = 0x0002;
constexpr GLenum kMapUnsynchronizedBit = 0x0020;
constexpr GLenum kMapPersistentBit = 0x0040;
constexpr GLenum kMapCoherentBit = 0x0080;
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
constexpr GLenum kSamplerBinding = 0x8919;
constexpr GLenum kFramebufferSrgb = 0x8DB9;
constexpr GLenum kRasterizerDiscard = 0x8C89;
constexpr GLenum kMultisample = 0x809D;
constexpr GLenum kSampleAlphaToCoverage = 0x809E;
constexpr GLenum kSampleAlphaToOne = 0x809F;
constexpr GLenum kSampleCoverage = 0x80A0;
constexpr GLenum kUnpackRowLength = 0x0CF2;
constexpr GLenum kMajorVersion = 0x821B;
constexpr GLenum kMinorVersion = 0x821C;
constexpr GLenum kNumberOfExtensions = 0x821D;

static_assert(std::is_standard_layout_v<DrawInstance>);
static_assert(offsetof(DrawInstance, uv) == offsetof(DrawInstance, bounds) + sizeof(Rect));
static_assert(offsetof(DrawInstance, color) == offsetof(DrawInstance, uv) + sizeof(Rect));
static_assert(offsetof(DrawInstance, thickness) == offsetof(DrawInstance, radius) + sizeof(float));
static_assert(offsetof(DrawInstance, lineStyle) == offsetof(DrawInstance, kind) + 3U);

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
using BufferStorageFn = void(APIENTRYP)(GLenum, GlSize, const void*, GLbitfield);
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
using Uniform2fFn = void(APIENTRYP)(GLint, GLfloat, GLfloat);
using Uniform1fFn = void(APIENTRYP)(GLint, GLfloat);
using Uniform1ivFn = void(APIENTRYP)(GLint, GLsizei, const GLint*);
using ActiveTextureFn = void(APIENTRYP)(GLenum);
using DrawArraysInstancedFn = void(APIENTRYP)(GLenum, GLint, GLsizei, GLsizei);
using DrawArraysInstancedBaseInstanceFn = void(APIENTRYP)(
    GLenum, GLint, GLsizei, GLsizei, GLuint);
using BlendFuncSeparateFn = void(APIENTRYP)(GLenum, GLenum, GLenum, GLenum);
using BlendEquationSeparateFn = void(APIENTRYP)(GLenum, GLenum);
using BindSamplerFn = void(APIENTRYP)(GLuint, GLuint);
using IsProgramFn = GLboolean(APIENTRYP)(GLuint);
using GetBooleanIndexedFn = void(APIENTRYP)(GLenum, GLuint, GLboolean*);
using ColorMaskIndexedFn = void(APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
using TexStorage2DFn = void(APIENTRYP)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using GetStringIndexedFn = const GLubyte*(APIENTRYP)(GLenum, GLuint);

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
    BufferStorageFn bufferStorage = nullptr;
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
    Uniform2fFn uniform2f = nullptr;
    Uniform1fFn uniform1f = nullptr;
    Uniform1ivFn uniform1iv = nullptr;
    ActiveTextureFn activeTexture = nullptr;
    DrawArraysInstancedFn drawArraysInstanced = nullptr;
    DrawArraysInstancedBaseInstanceFn drawArraysInstancedBaseInstance = nullptr;
    BlendFuncSeparateFn blendFuncSeparate = nullptr;
    BlendEquationSeparateFn blendEquationSeparate = nullptr;
    BindSamplerFn bindSampler = nullptr;
    IsProgramFn isProgram = nullptr;
    GetBooleanIndexedFn getBooleanIndexed = nullptr;
    ColorMaskIndexedFn colorMaskIndexed = nullptr;
    TexStorage2DFn texStorage2D = nullptr;
    GetStringIndexedFn getStringIndexed = nullptr;
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
    const bool loaded = load(gl.createShader, "glCreateShader")
        && load(gl.shaderSource, "glShaderSource")
        && load(gl.compileShader, "glCompileShader")
        && load(gl.getShaderIv, "glGetShaderiv")
        && load(gl.getShaderInfoLog, "glGetShaderInfoLog")
        && load(gl.deleteShader, "glDeleteShader")
        && load(gl.createProgram, "glCreateProgram")
        && load(gl.attachShader, "glAttachShader")
        && load(gl.linkProgram, "glLinkProgram")
        && load(gl.getProgramIv, "glGetProgramiv")
        && load(gl.getProgramInfoLog, "glGetProgramInfoLog")
        && load(gl.deleteProgram, "glDeleteProgram")
        && load(gl.useProgram, "glUseProgram")
        && load(gl.genVertexArrays, "glGenVertexArrays")
        && load(gl.bindVertexArray, "glBindVertexArray")
        && load(gl.deleteVertexArrays, "glDeleteVertexArrays")
        && load(gl.genBuffers, "glGenBuffers")
        && load(gl.bindBuffer, "glBindBuffer")
        && load(gl.bufferData, "glBufferData")
        && load(gl.mapBufferRange, "glMapBufferRange")
        && load(gl.unmapBuffer, "glUnmapBuffer")
        && load(gl.deleteBuffers, "glDeleteBuffers")
        && load(gl.fenceSync, "glFenceSync")
        && load(gl.clientWaitSync, "glClientWaitSync")
        && load(gl.deleteSync, "glDeleteSync")
        && load(gl.enableVertexAttribArray, "glEnableVertexAttribArray")
        && load(gl.vertexAttribPointer, "glVertexAttribPointer")
        && load(gl.vertexAttribIPointer, "glVertexAttribIPointer")
        && load(gl.vertexAttribDivisor, "glVertexAttribDivisor")
        && load(gl.getUniformLocation, "glGetUniformLocation")
        && load(gl.uniform2f, "glUniform2f")
        && load(gl.uniform1f, "glUniform1f")
        && load(gl.uniform1iv, "glUniform1iv")
        && load(gl.activeTexture, "glActiveTexture")
        && load(gl.drawArraysInstanced, "glDrawArraysInstanced")
        && load(gl.blendFuncSeparate, "glBlendFuncSeparate")
        && load(gl.blendEquationSeparate, "glBlendEquationSeparate")
        && load(gl.bindSampler, "glBindSampler")
        && load(gl.isProgram, "glIsProgram")
        && load(gl.getBooleanIndexed, "glGetBooleani_v")
        && load(gl.colorMaskIndexed, "glColorMaski")
        && load(gl.getStringIndexed, "glGetStringi");
    if (loaded) {
        gl.texStorage2D = reinterpret_cast<TexStorage2DFn>(loadOpenGlFunction("glTexStorage2D"));
        gl.bufferStorage = reinterpret_cast<BufferStorageFn>(
            loadOpenGlFunction("glBufferStorage"));
        gl.drawArraysInstancedBaseInstance = reinterpret_cast<DrawArraysInstancedBaseInstanceFn>(
            loadOpenGlFunction("glDrawArraysInstancedBaseInstance"));
    }
    return loaded;
}

[[nodiscard]] bool supportsCapability(
    const GlFunctions& gl,
    GLint requiredMajor,
    GLint requiredMinor,
    std::string_view extension) noexcept {
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(kMajorVersion, &major);
    glGetIntegerv(kMinorVersion, &minor);
    if (glGetError() != GL_NO_ERROR) return false;
    if (major > requiredMajor || (major == requiredMajor && minor >= requiredMinor)) {
        return true;
    }
    GLint extensionCount = 0;
    glGetIntegerv(kNumberOfExtensions, &extensionCount);
    if (glGetError() != GL_NO_ERROR || extensionCount < 0) return false;
    for (GLint index = 0; index < extensionCount; ++index) {
        const GLubyte* name = gl.getStringIndexed(GL_EXTENSIONS, static_cast<GLuint>(index));
        if (name != nullptr
            && std::string_view(reinterpret_cast<const char*>(name)) == extension) {
            return true;
        }
    }
    while (glGetError() != GL_NO_ERROR) {}
    return false;
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
layout(location = 0) in vec4 instanceBounds;
layout(location = 1) in vec4 instanceUv;
layout(location = 2) in vec4 instanceColor;
layout(location = 3) in vec2 instanceMetrics;
layout(location = 4) in uvec4 instanceStyle;

uniform vec2 viewportSize;
uniform vec2 logicalToFramebufferScale;
uniform vec2 logicalToFramebufferTranslation;

out vec2 pixelPosition;
out vec2 textureUv;
out vec4 tintColor;
out vec4 linePoints;
flat out vec4 lineNeighbors;
out vec2 shapeMetrics;
flat out uint textureSlot;
flat out uint primitiveKind;
flat out uint lineCap;
flat out uint lineJoin;
flat out uint lineFlags;
flat out uint shaderParameter;

const vec2 corners[4] = vec2[4](
    vec2(0.0, 0.0), vec2(1.0, 0.0),
    vec2(0.0, 1.0), vec2(1.0, 1.0));

void main() {
    vec2 corner = corners[gl_VertexID];
    uint kind = instanceStyle.x;
    uint decodedLineJoin = instanceStyle.w & 1u;
    uint decodedLineFlags = (instanceStyle.w >> 1u) & 3u;
    vec2 pixel;
    if (kind == 2u) {
        vec2 start = instanceBounds.xy;
        vec2 finish = instanceBounds.zw;
        vec2 segment = finish - start;
        float segmentLength = length(segment);
        vec2 direction = segment / segmentLength;
        vec2 normal = vec2(-direction.y, direction.x);
        float halfWidth = instanceMetrics.y * 0.5;
        uint joinMode = decodedLineJoin == 0u ? 0u : 2u;
        uint startMode = (decodedLineFlags & 1u) != 0u ? joinMode : instanceStyle.z;
        uint endMode = (decodedLineFlags & 2u) != 0u ? joinMode : instanceStyle.z;
        float startExtension = (((decodedLineFlags & 1u) != 0u || startMode != 0u)
            ? halfWidth : 0.0) + 2.0;
        float endExtension = (((decodedLineFlags & 2u) != 0u || endMode != 0u)
            ? halfWidth : 0.0) + 2.0;
        float along = mix(-startExtension, segmentLength + endExtension, corner.x);
        float across = mix(-halfWidth - 2.0, halfWidth + 2.0, corner.y);
        pixel = start + direction * along + normal * across;
    } else if (kind == 9u || kind == 14u) {
        float extent = instanceMetrics.y * 3.0 + 2.0;
        vec2 offset = kind == 9u ? instanceUv.xy : vec2(0.0);
        pixel = mix(
            instanceBounds.xy + offset - vec2(extent),
            instanceBounds.zw + offset + vec2(extent),
            corner);
    } else if (kind == 0u || kind == 5u || kind == 6u || kind == 7u
        || kind == 8u || kind == 10u || kind == 12u || kind == 13u
        || kind == 15u) {
        pixel = mix(
            instanceBounds.xy - vec2(2.0),
            instanceBounds.zw + vec2(2.0),
            corner);
    } else {
        pixel = mix(instanceBounds.xy, instanceBounds.zw, corner);
    }
    vec2 framebufferPixel = pixel * logicalToFramebufferScale
        + logicalToFramebufferTranslation;
    if (kind == 4u) {
        vec2 snapOrigin = (instanceStyle.w & 1u) != 0u
            ? instanceBounds.xy
            : instanceMetrics;
        vec2 physicalSnapOrigin = snapOrigin * logicalToFramebufferScale
            + logicalToFramebufferTranslation;
        framebufferPixel += floor(physicalSnapOrigin + vec2(0.5)) - physicalSnapOrigin;
    }
    vec2 normalized = framebufferPixel / viewportSize;
    gl_Position = vec4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);

    pixelPosition = pixel;
    textureUv = mix(instanceUv.xy, instanceUv.zw, corner);
    tintColor = instanceColor;
    linePoints = kind == 1u ? instanceUv : instanceBounds;
    lineNeighbors = instanceUv;
    shapeMetrics = instanceMetrics;
    textureSlot = instanceStyle.y;
    primitiveKind = kind;
    lineCap = instanceStyle.z;
    lineJoin = decodedLineJoin;
    lineFlags = decodedLineFlags;
    shaderParameter = instanceStyle.w;
}
)glsl";

constexpr const char* kFragmentShaderSource = R"glsl(
#version 330 core
in vec2 pixelPosition;
in vec2 textureUv;
in vec4 tintColor;
in vec4 linePoints;
flat in vec4 lineNeighbors;
in vec2 shapeMetrics;
flat in uint textureSlot;
flat in uint primitiveKind;
flat in uint lineCap;
flat in uint lineJoin;
flat in uint lineFlags;
flat in uint shaderParameter;

uniform sampler2D textures[8];
uniform int textureAlphaModes[8];
uniform float minimumAntialiasWidth;
out vec4 outputColor;

vec4 sampleTexture(uint slot, vec2 uv) {
    if (slot == 0u) return texture(textures[0], uv);
    if (slot == 1u) return texture(textures[1], uv);
    if (slot == 2u) return texture(textures[2], uv);
    if (slot == 3u) return texture(textures[3], uv);
    if (slot == 4u) return texture(textures[4], uv);
    if (slot == 5u) return texture(textures[5], uv);
    if (slot == 6u) return texture(textures[6], uv);
    return texture(textures[7], uv);
}

vec4 straightTextureSample(uint slot, vec2 uv) {
    vec4 sampled = sampleTexture(slot, uv);
    int alphaMode = textureAlphaModes[int(slot)];
    if (alphaMode == 2) {
        sampled.rgb = sampled.a > 0.00001 ? sampled.rgb / sampled.a : vec3(0.0);
    } else if (alphaMode == 3) {
        sampled.a = 1.0;
    }
    return sampled;
}

float roundedBoxDistance(vec2 point, vec2 halfSize, float radius) {
    vec2 q = abs(point) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

float variableRoundedBoxDistance(vec2 point, vec2 halfSize, vec4 radii) {
    float radius = point.x < 0.0
        ? (point.y < 0.0 ? radii.x : radii.w)
        : (point.y < 0.0 ? radii.y : radii.z);
    radius = min(max(radius, 0.0), min(halfSize.x, halfSize.y));
    vec2 q = abs(point) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

float ellipseDistance(vec2 point, vec2 halfSize) {
    vec2 safeHalfSize = max(halfSize, vec2(0.001));
    return (length(point / safeHalfSize) - 1.0) * min(safeHalfSize.x, safeHalfSize.y);
}

float arcDistance(vec2 point, vec2 halfSize, float start, float sweep, float thickness) {
    const float tau = 6.28318530718;
    float halfWidth = thickness * 0.5;
    vec2 pathRadii = max(halfSize - vec2(halfWidth), vec2(0.001));
    float angle = atan(point.y / pathRadii.y, point.x / pathRadii.x);
    float direction = sweep < 0.0 ? -1.0 : 1.0;
    float relative = mod(direction * (angle - start), tau);
    float span = abs(sweep);
    float radial = abs(ellipseDistance(point, pathRadii)) - halfWidth;
    if (span >= tau - 0.0001 || relative <= span) return radial;
    vec2 startPoint = vec2(cos(start), sin(start)) * pathRadii;
    float finish = start + sweep;
    vec2 finishPoint = vec2(cos(finish), sin(finish)) * pathRadii;
    return min(length(point - startPoint), length(point - finishPoint)) - halfWidth;
}

float ninePatchCoordinate(float value, float destinationBorder, float sourceBorder) {
    if (value < destinationBorder) {
        return value / max(destinationBorder, 0.0001) * sourceBorder;
    }
    if (value > 1.0 - destinationBorder) {
        return 1.0 - (1.0 - value) / max(destinationBorder, 0.0001) * sourceBorder;
    }
    return sourceBorder
        + (value - destinationBorder) / max(1.0 - destinationBorder * 2.0, 0.0001)
            * (1.0 - sourceBorder * 2.0);
}

float boxDistance(vec2 point, vec2 halfSize) {
    vec2 q = abs(point) - halfSize;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0);
}

float cappedSegmentDistance(
    vec2 point,
    vec2 start,
    vec2 finish,
    uint startMode,
    uint endMode,
    float halfWidth) {
    vec2 segment = finish - start;
    float segmentLength = length(segment);
    vec2 direction = segment / segmentLength;
    vec2 normal = vec2(-direction.y, direction.x);
    float startExtension = startMode == 1u ? halfWidth : 0.0;
    float endExtension = endMode == 1u ? halfWidth : 0.0;
    float center = (segmentLength + endExtension - startExtension) * 0.5;
    vec2 local = vec2(dot(point - start, direction) - center, dot(point - start, normal));
    float distanceValue = boxDistance(
        local,
        vec2((segmentLength + startExtension + endExtension) * 0.5, halfWidth));
    if (startMode == 2u) distanceValue = min(distanceValue, length(point - start) - halfWidth);
    if (endMode == 2u) distanceValue = min(distanceValue, length(point - finish) - halfWidth);
    return distanceValue;
}

float triangleDistance(vec2 point, vec2 a, vec2 b, vec2 c) {
    vec2 e0 = b - a;
    vec2 e1 = c - b;
    vec2 e2 = a - c;
    vec2 v0 = point - a;
    vec2 v1 = point - b;
    vec2 v2 = point - c;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float orientation = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 distanceValue = min(
        min(
            vec2(dot(pq0, pq0), orientation * (v0.x * e0.y - v0.y * e0.x)),
            vec2(dot(pq1, pq1), orientation * (v1.x * e1.y - v1.y * e1.x))),
        vec2(dot(pq2, pq2), orientation * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(distanceValue.x) * sign(distanceValue.y);
}

float bevelJoinDistance(vec2 point, vec2 before, vec2 joint, vec2 after, float halfWidth) {
    vec2 incoming = normalize(joint - before);
    vec2 outgoing = normalize(after - joint);
    float turn = incoming.x * outgoing.y - incoming.y * outgoing.x;
    if (abs(turn) < 0.0001) return 1e20;
    float outside = turn > 0.0 ? -1.0 : 1.0;
    vec2 incomingNormal = vec2(-incoming.y, incoming.x) * outside;
    vec2 outgoingNormal = vec2(-outgoing.y, outgoing.x) * outside;
    return triangleDistance(
        point,
        joint,
        joint + incomingNormal * halfWidth,
        joint + outgoingNormal * halfWidth);
}

vec4 premultiply(vec4 color) {
    return vec4(color.rgb * color.a, color.a);
}

void main() {
    float coverage = 1.0;
    vec4 color = tintColor;

    if (primitiveKind == 0u || primitiveKind == 1u
        || primitiveKind == 12u || primitiveKind == 15u) {
        vec2 primitiveSize = linePoints.zw - linePoints.xy;
        vec2 centered = pixelPosition - (linePoints.xy + linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            primitiveSize * 0.5,
            min(shapeMetrics.x, min(primitiveSize.x, primitiveSize.y) * 0.5));
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        float outer = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        if (primitiveKind == 1u || primitiveKind == 15u) {
            float innerDistance = distanceToEdge + max(shapeMetrics.y, 0.0);
            float inner = 1.0 - smoothstep(-antiAlias, antiAlias, innerDistance);
            coverage = max(outer - inner, 0.0);
        } else {
            coverage = outer;
        }
    } else if (primitiveKind == 2u) {
        uint joinMode = lineJoin == 0u ? 0u : 2u;
        bool hasPrevious = (lineFlags & 1u) != 0u;
        bool hasNext = (lineFlags & 2u) != 0u;
        uint startMode = hasPrevious ? joinMode : lineCap;
        uint endMode = hasNext ? joinMode : lineCap;
        float halfWidth = shapeMetrics.y * 0.5;
        float distanceToLine = cappedSegmentDistance(
            pixelPosition,
            linePoints.xy,
            linePoints.zw,
            startMode,
            endMode,
            halfWidth);
        if (hasNext && lineJoin == 0u) {
            distanceToLine = min(
                distanceToLine,
                bevelJoinDistance(
                    pixelPosition,
                    linePoints.xy,
                    linePoints.zw,
                    lineNeighbors.zw,
                    halfWidth));
        }
        float antiAlias = max(fwidth(distanceToLine), minimumAntialiasWidth);
        if (hasPrevious) {
            float previousDistance = cappedSegmentDistance(
                pixelPosition,
                lineNeighbors.xy,
                linePoints.xy,
                0u,
                joinMode,
                halfWidth);
            if (lineJoin == 0u) {
                previousDistance = min(
                    previousDistance,
                    bevelJoinDistance(
                        pixelPosition,
                        lineNeighbors.xy,
                        linePoints.xy,
                        linePoints.zw,
                        halfWidth));
            }
            if (previousDistance <= distanceToLine) discard;
        }
        if (hasNext) {
            float nextDistance = cappedSegmentDistance(
                pixelPosition,
                linePoints.zw,
                lineNeighbors.zw,
                joinMode,
                0u,
                halfWidth);
            if (nextDistance < distanceToLine) discard;
        }
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToLine);
    } else if (primitiveKind == 5u) {
        vec2 halfSize = (linePoints.zw - linePoints.xy) * 0.5;
        vec2 centered = pixelPosition - (linePoints.xy + linePoints.zw) * 0.5;
        float distanceToEdge = ellipseDistance(centered, halfSize);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    } else if (primitiveKind == 6u) {
        vec2 halfSize = (linePoints.zw - linePoints.xy) * 0.5;
        vec2 centered = pixelPosition - (linePoints.xy + linePoints.zw) * 0.5;
        float distanceToEdge = arcDistance(
            centered,
            halfSize,
            lineNeighbors.x,
            lineNeighbors.y,
            shapeMetrics.y);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    } else if (primitiveKind == 7u) {
        vec2 primitiveSize = linePoints.zw - linePoints.xy;
        vec2 centered = pixelPosition - (linePoints.xy + linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            primitiveSize * 0.5,
            min(primitiveSize.x, primitiveSize.y) * 0.5);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    } else if (primitiveKind == 8u || primitiveKind == 13u) {
        vec2 primitiveSize = linePoints.zw - linePoints.xy;
        vec2 halfSize = primitiveSize * 0.5;
        vec2 centered = pixelPosition - (linePoints.xy + linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            halfSize,
            min(shapeMetrics.x, min(primitiveSize.x, primitiveSize.y) * 0.5));
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        vec2 direction = vec2(cos(shapeMetrics.y), sin(shapeMetrics.y));
        float extent = max(dot(abs(direction), halfSize), 0.0001);
        float phaseShift = primitiveKind == 13u
            ? sin(float(shaderParameter) / 255.0 * 6.28318530718) * 0.25
            : 0.0;
        float amount = clamp(
            0.5 + dot(centered, direction) / (extent * 2.0) + phaseShift,
            0.0,
            1.0);
        color = mix(tintColor, lineNeighbors, amount);
    } else if (primitiveKind == 9u || primitiveKind == 14u) {
        vec2 offset = primitiveKind == 9u ? lineNeighbors.xy : vec2(0.0);
        vec2 center = (linePoints.xy + linePoints.zw) * 0.5 + offset;
        vec2 halfSize = (linePoints.zw - linePoints.xy) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            pixelPosition - center,
            halfSize,
            min(shapeMetrics.x, min(halfSize.x, halfSize.y)));
        float normalizedDistance = max(distanceToEdge, 0.0) / max(shapeMetrics.y, 0.001);
        coverage = distanceToEdge <= 0.0
            ? 1.0
            : exp(-0.5 * normalizedDistance * normalizedDistance);
    } else if (primitiveKind == 10u) {
        vec2 primitiveSize = linePoints.zw - linePoints.xy;
        vec2 halfSize = primitiveSize * 0.5;
        vec2 centered = pixelPosition - (linePoints.xy + linePoints.zw) * 0.5;
        float distanceToEdge = variableRoundedBoxDistance(centered, halfSize, lineNeighbors);
        float antiAlias = max(fwidth(distanceToEdge), minimumAntialiasWidth);
        float outer = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        float borderWidth = min(shapeMetrics.y, min(halfSize.x, halfSize.y));
        vec2 innerHalfSize = max(halfSize - vec2(borderWidth), vec2(0.001));
        float innerDistance = variableRoundedBoxDistance(
            centered,
            innerHalfSize,
            max(lineNeighbors - vec4(borderWidth), vec4(0.0)));
        float inner = 1.0 - smoothstep(-antiAlias, antiAlias, innerDistance);
        coverage = max(outer - inner, 0.0);
    } else if (primitiveKind == 3u) {
        color *= straightTextureSample(textureSlot, textureUv);
    } else if (primitiveKind == 4u) {
        color.a *= sampleTexture(textureSlot, textureUv).r;
    } else if (primitiveKind == 11u) {
        vec2 primitiveSize = linePoints.zw - linePoints.xy;
        vec2 local = clamp((pixelPosition - linePoints.xy) / primitiveSize, 0.0, 1.0);
        vec2 destinationBorder = min(
            vec2(shapeMetrics.x) / primitiveSize,
            vec2(0.5));
        vec2 mapped = vec2(
            ninePatchCoordinate(local.x, destinationBorder.x, shapeMetrics.y),
            ninePatchCoordinate(local.y, destinationBorder.y, shapeMetrics.y));
        vec2 uv = mix(lineNeighbors.xy, lineNeighbors.zw, mapped);
        color *= straightTextureSample(textureSlot, uv);
    } else if (primitiveKind == 16u) {
        float distanceValue = sampleTexture(textureSlot, textureUv).r;
        coverage = smoothstep(
            shapeMetrics.x - shapeMetrics.y,
            shapeMetrics.x + shapeMetrics.y,
            distanceValue);
    }

    color.a *= coverage;
    if (color.a <= 0.001) discard;
    outputColor = premultiply(color);
}
)glsl";

[[nodiscard]] GLuint compileShader(
    const GlFunctions& gl,
    GLenum type,
    const char* source,
    henia::detail::FixedError& error) noexcept {
    const GLuint shader = gl.createShader(type);
    if (const GLenum glError = consumeOperationErrors(); shader == 0 || glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI shader creation failed", glError, "shaderType", type);
        return 0;
    }
    gl.shaderSource(shader, 1, &source, nullptr);
    gl.compileShader(shader);
    GLint compiled = GL_FALSE;
    gl.getShaderIv(shader, kCompileStatus, &compiled);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI shader compilation failed", glError, "shader", shader);
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

struct GlState final {
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint activeTexture = 0;
    std::array<GLint, DrawBatch::kTextureCapacity> textures{};
    std::array<GLint, DrawBatch::kTextureCapacity> samplers{};
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    GLint sourceRgb = 0;
    GLint destinationRgb = 0;
    GLint sourceAlpha = 0;
    GLint destinationAlpha = 0;
    GLint equationRgb = 0;
    GLint equationAlpha = 0;
    std::array<GLint, 2> polygonMode{};
    std::array<GLboolean, 4> colorMask{};
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

} // namespace

struct OpenGlRenderer::Implementation final {
    struct UploadSlot final {
        GLuint buffer = 0;
        GlSync fence = nullptr;
        std::byte* mapped = nullptr;
    };

    struct PacketValidationCache final {
        std::uint64_t identity = 0;
        std::uint64_t revision = 0;
        std::vector<std::array<std::uint8_t, DrawBatch::kTextureCapacity>> semantics;
        bool valid = false;
    };

    GlFunctions gl{};
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLint viewportLocation = -1;
    GLint logicalScaleLocation = -1;
    GLint logicalTranslationLocation = -1;
    GLint minimumAntialiasWidthLocation = -1;
    GLint texturesLocation = -1;
    GLint textureAlphaModesLocation = -1;
    GLint maximumTextureSize = 0;
    std::size_t capacity = 0;
    std::size_t instanceBufferBytes = 0;
    std::vector<UploadSlot> uploadSlots;
    henia::detail::OpenGlUploadRing uploadRing;
    std::vector<detail::OpenGlTextureState> textures;
    std::vector<std::uint32_t> partialTextureUpdates;
    PacketValidationCache packetValidation;
    OpenGlRenderStatistics statistics{};
    henia::detail::ProfileTimeline profileTimeline;
    henia::detail::FixedError error;
    HGLRC ownerContext = nullptr;
    OpenGlUiStatePolicy statePolicy = OpenGlUiStatePolicy::Preserve;
    OpenGlUploadStrategy uploadStrategy = OpenGlUploadStrategy::TransientMap;
    GLuint configuredAttributeBuffer = 0;
    bool baseInstanceAvailable = false;
    bool immutableTextureStorage = false;
    bool ready = false;

    [[nodiscard]] bool initialize(
        std::size_t requestedCapacity,
        std::size_t requestedTextureCapacity,
        std::size_t requestedUploadSlots,
        OpenGlUiStatePolicy requestedStatePolicy,
        OpenGlUploadStrategy requestedUploadStrategy);
    [[nodiscard]] bool synchronizeTextures(TextureStore& store) noexcept;
    [[nodiscard]] bool bindExternalTexture(
        const TextureStore& store,
        TextureHandle handle,
        std::uint32_t textureObject,
        OpenGlExternalTextureOwnership ownership) noexcept;
    [[nodiscard]] bool render(
        const RenderPacket& packet,
        UiRenderViewport viewport,
        RenderTargetColorSpace targetColorSpace) noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    void abandon() noexcept;
    [[nodiscard]] henia::detail::UploadFenceStatus pollUploadSlot(std::size_t index) noexcept;
    [[nodiscard]] bool fenceUploadSlot(std::size_t index) noexcept;
    void configureAttributes(std::size_t firstInstance, bool configureInvariant) noexcept;
    [[nodiscard]] GlState captureState() const noexcept;
    [[nodiscard]] bool restoreState(const GlState& state) noexcept;
    [[nodiscard]] bool validateOwnerContext(const char* operation) noexcept;
    void discardHostErrors() noexcept;
};

bool OpenGlRenderer::Implementation::initialize(
    std::size_t requestedCapacity,
    std::size_t requestedTextureCapacity,
    std::size_t requestedUploadSlots,
    OpenGlUiStatePolicy requestedStatePolicy,
    OpenGlUploadStrategy requestedUploadStrategy) {
    if (ready) {
        if (!validateOwnerContext("initialize")) {
            ++statistics.lifecycleRejections;
            return false;
        }
        if (requestedCapacity != capacity
            || requestedTextureCapacity != textures.size()
            || requestedUploadSlots != uploadSlots.size()
            || requestedStatePolicy != statePolicy
            || requestedUploadStrategy != uploadStrategy) {
            ++statistics.lifecycleRejections;
            error = "OpenGL renderer is already initialized with a different configuration";
            return false;
        }
        error.clear();
        return true;
    }
    std::size_t instanceBytes = 0;
    const HGLRC currentContext = wglGetCurrentContext();
    if (currentContext == nullptr || requestedCapacity == 0 || requestedTextureCapacity == 0
        || requestedUploadSlots == 0
        || (requestedStatePolicy != OpenGlUiStatePolicy::Preserve
            && requestedStatePolicy != OpenGlUiStatePolicy::DedicatedContext)
        || (requestedUploadStrategy != OpenGlUploadStrategy::TransientMap
            && requestedUploadStrategy != OpenGlUploadStrategy::PersistentMap)
        || requestedCapacity > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || requestedTextureCapacity > std::numeric_limits<std::uint32_t>::max()
        || requestedUploadSlots > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || !checkedMultiply(requestedCapacity, sizeof(DrawInstance), instanceBytes)
        || instanceBytes > static_cast<std::size_t>(std::numeric_limits<GlSize>::max())) {
        error = "OpenGL renderer configuration has an invalid instance/texture/upload capacity";
        return false;
    }
    ownerContext = currentContext;
    statePolicy = requestedStatePolicy;
    uploadStrategy = requestedUploadStrategy;
    statistics = {};
    packetValidation = {};
    packetValidation.semantics.reserve(requestedCapacity);
    profileTimeline.reset();
    textures.resize(requestedTextureCapacity);
    partialTextureUpdates.reserve(requestedTextureCapacity);
    uploadSlots.resize(requestedUploadSlots);
    uploadRing.reset(requestedUploadSlots);
    if (!loadFunctions(gl)) {
        error = "OpenGL 3.3 entry points are unavailable";
        return false;
    }
    discardHostErrors();
    const bool persistentMappingAvailable = gl.bufferStorage != nullptr
        && supportsCapability(gl, 4, 4, "GL_ARB_buffer_storage");
    if (uploadStrategy == OpenGlUploadStrategy::PersistentMap
        && !persistentMappingAvailable) {
        error = "Persistent OpenGL uploads require OpenGL 4.4 or ARB_buffer_storage";
        return false;
    }
    baseInstanceAvailable = gl.drawArraysInstancedBaseInstance != nullptr
        && supportsCapability(gl, 4, 2, "GL_ARB_base_instance");
    statistics.persistentUploadActive =
        uploadStrategy == OpenGlUploadStrategy::PersistentMap;
    statistics.baseInstanceActive = baseInstanceAvailable;
    immutableTextureStorage = gl.texStorage2D != nullptr
        && supportsCapability(gl, 4, 2, "GL_ARB_texture_storage");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR || maximumTextureSize <= 0) {
        assignGlFailure(
            error,
            "OpenGL texture-limit query failed",
            glError,
            "maximumTextureSize",
            static_cast<std::uint64_t>(std::max(maximumTextureSize, 0)));
        return false;
    }

    const GLuint vertexShader = compileShader(gl, kVertexShader, kVertexShaderSource, error);
    if (vertexShader == 0) {
        return false;
    }
    const GLuint fragmentShader = compileShader(gl, kFragmentShader, kFragmentShaderSource, error);
    if (fragmentShader == 0) {
        gl.deleteShader(vertexShader);
        return false;
    }

    program = gl.createProgram();
    if (const GLenum glError = consumeOperationErrors(); program == 0 || glError != GL_NO_ERROR) {
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        assignGlFailure(error, "OpenGL UI program creation failed", glError, "program", program);
        program = 0;
        return false;
    }
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);

    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI program link submission failed", glError, "program", program);
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
        gl.deleteProgram(program);
        program = 0;
        return false;
    }
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI program link-status query failed", glError, "program", program);
        return false;
    }

    viewportLocation = gl.getUniformLocation(program, "viewportSize");
    logicalScaleLocation = gl.getUniformLocation(program, "logicalToFramebufferScale");
    logicalTranslationLocation = gl.getUniformLocation(
        program, "logicalToFramebufferTranslation");
    minimumAntialiasWidthLocation = gl.getUniformLocation(program, "minimumAntialiasWidth");
    texturesLocation = gl.getUniformLocation(program, "textures");
    textureAlphaModesLocation = gl.getUniformLocation(program, "textureAlphaModes");
    const GLenum uniformError = consumeOperationErrors();
    if (uniformError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI uniform lookup failed", uniformError, "program", program);
        return false;
    }
    if (viewportLocation < 0 || logicalScaleLocation < 0
        || logicalTranslationLocation < 0 || minimumAntialiasWidthLocation < 0
        || texturesLocation < 0
        || textureAlphaModesLocation < 0) {
        error = "HeniaUI shader uniforms are unavailable";
        return false;
    }

    gl.genVertexArrays(1, &vertexArray);
    if (const GLenum glError = consumeOperationErrors(); vertexArray == 0 || glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI vertex-array creation failed", glError, "object", vertexArray);
        return false;
    }
    for (std::size_t index = 0; index < uploadSlots.size(); ++index) {
        UploadSlot& slot = uploadSlots[index];
        gl.genBuffers(1, &slot.buffer);
        if (const GLenum glError = consumeOperationErrors(); slot.buffer == 0 || glError != GL_NO_ERROR) {
            assignGlFailure(error, "OpenGL UI instance-buffer creation failed", glError, "uploadSlot", index);
            return false;
        }
    }

    GLint previousVertexArray = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(kVertexArrayBinding, &previousVertexArray);
    glGetIntegerv(kArrayBufferBinding, &previousArrayBuffer);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI initialization-state capture failed", glError, "object", 0);
        return false;
    }
    gl.bindVertexArray(vertexArray);
    const auto restoreInitializationState = [&]() noexcept {
        gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(previousArrayBuffer));
        gl.bindVertexArray(static_cast<GLuint>(previousVertexArray));
    };
    for (std::size_t index = 0; index < uploadSlots.size(); ++index) {
        UploadSlot& slot = uploadSlots[index];
        gl.bindBuffer(kArrayBuffer, slot.buffer);
        if (uploadStrategy == OpenGlUploadStrategy::PersistentMap) {
            constexpr GLbitfield flags = kMapWriteBit | kMapPersistentBit | kMapCoherentBit;
            gl.bufferStorage(
                kArrayBuffer,
                static_cast<GlSize>(instanceBytes),
                nullptr,
                flags);
            slot.mapped = static_cast<std::byte*>(gl.mapBufferRange(
                kArrayBuffer,
                0,
                static_cast<GlSize>(instanceBytes),
                flags));
            ++statistics.instanceMapOperations;
        } else {
            gl.bufferData(
                kArrayBuffer,
                static_cast<GlSize>(instanceBytes),
                nullptr,
                kDynamicDraw);
        }
        if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
            restoreInitializationState();
            assignGlFailure(
                error,
                "OpenGL UI instance-buffer storage allocation failed",
                glError,
                "uploadSlot",
                index);
            return false;
        }
        if (uploadStrategy == OpenGlUploadStrategy::PersistentMap && slot.mapped == nullptr) {
            restoreInitializationState();
            error = "OpenGL persistent instance-buffer mapping returned null";
            return false;
        }
    }
    gl.bindBuffer(kArrayBuffer, uploadSlots.front().buffer);
    configureAttributes(0, true);
    configuredAttributeBuffer = uploadSlots.front().buffer;
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        restoreInitializationState();
        assignGlFailure(error, "OpenGL UI vertex-layout setup failed", glError, "object", vertexArray);
        return false;
    }
    restoreInitializationState();
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(error, "OpenGL UI initialization-state restoration failed", glError, "object", 0);
        return false;
    }

    capacity = requestedCapacity;
    instanceBufferBytes = instanceBytes;
    ready = true;
    error.clear();
    return true;
}

bool OpenGlRenderer::Implementation::synchronizeTextures(TextureStore& store) noexcept {
    if (!ready) {
        error = "OpenGL renderer is not initialized";
        return false;
    }
    if (!validateOwnerContext("synchronizeTextures")) {
        return false;
    }
    if (store.slotCount() > textures.size()) {
        error = "OpenGL texture store exceeds configured capacity";
        return false;
    }

    bool requiresSynchronization = false;
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const TextureHandle handle = store.handleAt(index);
        detail::OpenGlTextureState& texture = textures[index];
        if (!handle.valid()) {
            requiresSynchronization = requiresSynchronization || texture.object != 0;
            continue;
        }
        TextureView view = store.view(handle);
        const std::uint32_t pixelBytes = view.format == TextureFormat::Alpha8 ? 1U : 4U;
        std::size_t minimumPitch = 0;
        std::size_t requiredBytes = 0;
        if (!view.handle.valid() || view.revision == 0
            || (view.format != TextureFormat::Alpha8 && view.format != TextureFormat::Rgba8)
            || view.width == 0 || view.height == 0
            || view.width > static_cast<std::uint32_t>(maximumTextureSize)
            || view.height > static_cast<std::uint32_t>(maximumTextureSize)
            || !checkedMultiply(
                static_cast<std::size_t>(view.width),
                static_cast<std::size_t>(pixelBytes),
                minimumPitch)
            || view.rowPitch < minimumPitch
            || view.rowPitch % pixelBytes != 0
            || view.rowPitch / pixelBytes
                > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max())
            || !checkedMultiply(
                static_cast<std::size_t>(view.rowPitch),
                static_cast<std::size_t>(view.height),
                requiredBytes)) {
            ++statistics.textureSynchronizationFailures;
            assignGlFailure(
                error,
                "OpenGL texture metadata is invalid",
                GL_INVALID_VALUE,
                "texture",
                handle.packed());
            return false;
        }
        if (view.backingPolicy == TextureBackingPolicy::ExternalGpu) {
            requiresSynchronization = requiresSynchronization
                || (texture.object != 0 && texture.handle != handle.packed());
            continue;
        }
        if (texture.object != 0 && texture.handle == handle.packed()
            && texture.revision == view.revision) {
            continue;
        }
        if (!view.backingAvailable) {
            static_cast<void>(store.ensureCpuBacking(handle));
            view = store.view(handle);
        }
        if (!view.backingAvailable || requiredBytes != view.pixels.size()
            || view.pixels.data() == nullptr) {
            ++statistics.textureSynchronizationFailures;
            error = "OpenGL texture CPU backing is unavailable; restore it before synchronization";
            return false;
        }
        requiresSynchronization = requiresSynchronization
            || texture.object == 0 || texture.handle != handle.packed()
            || texture.revision != view.revision;
    }
    if (!requiresSynchronization) {
        error.clear();
        return true;
    }

    discardHostErrors();
    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 0;
    GLint previousUnpackRowLength = 0;
    GLint previousUnpackBuffer = 0;
    glGetIntegerv(kTextureBinding2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGetIntegerv(kUnpackRowLength, &previousUnpackRowLength);
    glGetIntegerv(kPixelUnpackBufferBinding, &previousUnpackBuffer);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        ++statistics.textureSynchronizationFailures;
        assignGlFailure(error, "OpenGL texture upload-state capture failed", glError, "texture", 0);
        return false;
    }
    gl.bindBuffer(kPixelUnpackBuffer, 0);
    const auto restoreUploadState = [&]() noexcept {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glPixelStorei(kUnpackRowLength, previousUnpackRowLength);
        gl.bindBuffer(kPixelUnpackBuffer, static_cast<GLuint>(previousUnpackBuffer));
        return consumeOperationErrors();
    };
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        static_cast<void>(restoreUploadState());
        ++statistics.textureSynchronizationFailures;
        assignGlFailure(error, "OpenGL texture unpack-state setup failed", glError, "texture", 0);
        return false;
    }
    detail::OpenGlTextureTransaction transaction(textures);
    const auto destroyTexture = [](std::uint32_t object) noexcept {
        const GLuint name = object;
        glDeleteTextures(1, &name);
    };

    partialTextureUpdates.clear();
    std::uint64_t pendingFullBytes = 0;
    std::uint64_t pendingPartialBytes = 0;
    std::uint64_t pendingDirtyHistoryFallbacks = 0;
    std::uint64_t replacedTextures = 0;
    const auto rollbackPartials = [&]() noexcept {
        GLenum rollbackError = GL_NO_ERROR;
        for (const std::uint32_t index : partialTextureUpdates) {
            const TextureHandle handle = store.handleAt(index);
            const TextureView view = store.view(handle);
            detail::OpenGlTextureState& texture = textures[index];
            const std::uint32_t pixelBytes = view.format == TextureFormat::Alpha8 ? 1U : 4U;
            const GLenum sourceFormat = view.format == TextureFormat::Alpha8 ? kRed : GL_RGBA;
            glBindTexture(GL_TEXTURE_2D, texture.object);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(kUnpackRowLength, static_cast<GLint>(view.dirtyRegion.width));
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                static_cast<GLint>(view.dirtyRegion.x),
                static_cast<GLint>(view.dirtyRegion.y),
                static_cast<GLsizei>(view.dirtyRegion.width),
                static_cast<GLsizei>(view.dirtyRegion.height),
                sourceFormat,
                GL_UNSIGNED_BYTE,
                view.rollbackPixels.data());
            static_cast<void>(pixelBytes);
            const GLenum currentError = consumeOperationErrors();
            if (rollbackError == GL_NO_ERROR) rollbackError = currentError;
        }
        return rollbackError;
    };

    for (std::size_t index = 0; index < textures.size(); ++index) {
        const TextureHandle handle = store.handleAt(index);
        if (!handle.valid()) continue;
        const TextureView view = store.view(handle);
        detail::OpenGlTextureState& texture = textures[index];
        if (view.backingPolicy == TextureBackingPolicy::ExternalGpu
            || (texture.object != 0 && texture.handle == handle.packed()
                && texture.revision == view.revision)) {
            continue;
        }
        const std::uint32_t pixelBytes = view.format == TextureFormat::Alpha8 ? 1U : 4U;
        const bool partial = texture.object != 0 && texture.handle == handle.packed()
            && texture.revision + 1U == view.revision && !view.fullUpdate
            && view.dirtyRegion.width > 0 && view.dirtyRegion.height > 0
            && view.dirtyRegion.x <= view.width
            && view.dirtyRegion.width <= view.width - view.dirtyRegion.x
            && view.dirtyRegion.y <= view.height
            && view.dirtyRegion.height <= view.height - view.dirtyRegion.y
            && view.rollbackPixels.size()
                == static_cast<std::size_t>(view.dirtyRegion.width)
                    * view.dirtyRegion.height * pixelBytes;
        if (partial) {
            partialTextureUpdates.push_back(static_cast<std::uint32_t>(index));
            glBindTexture(GL_TEXTURE_2D, texture.object);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(kUnpackRowLength, static_cast<GLint>(view.rowPitch / pixelBytes));
            const GLenum sourceFormat = view.format == TextureFormat::Alpha8 ? kRed : GL_RGBA;
            const std::size_t sourceOffset = static_cast<std::size_t>(view.dirtyRegion.y)
                    * view.rowPitch
                + static_cast<std::size_t>(view.dirtyRegion.x) * pixelBytes;
            glTexSubImage2D(
                GL_TEXTURE_2D,
                0,
                static_cast<GLint>(view.dirtyRegion.x),
                static_cast<GLint>(view.dirtyRegion.y),
                static_cast<GLsizei>(view.dirtyRegion.width),
                static_cast<GLsizei>(view.dirtyRegion.height),
                sourceFormat,
                GL_UNSIGNED_BYTE,
                view.pixels.data() + sourceOffset);
            if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
                const henia::detail::FixedError diagnostic = error;
                transaction.rollback(destroyTexture);
                const GLenum rollbackError = rollbackPartials();
                if (restoreUploadState() != GL_NO_ERROR || rollbackError != GL_NO_ERROR) {
                    ++statistics.stateRestoreFailures;
                }
                error = diagnostic;
                assignGlFailure(
                    error,
                    "OpenGL partial texture upload failed",
                    glError,
                    "texture",
                    handle.packed());
                ++statistics.textureSynchronizationFailures;
                return false;
            }
            pendingPartialBytes += static_cast<std::uint64_t>(view.dirtyRegion.width)
                * view.dirtyRegion.height * pixelBytes;
            continue;
        }
        if (texture.object != 0 && texture.handle == handle.packed()
            && !view.fullUpdate && view.revision > texture.revision
            && view.revision - texture.revision > 1U) {
            ++pendingDirtyHistoryFallbacks;
        }
        const bool staged = transaction.stage(
            index,
            handle.packed(),
            view.revision,
            [&]() noexcept {
                GLuint candidate = 0;
                glGenTextures(1, &candidate);
                const GLenum glError = consumeOperationErrors();
                if (candidate == 0 || glError != GL_NO_ERROR) {
                    if (candidate != 0) {
                        glDeleteTextures(1, &candidate);
                    }
                    assignGlFailure(
                        error,
                        "OpenGL texture object creation failed",
                        glError,
                        "texture",
                        handle.packed());
                    return 0U;
                }
                return candidate;
            },
            [&](std::uint32_t candidate) noexcept {
                glBindTexture(GL_TEXTURE_2D, candidate);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                const GLint bytesPerPixel = view.format == TextureFormat::Alpha8 ? 1 : 4;
                glPixelStorei(kUnpackRowLength, static_cast<GLint>(view.rowPitch / bytesPerPixel));
                if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
                    assignGlFailure(
                        error,
                        "OpenGL texture configuration failed",
                        glError,
                        "texture",
                        handle.packed());
                    return false;
                }

                const GLenum internalFormat = view.format == TextureFormat::Alpha8
                    ? kR8
                    : (view.colorSpace == TextureColorSpace::Srgb ? kSrgb8Alpha8 : kRgba8);
                const GLenum sourceFormat = view.format == TextureFormat::Alpha8 ? kRed : GL_RGBA;
                if (immutableTextureStorage) {
                    gl.texStorage2D(
                        GL_TEXTURE_2D,
                        1,
                        internalFormat,
                        static_cast<GLsizei>(view.width),
                        static_cast<GLsizei>(view.height));
                    const GLenum storageError = consumeOperationErrors();
                    if (storageError == GL_INVALID_ENUM || storageError == GL_INVALID_OPERATION) {
                        immutableTextureStorage = false;
                    } else if (storageError != GL_NO_ERROR) {
                        assignGlFailure(
                            error,
                            "OpenGL immutable texture storage allocation failed",
                            storageError,
                            "texture",
                            handle.packed());
                        return false;
                    }
                }
                if (immutableTextureStorage) {
                    glTexSubImage2D(
                        GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        static_cast<GLsizei>(view.width),
                        static_cast<GLsizei>(view.height),
                        sourceFormat,
                        GL_UNSIGNED_BYTE,
                        view.pixels.data());
                } else {
                    glTexImage2D(
                        GL_TEXTURE_2D,
                        0,
                        static_cast<GLint>(internalFormat),
                        static_cast<GLsizei>(view.width),
                        static_cast<GLsizei>(view.height),
                        0,
                        sourceFormat,
                        GL_UNSIGNED_BYTE,
                        view.pixels.data());
                }
                if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
                    assignGlFailure(
                        error,
                        "OpenGL texture pixel upload failed",
                        glError,
                        "texture",
                        handle.packed());
                    return false;
                }
                return true;
            },
            destroyTexture);
        if (!staged) {
            const henia::detail::FixedError diagnostic = error;
            transaction.rollback(destroyTexture);
            const GLenum rollbackError = rollbackPartials();
            if (restoreUploadState() != GL_NO_ERROR || rollbackError != GL_NO_ERROR) {
                ++statistics.stateRestoreFailures;
            }
            static_cast<void>(consumeOperationErrors());
            error = diagnostic;
            ++statistics.textureSynchronizationFailures;
            return false;
        }
        texture.stagedByteSize = static_cast<std::uint64_t>(view.width)
            * view.height * pixelBytes;
        texture.stagedWidth = view.width;
        texture.stagedHeight = view.height;
        texture.stagedFormat = static_cast<std::uint8_t>(view.format);
        texture.stagedAlphaMode = static_cast<std::uint8_t>(view.alphaMode);
        texture.stagedColorSpace = static_cast<std::uint8_t>(view.colorSpace);
        texture.stagedExternal = false;
        texture.stagedOwned = true;
        pendingFullBytes += texture.stagedByteSize;
        if (texture.object != 0) ++replacedTextures;
    }
    if (const GLenum restoreError = restoreUploadState(); restoreError != GL_NO_ERROR) {
        transaction.rollback(destroyTexture);
        gl.bindBuffer(kPixelUnpackBuffer, 0);
        const GLenum rollbackError = rollbackPartials();
        static_cast<void>(restoreUploadState());
        static_cast<void>(consumeOperationErrors());
        ++statistics.stateRestoreFailures;
        ++statistics.textureSynchronizationFailures;
        assignGlFailure(
            error,
            rollbackError == GL_NO_ERROR
                ? "OpenGL texture upload-state restoration failed"
                : "OpenGL partial texture rollback failed",
            rollbackError == GL_NO_ERROR ? restoreError : rollbackError,
            "texture",
            0);
        return false;
    }
    const std::size_t committed = transaction.commit(destroyTexture);
    for (const std::uint32_t index : partialTextureUpdates) {
        const TextureHandle handle = store.handleAt(index);
        detail::OpenGlTextureState& texture = textures[index];
        texture.handle = handle.packed();
        texture.revision = store.view(handle).revision;
    }
    std::uint64_t directlyRetired = 0;
    for (std::size_t index = 0; index < textures.size(); ++index) {
        const TextureHandle handle = store.handleAt(index);
        detail::OpenGlTextureState& texture = textures[index];
        if (texture.object == 0 || (handle.valid() && texture.handle == handle.packed())) continue;
        if (texture.owned) destroyTexture(texture.object);
        texture = {};
        ++directlyRetired;
    }
    statistics.textureUploads += committed + partialTextureUpdates.size();
    statistics.fullTextureUploads += committed;
    statistics.partialTextureUploads += partialTextureUpdates.size();
    statistics.textureDirtyHistoryFallbacks += pendingDirtyHistoryFallbacks;
    statistics.uploadedTextureBytes += pendingFullBytes + pendingPartialBytes;
    statistics.retiredTextures += replacedTextures + directlyRetired;
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    for (const detail::OpenGlTextureState& texture : textures) {
        statistics.gpuTextureBytes += texture.byteSize;
        if (texture.object != 0 && texture.external) ++statistics.externalTextures;
    }
    if (const GLenum cleanupError = consumeOperationErrors(); cleanupError != GL_NO_ERROR) {
        ++statistics.textureSynchronizationFailures;
        assignGlFailure(
            error,
            "OpenGL replaced-texture cleanup failed after commit",
            cleanupError,
            "texture",
            0);
        return false;
    }
    error.clear();
    return true;
}

bool OpenGlRenderer::Implementation::bindExternalTexture(
    const TextureStore& store,
    TextureHandle handle,
    std::uint32_t textureObject,
    OpenGlExternalTextureOwnership ownership) noexcept {
    if (!ready) {
        error = "OpenGL renderer is not initialized";
        return false;
    }
    if (!validateOwnerContext("bindExternalTexture")) return false;
    const TextureView view = store.view(handle);
    if (!view.handle.valid() || view.backingPolicy != TextureBackingPolicy::ExternalGpu
        || handle.value() > textures.size() || textureObject == 0) {
        error = "OpenGL external texture binding is invalid";
        return false;
    }

    discardHostErrors();
    if (glIsTexture(static_cast<GLuint>(textureObject)) != GL_TRUE) {
        static_cast<void>(consumeOperationErrors());
        error = "OpenGL external texture object is not a live GL_TEXTURE_2D object";
        return false;
    }
    GLint previousTexture = 0;
    glGetIntegerv(kTextureBinding2D, &previousTexture);
    if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
        assignGlFailure(
            error,
            "OpenGL external texture state capture failed",
            glError,
            "texture",
            handle.packed());
        return false;
    }
    GLint width = 0;
    GLint height = 0;
    GLint internalFormat = 0;
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureObject));
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
    const GLenum validationError = consumeOperationErrors();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    const GLenum restoreError = consumeOperationErrors();
    if (restoreError != GL_NO_ERROR) {
        ++statistics.stateRestoreFailures;
        assignGlFailure(
            error,
            "OpenGL external texture state restoration failed",
            restoreError,
            "texture",
            handle.packed());
        return false;
    }
    if (validationError != GL_NO_ERROR) {
        assignGlFailure(
            error,
            "OpenGL external texture validation failed",
            validationError,
            "texture",
            handle.packed());
        return false;
    }
    const GLint expectedFormat = view.format == TextureFormat::Alpha8
        ? kR8
        : (view.colorSpace == TextureColorSpace::Srgb ? kSrgb8Alpha8 : kRgba8);
    if (width != static_cast<GLint>(view.width)
        || height != static_cast<GLint>(view.height)
        || internalFormat != expectedFormat) {
        error = "OpenGL external texture storage does not match its TextureStore entry";
        return false;
    }

    detail::OpenGlTextureState& texture = textures[handle.value() - 1U];
    const bool sameObject = texture.object == textureObject;
    if (texture.object != 0 && !sameObject) {
        if (texture.owned) {
            const GLuint previous = texture.object;
            glDeleteTextures(1, &previous);
            if (const GLenum glError = consumeOperationErrors(); glError != GL_NO_ERROR) {
                assignGlFailure(
                    error,
                    "OpenGL replaced external texture cleanup failed",
                    glError,
                    "texture",
                    handle.packed());
                return false;
            }
        }
        ++statistics.retiredTextures;
    }
    texture = {
        .object = textureObject,
        .handle = handle.packed(),
        .revision = view.revision,
        .byteSize = static_cast<std::uint64_t>(view.rowPitch) * view.height,
        .width = view.width,
        .height = view.height,
        .format = static_cast<std::uint8_t>(view.format),
        .alphaMode = static_cast<std::uint8_t>(view.alphaMode),
        .colorSpace = static_cast<std::uint8_t>(view.colorSpace),
        .external = true,
        .owned = ownership == OpenGlExternalTextureOwnership::Transferred,
    };
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    for (const detail::OpenGlTextureState& current : textures) {
        statistics.gpuTextureBytes += current.byteSize;
        if (current.object != 0 && current.external) ++statistics.externalTextures;
    }
    error.clear();
    return true;
}

bool OpenGlRenderer::Implementation::render(
    const RenderPacket& packet,
    UiRenderViewport viewport,
    RenderTargetColorSpace targetColorSpace) noexcept {
    const std::uint32_t width = viewport.framebufferWidth;
    const std::uint32_t height = viewport.framebufferHeight;
    const std::uint64_t frameAttemptId = ++statistics.frameAttempts;
    if (!ready) {
        ++statistics.rejectedFrames;
        error = "OpenGL renderer is not initialized";
        return false;
    }
    if (!validateOwnerContext("render")) {
        ++statistics.rejectedFrames;
        return false;
    }
    if (targetColorSpace != RenderTargetColorSpace::Linear
        && targetColorSpace != RenderTargetColorSpace::Srgb) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error = "targetColorSpace is invalid";
        return false;
    }
    if (!valid(viewport)) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error = "UI render viewport transform or framebuffer size is invalid";
        return false;
    }
    if (width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error = "viewportWidth/viewportHeight is outside the GLsizei range";
        return false;
    }
    if (packet.instances().size() > capacity
        || packet.instances().size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "Render packet exceeds the preallocated OpenGL instance capacity";
        return false;
    }
    std::size_t packetBytes = 0;
    if (!checkedMultiply(packet.instances().size(), sizeof(DrawInstance), packetBytes)
        || packetBytes > instanceBufferBytes
        || packetBytes > static_cast<std::size_t>(std::numeric_limits<GlSize>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "Render packet byte range exceeds the OpenGL instance buffer";
        return false;
    }
    const bool validationCacheHit = packetValidation.valid
        && packetValidation.identity == packet.identity()
        && packetValidation.revision == packet.revision()
        && packetValidation.semantics.size() == packet.batches().size();
    bool hasVisibleBatches = false;
    bool usesTextures = false;
    for (const DrawBatch& batch : packet.batches()) {
        const std::size_t first = batch.firstInstance;
        const std::size_t count = batch.instanceCount;
        if (!validationCacheHit && (batch.textureCount > DrawBatch::kTextureCapacity
            || first > packet.instances().size() || count > packet.instances().size() - first)) {
            ++statistics.rejectedFrames;
            ++statistics.invalidInputFrames;
            error = "Render packet batch instance/texture range is invalid";
            return false;
        }
        bool visible = batch.instanceCount != 0;
        if (batch.clip.enabled) {
            if (!validationCacheHit && !validateRect(batch.clip.area, "clip.area").empty()) {
                ++statistics.rejectedFrames;
                ++statistics.invalidInputFrames;
                error = "clip.area is invalid";
                return false;
            }
            ScissorRect scissor{};
            visible = visible && makeScissorRect(batch.clip.area, viewport, scissor);
        }
        hasVisibleBatches = hasVisibleBatches || visible;
        usesTextures = usesTextures || (visible && batch.textureCount != 0);
    }
    if (packet.instances().empty() || packet.batches().empty() || !hasVisibleBatches) {
        ++statistics.successfulFrames;
        static_cast<void>(profileTimeline.complete({
            .frameAttemptId = frameAttemptId,
            .producerIdentity = packet.identity(),
            .producerRevision = packet.revision(),
            .producerBuildNanoseconds = packet.cpuBuildNanoseconds(),
            .uploadKind = InstanceUploadKind::ZeroWorkRevision,
        }));
        statistics.profile = profileTimeline.profile();
        error.clear();
        return true;
    }

    constexpr std::uint8_t kImageSemantic = 1U << 0U;
    constexpr std::uint8_t kMaskSemantic = 1U << 1U;
    constexpr std::uint8_t kSdfSemantic = 1U << 2U;
    if (!validationCacheHit) {
        packetValidation.valid = false;
        packetValidation.semantics.assign(packet.batches().size(), {});
        for (std::size_t batchIndex = 0; batchIndex < packet.batches().size(); ++batchIndex) {
            const DrawBatch& batch = packet.batches()[batchIndex];
            for (std::size_t index = batch.firstInstance;
                 index < static_cast<std::size_t>(batch.firstInstance) + batch.instanceCount;
                 ++index) {
                const DrawInstance& instance = packet.instances()[index];
                std::uint8_t semantic = 0;
                if (instance.kind == PrimitiveKind::Image
                    || instance.kind == PrimitiveKind::NinePatch) {
                    semantic = kImageSemantic;
                } else if (instance.kind == PrimitiveKind::Glyph) {
                    semantic = kMaskSemantic;
                } else if (instance.kind == PrimitiveKind::SdfIcon) {
                    semantic = kSdfSemantic;
                }
                if (semantic == 0) continue;
                if (instance.textureSlot >= batch.textureCount) {
                    ++statistics.rejectedFrames;
                    ++statistics.invalidInputFrames;
                    error = "Render packet textured instance has an invalid texture slot";
                    return false;
                }
                packetValidation.semantics[batchIndex][instance.textureSlot] |= semantic;
            }
        }
        packetValidation.identity = packet.identity();
        packetValidation.revision = packet.revision();
        packetValidation.valid = true;
        ++statistics.packetValidationWalks;
        statistics.validatedInstances += packet.instances().size();
    } else {
        ++statistics.packetValidationCacheHits;
    }

    for (std::size_t batchIndex = 0; batchIndex < packet.batches().size(); ++batchIndex) {
        const DrawBatch& batch = packet.batches()[batchIndex];
        if (batch.clip.enabled) {
            ScissorRect scissor{};
            if (!makeScissorRect(batch.clip.area, viewport, scissor)) continue;
        }
        for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
            const TextureHandle handle = batch.textures[slot];
            if (!handle.valid() || handle.value() > textures.size()
                || textures[handle.value() - 1U].object == 0
                || textures[handle.value() - 1U].handle != handle.packed()) {
                ++statistics.rejectedFrames;
                error = "Render packet references an unsynchronized texture";
                return false;
            }
            const detail::OpenGlTextureState& texture =
                textures[handle.value() - 1U];
            const std::uint8_t semantic = packetValidation.semantics[batchIndex][slot];
            const bool alphaMask = texture.alphaMode
                == static_cast<std::uint8_t>(TextureAlphaMode::AlphaMask);
            const bool premultiplied = texture.alphaMode
                == static_cast<std::uint8_t>(TextureAlphaMode::Premultiplied);
            const bool srgb = texture.colorSpace
                == static_cast<std::uint8_t>(TextureColorSpace::Srgb);
            if (((semantic & kImageSemantic) != 0 && alphaMask)
                || ((semantic & kMaskSemantic) != 0 && !alphaMask)
                || ((semantic & kSdfSemantic) != 0 && (premultiplied || srgb))) {
                ++statistics.rejectedFrames;
                ++statistics.invalidInputFrames;
                error = "Render packet primitive is incompatible with its texture semantics";
                return false;
            }
        }
    }

    const henia::detail::UploadSelection upload = uploadRing.select(
        packet.identity(),
        packet.revision(),
        false,
        [this](std::size_t index) noexcept { return pollUploadSlot(index); });
    if (upload.kind == henia::detail::UploadSelectionKind::Exhausted) {
        ++statistics.uploadSlotExhaustions;
        ++statistics.rejectedFrames;
        error = "OpenGL instance upload ring has no fence-safe slot";
        return false;
    }
    UploadSlot& uploadSlot = uploadSlots[upload.slot];

    const bool preserveState = statePolicy == OpenGlUiStatePolicy::Preserve;
    discardHostErrors();
    GlState previous{};
    if (preserveState) {
        previous = captureState();
        ++statistics.stateCaptures;
        if (consumeOperationErrors() != GL_NO_ERROR) {
            ++statistics.rejectedFrames;
            error = "OpenGL UI state capture failed";
            return false;
        }
    } else {
        ++statistics.dedicatedContextFrames;
    }
    const auto restoreIfNeeded = [&]() noexcept {
        if (!preserveState) return true;
        ++statistics.stateRestorations;
        return restoreState(previous);
    };
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    if (targetColorSpace == RenderTargetColorSpace::Srgb) glEnable(kFramebufferSrgb);
    else glDisable(kFramebufferSrgb);
    glDisable(kRasterizerDiscard);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_DITHER);
    glEnable(kMultisample);
    glDisable(kSampleAlphaToCoverage);
    glDisable(kSampleAlphaToOne);
    glDisable(kSampleCoverage);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.colorMaskIndexed(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    gl.blendEquationSeparate(kFunctionAdd, kFunctionAdd);
    gl.useProgram(program);
    gl.uniform2f(viewportLocation, static_cast<float>(width), static_cast<float>(height));
    gl.uniform2f(
        logicalScaleLocation,
        viewport.logicalToFramebuffer.scale.x,
        viewport.logicalToFramebuffer.scale.y);
    gl.uniform2f(
        logicalTranslationLocation,
        viewport.logicalToFramebuffer.translation.x,
        viewport.logicalToFramebuffer.translation.y);
    gl.uniform1f(
        minimumAntialiasWidthLocation,
        0.75F / std::max(
            viewport.logicalToFramebuffer.scale.x,
            viewport.logicalToFramebuffer.scale.y));
    if (usesTextures) {
        constexpr std::array<GLint, DrawBatch::kTextureCapacity> textureUnits{0, 1, 2, 3, 4, 5, 6, 7};
        gl.uniform1iv(
            texturesLocation,
            static_cast<GLsizei>(textureUnits.size()),
            textureUnits.data());
        for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
            gl.bindSampler(slot, 0);
        }
    }
    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, uploadSlot.buffer);
    if (baseInstanceAvailable && configuredAttributeBuffer != uploadSlot.buffer) {
        configureAttributes(0, false);
        configuredAttributeBuffer = uploadSlot.buffer;
    }
    if (const GLenum setupError = consumeOperationErrors(); setupError != GL_NO_ERROR) {
        const bool restored = restoreIfNeeded();
        ++statistics.rejectedFrames;
        if (restored) {
            assignGlFailure(
                error,
                "OpenGL UI pipeline setup failed",
                setupError,
                "uploadSlot",
                upload.slot);
        }
        return false;
    }

    std::uint64_t cpuUploadNanoseconds = 0;
    if (upload.requiresUpload()) {
        const auto uploadStarted = std::chrono::steady_clock::now();
        if (uploadStrategy == OpenGlUploadStrategy::PersistentMap) {
            if (uploadSlot.mapped == nullptr) {
                const bool restored = restoreIfNeeded();
                ++statistics.rejectedFrames;
                if (restored) error = "OpenGL persistent instance mapping is unavailable";
                return false;
            }
            std::memcpy(uploadSlot.mapped, packet.instances().data(), packetBytes);
            ++statistics.persistentInstanceCopies;
        } else {
            void* mapped = gl.mapBufferRange(
                kArrayBuffer,
                0,
                static_cast<GlSize>(packetBytes),
                kMapWriteBit | kMapUnsynchronizedBit);
            ++statistics.instanceMapOperations;
            const GLenum mapError = consumeOperationErrors();
            if (mapped == nullptr || mapError != GL_NO_ERROR) {
                if (mapped != nullptr) {
                    static_cast<void>(gl.unmapBuffer(kArrayBuffer));
                    ++statistics.instanceUnmapOperations;
                    static_cast<void>(consumeOperationErrors());
                }
                const bool restored = restoreIfNeeded();
                ++statistics.rejectedFrames;
                if (restored) {
                    assignGlFailure(
                        error,
                        "OpenGL UI instance-buffer mapping failed",
                        mapError,
                        "uploadSlot",
                        upload.slot);
                }
                return false;
            }
            std::memcpy(mapped, packet.instances().data(), packetBytes);
            const GLboolean unmapped = gl.unmapBuffer(kArrayBuffer);
            ++statistics.instanceUnmapOperations;
            const GLenum unmapError = consumeOperationErrors();
            if (unmapped != GL_TRUE || unmapError != GL_NO_ERROR) {
                uploadRing.invalidate(upload.slot);
                const bool restored = restoreIfNeeded();
                ++statistics.rejectedFrames;
                if (restored) {
                    assignGlFailure(
                        error,
                        "OpenGL UI instance-buffer unmap failed",
                        unmapError,
                        "uploadSlot",
                        upload.slot);
                }
                return false;
            }
        }
        uploadRing.markUploaded(upload.slot, packet.identity(), packet.revision());
        ++statistics.instanceUploads;
        statistics.uploadedInstanceBytes += packetBytes;
        cpuUploadNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - uploadStarted).count());
    }

    const auto submitStarted = std::chrono::steady_clock::now();
    bool submitted = false;
    std::array<GLuint, DrawBatch::kTextureCapacity> boundTextureObjects{};
    boundTextureObjects.fill(std::numeric_limits<GLuint>::max());
    std::array<GLint, DrawBatch::kTextureCapacity> boundAlphaModes{};
    boundAlphaModes.fill(-1);
    std::optional<bool> activeTexturePath;
    for (const DrawBatch& batch : packet.batches()) {
        if (batch.instanceCount == 0) {
            continue;
        }
        ScissorRect scissor{};
        if (batch.clip.enabled
            && !makeScissorRect(batch.clip.area, viewport, scissor)) {
            continue;
        }
        const bool batchUsesTextures = batch.textureCount != 0;
        if (!activeTexturePath.has_value() || *activeTexturePath != batchUsesTextures) {
            activeTexturePath = batchUsesTextures;
            ++statistics.texturePathRuns;
        }
        if (batchUsesTextures) ++statistics.texturedBatches;
        else ++statistics.textureFreeBatches;
        if (batch.blend == BlendMode::Additive) {
            gl.blendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        } else {
            gl.blendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }

        if (batch.clip.enabled) {
            glScissor(
                static_cast<GLint>(scissor.left),
                static_cast<GLint>(height - static_cast<std::uint32_t>(scissor.bottom)),
                static_cast<GLsizei>(scissor.right - scissor.left),
                static_cast<GLsizei>(scissor.bottom - scissor.top));
        } else {
            glScissor(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        }

        if (batchUsesTextures) {
            std::array<GLint, DrawBatch::kTextureCapacity> alphaModes{};
            for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
                const GLuint object = slot < batch.textureCount
                    ? textures[batch.textures[slot].value() - 1U].object
                    : 0;
                if (boundTextureObjects[slot] != object) {
                    gl.activeTexture(kTexture0 + slot);
                    glBindTexture(GL_TEXTURE_2D, object);
                    boundTextureObjects[slot] = object;
                    ++statistics.textureBindingChanges;
                }
                if (slot < batch.textureCount) {
                    alphaModes[slot] = static_cast<GLint>(
                        textures[batch.textures[slot].value() - 1U].alphaMode);
                }
            }
            if (boundAlphaModes != alphaModes) {
                gl.uniform1iv(
                    textureAlphaModesLocation,
                    static_cast<GLsizei>(alphaModes.size()),
                    alphaModes.data());
                boundAlphaModes = alphaModes;
                ++statistics.textureAlphaModeUploads;
            }
        }

        if (baseInstanceAvailable) {
            gl.drawArraysInstancedBaseInstance(
                GL_TRIANGLE_STRIP,
                0,
                4,
                static_cast<GLsizei>(batch.instanceCount),
                batch.firstInstance);
            ++statistics.baseInstanceDraws;
        } else {
            configureAttributes(batch.firstInstance, false);
            configuredAttributeBuffer = uploadSlot.buffer;
            gl.drawArraysInstanced(
                GL_TRIANGLE_STRIP,
                0,
                4,
                static_cast<GLsizei>(batch.instanceCount));
        }
        submitted = true;
        ++statistics.drawCalls;
        statistics.submittedInstances += batch.instanceCount;
        statistics.generatedVertices += static_cast<std::uint64_t>(batch.instanceCount) * 4U;
    }

    if (const GLenum submissionError = consumeOperationErrors(); submissionError != GL_NO_ERROR) {
        const bool restored = restoreIfNeeded();
        ++statistics.rejectedFrames;
        if (restored) {
            assignGlFailure(
                error,
                "OpenGL UI batch draw submission failed",
                submissionError,
                "uploadSlot",
                upload.slot);
        }
        return false;
    }

    if (submitted && !fenceUploadSlot(upload.slot)) {
        static_cast<void>(restoreIfNeeded());
        ++statistics.rejectedFrames;
        return false;
    }

    if (!restoreIfNeeded()) {
        ++statistics.rejectedFrames;
        return false;
    }
    const std::uint64_t cpuDrawSubmitNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - submitStarted).count());
    ++statistics.successfulFrames;
    static_cast<void>(profileTimeline.complete({
        .frameAttemptId = frameAttemptId,
        .producerIdentity = packet.identity(),
        .producerRevision = packet.revision(),
        .producerBuildNanoseconds = packet.cpuBuildNanoseconds(),
        .cpuUploadNanoseconds = cpuUploadNanoseconds,
        .cpuDrawSubmitNanoseconds = cpuDrawSubmitNanoseconds,
        .uploadedInstanceBytes = upload.requiresUpload() ? packetBytes : 0,
        .uploadRangeCount = upload.requiresUpload() ? 1U : 0U,
        .submissionSlot = static_cast<std::uint32_t>(upload.slot),
        .uploadKind = upload.requiresUpload()
            ? InstanceUploadKind::Full
            : InstanceUploadKind::None,
    }));
    statistics.profile = profileTimeline.profile();
    error.clear();
    return true;
}

henia::detail::UploadFenceStatus OpenGlRenderer::Implementation::pollUploadSlot(
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

bool OpenGlRenderer::Implementation::fenceUploadSlot(std::size_t index) noexcept {
    UploadSlot& slot = uploadSlots[index];
    GlSync fence = gl.fenceSync(kSyncGpuCommandsComplete, 0);
    const GLenum fenceError = consumeOperationErrors();
    if (fence == nullptr || fenceError != GL_NO_ERROR) {
        if (slot.fence != nullptr) {
            gl.deleteSync(slot.fence);
            slot.fence = nullptr;
        }
        uploadRing.markFenceFailed(index);
        ++statistics.uploadFenceFailures;
        assignGlFailure(
            error,
            "OpenGL UI upload fence creation failed",
            fenceError,
            "uploadSlot",
            index);
        return false;
    }
    if (slot.fence != nullptr) {
        gl.deleteSync(slot.fence);
    }
    slot.fence = fence;
    uploadRing.markSubmitted(index);
    return true;
}

bool OpenGlRenderer::Implementation::shutdown() noexcept {
    const bool hadOwner = ownerContext != nullptr;
    if (hadOwner && wglGetCurrentContext() != ownerContext) {
        ++statistics.wrongContextCalls;
        error = "OpenGL UI shutdown requires the initialize() context; resources were preserved";
        return false;
    }
    if (hadOwner) {
        discardHostErrors();
        for (detail::OpenGlTextureState& texture : textures) {
            if (texture.object != 0 && texture.owned) {
                const GLuint object = texture.object;
                glDeleteTextures(1, &object);
            }
            if (texture.stagedObject != 0 && texture.stagedOwned) {
                const GLuint object = texture.stagedObject;
                glDeleteTextures(1, &object);
            }
        }
        for (UploadSlot& slot : uploadSlots) {
            if (slot.fence != nullptr && gl.deleteSync != nullptr) {
                gl.deleteSync(slot.fence);
                slot.fence = nullptr;
            }
            if (slot.buffer != 0 && slot.mapped != nullptr && gl.unmapBuffer != nullptr) {
                gl.bindBuffer(kArrayBuffer, slot.buffer);
                static_cast<void>(gl.unmapBuffer(kArrayBuffer));
                slot.mapped = nullptr;
                ++statistics.instanceUnmapOperations;
            }
            if (slot.buffer != 0 && gl.deleteBuffers != nullptr) {
                gl.deleteBuffers(1, &slot.buffer);
            }
        }
        if (vertexArray != 0 && gl.deleteVertexArrays != nullptr) {
            gl.deleteVertexArrays(1, &vertexArray);
        }
        if (program != 0 && gl.deleteProgram != nullptr) {
            gl.deleteProgram(program);
        }
    }
    textures.clear();
    partialTextureUpdates.clear();
    uploadSlots.clear();
    uploadRing.clear();
    program = 0;
    vertexArray = 0;
    viewportLocation = -1;
    logicalScaleLocation = -1;
    logicalTranslationLocation = -1;
    minimumAntialiasWidthLocation = -1;
    texturesLocation = -1;
    textureAlphaModesLocation = -1;
    maximumTextureSize = 0;
    capacity = 0;
    instanceBufferBytes = 0;
    ownerContext = nullptr;
    statePolicy = OpenGlUiStatePolicy::Preserve;
    uploadStrategy = OpenGlUploadStrategy::TransientMap;
    configuredAttributeBuffer = 0;
    baseInstanceAvailable = false;
    packetValidation = {};
    immutableTextureStorage = false;
    ready = false;
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    if (hadOwner && consumeOperationErrors() != GL_NO_ERROR) {
        error = "OpenGL UI resource destruction generated an error";
        return false;
    }
    error.clear();
    return true;
}

void OpenGlRenderer::Implementation::abandon() noexcept {
    const bool hadOwner = ownerContext != nullptr;
    textures.clear();
    partialTextureUpdates.clear();
    uploadSlots.clear();
    uploadRing.clear();
    program = 0;
    vertexArray = 0;
    viewportLocation = -1;
    logicalScaleLocation = -1;
    logicalTranslationLocation = -1;
    minimumAntialiasWidthLocation = -1;
    texturesLocation = -1;
    textureAlphaModesLocation = -1;
    maximumTextureSize = 0;
    capacity = 0;
    instanceBufferBytes = 0;
    ownerContext = nullptr;
    statePolicy = OpenGlUiStatePolicy::Preserve;
    uploadStrategy = OpenGlUploadStrategy::TransientMap;
    configuredAttributeBuffer = 0;
    baseInstanceAvailable = false;
    packetValidation = {};
    immutableTextureStorage = false;
    ready = false;
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    if (hadOwner) {
        ++statistics.abandonedContexts;
    }
    error.clear();
}

bool OpenGlRenderer::Implementation::validateOwnerContext(const char* operation) noexcept {
    static_cast<void>(operation);
    if (ownerContext != nullptr && wglGetCurrentContext() == ownerContext) {
        return true;
    }
    ++statistics.wrongContextCalls;
    error = "OpenGL UI call requires the exact context used by initialize()";
    return false;
}

void OpenGlRenderer::Implementation::discardHostErrors() noexcept {
    std::uint64_t discarded = 0;
    while (discarded < 64 && glGetError() != GL_NO_ERROR) {
        ++discarded;
    }
    statistics.ignoredHostErrors += discarded;
}

void OpenGlRenderer::Implementation::configureAttributes(
    std::size_t firstInstance,
    bool configureInvariant) noexcept {
    std::size_t base = 0;
    static_cast<void>(checkedMultiply(firstInstance, sizeof(DrawInstance), base));
    const auto pointer = [base](std::size_t memberOffset) {
        return reinterpret_cast<const void*>(base + memberOffset);
    };
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(DrawInstance));

    gl.vertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, bounds)));
    gl.vertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, uv)));
    gl.vertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, color)));
    gl.vertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, radius)));
    gl.vertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, stride, pointer(offsetof(DrawInstance, kind)));
    ++statistics.attributeOffsetUpdates;
    if (configureInvariant) {
        for (GLuint index = 0; index <= 4; ++index) {
            gl.enableVertexAttribArray(index);
            gl.vertexAttribDivisor(index, 1);
        }
        ++statistics.attributeFormatConfigurations;
    }
}

GlState OpenGlRenderer::Implementation::captureState() const noexcept {
    GlState state{};
    glGetIntegerv(kCurrentProgram, &state.program);
    glGetIntegerv(kVertexArrayBinding, &state.vertexArray);
    glGetIntegerv(kArrayBufferBinding, &state.arrayBuffer);
    glGetIntegerv(kActiveTexture, &state.activeTexture);
    glGetIntegerv(GL_VIEWPORT, state.viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor.data());
    glGetIntegerv(kBlendSourceRgb, &state.sourceRgb);
    glGetIntegerv(kBlendDestinationRgb, &state.destinationRgb);
    glGetIntegerv(kBlendSourceAlpha, &state.sourceAlpha);
    glGetIntegerv(kBlendDestinationAlpha, &state.destinationAlpha);
    glGetIntegerv(kBlendEquationRgb, &state.equationRgb);
    glGetIntegerv(kBlendEquationAlpha, &state.equationAlpha);
    glGetIntegerv(GL_POLYGON_MODE, state.polygonMode.data());
    henia::detail::normalizeCapturedPolygonModes(state.polygonMode);
    gl.getBooleanIndexed(GL_COLOR_WRITEMASK, 0, state.colorMask.data());
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
    for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
        gl.activeTexture(kTexture0 + slot);
        glGetIntegerv(kTextureBinding2D, &state.textures[slot]);
        glGetIntegerv(kSamplerBinding, &state.samplers[slot]);
    }
    gl.activeTexture(static_cast<GLenum>(state.activeTexture));
    return state;
}

bool OpenGlRenderer::Implementation::restoreState(const GlState& state) noexcept {
    GLenum firstError = GL_NO_ERROR;
    const char* failureCategory = nullptr;
    const auto captureRestoreError = [&](const char* category) noexcept {
        const GLenum current = consumeOperationErrors();
        if (firstError == GL_NO_ERROR && current != GL_NO_ERROR) {
            firstError = current;
            failureCategory = category;
        }
    };
    for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
        gl.activeTexture(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[slot]));
        gl.bindSampler(slot, static_cast<GLuint>(state.samplers[slot]));
    }
    gl.activeTexture(static_cast<GLenum>(state.activeTexture));
    captureRestoreError("OpenGL UI texture/sampler state restoration failed");
    gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(state.arrayBuffer));
    gl.bindVertexArray(static_cast<GLuint>(state.vertexArray));
    const GLuint savedProgram = static_cast<GLuint>(state.program);
    gl.useProgram(savedProgram == 0 || gl.isProgram(savedProgram) == GL_TRUE ? savedProgram : 0);
    captureRestoreError("OpenGL UI object binding restoration failed");
    gl.blendFuncSeparate(
        static_cast<GLenum>(state.sourceRgb),
        static_cast<GLenum>(state.destinationRgb),
        static_cast<GLenum>(state.sourceAlpha),
        static_cast<GLenum>(state.destinationAlpha));
    gl.blendEquationSeparate(
        static_cast<GLenum>(state.equationRgb),
        static_cast<GLenum>(state.equationAlpha));
    captureRestoreError("OpenGL UI blend state restoration failed");
    if (state.polygonMode[0] == state.polygonMode[1]) {
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(state.polygonMode[0]));
    } else {
        glPolygonMode(GL_FRONT, static_cast<GLenum>(state.polygonMode[0]));
        glPolygonMode(GL_BACK, static_cast<GLenum>(state.polygonMode[1]));
    }
    captureRestoreError("OpenGL UI polygon-mode restoration failed");
    gl.colorMaskIndexed(
        0, state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
    captureRestoreError("OpenGL UI color-mask restoration failed");
    (state.blend == GL_TRUE ? glEnable : glDisable)(GL_BLEND);
    (state.scissorTest == GL_TRUE ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (state.depthTest == GL_TRUE ? glEnable : glDisable)(GL_DEPTH_TEST);
    (state.cullFace == GL_TRUE ? glEnable : glDisable)(GL_CULL_FACE);
    (state.stencilTest == GL_TRUE ? glEnable : glDisable)(GL_STENCIL_TEST);
    (state.framebufferSrgb == GL_TRUE ? glEnable : glDisable)(kFramebufferSrgb);
    (state.rasterizerDiscard == GL_TRUE ? glEnable : glDisable)(kRasterizerDiscard);
    (state.colorLogicOp == GL_TRUE ? glEnable : glDisable)(GL_COLOR_LOGIC_OP);
    (state.dither == GL_TRUE ? glEnable : glDisable)(GL_DITHER);
    (state.multisample == GL_TRUE ? glEnable : glDisable)(kMultisample);
    (state.sampleAlphaToCoverage == GL_TRUE ? glEnable : glDisable)(kSampleAlphaToCoverage);
    (state.sampleAlphaToOne == GL_TRUE ? glEnable : glDisable)(kSampleAlphaToOne);
    (state.sampleCoverage == GL_TRUE ? glEnable : glDisable)(kSampleCoverage);
    captureRestoreError("OpenGL UI capability restoration failed");
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]);
    captureRestoreError("OpenGL UI viewport/scissor restoration failed");
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

OpenGlRenderer::OpenGlRenderer() : mImplementation(std::make_unique<Implementation>()) {}

OpenGlRenderer::~OpenGlRenderer() { static_cast<void>(mImplementation->shutdown()); }

bool OpenGlRenderer::initialize(
    std::size_t instanceCapacity,
    std::size_t textureCapacityValue,
    std::size_t uploadSlotCountValue,
    OpenGlUiStatePolicy statePolicy,
    OpenGlUploadStrategy uploadStrategy) noexcept {
    try {
        const bool wasReady = mImplementation->ready;
        const bool initialized = mImplementation->initialize(
            instanceCapacity,
            textureCapacityValue,
            uploadSlotCountValue,
            statePolicy,
            uploadStrategy);
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
        mImplementation->error = "OpenGL renderer initialization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool OpenGlRenderer::synchronizeTextures(TextureStore& textures) noexcept {
    return mImplementation->synchronizeTextures(textures);
}

bool OpenGlRenderer::bindExternalTexture(
    const TextureStore& textures,
    TextureHandle handle,
    std::uint32_t textureObject,
    OpenGlExternalTextureOwnership ownership) noexcept {
    return mImplementation->bindExternalTexture(textures, handle, textureObject, ownership);
}

bool OpenGlRenderer::render(
    const RenderPacket& packet,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    RenderTargetColorSpace targetColorSpace) noexcept {
    return render(
        packet,
        {
            .framebufferWidth = viewportWidth,
            .framebufferHeight = viewportHeight,
        },
        targetColorSpace);
}

bool OpenGlRenderer::render(
    const RenderPacket& packet,
    UiRenderViewport viewport,
    RenderTargetColorSpace targetColorSpace) noexcept {
    return mImplementation->render(packet, viewport, targetColorSpace);
}
bool OpenGlRenderer::reportGpuTime(
    std::uint64_t sampleId,
    std::uint64_t nanoseconds) noexcept {
    if (!mImplementation->ready) return false;
    const bool reported = mImplementation->profileTimeline.reportGpuTime(sampleId, nanoseconds);
    mImplementation->statistics.profile = mImplementation->profileTimeline.profile();
    return reported;
}

bool OpenGlRenderer::shutdown() noexcept { return mImplementation->shutdown(); }

void OpenGlRenderer::abandon() noexcept { mImplementation->abandon(); }

bool OpenGlRenderer::initialized() const noexcept { return mImplementation->ready; }

std::size_t OpenGlRenderer::instanceCapacity() const noexcept { return mImplementation->capacity; }

std::size_t OpenGlRenderer::textureCapacity() const noexcept {
    return mImplementation->textures.size();
}

std::size_t OpenGlRenderer::uploadSlotCount() const noexcept {
    return mImplementation->uploadSlots.size();
}

OpenGlRenderStatistics OpenGlRenderer::statistics() const noexcept { return mImplementation->statistics; }

std::string_view OpenGlRenderer::lastError() const noexcept { return mImplementation->error.view(); }

} // namespace henia::ui
