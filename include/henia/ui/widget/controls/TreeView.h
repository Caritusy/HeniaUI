#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace henia::ui {

inline constexpr std::size_t kTreeRoot = std::numeric_limits<std::size_t>::max();

struct TreeViewNode final {
    std::string text;
    std::size_t parent = kTreeRoot;
    bool expanded = true;
};

struct TreeViewStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color text{0.86F, 0.92F, 0.97F, 1.0F};
    Color background{0.025F, 0.038F, 0.058F, 1.0F};
    Color alternate{0.032F, 0.049F, 0.072F, 1.0F};
    Color selected{0.08F, 0.24F, 0.32F, 1.0F};
    Color disclosure{0.58F, 0.70F, 0.79F, 1.0F};
    Color focus{0.10F, 0.72F, 0.91F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    float width = 300.0F;
    float height = 260.0F;
    float rowHeight = 28.0F;
    float indentation = 18.0F;
    float horizontalPadding = 7.0F;
    float wheelRows = 3.0F;
    float radius = 6.0F;
};

class TreeView final : public Widget {
public:
    explicit TreeView(std::vector<TreeViewNode> nodes = {}, TreeViewStyle style = {});

    void setNodes(std::vector<TreeViewNode> nodes);
    [[nodiscard]] std::size_t nodeCount() const noexcept;
    [[nodiscard]] const TreeViewNode* node(std::size_t index) const noexcept;
    [[nodiscard]] bool setExpanded(std::size_t index, bool expanded, bool notify = false);
    void setSelectedIndex(std::optional<std::size_t> index) noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedIndex() const noexcept;
    [[nodiscard]] std::size_t visibleNodeCount() const noexcept;
    [[nodiscard]] std::size_t lastPaintedRowCount() const noexcept;
    void setScrollOffset(float offset) noexcept;
    [[nodiscard]] float scrollOffset() const noexcept;
    void setStyle(TreeViewStyle style) noexcept;
    void setOnSelectionChanged(Callback<std::size_t> callback) noexcept;
    void setOnExpansionChanged(Callback<std::size_t, bool> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    [[nodiscard]] bool visible(std::size_t index) const noexcept;
    [[nodiscard]] bool hasChildren(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t depth(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> nodeAtVisibleRow(std::size_t row) const noexcept;
    [[nodiscard]] std::optional<std::size_t> visibleRowOf(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> adjacentVisible(std::size_t index, bool backwards) const noexcept;
    [[nodiscard]] float maximumScrollOffset() const noexcept;
    [[nodiscard]] bool updateScroll(float offset) noexcept;
    [[nodiscard]] bool select(std::size_t index, bool notify);
    void reveal(std::size_t index) noexcept;

    std::vector<TreeViewNode> mNodes;
    TreeViewStyle mStyle{};
    Callback<std::size_t> mOnSelectionChanged{};
    Callback<std::size_t, bool> mOnExpansionChanged{};
    std::optional<std::size_t> mSelected;
    std::size_t mLastPaintedRows = 0;
    float mScrollOffset = 0.0F;
};

} // namespace henia::ui
