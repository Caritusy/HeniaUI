#include "henia/ui/widget/controls/TreeView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace henia::ui {

TreeView::TreeView(std::vector<TreeViewNode> nodes, TreeViewStyle style)
    : Widget(WidgetKind::TreeView), mStyle(style) { setNodes(std::move(nodes)); }

void TreeView::setNodes(std::vector<TreeViewNode> nodes) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].parent >= index) nodes[index].parent = kTreeRoot;
    }
    mNodes = std::move(nodes);
    if (mSelected.has_value() && *mSelected >= mNodes.size()) mSelected.reset();
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markPaintDirty();
}
std::size_t TreeView::nodeCount() const noexcept { return mNodes.size(); }
const TreeViewNode* TreeView::node(std::size_t index) const noexcept {
    return index < mNodes.size() ? &mNodes[index] : nullptr;
}
bool TreeView::setExpanded(std::size_t index, bool expanded, bool notify) {
    if (index >= mNodes.size() || !hasChildren(index) || mNodes[index].expanded == expanded) {
        return false;
    }
    mNodes[index].expanded = expanded;
    if (mSelected.has_value() && !visible(*mSelected)) mSelected = index;
    mScrollOffset = std::min(mScrollOffset, maximumScrollOffset());
    markPaintDirty();
    if (notify) mOnExpansionChanged(index, expanded);
    return true;
}
void TreeView::setSelectedIndex(std::optional<std::size_t> index) noexcept {
    if (index.has_value() && (*index >= mNodes.size() || !visible(*index))) index.reset();
    if (mSelected == index) return;
    mSelected = index;
    if (mSelected.has_value()) reveal(*mSelected);
    markPaintDirty();
}
std::optional<std::size_t> TreeView::selectedIndex() const noexcept { return mSelected; }
std::size_t TreeView::visibleNodeCount() const noexcept {
    std::size_t count = 0;
    for (std::size_t index = 0; index < mNodes.size(); ++index) if (visible(index)) ++count;
    return count;
}
std::size_t TreeView::lastPaintedRowCount() const noexcept { return mLastPaintedRows; }
void TreeView::setScrollOffset(float offset) noexcept { static_cast<void>(updateScroll(offset)); }
float TreeView::scrollOffset() const noexcept { return mScrollOffset; }
void TreeView::setStyle(TreeViewStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void TreeView::setOnSelectionChanged(Callback<std::size_t> callback) noexcept {
    mOnSelectionChanged = callback;
}
void TreeView::setOnExpansionChanged(Callback<std::size_t, bool> callback) noexcept {
    mOnExpansionChanged = callback;
}
bool TreeView::acceptsPointerInput() const noexcept { return true; }
bool TreeView::acceptsKeyboardFocus() const noexcept { return true; }

bool TreeView::handleInput(const InputEvent& event) {
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
        const std::size_t row = static_cast<std::size_t>(std::max(logicalY, 0.0F) / mStyle.rowHeight);
        if (const auto index = nodeAtVisibleRow(row)) {
            const float disclosureEnd = frame().min.x + mStyle.horizontalPadding
                + static_cast<float>(depth(*index) + 1U) * mStyle.indentation;
            if (event.position.x <= disclosureEnd && hasChildren(*index)) {
                static_cast<void>(setExpanded(*index, !mNodes[*index].expanded, true));
            } else {
                static_cast<void>(select(*index, true));
            }
        }
        return true;
    }
    if (event.kind != InputEventKind::KeyDown || !focused() || mNodes.empty()) return false;
    std::size_t index = mSelected.value_or(*nodeAtVisibleRow(0));
    switch (event.key) {
        case KeyCode::Up:
            if (const auto adjacent = adjacentVisible(index, true)) return select(*adjacent, true);
            return false;
        case KeyCode::Down:
            if (const auto adjacent = adjacentVisible(index, false)) return select(*adjacent, true);
            return false;
        case KeyCode::Home:
            if (const auto first = nodeAtVisibleRow(0)) return select(*first, true);
            return false;
        case KeyCode::End:
            if (const auto last = nodeAtVisibleRow(visibleNodeCount() - 1U)) return select(*last, true);
            return false;
        case KeyCode::Left:
            if (mNodes[index].expanded && hasChildren(index)) return setExpanded(index, false, true);
            if (mNodes[index].parent != kTreeRoot) return select(mNodes[index].parent, true);
            return false;
        case KeyCode::Right:
            if (hasChildren(index) && !mNodes[index].expanded) return setExpanded(index, true, true);
            for (std::size_t child = index + 1U; child < mNodes.size(); ++child) {
                if (mNodes[child].parent == index) return select(child, true);
            }
            return false;
        default: return false;
    }
}

