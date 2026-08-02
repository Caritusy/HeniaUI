#include "henia/ui/widget/controls/ListView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace henia::ui {
namespace {

constexpr std::size_t kUnassignedItem = std::numeric_limits<std::size_t>::max();

[[nodiscard]] float usableExtent(float value, float fallback) noexcept {
    return std::isfinite(value) && value > 0.0F ? value : std::max(fallback, 1.0F);
}

} // namespace

ListView::ListView(std::vector<std::string> items, ListViewStyle style)
    : Widget(WidgetKind::ListView), mItems(std::move(items)), mStyle(style) {}

void ListView::setItems(std::vector<std::string> items) {
    if (usesRecycledWidgets()) resetRecycledPool();
    mItems = std::move(items);
    mLabelProvider = {};
    mRecycledSource = {};
    mItemOffsets.clear();
    mVirtualItemCount = 0;
    mSourceKind = SourceKind::OwnedLabels;
    if (mSelected.has_value() && *mSelected >= mItems.size()) mSelected.reset();
    mSelectedKey = mSelected.has_value()
        ? std::optional<ListItemKey>{static_cast<ListItemKey>(*mSelected)}
        : std::nullopt;
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markLayoutDirty();
}

void ListView::setVirtualItems(
    std::size_t count,
    ValueCallback<std::string_view, std::size_t> provider) {
    if (usesRecycledWidgets()) resetRecycledPool();
    mItems.clear();
    mRecycledSource = {};
    mItemOffsets.clear();
    mVirtualItemCount = count;
    mLabelProvider = provider;
    mSourceKind = SourceKind::VirtualLabels;
    if (mSelected.has_value() && *mSelected >= count) mSelected.reset();
    mSelectedKey = mSelected.has_value()
        ? std::optional<ListItemKey>{static_cast<ListItemKey>(*mSelected)}
        : std::nullopt;
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markLayoutDirty();
}

void ListView::setRecycledItems(VirtualListSource source) {
    if (!source.itemKey.valid() || !source.createWidget.valid() || !source.bindWidget.valid()) {
        throw std::invalid_argument(
            "VirtualListSource requires itemKey, createWidget, and bindWidget callbacks");
    }
    resetRecycledPool();
    mItems.clear();
    mLabelProvider = {};
    mRecycledSource = source;
    mVirtualItemCount = source.itemCount;
    mSourceKind = SourceKind::RecycledWidgets;
    mSelected.reset();
    mSelectedKey.reset();
    mScrollOffset = 0.0F;
    rebuildItemOffsets();
    markLayoutDirty();
}

void ListView::refreshRecycledItems(std::size_t count) {
    if (!usesRecycledWidgets()) {
        throw std::logic_error("refreshRecycledItems requires a recycled item source");
    }
    const std::optional<ListItemKey> preservedKey = mSelectedKey;
    mVirtualItemCount = count;
    mRecycledSource.itemCount = count;
    rebuildItemOffsets();
    mSelected = preservedKey.has_value() ? findItemByKey(*preservedKey) : std::nullopt;
    mSelectedKey = mSelected.has_value() ? preservedKey : std::nullopt;
    for (RealizedListItem& itemValue : mRealizedItems) {
        itemValue.index = kUnassignedItem;
        itemValue.key = 0;
        itemValue.selected = false;
    }
    mRealizedItemCount = 0;
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    if (mSelected.has_value()) reveal(*mSelected);
    markLayoutDirty();
}

std::size_t ListView::itemCount() const noexcept {
    return mSourceKind == SourceKind::OwnedLabels ? mItems.size() : mVirtualItemCount;
}

std::string_view ListView::item(std::size_t index) const {
    if (index >= itemCount()) return {};
    if (mSourceKind == SourceKind::VirtualLabels) return mLabelProvider(index);
    if (mSourceKind == SourceKind::OwnedLabels) return mItems[index];
    return {};
}

ListItemKey ListView::itemKey(std::size_t index) const {
    if (index >= itemCount()) return 0;
    return usesRecycledWidgets()
        ? mRecycledSource.itemKey(index)
        : static_cast<ListItemKey>(index);
}

