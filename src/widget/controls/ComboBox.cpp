#include "henia/ui/widget/controls/ComboBox.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

ComboBox::ComboBox(std::vector<std::string> items, std::size_t selected, ComboBoxStyle style)
    : Widget(WidgetKind::ComboBox), mItems(std::move(items)), mStyle(style) {
    mSelected = mItems.empty() ? 0 : std::min(selected, mItems.size() - 1U);
    mHighlighted = mSelected;
}

void ComboBox::setItems(std::vector<std::string> items) {
    mItems = std::move(items);
    mSelected = mItems.empty() ? 0 : std::min(mSelected, mItems.size() - 1U);
    mHighlighted = mSelected;
    mFirstVisible = 0;
    if (mItems.empty()) mOpen = false;
    markLayoutDirty();
}
std::size_t ComboBox::itemCount() const noexcept { return mItems.size(); }
std::string_view ComboBox::item(std::size_t index) const noexcept {
    return index < mItems.size() ? std::string_view(mItems[index]) : std::string_view{};
}
void ComboBox::setSelectedIndex(std::size_t index) noexcept { static_cast<void>(select(index, false)); }
std::size_t ComboBox::selectedIndex() const noexcept { return mSelected; }
std::string_view ComboBox::selectedText() const noexcept { return item(mSelected); }
void ComboBox::setOpen(bool openValue) noexcept {
    openValue = openValue && !mItems.empty();
    if (mOpen == openValue) return;
    mOpen = openValue;
    mHighlighted = mSelected;
    ensureHighlightedVisible();
    markLayoutDirty();
}
bool ComboBox::open() const noexcept { return mOpen; }
void ComboBox::setStyle(ComboBoxStyle style) noexcept { mStyle = style; markLayoutDirty(); }
const ComboBoxStyle& ComboBox::style() const noexcept { return mStyle; }
void ComboBox::setOnSelectionChanged(Callback<std::size_t> callback) noexcept {
    mOnSelectionChanged = callback;
}
bool ComboBox::acceptsPointerInput() const noexcept { return true; }
bool ComboBox::acceptsKeyboardFocus() const noexcept { return true; }

bool ComboBox::handleInput(const InputEvent& event) {
    if (event.kind == InputEventKind::FocusLost) {
        if (mOpen) setOpen(false);
        return true;
    }
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerScroll && mOpen && contains(event.position)) {
        if (event.scrollY > 0.0F && mFirstVisible > 0) --mFirstVisible;
        else if (event.scrollY < 0.0F && mFirstVisible + visibleCount() < mItems.size()) {
            ++mFirstVisible;
        } else return false;
        markPaintDirty();
        return true;
    }
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) return true;
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary && contains(event.position)) {
        const float localY = event.position.y - frame().min.y;
        if (localY < mStyle.rowHeight) {
            setOpen(!mOpen);
        } else if (mOpen && mStyle.rowHeight > 0.0F) {
            const std::size_t row = static_cast<std::size_t>(
                (localY - mStyle.rowHeight) / mStyle.rowHeight);
            const std::size_t index = mFirstVisible + row;
            if (index < mItems.size() && row < visibleCount()) {
                static_cast<void>(select(index, true));
                setOpen(false);
            }
        }
        return true;
    }
    if (event.kind != InputEventKind::KeyDown || !focused() || mItems.empty()) return false;
    switch (event.key) {
        case KeyCode::Enter:
        case KeyCode::Space:
            if (mOpen) { static_cast<void>(select(mHighlighted, true)); setOpen(false); }
            else setOpen(true);
            return true;
        case KeyCode::Escape:
            if (!mOpen) return false;
            setOpen(false);
            return true;
        case KeyCode::Up:
            if (mOpen) {
                mHighlighted = mHighlighted == 0 ? mItems.size() - 1U : mHighlighted - 1U;
                ensureHighlightedVisible();
                markPaintDirty();
                return true;
            }
            return select(mSelected == 0 ? mItems.size() - 1U : mSelected - 1U, true);
        case KeyCode::Down:
            if (mOpen) {
                mHighlighted = (mHighlighted + 1U) % mItems.size();
                ensureHighlightedVisible();
                markPaintDirty();
                return true;
            }
            return select((mSelected + 1U) % mItems.size(), true);
        case KeyCode::Home:
            if (mOpen) { mHighlighted = 0; ensureHighlightedVisible(); markPaintDirty(); return true; }
            return select(0, true);
        case KeyCode::End:
            if (mOpen) { mHighlighted = mItems.size() - 1U; ensureHighlightedVisible(); markPaintDirty(); return true; }
            return select(mItems.size() - 1U, true);
        default: return false;
    }
}

