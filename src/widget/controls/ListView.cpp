#include "henia/ui/widget/controls/ListView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace henia::ui {

ListView::ListView(std::vector<std::string> items, ListViewStyle style)
    : Widget(WidgetKind::ListView), mItems(std::move(items)), mStyle(style) {}

void ListView::setItems(std::vector<std::string> items) {
    mItems = std::move(items);
    mLabelProvider = {};
    mVirtualItemCount = 0;
    if (mSelected.has_value() && *mSelected >= mItems.size()) mSelected.reset();
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markPaintDirty();
}
void ListView::setVirtualItems(
    std::size_t count,
    ValueCallback<std::string_view, std::size_t> provider) noexcept {
    mItems.clear();
    mVirtualItemCount = count;
    mLabelProvider = provider;
    if (mSelected.has_value() && *mSelected >= count) mSelected.reset();
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markPaintDirty();
}
std::size_t ListView::itemCount() const noexcept {
    return mLabelProvider.valid() ? mVirtualItemCount : mItems.size();
}
std::string_view ListView::item(std::size_t index) const {
    if (index >= itemCount()) return {};
    return mLabelProvider.valid() ? mLabelProvider(index) : std::string_view(mItems[index]);
}
void ListView::setSelectedIndex(std::optional<std::size_t> index) noexcept {
    if (index.has_value() && *index >= itemCount()) index.reset();
    if (mSelected == index) return;
    mSelected = index;
    if (mSelected.has_value()) reveal(*mSelected);
    markPaintDirty();
}
std::optional<std::size_t> ListView::selectedIndex() const noexcept { return mSelected; }
void ListView::setScrollOffset(float offset) noexcept { static_cast<void>(updateScroll(offset)); }
float ListView::scrollOffset() const noexcept { return mScrollOffset; }
std::size_t ListView::lastPaintedRowCount() const noexcept { return mLastPaintedRows; }
void ListView::setStyle(ListViewStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void ListView::setOnSelectionChanged(Callback<std::size_t> callback) noexcept {
    mOnSelectionChanged = callback;
}
bool ListView::acceptsPointerInput() const noexcept { return true; }
bool ListView::acceptsKeyboardFocus() const noexcept { return true; }

bool ListView::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerScroll && contains(event.position)) {
        return updateScroll(mScrollOffset - event.scrollY * mStyle.rowHeight * mStyle.wheelRows);
    }
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) return true;
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary && contains(event.position)
        && mStyle.rowHeight > 0.0F) {
        const float logicalY = event.position.y - frame().min.y + mScrollOffset;
        const std::size_t index = static_cast<std::size_t>(std::max(logicalY, 0.0F) / mStyle.rowHeight);
        if (index < itemCount()) static_cast<void>(select(index, true));
        return true;
    }
    if (event.kind != InputEventKind::KeyDown || !focused() || itemCount() == 0) return false;
    std::size_t index = mSelected.value_or(0);
    switch (event.key) {
        case KeyCode::Up: index = index == 0 ? itemCount() - 1U : index - 1U; break;
        case KeyCode::Down: index = (index + 1U) % itemCount(); break;
        case KeyCode::Home: index = 0; break;
        case KeyCode::End: index = itemCount() - 1U; break;
        case KeyCode::PageUp: {
            const std::size_t page = std::max<std::size_t>(
                static_cast<std::size_t>(frame().height() / std::max(mStyle.rowHeight, 1.0F)), 1U);
            index = index > page ? index - page : 0;
            break;
        }
        case KeyCode::PageDown: {
            const std::size_t page = std::max<std::size_t>(
                static_cast<std::size_t>(frame().height() / std::max(mStyle.rowHeight, 1.0F)), 1U);
            index = std::min(index + page, itemCount() - 1U);
            break;
        }
        default: return false;
    }
    return select(index, true);
}

Vec2 ListView::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void ListView::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    Canvas::ClipScope clip = canvas.scopedClip(frame());
    mLastPaintedRows = 0;
    if (mStyle.rowHeight > 0.0F && itemCount() != 0) {
        const std::size_t first = static_cast<std::size_t>(mScrollOffset / mStyle.rowHeight);
        float y = frame().min.y - std::fmod(mScrollOffset, mStyle.rowHeight);
        for (std::size_t index = first; index < itemCount() && y < frame().max.y;
             ++index, y += mStyle.rowHeight) {
            const Rect row{{frame().min.x, y}, {frame().max.x, y + mStyle.rowHeight}};
            const Color background = mSelected == index ? mStyle.selected
                : (index % 2U == 0 ? mStyle.background : mStyle.alternate);
            canvas.fillRect(row, background, 0.0F);
            const std::string_view label = item(index);
            const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, label);
            text.draw(canvas, mStyle.font, mStyle.fontSize,
                {row.min.x + mStyle.horizontalPadding,
                 row.min.y + std::max((row.height() - metrics.height) * 0.5F, 0.0F)},
                mStyle.text, label);
            ++mLastPaintedRows;
        }
    }
    static_cast<void>(clip.reset());
    canvas.strokeRect(frame(), focused() ? mStyle.focus : mStyle.border, mStyle.radius, 1.0F);
}

float ListView::maximumScrollOffset() const noexcept {
    const double extent = static_cast<double>(itemCount()) * std::max(mStyle.rowHeight, 0.0F);
    const double result = std::max(
        extent - std::max(static_cast<double>(frame().height()), 0.0), 0.0);
    return static_cast<float>(std::min(
        result, static_cast<double>(std::numeric_limits<float>::max())));
}
bool ListView::updateScroll(float offset) noexcept {
    if (!std::isfinite(offset)) offset = 0.0F;
    const float next = std::clamp(offset, 0.0F, maximumScrollOffset());
    if (next == mScrollOffset) return false;
    mScrollOffset = next;
    markPaintDirty();
    return true;
}
bool ListView::select(std::size_t index, bool notify) {
    if (index >= itemCount() || mSelected == index) return false;
    mSelected = index;
    reveal(index);
    markPaintDirty();
    if (notify) mOnSelectionChanged(index);
    return true;
}
void ListView::reveal(std::size_t index) noexcept {
    const float top = static_cast<float>(index) * mStyle.rowHeight;
    const float bottom = top + mStyle.rowHeight;
    if (top < mScrollOffset) mScrollOffset = top;
    else if (bottom > mScrollOffset + frame().height()) {
        mScrollOffset = bottom - frame().height();
    }
    mScrollOffset = std::clamp(mScrollOffset, 0.0F, maximumScrollOffset());
}

} // namespace henia::ui
