#pragma once

#include "henia/ui/DisplayList.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace henia::ui {

struct GlyphQuad final {
    Rect bounds{};
    Rect uv{};
};

class Canvas final {
public:
    static constexpr std::size_t kMaximumClipDepth = 32;

    explicit Canvas(DisplayList& displayList) noexcept;

    void reset() noexcept;
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
    [[nodiscard]] std::uint64_t invalidInputCommands() const noexcept;
    [[nodiscard]] std::uint64_t capacityRejectedCommands() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    [[nodiscard]] ClipRect currentClip() const noexcept;
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
    std::array<ClipRect, kMaximumClipDepth> mClips{};
    std::size_t mClipDepth = 0;
    BlendMode mBlendMode = BlendMode::PremultipliedAlpha;
    std::uint64_t mRejectedCommands = 0;
    std::uint64_t mInvalidInputCommands = 0;
    std::uint64_t mCapacityRejectedCommands = 0;
    std::string_view mLastError{};
};

} // namespace henia::ui
