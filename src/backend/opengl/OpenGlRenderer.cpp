#include "henia/ui/backend/opengl/OpenGlRenderer.h"

#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY*
#endif

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace henia::ui {
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
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kActiveTexture = 0x84E0;
constexpr GLenum kTextureBinding2D = 0x8069;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kR8 = 0x8229;
constexpr GLenum kRed = 0x1903;
constexpr GLenum kRgba8 = 0x8058;
constexpr GLenum kMapWriteBit = 0x0002;
constexpr GLenum kMapInvalidateBufferBit = 0x0008;
constexpr GLenum kMapUnsynchronizedBit = 0x0020;
constexpr GLenum kBlendSourceRgb = 0x80C9;
constexpr GLenum kBlendDestinationRgb = 0x80C8;
constexpr GLenum kBlendSourceAlpha = 0x80CB;
constexpr GLenum kBlendDestinationAlpha = 0x80CA;
constexpr GLenum kUnpackRowLength = 0x0CF2;

static_assert(std::is_standard_layout_v<DrawInstance>);
static_assert(offsetof(DrawInstance, pointB) == offsetof(DrawInstance, pointA) + sizeof(Vec2));
static_assert(offsetof(DrawInstance, thickness) == offsetof(DrawInstance, radius) + sizeof(float));

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
using Uniform2fFn = void(APIENTRYP)(GLint, GLfloat, GLfloat);
using Uniform1ivFn = void(APIENTRYP)(GLint, GLsizei, const GLint*);
using ActiveTextureFn = void(APIENTRYP)(GLenum);
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
    Uniform2fFn uniform2f = nullptr;
    Uniform1ivFn uniform1iv = nullptr;
    ActiveTextureFn activeTexture = nullptr;
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
    return load(gl.createShader, "glCreateShader")
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
        && load(gl.enableVertexAttribArray, "glEnableVertexAttribArray")
        && load(gl.vertexAttribPointer, "glVertexAttribPointer")
        && load(gl.vertexAttribIPointer, "glVertexAttribIPointer")
        && load(gl.vertexAttribDivisor, "glVertexAttribDivisor")
        && load(gl.getUniformLocation, "glGetUniformLocation")
        && load(gl.uniform2f, "glUniform2f")
        && load(gl.uniform1iv, "glUniform1iv")
        && load(gl.activeTexture, "glActiveTexture")
        && load(gl.drawArraysInstanced, "glDrawArraysInstanced")
        && load(gl.blendFuncSeparate, "glBlendFuncSeparate");
}

constexpr const char* kVertexShaderSource = R"glsl(
#version 330 core
layout(location = 0) in vec4 instanceBounds;
layout(location = 1) in vec4 instanceUv;
layout(location = 2) in vec4 instancePoints;
layout(location = 3) in vec4 instanceColor;
layout(location = 4) in vec2 instanceMetrics;
layout(location = 5) in uint instanceTextureSlot;
layout(location = 6) in uint instanceKind;

uniform vec2 viewportSize;

out vec2 pixelPosition;
out vec2 localPosition;
out vec2 primitiveSize;
out vec2 textureUv;
out vec4 tintColor;
out vec4 linePoints;
out vec2 shapeMetrics;
flat out uint textureSlot;
flat out uint primitiveKind;

const vec2 corners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    vec2 corner = corners[gl_VertexID];
    vec2 pixel = mix(instanceBounds.xy, instanceBounds.zw, corner);
    vec2 normalized = pixel / viewportSize;
    gl_Position = vec4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);

    pixelPosition = pixel;
    localPosition = corner;
    primitiveSize = instanceBounds.zw - instanceBounds.xy;
    textureUv = mix(instanceUv.xy, instanceUv.zw, corner);
    tintColor = instanceColor;
    linePoints = instancePoints;
    shapeMetrics = instanceMetrics;
    textureSlot = instanceTextureSlot;
    primitiveKind = instanceKind;
}
)glsl";