Vec2 ComboBox::onMeasure(TextPainter&, Constraints) {
    return {mStyle.width, mStyle.rowHeight * static_cast<float>(1U + (mOpen ? visibleCount() : 0U))};
}

void ComboBox::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    const Rect header{{frame().min.x, frame().min.y},
                      {frame().max.x, std::min(frame().max.y, frame().min.y + mStyle.rowHeight)}};
    canvas.fillRect(header, hovered() ? mStyle.hover : mStyle.background, mStyle.radius);
    canvas.strokeRect(header, focused() ? mStyle.focus : mStyle.border, mStyle.radius, 1.0F);
    const std::string_view selected = selectedText();
    const std::string_view display = selected.empty() ? std::string_view("Select…") : selected;
    const TextMetrics selectedMetrics = text.measure(mStyle.font, mStyle.fontSize, display);
    text.draw(canvas, mStyle.font, mStyle.fontSize,
        {header.min.x + mStyle.horizontalPadding,
         header.min.y + std::max((header.height() - selectedMetrics.height) * 0.5F, 0.0F)},
        selected.empty() ? mStyle.muted : mStyle.text, display);
    const float arrowX = header.max.x - 13.0F;
    const float arrowY = (header.min.y + header.max.y) * 0.5F;
    canvas.line({arrowX - 4.0F, arrowY - (mOpen ? -2.0F : 2.0F)}, {arrowX, arrowY + (mOpen ? -2.0F : 2.0F)},
        mStyle.text, 1.5F, LineCap::Round);
    canvas.line({arrowX, arrowY + (mOpen ? -2.0F : 2.0F)}, {arrowX + 4.0F, arrowY - (mOpen ? -2.0F : 2.0F)},
        mStyle.text, 1.5F, LineCap::Round);
    if (!mOpen) return;
    for (std::size_t row = 0; row < visibleCount(); ++row) {
        const std::size_t index = mFirstVisible + row;
        const Rect bounds{{frame().min.x, header.max.y + mStyle.rowHeight * static_cast<float>(row)},
            {frame().max.x, header.max.y + mStyle.rowHeight * static_cast<float>(row + 1U)}};
        canvas.fillRect(bounds, index == mHighlighted ? mStyle.selected : mStyle.background, 0.0F);
        const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, mItems[index]);
        text.draw(canvas, mStyle.font, mStyle.fontSize,
            {bounds.min.x + mStyle.horizontalPadding,
             bounds.min.y + std::max((bounds.height() - metrics.height) * 0.5F, 0.0F)},
            mStyle.text, mItems[index]);
    }
    canvas.strokeRect({{frame().min.x, header.max.y}, frame().max}, mStyle.border, 0.0F, 1.0F);
}

std::size_t ComboBox::visibleCount() const noexcept {
    return std::min(mItems.size(), std::max<std::size_t>(mStyle.maximumVisibleItems, 1U));
}
void ComboBox::ensureHighlightedVisible() noexcept {
    const std::size_t count = visibleCount();
    if (count == 0) { mFirstVisible = 0; return; }
    if (mHighlighted < mFirstVisible) mFirstVisible = mHighlighted;
    else if (mHighlighted >= mFirstVisible + count) mFirstVisible = mHighlighted - count + 1U;
}
bool ComboBox::select(std::size_t index, bool notify) {
    if (index >= mItems.size() || index == mSelected) return false;
    mSelected = index;
    mHighlighted = index;
    ensureHighlightedVisible();
    markPaintDirty();
    if (notify) mOnSelectionChanged(mSelected);
    return true;
}

} // namespace henia::ui
