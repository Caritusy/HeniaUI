#pragma once

#include "henia/ui/DisplayList.h"

#include <array>
#include <cstddef>
#include <span>

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

    void line(Vec2 from, Vec2 to, Color color, float thickness) noexcept;
    void polyline(std::span<const Vec2> points, Color color, float thickness, bool closed) noexcept;
    void fillRect(Rect rect, Color color, float rounding = 0.0F) noexcept;
    void strokeRect(Rect rect, Color color, float rounding, float thickness) noexcept;
    void image(TextureHandle texture, Rect rect, Color tint = {}) noexcept;
    void glyphs(TextureHandle atlas, std::span<const GlyphQuad> glyphs, Color color) noexcept;

    void setBlendMode(BlendMode blendMode) noexcept;
    [[nodiscard]] BlendMode blendMode() const noexcept;
    [[nodiscard]] std::size_t clipDepth() const noexcept;
    [[nodiscard]] std::uint64_t rejectedCommands() const noexcept;

private:
    [[nodiscard]] ClipRect currentClip() const noexcept;
    void append(DrawCommand command) noexcept;

    DisplayList* mDisplayList = nullptr;
    std::array<ClipRect, kMaximumClipDepth> mClips{};
    std::size_t mClipDepth = 0;
    BlendMode mBlendMode = BlendMode::PremultipliedAlpha;
    std::uint64_t mRejectedCommands = 0;
};

} // namespace henia::ui