constexpr const char* kFragmentShaderSource = R"glsl(
#version 330 core
in vec2 pixelPosition;
in vec2 localPosition;
in vec2 primitiveSize;
in vec2 textureUv;
in vec4 tintColor;
in vec4 linePoints;
in vec2 shapeMetrics;
flat in uint textureSlot;
flat in uint primitiveKind;

uniform sampler2D textures[8];
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

float roundedBoxDistance(vec2 point, vec2 halfSize, float radius) {
    vec2 q = abs(point) - halfSize + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

float segmentDistance(vec2 point, vec2 start, vec2 finish) {
    vec2 segment = finish - start;
    float denominator = max(dot(segment, segment), 0.0001);
    float projection = clamp(dot(point - start, segment) / denominator, 0.0, 1.0);
    return length(point - (start + projection * segment));
}

vec4 premultiply(vec4 color) {
    return vec4(color.rgb * color.a, color.a);
}

void main() {
    float coverage = 1.0;
    vec4 color = tintColor;

    if (primitiveKind == 0u || primitiveKind == 1u) {
        vec2 centered = (localPosition - vec2(0.5)) * primitiveSize;
        float distanceToEdge = roundedBoxDistance(
            centered,
            primitiveSize * 0.5,
            min(shapeMetrics.x, min(primitiveSize.x, primitiveSize.y) * 0.5));
        float antiAlias = max(fwidth(distanceToEdge), 0.75);
        float outer = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        if (primitiveKind == 1u) {
            float innerDistance = distanceToEdge + max(shapeMetrics.y, 0.0);
            float inner = 1.0 - smoothstep(-antiAlias, antiAlias, innerDistance);
            coverage = max(outer - inner, 0.0);
        } else {
            coverage = outer;
        }
    } else if (primitiveKind == 2u) {
        float distanceToLine = segmentDistance(pixelPosition, linePoints.xy, linePoints.zw);
        float antiAlias = max(fwidth(distanceToLine), 0.75);
        coverage = 1.0 - smoothstep(
            shapeMetrics.y * 0.5 - antiAlias,
            shapeMetrics.y * 0.5 + antiAlias,
            distanceToLine);
    } else if (primitiveKind == 3u) {
        color *= sampleTexture(textureSlot, textureUv);
    } else if (primitiveKind == 4u) {
        color.a *= sampleTexture(textureSlot, textureUv).r;
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
    std::string& error) {
    const GLuint shader = gl.createShader(type);
    if (shader == 0) {
        error = "OpenGL failed to create a shader";
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
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
    gl.getShaderInfoLog(shader, length, nullptr, log.data());
    error.assign(log.data());
    gl.deleteShader(shader);
    return 0;
}

struct GlState final {
    GLint program = 0;
    GLint vertexArray = 0;
    GLint arrayBuffer = 0;
    GLint activeTexture = 0;
    std::array<GLint, DrawBatch::kTextureCapacity> textures{};
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    GLint sourceRgb = 0;
    GLint destinationRgb = 0;
    GLint sourceAlpha = 0;
    GLint destinationAlpha = 0;
    GLboolean blend = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
};

} // namespace

struct OpenGlRenderer::Implementation final {
    struct GpuTexture final {
        GLuint object = 0;
        std::uint64_t revision = 0;
    };

    GlFunctions gl{};
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint instanceBuffer = 0;
    GLint viewportLocation = -1;
    GLint texturesLocation = -1;
    std::size_t capacity = 0;
    std::uint64_t uploadedIdentity = 0;
    std::uint64_t uploadedRevision = 0;
    std::vector<GpuTexture> textures;
    OpenGlRenderStatistics statistics{};
    std::string error;
    bool ready = false;

    [[nodiscard]] bool initialize(std::size_t requestedCapacity) noexcept;
    [[nodiscard]] bool synchronizeTextures(const TextureStore& store) noexcept;
    [[nodiscard]] bool render(
        const RenderPacket& packet,
        std::uint32_t width,
        std::uint32_t height) noexcept;
    void shutdown() noexcept;
    void configureAttributes(std::size_t firstInstance) const noexcept;
    [[nodiscard]] GlState captureState() const noexcept;
    void restoreState(const GlState& state) const noexcept;
};

bool OpenGlRenderer::Implementation::initialize(std::size_t requestedCapacity) noexcept {
    if (ready) {
        return true;
    }
    if (wglGetCurrentContext() == nullptr || requestedCapacity == 0
        || requestedCapacity > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        error = "OpenGL 3.3 context and a non-zero instance capacity are required";
        return false;
    }
    if (!loadFunctions(gl)) {
        error = "OpenGL 3.3 entry points are unavailable";
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
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)));
        gl.getProgramInfoLog(program, length, nullptr, log.data());
        error.assign(log.data());
        gl.deleteProgram(program);
        program = 0;
        return false;
    }

    viewportLocation = gl.getUniformLocation(program, "viewportSize");
    texturesLocation = gl.getUniformLocation(program, "textures");
    if (viewportLocation < 0 || texturesLocation < 0) {
        error = "HeniaUI shader uniforms are unavailable";
        shutdown();
        return false;
    }

    gl.genVertexArrays(1, &vertexArray);
    gl.genBuffers(1, &instanceBuffer);
    if (vertexArray == 0 || instanceBuffer == 0) {
        error = "OpenGL failed to allocate renderer buffers";
        shutdown();
        return false;
    }

    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, instanceBuffer);
    gl.bufferData(
        kArrayBuffer,
        static_cast<GlSize>(requestedCapacity * sizeof(DrawInstance)),
        nullptr,
        kDynamicDraw);
    configureAttributes(0);
    gl.bindBuffer(kArrayBuffer, 0);
    gl.bindVertexArray(0);

    capacity = requestedCapacity;
    ready = true;
    error.clear();
    return true;
}

