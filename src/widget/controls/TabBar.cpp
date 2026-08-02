#include "henia/ui/widget/controls/TabBar.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

TabBar::TabBar(std::vector<std::string> tabs, std::size_t selected, TabBarStyle style)
    : Widget(WidgetKind::TabBar), mTabs(std::move(tabs)), mStyle(style) {
    mSelected = mTabs.empty() ? 0 : std::min(selected, mTabs.size() - 1U);
}

void TabBar::setTabs(std::vector<std::string> tabs) {
    mTabs = std::move(tabs);
    mSelected = mTabs.empty() ? 0 : std::min(mSelected, mTabs.size() - 1U);
    markLayoutDirty();
}
std::size_t TabBar::tabCount() const noexcept { return mTabs.size(); }
std::string_view TabBar::tab(std::size_t index) const noexcept {
    return index < mTabs.size() ? std::string_view(mTabs[index]) : std::string_view{};
}
void TabBar::setSelectedIndex(std::size_t index) noexcept { static_cast<void>(select(index, false)); }
std::size_t TabBar::selectedIndex() const noexcept { return mSelected; }
void TabBar::setStyle(TabBarStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void TabBar::setOnSelectionChanged(Callback<std::size_t> callback) noexcept {
    mOnSelectionChanged = callback;
}
bool TabBar::acceptsPointerInput() const noexcept { return true; }
bool TabBar::acceptsKeyboardFocus() const noexcept { return true; }

bool TabBar::handleInput(const InputEvent& event) {
    if (!enabled() || mTabs.empty()) return false;
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) return true;
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary && contains(event.position)) {
        static_cast<void>(select(indexAt(event.position.x), true));
        return true;
    }
    if (event.kind != InputEventKind::KeyDown || !focused()) return false;
    switch (event.key) {
        case KeyCode::Left:
            return select(mSelected == 0 ? mTabs.size() - 1U : mSelected - 1U, true);
        case KeyCode::Right:
            return select((mSelected + 1U) % mTabs.size(), true);
        case KeyCode::Home: return select(0, true);
        case KeyCode::End: return select(mTabs.size() - 1U, true);
        default: return false;
    }
}

Vec2 TabBar::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void TabBar::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    if (mTabs.empty()) return;
    const float tabWidth = frame().width() / static_cast<float>(mTabs.size());
    for (std::size_t index = 0; index < mTabs.size(); ++index) {
        const Rect bounds{{frame().min.x + tabWidth * static_cast<float>(index), frame().min.y},
            {frame().min.x + tabWidth * static_cast<float>(index + 1U), frame().max.y}};
        const bool active = index == mSelected;
        if (active) {
            canvas.fillRect(bounds, mStyle.active, mStyle.radius);
            canvas.fillRect({{bounds.min.x + 4.0F, bounds.max.y - 2.0F},
                             {bounds.max.x - 4.0F, bounds.max.y}}, mStyle.accent, 1.0F);
        }
        const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, mTabs[index]);
        text.draw(canvas, mStyle.font, mStyle.fontSize,
            {bounds.min.x + std::max((bounds.width() - metrics.width) * 0.5F, 0.0F),
             bounds.min.y + std::max((bounds.height() - metrics.height) * 0.5F, 0.0F)},
            active ? mStyle.activeText : mStyle.text, mTabs[index]);
    }
    if (focused()) canvas.strokeRect(frame(), mStyle.focus, mStyle.radius, 1.0F);
}

bool TabBar::select(std::size_t index, bool notify) {
    if (index >= mTabs.size() || index == mSelected) return false;
    mSelected = index;
    markPaintDirty();
    if (notify) mOnSelectionChanged(mSelected);
    return true;
}

std::size_t TabBar::indexAt(float x) const noexcept {
    if (mTabs.empty() || frame().width() <= 0.0F) return 0;
    const float amount = std::clamp((x - frame().min.x) / frame().width(), 0.0F, 0.999999F);
    return std::min(static_cast<std::size_t>(amount * static_cast<float>(mTabs.size())),
        mTabs.size() - 1U);
}

} // namespace henia::ui