Vec2 TreeView::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void TreeView::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    Canvas::ClipScope clip = canvas.scopedClip(frame());
    mLastPaintedRows = 0;
    if (mStyle.rowHeight > 0.0F) {
        const std::size_t firstRow = static_cast<std::size_t>(mScrollOffset / mStyle.rowHeight);
        std::size_t visibleRow = 0;
        float y = frame().min.y - std::fmod(mScrollOffset, mStyle.rowHeight);
        for (std::size_t index = 0; index < mNodes.size() && y < frame().max.y; ++index) {
            if (!visible(index)) continue;
            if (visibleRow++ < firstRow) continue;
            const Rect row{{frame().min.x, y}, {frame().max.x, y + mStyle.rowHeight}};
            const Color background = mSelected == index ? mStyle.selected
                : (visibleRow % 2U == 0 ? mStyle.alternate : mStyle.background);
            canvas.fillRect(row, background, 0.0F);
            const float indent = mStyle.horizontalPadding + static_cast<float>(depth(index)) * mStyle.indentation;
            const float disclosureX = row.min.x + indent + mStyle.indentation * 0.5F;
            const float centerY = (row.min.y + row.max.y) * 0.5F;
            if (hasChildren(index)) {
                if (mNodes[index].expanded) {
                    canvas.line({disclosureX - 4.0F, centerY - 2.0F}, {disclosureX, centerY + 2.0F},
                        mStyle.disclosure, 1.5F, LineCap::Round);
                    canvas.line({disclosureX, centerY + 2.0F}, {disclosureX + 4.0F, centerY - 2.0F},
                        mStyle.disclosure, 1.5F, LineCap::Round);
                } else {
                    canvas.line({disclosureX - 2.0F, centerY - 4.0F}, {disclosureX + 2.0F, centerY},
                        mStyle.disclosure, 1.5F, LineCap::Round);
                    canvas.line({disclosureX + 2.0F, centerY}, {disclosureX - 2.0F, centerY + 4.0F},
                        mStyle.disclosure, 1.5F, LineCap::Round);
                }
            }
            const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, mNodes[index].text);
            text.draw(canvas, mStyle.font, mStyle.fontSize,
                {row.min.x + indent + mStyle.indentation,
                 row.min.y + std::max((row.height() - metrics.height) * 0.5F, 0.0F)},
                mStyle.text, mNodes[index].text);
            ++mLastPaintedRows;
            y += mStyle.rowHeight;
        }
    }
    static_cast<void>(clip.reset());
    canvas.strokeRect(frame(), focused() ? mStyle.focus : mStyle.border, mStyle.radius, 1.0F);
}

bool TreeView::visible(std::size_t index) const noexcept {
    if (index >= mNodes.size()) return false;
    std::size_t parent = mNodes[index].parent;
    while (parent != kTreeRoot) {
        if (parent >= index || !mNodes[parent].expanded) return false;
        parent = mNodes[parent].parent;
    }
    return true;
}
bool TreeView::hasChildren(std::size_t index) const noexcept {
    for (std::size_t child = index + 1U; child < mNodes.size(); ++child) {
        if (mNodes[child].parent == index) return true;
    }
    return false;
}
std::size_t TreeView::depth(std::size_t index) const noexcept {
    std::size_t result = 0;
    std::size_t parent = index < mNodes.size() ? mNodes[index].parent : kTreeRoot;
    while (parent != kTreeRoot && parent < index) {
        ++result;
        parent = mNodes[parent].parent;
    }
    return result;
}
std::optional<std::size_t> TreeView::nodeAtVisibleRow(std::size_t row) const noexcept {
    std::size_t current = 0;
    for (std::size_t index = 0; index < mNodes.size(); ++index) {
        if (!visible(index)) continue;
        if (current++ == row) return index;
    }
    return std::nullopt;
}
std::optional<std::size_t> TreeView::visibleRowOf(std::size_t index) const noexcept {
    if (!visible(index)) return std::nullopt;
    std::size_t row = 0;
    for (std::size_t current = 0; current < index; ++current) if (visible(current)) ++row;
    return row;
}
std::optional<std::size_t> TreeView::adjacentVisible(std::size_t index, bool backwards) const noexcept {
    const auto row = visibleRowOf(index);
    if (!row.has_value()) return nodeAtVisibleRow(0);
    const std::size_t count = visibleNodeCount();
    if (count == 0) return std::nullopt;
    const std::size_t target = backwards ? (*row == 0 ? count - 1U : *row - 1U)
                                        : (*row + 1U) % count;
    return nodeAtVisibleRow(target);
}
float TreeView::maximumScrollOffset() const noexcept {
    const double extent = static_cast<double>(visibleNodeCount()) * std::max(mStyle.rowHeight, 0.0F);
    const double result = std::max(
        extent - std::max(static_cast<double>(frame().height()), 0.0), 0.0);
    return static_cast<float>(std::min(
        result, static_cast<double>(std::numeric_limits<float>::max())));
}
bool TreeView::updateScroll(float offset) noexcept {
    if (!std::isfinite(offset)) offset = 0.0F;
    const float next = std::clamp(offset, 0.0F, maximumScrollOffset());
    if (next == mScrollOffset) return false;
    mScrollOffset = next;
    markPaintDirty();
    return true;
}
bool TreeView::select(std::size_t index, bool notify) {
    if (index >= mNodes.size() || !visible(index) || mSelected == index) return false;
    mSelected = index;
    reveal(index);
    markPaintDirty();
    if (notify) mOnSelectionChanged(index);
    return true;
}
void TreeView::reveal(std::size_t index) noexcept {
    const auto row = visibleRowOf(index);
    if (!row.has_value()) return;
    const float top = static_cast<float>(*row) * mStyle.rowHeight;
    const float bottom = top + mStyle.rowHeight;
    if (top < mScrollOffset) mScrollOffset = top;
    else if (bottom > mScrollOffset + frame().height()) mScrollOffset = bottom - frame().height();
    mScrollOffset = std::clamp(mScrollOffset, 0.0F, maximumScrollOffset());
}

} // namespace henia::ui