bool OpenGlRenderer::Implementation::synchronizeTextures(const TextureStore& store) noexcept {
    if (!ready || wglGetCurrentContext() == nullptr) {
        error = "Texture synchronization requires the renderer context";
        return false;
    }
    if (textures.size() < store.size()) {
        textures.resize(store.size());
    }

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 0;
    GLint previousUnpackRowLength = 0;
    glGetIntegerv(kTextureBinding2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glGetIntegerv(kUnpackRowLength, &previousUnpackRowLength);
    const auto restoreUploadState = [&]() {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
        glPixelStorei(kUnpackRowLength, previousUnpackRowLength);
    };

    for (std::uint32_t value = 1; value <= store.size(); ++value) {
        const TextureView view = store.view(TextureHandle{value});
        if (!view.handle.valid()) {
            continue;
        }
        GpuTexture& texture = textures[value - 1U];
        if (texture.object != 0 && texture.revision == view.revision) {
            continue;
        }
        if (texture.object == 0) {
            glGenTextures(1, &texture.object);
        }
        if (texture.object == 0) {
            error = "OpenGL failed to create a texture";
            restoreUploadState();
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, texture.object);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const GLint bytesPerPixel = view.format == TextureFormat::Alpha8 ? 1 : 4;
        glPixelStorei(kUnpackRowLength, static_cast<GLint>(view.rowPitch / bytesPerPixel));
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            view.format == TextureFormat::Alpha8 ? static_cast<GLint>(kR8) : static_cast<GLint>(kRgba8),
            static_cast<GLsizei>(view.width),
            static_cast<GLsizei>(view.height),
            0,
            view.format == TextureFormat::Alpha8 ? kRed : GL_RGBA,
            GL_UNSIGNED_BYTE,
            view.pixels.data());
        glPixelStorei(kUnpackRowLength, 0);
        texture.revision = view.revision;
        ++statistics.textureUploads;
    }
    restoreUploadState();
    error.clear();
    return true;
}

