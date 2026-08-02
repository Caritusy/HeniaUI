#pragma once

#include "henia/ui/Canvas.h"
#include "henia/ui/input/InputEvent.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/theme/Theme.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace henia::ui {

struct Insets final {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    friend constexpr bool operator==(Insets, Insets) noexcept = default;
};

struct Constraints final {
    Vec2 minimum{};
    Vec2 maximum{};

    friend constexpr bool operator==(Constraints, Constraints) noexcept = default;
};

struct LayoutParameters final {
    float width = -1.0F;
    float height = -1.0F;
    float flexGrow = 0.0F;
    Insets margin{};
};

enum class WidgetKind : std::uint8_t {
    Generic,
    Panel,
    Label,
    Button,
    NumericInput,
};

class Widget {
public:
    explicit Widget(WidgetKind kind = WidgetKind::Generic) noexcept;
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    [[nodiscard]] WidgetKind kind() const noexcept;
    // Stable and non-zero for this widget's lifetime.
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] Widget* parent() const noexcept;
    [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const noexcept;
    [[nodiscard]] Rect frame() const noexcept;
    [[nodiscard]] const LayoutParameters& layoutParameters() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool hovered() const noexcept;
    [[nodiscard]] bool pressed() const noexcept;
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] bool layoutDirty() const noexcept;
    [[nodiscard]] bool paintDirty() const noexcept;
    [[nodiscard]] bool subtreePaintDirty() const noexcept;
    // One retained local paint segment is owned by every widget. Its identity
    // is stable for the widget lifetime and its revision changes only when
    // that widget's onPaint output is rebuilt.
    [[nodiscard]] std::uint64_t paintSegmentIdentity() const noexcept;
    [[nodiscard]] std::uint64_t paintRevision() const noexcept;

    void setLayoutParameters(LayoutParameters parameters) noexcept;
    void setVisible(bool visible) noexcept;
    void setEnabled(bool enabled) noexcept;
    Widget& addChild(std::unique_ptr<Widget> child);

    template <typename Type, typename... Arguments>
    Type& emplaceChild(Arguments&&... arguments) {
        auto child = std::make_unique<Type>(std::forward<Arguments>(arguments)...);
        Type& reference = *child;
        addChild(std::move(child));
        return reference;
    }

    // Measurement cache keys use normalized minimum and maximum constraints.
    // Invalid axes collapse deterministically instead of reaching std::clamp
    // with an inverted range.
    [[nodiscard]] Vec2 measure(TextPainter& text, Constraints constraints);
    void arrange(TextPainter& text, Rect frame);
    void paint(Canvas& canvas, TextPainter& text, const Theme& theme);
    [[nodiscard]] Widget* hitTest(Vec2 point) noexcept;
    [[nodiscard]] virtual bool handleInput(const InputEvent& event);

    void markLayoutDirty() noexcept;
    void markPaintDirty() noexcept;

protected:
    [[nodiscard]] virtual Vec2 onMeasure(TextPainter& text, Constraints constraints);
    virtual void onArrange(TextPainter& text, Rect frame);
    virtual void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme);
    [[nodiscard]] bool contains(Vec2 point) const noexcept;

    std::vector<std::unique_ptr<Widget>> mChildren;

private:
    friend class UiDocument;

    struct MeasurementCacheEntry final {
        Constraints constraints{};
        Vec2 measured{};
        bool valid = false;
    };

    [[nodiscard]] std::unique_ptr<Widget> detachChild(std::uint64_t identity) noexcept;
    void setHovered(bool hovered) noexcept;
    void setPressed(bool pressed) noexcept;
    void setFocused(bool focused) noexcept;
    void markPaintTopologyDirty() noexcept;
    void markPaintDirtyRecursive() noexcept;
    void clearPaintDirtyRecursive() noexcept;

    WidgetKind mKind = WidgetKind::Generic;
    std::uint64_t mIdentity = 0;
    Widget* mParent = nullptr;
    Rect mFrame{};
    LayoutParameters mLayout{};
    Vec2 mMeasured{};
    Constraints mMeasuredConstraints{};
    Constraints mArrangedMeasurementConstraints{};
    std::array<MeasurementCacheEntry, 2> mMeasurementCache{};
    std::size_t mNextMeasurementCacheEntry = 0;
    DisplayList mPaintSegment;
    std::uint64_t mPaintRevision = 0;
    std::size_t mRetainedSegmentBegin = 0;
    std::size_t mRetainedSegmentEnd = 0;
    bool mVisible = true;
    bool mEnabled = true;
    bool mHovered = false;
    bool mPressed = false;
    bool mFocused = false;
    bool mLayoutDirty = true;
    bool mMeasurementDirty = true;
    bool mHasMeasuredConstraints = false;
    bool mHasArrangedMeasurementConstraints = false;
    bool mPaintDirty = true;
    bool mSubtreePaintDirty = true;
    bool mSubtreePaintTopologyDirty = true;
};

} // namespace henia::ui
