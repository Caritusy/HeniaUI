#pragma once

#include "henia/ui/DisplayList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace henia::ui {

struct GlyphQuad final {
    Rect bounds{};
    Rect uv{};
    GlyphRasterPlacement rasterPlacement = GlyphRasterPlacement::Smooth;
};

enum class EffectLayerKind : std::uint8_t {
    Tint,
    Gradient,
    AnimatedGradient,
    Glow,
    SoftShadow,
    Outline,
};

// Compact, ordered effect description. The host owns animation time and passes
// phase in cycles, keeping rendering deterministic and free of hidden clocks.
struct EffectLayer final {
    EffectLayerKind kind = EffectLayerKind::Tint;
    Color color{};
    Color secondaryColor{};
    Vec2 vector{1.0F, 0.0F};
    float amount = 0.0F;
    float phase = 0.0F;
    bool enabled = true;
};

class Canvas final {
public:
    static constexpr std::size_t kMaximumClipDepth = 32;

    class ClipScope final {
    public:
        ClipScope() noexcept = default;
        ~ClipScope();
        ClipScope(const ClipScope&) = delete;
        ClipScope& operator=(const ClipScope&) = delete;
        ClipScope(ClipScope&& other) noexcept;
        ClipScope& operator=(ClipScope&& other) noexcept;

        // active() means this object still owns its push token. reset() may be
        // called out of order; removal is deferred until child entries leave.
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool reset() noexcept;

    private:
        friend class Canvas;
        ClipScope(Canvas& canvas, std::uint64_t token) noexcept;

        Canvas* mCanvas = nullptr;
        std::uint64_t mToken = 0;
    };

    explicit Canvas(DisplayList& displayList) noexcept;

    void reset() noexcept;
    // Prefer scopedClip() for conditional or nested clipping. A failed push
    // returns an inactive scope whose destructor cannot pop a parent.
    [[nodiscard]] ClipScope scopedClip(Rect rect) noexcept;
    [[nodiscard]] bool pushClip(Rect rect) noexcept;
    [[nodiscard]] bool popClip() noexcept;

    void line(
        Vec2 from,
        Vec2 to,
        Color color,
        float thickness,
        LineCap cap = LineCap::Round) noexcept;
    void polyline(
        std::span<const Vec2> points,
        Color color,
        float thickness,
        bool closed,
        LineCap cap = LineCap::Round,
        LineJoin join = LineJoin::Round) noexcept;
    void fillRect(Rect rect, Color color, float rounding = 0.0F) noexcept;
    void strokeRect(Rect rect, Color color, float rounding, float thickness) noexcept;
    void circle(Vec2 center, float radius, Color color) noexcept;
    void ellipse(Rect rect, Color color) noexcept;
    void arc(
        Rect ellipseBounds,
        float startRadians,
        float sweepRadians,
        Color color,
        float thickness) noexcept;
    void capsule(Rect rect, Color color) noexcept;
    void gradientRect(
        Rect rect,
        Color start,
        Color finish,
        Vec2 direction = {1.0F, 0.0F},
        float rounding = 0.0F) noexcept;
    void tintRect(Rect rect, Color color, float rounding = 0.0F) noexcept;
    void animatedGradientRect(
        Rect rect,
        Color start,
        Color finish,
        Vec2 direction,
        float phase,
        float rounding = 0.0F) noexcept;
    void roundedGlow(
        Rect rect,
        Color color,
        float rounding,
        float glowRadius) noexcept;
    // One analytic instance, unlike strokeRect's fragment-minimizing eight
    // regions. Prefer this when composability matters more than fill rate.
    void roundedOutline(
        Rect rect,
        Color color,
        float rounding,
        float thickness) noexcept;
    // Alpha8 or linear RGBA textures are interpreted as a signed-distance
    // coverage field in the red channel. edge and softness are normalized.
    void sdfIcon(
        TextureHandle texture,
        Rect rect,
        Rect sourceUv,
        Color tint = {},
        float edge = 0.5F,
        float softness = 0.05F) noexcept;
    // Emits enabled layers in caller order. This is a local overlay pipeline:
    // no full-screen pass or intermediate render target is introduced.
    void effectRect(
        Rect rect,
        float rounding,
        std::span<const EffectLayer> layers) noexcept;
    // A compact analytic approximation: blurRadius controls a Gaussian-like
    // falloff without allocating an intermediate blur target.
    void roundedShadow(
        Rect rect,
        Color color,
        float rounding,
        float blurRadius,
        Vec2 offset = {}) noexcept;
    void border(
        Rect rect,
        Color color,
        CornerRadii radii,
        float thickness) noexcept;
    // Uniform-border nine-patch mapping in one textured instance. sourceBorder
    // is normalized within sourceUv and must be in (0, 0.5).
    void ninePatch(
        TextureHandle texture,
        Rect rect,
        Rect sourceUv,
        float destinationBorder,
        float sourceBorder,
        Color tint = {}) noexcept;
    void image(TextureHandle texture, Rect rect, Color tint = {}) noexcept;
    void glyphs(TextureHandle atlas, std::span<const GlyphQuad> glyphs, Color color) noexcept;
    void glyphs(
        TextureHandle atlas,
        Vec2 origin,
        std::span<const GlyphQuad> glyphs,
        Color color) noexcept;

    void setBlendMode(BlendMode blendMode) noexcept;
    [[nodiscard]] BlendMode blendMode() const noexcept;
    [[nodiscard]] std::size_t clipDepth() const noexcept;
    [[nodiscard]] std::uint64_t rejectedCommands() const noexcept;
    [[nodiscard]] std::uint64_t clippedCommands() const noexcept;
    [[nodiscard]] std::uint64_t invalidInputCommands() const noexcept;
    [[nodiscard]] std::uint64_t capacityRejectedCommands() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct ClipEntry final {
        ClipRect clip{};
        std::uint64_t token = 0;
        bool empty = false;
        bool releasePending = false;
    };

    [[nodiscard]] bool pushClip(Rect rect, std::uint64_t token) noexcept;
    [[nodiscard]] bool releaseClip(std::uint64_t token) noexcept;
    void collapseReleasedClips() noexcept;
    [[nodiscard]] ClipRect currentClip() const noexcept;
    [[nodiscard]] bool commandOverlapsCurrentClip(const DrawCommand& command) const noexcept;
    void rejectInvalid(std::string_view field) noexcept;
    void append(DrawCommand command) noexcept;
    void appendLine(
        Vec2 from,
        Vec2 to,
        Vec2 previous,
        Vec2 next,
        Color color,
        float thickness,
        LineCap cap,
        LineJoin join,
        std::uint8_t flags) noexcept;

    DisplayList* mDisplayList = nullptr;
    std::array<ClipEntry, kMaximumClipDepth> mClips{};
    std::size_t mClipDepth = 0;
    std::uint64_t mNextClipToken = 1;
    BlendMode mBlendMode = BlendMode::PremultipliedAlpha;
    std::uint64_t mRejectedCommands = 0;
    std::uint64_t mClippedCommands = 0;
    std::uint64_t mInvalidInputCommands = 0;
    std::uint64_t mCapacityRejectedCommands = 0;
    std::string_view mLastError{};
};

} // namespace henia::ui