bool OpenGlRenderer::Implementation::render(
    const RenderPacket& packet,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    if (!ready || wglGetCurrentContext() == nullptr || width == 0 || height == 0
        || packet.instances().size() > capacity
        || packet.instances().size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        ++statistics.rejectedFrames;
        error = packet.instances().size() > capacity
            ? "Render packet exceeds the preallocated OpenGL instance capacity"
            : "OpenGL render prerequisites are unavailable";
        return false;
    }
    if (packet.instances().empty() || packet.batches().empty()) {
        ++statistics.frames;
        error.clear();
        return true;
    }

    for (const DrawBatch& batch : packet.batches()) {
        for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
            const TextureHandle handle = batch.textures[slot];
            if (!handle.valid() || handle.value() > textures.size()
                || textures[handle.value() - 1U].object == 0) {
                ++statistics.rejectedFrames;
                error = "Render packet references an unsynchronized texture";
                return false;
            }
        }
    }

    const GlState previous = captureState();
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    gl.useProgram(program);
    gl.uniform2f(viewportLocation, static_cast<float>(width), static_cast<float>(height));
    constexpr std::array<GLint, DrawBatch::kTextureCapacity> textureUnits{0, 1, 2, 3, 4, 5, 6, 7};
    gl.uniform1iv(texturesLocation, static_cast<GLsizei>(textureUnits.size()), textureUnits.data());
    gl.bindVertexArray(vertexArray);
    gl.bindBuffer(kArrayBuffer, instanceBuffer);

    if (uploadedIdentity != packet.identity() || uploadedRevision != packet.revision()) {
        void* mapped = gl.mapBufferRange(
            kArrayBuffer,
            0,
            static_cast<GlSize>(packet.instances().size_bytes()),
            kMapWriteBit | kMapInvalidateBufferBit | kMapUnsynchronizedBit);
        if (mapped == nullptr) {
            restoreState(previous);
            ++statistics.rejectedFrames;
            error = "OpenGL instance upload mapping failed";
            return false;
        }
        std::memcpy(mapped, packet.instances().data(), packet.instances().size_bytes());
        if (gl.unmapBuffer(kArrayBuffer) != GL_TRUE) {
            restoreState(previous);
            ++statistics.rejectedFrames;
            error = "OpenGL instance upload was corrupted";
            return false;
        }
        uploadedIdentity = packet.identity();
        uploadedRevision = packet.revision();
        ++statistics.instanceUploads;
    }

    for (const DrawBatch& batch : packet.batches()) {
        if (batch.instanceCount == 0) {
            continue;
        }
        if (batch.blend == BlendMode::Additive) {
            gl.blendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
        } else {
            gl.blendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }

        if (batch.clip.enabled) {
            const GLint x = static_cast<GLint>(batch.clip.area.min.x);
            const GLint y = static_cast<GLint>(static_cast<float>(height) - batch.clip.area.max.y);
            const GLsizei clipWidth = static_cast<GLsizei>(batch.clip.area.width());
            const GLsizei clipHeight = static_cast<GLsizei>(batch.clip.area.height());
            glScissor(x, y, clipWidth, clipHeight);
        } else {
            glScissor(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        }

        for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
            gl.activeTexture(kTexture0 + slot);
            const GLuint object = slot < batch.textureCount
                ? textures[batch.textures[slot].value() - 1U].object
                : 0;
            glBindTexture(GL_TEXTURE_2D, object);
        }

        configureAttributes(batch.firstInstance);
        gl.drawArraysInstanced(
            GL_TRIANGLES,
            0,
            6,
            static_cast<GLsizei>(batch.instanceCount));
        ++statistics.drawCalls;
        statistics.submittedInstances += batch.instanceCount;
    }

    restoreState(previous);
    ++statistics.frames;
    error.clear();
    return true;
}

void OpenGlRenderer::Implementation::shutdown() noexcept {
    if (wglGetCurrentContext() != nullptr) {
        for (GpuTexture& texture : textures) {
            if (texture.object != 0) {
                glDeleteTextures(1, &texture.object);
            }
        }
        if (instanceBuffer != 0 && gl.deleteBuffers != nullptr) {
            gl.deleteBuffers(1, &instanceBuffer);
        }
        if (vertexArray != 0 && gl.deleteVertexArrays != nullptr) {
            gl.deleteVertexArrays(1, &vertexArray);
        }
        if (program != 0 && gl.deleteProgram != nullptr) {
            gl.deleteProgram(program);
        }
    }
    textures.clear();
    program = 0;
    vertexArray = 0;
    instanceBuffer = 0;
    viewportLocation = -1;
    texturesLocation = -1;
    capacity = 0;
    uploadedIdentity = 0;
    uploadedRevision = 0;
    ready = false;
}