void ListView::setSelectedIndex(std::optional<std::size_t> index) {
    if (index.has_value() && *index >= itemCount()) index.reset();
    const std::optional<ListItemKey> key = index.has_value()
        ? std::optional<ListItemKey>{itemKey(*index)}
        : std::nullopt;
    if (mSelected == index && mSelectedKey == key) return;
    mSelected = index;
    mSelectedKey = key;
    if (mSelected.has_value()) reveal(*mSelected);
    if (usesRecycledWidgets()) markLayoutDirty();
    else markPaintDirty();
}

std::optional<std::size_t> ListView::selectedIndex() const noexcept { return mSelected; }

void ListView::setSelectedItemKey(std::optional<ListItemKey> key) {
    if (!key.has_value()) {
        setSelectedIndex(std::nullopt);
        return;
    }
    const std::optional<std::size_t> index = findItemByKey(*key);
    setSelectedIndex(index);
}

std::optional<ListItemKey> ListView::selectedItemKey() const noexcept { return mSelectedKey; }

void ListView::setScrollOffset(float offset) noexcept { static_cast<void>(updateScroll(offset)); }
float ListView::scrollOffset() const noexcept { return mScrollOffset; }
std::size_t ListView::lastPaintedRowCount() const noexcept { return mLastPaintedRows; }
std::span<const RealizedListItem> ListView::realizedItems() const noexcept {
    return {mRealizedItems.data(), mRealizedItemCount};
}
std::size_t ListView::pooledWidgetCount() const noexcept { return mRealizedItems.size(); }
std::uint64_t ListView::widgetCreationCount() const noexcept { return mWidgetCreationCount; }
std::uint64_t ListView::widgetBindCount() const noexcept { return mWidgetBindCount; }

void ListView::setStyle(ListViewStyle style) {
    mStyle = style;
    if (usesRecycledWidgets()) rebuildItemOffsets();
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markLayoutDirty();
}

void ListView::setOnSelectionChanged(Callback<std::size_t> callback) noexcept {
    mOnSelectionChanged = callback;
}

void ListView::setOnItemSelectionChanged(
    Callback<std::size_t, ListItemKey> callback) noexcept {
    mOnItemSelectionChanged = callback;
}

bool ListView::acceptsPointerInput() const noexcept { return true; }
bool ListView::acceptsKeyboardFocus() const noexcept { return true; }
bool ListView::allowsChildInteraction() const noexcept { return !usesRecycledWidgets(); }

bool ListView::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerScroll && contains(event.position)) {
        return updateScroll(mScrollOffset - event.scrollY
            * usableExtent(mStyle.rowHeight, 1.0F) * mStyle.wheelRows);
    }
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) return true;
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary && contains(event.position)) {
        const float logicalY = event.position.y - frame().min.y + mScrollOffset;
        const std::size_t index = indexAtOffset(std::max(logicalY, 0.0F));
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
            const float target = std::max(itemTop(index) - frame().height(), 0.0F);
            index = indexAtOffset(target);
            break;
        }
        case KeyCode::PageDown: {
            const float target = std::min(
                itemTop(index) + frame().height(), maximumScrollOffset());
            index = indexAtOffset(target);
            break;
        }
        default: return false;
    }
    return select(index, true);
}

Vec2 ListView::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void ListView::onArrange(TextPainter& text, Rect arrangedFrame) {
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    if (usesRecycledWidgets()) realizeItems(text, arrangedFrame);
    else Widget::onArrange(text, arrangedFrame);
}

