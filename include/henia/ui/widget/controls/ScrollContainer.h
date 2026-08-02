#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <memory>

namespace henia::ui {

struct ScrollContainerStyle final {
    Color background{0.0F, 0.0F, 0.0F, 0.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    Color scrollbarTrack{0.05F, 0.08F, 0.11F, 0.65F};
    Color scrollbarThumb{0.22F, 0.38F, 0.48F, 0.9F};
    float width = 260.0F;
    float height = 220.0F;
    float wheelStep = 42.0F;
    float scrollbarWidth = 6.0F;
    float radius = 6.0F;
};

class ScrollContainer final : public Widget {
public:
    explicit ScrollContainer(std::unique_ptr<Widget> content = {},
        ScrollContainerStyle style = {});

    [[nodiscard]] Widget* content() const noexcept;
    void setScrollOffset(float offset) noexcept;
    [[nodiscard]] float scrollOffset() const noexcept;
    [[nodiscard]] float maximumScrollOffset() const noexcept;
    [[nodiscard]] float contentExtent() const noexcept;
    void setStyle(ScrollContainerStyle style) noexcept;
    void setOnScrollChanged(Callback<float> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onArrange(TextPainter& text, Rect frame) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;
    [[nodiscard]] bool clipsChildren() const noexcept override;
    [[nodiscard]] Rect childrenClipRect() const noexcept override;

private:
    [[nodiscard]] bool updateOffset(float offset, bool notify) noexcept;

    ScrollContainerStyle mStyle{};
    Callback<float> mOnScrollChanged{};
    float mScrollOffset = 0.0F;
    float mMaximumScrollOffset = 0.0F;
    float mContentExtent = 0.0F;
};

} // namespace henia::ui