void OpenGlRenderer::Implementation::configureAttributes(std::size_t firstInstance) const noexcept {
    const auto base = firstInstance * sizeof(DrawInstance);
    const auto pointer = [base](std::size_t memberOffset) {
        return reinterpret_cast<const void*>(base + memberOffset);
    };
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(DrawInstance));

    gl.vertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, bounds)));
    gl.vertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, uv)));
    gl.vertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, pointA)));
    gl.vertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, color)));
    gl.vertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, pointer(offsetof(DrawInstance, radius)));
    gl.vertexAttribIPointer(5, 1, GL_UNSIGNED_INT, stride, pointer(offsetof(DrawInstance, textureSlot)));
    gl.vertexAttribIPointer(6, 1, GL_UNSIGNED_BYTE, stride, pointer(offsetof(DrawInstance, kind)));
    for (GLuint index = 0; index <= 6; ++index) {
        gl.enableVertexAttribArray(index);
        gl.vertexAttribDivisor(index, 1);
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
    state.blend = glIsEnabled(GL_BLEND);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
        gl.activeTexture(kTexture0 + slot);
        glGetIntegerv(kTextureBinding2D, &state.textures[slot]);
    }
    gl.activeTexture(static_cast<GLenum>(state.activeTexture));
    return state;
}

void OpenGlRenderer::Implementation::restoreState(const GlState& state) const noexcept {
    for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
        gl.activeTexture(kTexture0 + slot);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textures[slot]));
    }
    gl.activeTexture(static_cast<GLenum>(state.activeTexture));
    gl.bindBuffer(kArrayBuffer, static_cast<GLuint>(state.arrayBuffer));
    gl.bindVertexArray(static_cast<GLuint>(state.vertexArray));
    gl.useProgram(static_cast<GLuint>(state.program));
    gl.blendFuncSeparate(
        static_cast<GLenum>(state.sourceRgb),
        static_cast<GLenum>(state.destinationRgb),
        static_cast<GLenum>(state.sourceAlpha),
        static_cast<GLenum>(state.destinationAlpha));
    (state.blend == GL_TRUE ? glEnable : glDisable)(GL_BLEND);
    (state.scissorTest == GL_TRUE ? glEnable : glDisable)(GL_SCISSOR_TEST);
    (state.depthTest == GL_TRUE ? glEnable : glDisable)(GL_DEPTH_TEST);
    (state.cullFace == GL_TRUE ? glEnable : glDisable)(GL_CULL_FACE);
    glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
    glScissor(state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]);
}

OpenGlRenderer::OpenGlRenderer() : mImplementation(std::make_unique<Implementation>()) {}

OpenGlRenderer::~OpenGlRenderer() { mImplementation->shutdown(); }

OpenGlRenderer::OpenGlRenderer(OpenGlRenderer&&) noexcept = default;

OpenGlRenderer& OpenGlRenderer::operator=(OpenGlRenderer&&) noexcept = default;

bool OpenGlRenderer::initialize(std::size_t instanceCapacity) noexcept {
    return mImplementation->initialize(instanceCapacity);
}

bool OpenGlRenderer::synchronizeTextures(const TextureStore& textures) noexcept {
    return mImplementation->synchronizeTextures(textures);
}

bool OpenGlRenderer::render(
    const RenderPacket& packet,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight) noexcept {
    return mImplementation->render(packet, viewportWidth, viewportHeight);
}

void OpenGlRenderer::shutdown() noexcept { mImplementation->shutdown(); }

bool OpenGlRenderer::initialized() const noexcept { return mImplementation->ready; }

std::size_t OpenGlRenderer::instanceCapacity() const noexcept { return mImplementation->capacity; }

OpenGlRenderStatistics OpenGlRenderer::statistics() const noexcept { return mImplementation->statistics; }

std::string_view OpenGlRenderer::lastError() const noexcept { return mImplementation->error; }

} // namespace henia::ui