void ListView::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    Canvas::ClipScope clip = canvas.scopedClip(frame());
    mLastPaintedRows = 0;
    if (usesRecycledWidgets()) {
        for (const RealizedListItem& realized : realizedItems()) {
            const float top = frame().min.y + itemTop(realized.index) - mScrollOffset;
            const Rect row{{frame().min.x, top},
                {frame().max.x, top + itemExtent(realized.index)}};
            if (row.max.y <= frame().min.y || row.min.y >= frame().max.y) continue;
            const Color background = realized.selected ? mStyle.selected
                : (realized.index % 2U == 0 ? mStyle.background : mStyle.alternate);
            canvas.fillRect(row, background, 0.0F);
            ++mLastPaintedRows;
        }
    } else if (mStyle.rowHeight > 0.0F && itemCount() != 0) {
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

bool ListView::clipsChildren() const noexcept { return usesRecycledWidgets(); }
Rect ListView::childrenClipRect() const noexcept { return frame(); }

bool ListView::usesRecycledWidgets() const noexcept {
    return mSourceKind == SourceKind::RecycledWidgets;
}

float ListView::itemTop(std::size_t index) const noexcept {
    if (!mItemOffsets.empty() && index < mItemOffsets.size()) {
        return static_cast<float>(std::min(
            mItemOffsets[index], static_cast<double>(std::numeric_limits<float>::max())));
    }
    const double top = static_cast<double>(index) * usableExtent(mStyle.rowHeight, 1.0F);
    return static_cast<float>(std::min(
        top, static_cast<double>(std::numeric_limits<float>::max())));
}

float ListView::itemExtent(std::size_t index) const {
    if (!mItemOffsets.empty() && index + 1U < mItemOffsets.size()) {
        return static_cast<float>(mItemOffsets[index + 1U] - mItemOffsets[index]);
    }
    return usableExtent(mStyle.rowHeight, 1.0F);
}

float ListView::totalExtent() const noexcept {
    if (!mItemOffsets.empty()) {
        return static_cast<float>(std::min(
            mItemOffsets.back(), static_cast<double>(std::numeric_limits<float>::max())));
    }
    const double extent = static_cast<double>(itemCount())
        * usableExtent(mStyle.rowHeight, 1.0F);
    return static_cast<float>(std::min(
        extent, static_cast<double>(std::numeric_limits<float>::max())));
}

std::size_t ListView::indexAtOffset(float offset) const noexcept {
    if (itemCount() == 0) return 0;
    const double target = std::max(static_cast<double>(offset), 0.0);
    if (!mItemOffsets.empty()) {
        const auto upper = std::upper_bound(mItemOffsets.begin(), mItemOffsets.end(), target);
        const std::size_t position = upper == mItemOffsets.begin()
            ? 0U
            : static_cast<std::size_t>(std::distance(mItemOffsets.begin(), upper) - 1);
        return std::min(position, itemCount() - 1U);
    }
    const double row = usableExtent(mStyle.rowHeight, 1.0F);
    return std::min(static_cast<std::size_t>(target / row), itemCount() - 1U);
}

std::size_t ListView::firstRealizedIndex() const noexcept {
    const std::size_t visible = indexAtOffset(mScrollOffset);
    return visible > mStyle.overscanRows ? visible - mStyle.overscanRows : 0U;
}

std::size_t ListView::realizedItemLimit(std::size_t first) const noexcept {
    const double bottom = static_cast<double>(mScrollOffset)
        + std::max(static_cast<double>(frame().height()), 0.0);
    std::size_t end = first;
    while (end < itemCount() && static_cast<double>(itemTop(end)) < bottom) ++end;
    const std::size_t remaining = itemCount() - end;
    end += std::min(mStyle.overscanRows, remaining);
    return end;
}

void ListView::rebuildItemOffsets() {
    mItemOffsets.clear();
    if (!usesRecycledWidgets() || !mRecycledSource.itemExtent.valid()) return;
    if (itemCount() == std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("Virtual list item count exceeds addressable offset storage");
    }
    mItemOffsets.resize(itemCount() + 1U);
    mItemOffsets[0] = 0.0;
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    for (std::size_t index = 0; index < itemCount(); ++index) {
        const float extent = usableExtent(
            mRecycledSource.itemExtent(index), mStyle.rowHeight);
        mItemOffsets[index + 1U] = std::min(
            mItemOffsets[index] + static_cast<double>(extent), maximum);
    }
}

void ListView::resetRecycledPool() {
    if (!mChildren.empty()) clearChildren();
    mRealizedItems.clear();
    mRealizedItemCount = 0;
    mWidgetCreationCount = 0;
    mWidgetBindCount = 0;
}

void ListView::realizeItems(TextPainter& text, Rect arrangedFrame) {
    const std::size_t first = firstRealizedIndex();
    const std::size_t end = realizedItemLimit(first);
    const std::size_t needed = end - first;
    if (mRealizedItems.size() < needed) {
        mChildren.reserve(needed);
        mRealizedItems.reserve(needed);
    }
    while (mRealizedItems.size() < needed) {
        std::unique_ptr<Widget> widget = mRecycledSource.createWidget();
        if (widget == nullptr) {
            throw std::runtime_error("Virtual list createWidget returned null");
        }
        Widget* pointer = &addChild(std::move(widget));
        mRealizedItems.push_back({.widget = pointer, .index = kUnassignedItem});
        ++mWidgetCreationCount;
    }

    const float width = std::max(arrangedFrame.width(), 0.0F);
    for (std::size_t slotIndex = 0; slotIndex < needed; ++slotIndex) {
        RealizedListItem& slot = mRealizedItems[slotIndex];
        const std::size_t index = first + slotIndex;
        const ListItemKey key = itemKey(index);
        const bool selected = mSelectedKey.has_value() && *mSelectedKey == key;
        if (slot.index != index || slot.key != key || slot.selected != selected) {
            mRecycledSource.bindWidget(*slot.widget, index, key, selected);
            slot.index = index;
            slot.key = key;
            slot.selected = selected;
            ++mWidgetBindCount;
        }
        if (!slot.widget->visible()) slot.widget->setVisible(true);
        const float extent = itemExtent(index);
        const float top = arrangedFrame.min.y + itemTop(index) - mScrollOffset;
        static_cast<void>(slot.widget->measure(text, {{width, extent}, {width, extent}}));
        slot.widget->arrange(text,
            {{arrangedFrame.min.x, top}, {arrangedFrame.max.x, top + extent}});
    }
    for (std::size_t slotIndex = needed; slotIndex < mRealizedItems.size(); ++slotIndex) {
        RealizedListItem& slot = mRealizedItems[slotIndex];
        if (slot.widget->visible()) slot.widget->setVisible(false);
        slot.index = kUnassignedItem;
        slot.key = 0;
        slot.selected = false;
    }
    mRealizedItemCount = needed;
}

std::optional<std::size_t> ListView::findItemByKey(ListItemKey key) const {
    for (std::size_t index = 0; index < itemCount(); ++index) {
        if (itemKey(index) == key) return index;
    }
    return std::nullopt;
}

float ListView::maximumScrollOffset() const noexcept {
    if (!usesRecycledWidgets()) {
        const double extent = static_cast<double>(itemCount())
            * std::max(mStyle.rowHeight, 0.0F);
        const double result = std::max(
            extent - std::max(static_cast<double>(frame().height()), 0.0), 0.0);
        return static_cast<float>(std::min(
            result, static_cast<double>(std::numeric_limits<float>::max())));
    }
    return std::max(totalExtent() - std::max(frame().height(), 0.0F), 0.0F);
}

bool ListView::updateScroll(float offset) noexcept {
    if (!std::isfinite(offset)) offset = 0.0F;
    const float next = std::clamp(offset, 0.0F, maximumScrollOffset());
    if (next == mScrollOffset) return false;
    mScrollOffset = next;
    if (usesRecycledWidgets()) markLayoutDirty();
    else markPaintDirty();
    return true;
}

bool ListView::select(std::size_t index, bool notify) {
    if (index >= itemCount()) return false;
    const ListItemKey key = itemKey(index);
    if (mSelected == index && mSelectedKey == key) return false;
    mSelected = index;
    mSelectedKey = key;
    reveal(index);
    if (usesRecycledWidgets()) markLayoutDirty();
    else markPaintDirty();
    if (notify) {
        mOnSelectionChanged(index);
        mOnItemSelectionChanged(index, key);
    }
    return true;
}

void ListView::reveal(std::size_t index) noexcept {
    const float top = itemTop(index);
    const float bottom = top + (mItemOffsets.empty()
        ? usableExtent(mStyle.rowHeight, 1.0F)
        : static_cast<float>(mItemOffsets[index + 1U] - mItemOffsets[index]));
    if (top < mScrollOffset) mScrollOffset = top;
    else if (bottom > mScrollOffset + frame().height()) {
        mScrollOffset = bottom - frame().height();
    }
    mScrollOffset = std::clamp(mScrollOffset, 0.0F, maximumScrollOffset());
}

} // namespace henia::ui
