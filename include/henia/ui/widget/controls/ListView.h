#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace henia::ui {

struct ListViewStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color text{0.86F, 0.92F, 0.97F, 1.0F};
    Color background{0.025F, 0.038F, 0.058F, 1.0F};
    Color alternate{0.032F, 0.049F, 0.072F, 1.0F};
    Color selected{0.08F, 0.24F, 0.32F, 1.0F};
    Color focus{0.10F, 0.72F, 0.91F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    float width = 280.0F;
    float height = 240.0F;
    float rowHeight = 28.0F;
    float horizontalPadding = 9.0F;
    float wheelRows = 3.0F;
    float radius = 6.0F;
    std::size_t overscanRows = 1;
};

using ListItemKey = std::uint64_t;

// The source and all callback contexts are non-owning and must outlive the
// ListView. createWidget is called only when the viewport needs a larger pool;
// bindWidget retargets an existing presentation widget to one logical item.
// itemKey values must be unique and stable across refreshes. An absent
// itemExtent callback selects the fixed ListViewStyle::rowHeight path.
struct VirtualListSource final {
    std::size_t itemCount = 0;
    ValueCallback<ListItemKey, std::size_t> itemKey{};
    ValueCallback<float, std::size_t> itemExtent{};
    ValueCallback<std::unique_ptr<Widget>> createWidget{};
    Callback<Widget&, std::size_t, ListItemKey, bool> bindWidget{};
};

struct RealizedListItem final {
    Widget* widget = nullptr;
    std::size_t index = 0;
    ListItemKey key = 0;
    bool selected = false;
};

class ListView final : public Widget {
public:
    explicit ListView(std::vector<std::string> items = {}, ListViewStyle style = {});

    void setItems(std::vector<std::string> items);
    void setVirtualItems(
        std::size_t itemCount,
        ValueCallback<std::string_view, std::size_t> labelProvider);
    // Lazily creates a viewport-sized presentation pool and recycles it while
    // scrolling. Source callbacks may propagate exceptions to the host.
    void setRecycledItems(VirtualListSource source);
    // Rebuilds optional variable-height offsets and preserves selection by key
    // when the external data set is reordered or resized.
    void refreshRecycledItems(std::size_t itemCount);
    [[nodiscard]] std::size_t itemCount() const noexcept;
    [[nodiscard]] std::string_view item(std::size_t index) const;
    [[nodiscard]] ListItemKey itemKey(std::size_t index) const;
    void setSelectedIndex(std::optional<std::size_t> index);
    [[nodiscard]] std::optional<std::size_t> selectedIndex() const noexcept;
    void setSelectedItemKey(std::optional<ListItemKey> key);
    [[nodiscard]] std::optional<ListItemKey> selectedItemKey() const noexcept;
    void setScrollOffset(float offset) noexcept;
    [[nodiscard]] float scrollOffset() const noexcept;
    [[nodiscard]] std::size_t lastPaintedRowCount() const noexcept;
    [[nodiscard]] std::span<const RealizedListItem> realizedItems() const noexcept;
    [[nodiscard]] std::size_t pooledWidgetCount() const noexcept;
    [[nodiscard]] std::uint64_t widgetCreationCount() const noexcept;
    [[nodiscard]] std::uint64_t widgetBindCount() const noexcept;
    void setStyle(ListViewStyle style);
    void setOnSelectionChanged(Callback<std::size_t> callback) noexcept;
    void setOnItemSelectionChanged(
        Callback<std::size_t, ListItemKey> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool allowsChildInteraction() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onArrange(TextPainter& text, Rect frame) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;
    [[nodiscard]] bool clipsChildren() const noexcept override;
    [[nodiscard]] Rect childrenClipRect() const noexcept override;

private:
    enum class SourceKind : std::uint8_t {
        OwnedLabels,
        VirtualLabels,
        RecycledWidgets,
    };

    [[nodiscard]] bool usesRecycledWidgets() const noexcept;
    [[nodiscard]] float itemTop(std::size_t index) const noexcept;
    [[nodiscard]] float itemExtent(std::size_t index) const;
    [[nodiscard]] float totalExtent() const noexcept;
    [[nodiscard]] std::size_t indexAtOffset(float offset) const noexcept;
    [[nodiscard]] std::size_t firstRealizedIndex() const noexcept;
    [[nodiscard]] std::size_t realizedItemLimit(std::size_t first) const noexcept;
    void rebuildItemOffsets();
    void resetRecycledPool();
    void realizeItems(TextPainter& text, Rect arrangedFrame);
    [[nodiscard]] std::optional<std::size_t> findItemByKey(ListItemKey key) const;
    [[nodiscard]] float maximumScrollOffset() const noexcept;
    [[nodiscard]] bool updateScroll(float offset) noexcept;
    [[nodiscard]] bool select(std::size_t index, bool notify);
    void reveal(std::size_t index) noexcept;

    std::vector<std::string> mItems;
    ValueCallback<std::string_view, std::size_t> mLabelProvider{};
    VirtualListSource mRecycledSource{};
    std::vector<double> mItemOffsets;
    std::vector<RealizedListItem> mRealizedItems;
    ListViewStyle mStyle{};
    Callback<std::size_t> mOnSelectionChanged{};
    Callback<std::size_t, ListItemKey> mOnItemSelectionChanged{};
    std::optional<std::size_t> mSelected;
    std::optional<ListItemKey> mSelectedKey;
    std::size_t mVirtualItemCount = 0;
    std::size_t mRealizedItemCount = 0;
    std::size_t mLastPaintedRows = 0;
    std::uint64_t mWidgetCreationCount = 0;
    std::uint64_t mWidgetBindCount = 0;
    float mScrollOffset = 0.0F;
    SourceKind mSourceKind = SourceKind::OwnedLabels;
};

} // namespace henia::ui
