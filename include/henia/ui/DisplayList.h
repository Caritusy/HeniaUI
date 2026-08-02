#pragma once

#include "henia/ui/ResourceHandle.h"
#include "henia/ui/Types.h"

#include <cstddef>
#include <span>
#include <vector>

namespace henia::ui {

enum class CapacityPolicy : std::uint8_t {
    Grow,
    Fixed,
};

enum class PrimitiveKind : std::uint8_t {
    SolidRect,
    StrokeRect,
    Line,
    Image,
    Glyph,
};

enum class LineCap : std::uint8_t {
    Butt,
    Square,
    Round,
};

enum class LineJoin : std::uint8_t {
    Bevel,
    Round,
};

inline constexpr std::uint8_t kLineHasPrevious = 1U << 0U;
inline constexpr std::uint8_t kLineHasNext = 1U << 1U;
inline constexpr float kAnalyticAaFringe = 2.0F;

struct DrawCommand final {
    PrimitiveKind kind = PrimitiveKind::SolidRect;
    BlendMode blend     = BlendMode::PremultipliedAlpha;
    ClipRect clip{};
    TextureHandle texture{};
    Rect bounds{};
    Rect uv{{0.0F, 0.0F}, {1.0F, 1.0F}};
    Vec2 pointA{};
    Vec2 pointB{};
    Color color{};
    float radius    = 0.0F;
    float thickness = 0.0F;
    LineCap lineCap = LineCap::Round;
    LineJoin lineJoin = LineJoin::Round;
    std::uint8_t lineFlags = 0;
};

// A stable, independently revisioned slice of retained paint output. Segment
// order is the document's global paint order; commands never borrow state from
// an adjacent segment.
struct DisplayListSegment final {
    std::uint64_t identity = 0;
    std::uint64_t revision = 0;
    std::span<const DrawCommand> commands{};
};

class DisplayList final {
public:
    void reserve(
        std::size_t commandCapacity,
        CapacityPolicy capacityPolicy = CapacityPolicy::Grow);
    void clear() noexcept;
    [[nodiscard]] bool append(const DrawCommand& command) noexcept;

    [[nodiscard]] std::span<const DrawCommand> commands() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] CapacityPolicy capacityPolicy() const noexcept;
    [[nodiscard]] std::uint64_t capacityGrowths() const noexcept;

private:
    std::vector<DrawCommand> mCommands;
    CapacityPolicy mCapacityPolicy = CapacityPolicy::Grow;
    std::uint64_t mCapacityGrowths = 0;
};

} // namespace henia::ui
