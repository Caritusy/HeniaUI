#include "henia/ui/widget/controls/ScrollContainer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace henia::ui {

ScrollContainer::ScrollContainer(std::unique_ptr<Widget> contentValue, ScrollContainerStyle style)
    : Widget(WidgetKind::ScrollContainer), mStyle(style) {
    if (contentValue != nullptr) addChild(std::move(contentValue));
}

Widget* ScrollContainer::content() const noexcept {
    return mChildren.empty() ? nullptr : mChildren.front().get();
}
void ScrollContainer::setScrollOffset(float offset) noexcept {
    static_cast<void>(updateOffset(offset, false));
}
float ScrollContainer::scrollOffset() const noexcept { return mScrollOffset; }
float ScrollContainer::maximumScrollOffset() const noexcept { return mMaximumScrollOffset; }
float ScrollContainer::contentExtent() const noexcept { return mContentExtent; }
void ScrollContainer::setStyle(ScrollContainerStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void ScrollContainer::setOnScrollChanged(Callback<float> callback) noexcept { mOnScrollChanged = callback; }
bool ScrollContainer::acceptsPointerInput() const noexcept { return true; }
bool ScrollContainer::acceptsKeyboardFocus() const noexcept { return true; }

bool ScrollContainer::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerScroll && contains(event.position)) {
        return updateOffset(mScrollOffset - event.scrollY * mStyle.wheelStep, true);
    }
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) return true;
    if (event.kind != InputEventKind::KeyDown || !focused()) return false;
    switch (event.key) {
        case KeyCode::Up: return updateOffset(mScrollOffset - mStyle.wheelStep, true);
        case KeyCode::Down: return updateOffset(mScrollOffset + mStyle.wheelStep, true);
        case KeyCode::PageUp: return updateOffset(mScrollOffset - frame().height(), true);
        case KeyCode::PageDown: return updateOffset(mScrollOffset + frame().height(), true);
        case KeyCode::Home: return updateOffset(0.0F, true);
        case KeyCode::End: return updateOffset(mMaximumScrollOffset, true);
        default: return false;
    }
}

Vec2 ScrollContainer::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void ScrollContainer::onArrange(TextPainter& text, Rect arrangedFrame) {
    Widget* child = content();
    if (child == nullptr) {
        mContentExtent = 0.0F;
        mMaximumScrollOffset = 0.0F;
        mScrollOffset = 0.0F;
        return;
    }
    const float width = std::max(arrangedFrame.width(), 0.0F);
    const Vec2 measured = child->measure(text, {{width, 0.0F},
        {width, std::numeric_limits<float>::max()}});
    mContentExtent = measured.y;
    mMaximumScrollOffset = std::max(mContentExtent - std::max(arrangedFrame.height(), 0.0F), 0.0F);
    mScrollOffset = std::clamp(mScrollOffset, 0.0F, mMaximumScrollOffset);
    child->arrange(text, {{arrangedFrame.min.x, arrangedFrame.min.y - mScrollOffset},
        {arrangedFrame.max.x, arrangedFrame.min.y - mScrollOffset + measured.y}});
}

void ScrollContainer::onPaint(Canvas& canvas, TextPainter&, const Theme&) {
    if (mStyle.background.alpha > 0.0F) canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    if (mStyle.border.alpha > 0.0F) canvas.strokeRect(frame(), mStyle.border, mStyle.radius, 1.0F);
    if (mMaximumScrollOffset <= 0.0F || mContentExtent <= 0.0F) return;
    const float trackWidth = std::max(mStyle.scrollbarWidth, 1.0F);
    const Rect track{{frame().max.x - trackWidth, frame().min.y}, frame().max};
    canvas.fillRect(track, mStyle.scrollbarTrack, trackWidth * 0.5F);
    const float viewport = std::max(frame().height(), 0.0F);
    const float thumbHeight = std::max(viewport * viewport / mContentExtent, 18.0F);
    const float travel = std::max(viewport - thumbHeight, 0.0F);
    const float amount = mMaximumScrollOffset <= 0.0F ? 0.0F : mScrollOffset / mMaximumScrollOffset;
    const float top = frame().min.y + travel * amount;
    canvas.fillRect({{track.min.x, top}, {track.max.x, top + thumbHeight}},
        mStyle.scrollbarThumb, trackWidth * 0.5F);
}

bool ScrollContainer::clipsChildren() const noexcept { return true; }
Rect ScrollContainer::childrenClipRect() const noexcept { return frame(); }

bool ScrollContainer::updateOffset(float offset, bool notify) noexcept {
    if (!std::isfinite(offset)) offset = 0.0F;
    const float next = std::clamp(offset, 0.0F, mMaximumScrollOffset);
    if (next == mScrollOffset) return false;
    mScrollOffset = next;
    markLayoutDirty();
    if (notify) mOnScrollChanged(mScrollOffset);
    return true;
}

} // namespace henia::ui
