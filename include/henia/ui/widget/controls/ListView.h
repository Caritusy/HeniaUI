#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <optional>
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
};

class ListView final : public Widget {
public:
    explicit ListView(std::vector<std::string> items = {}, ListViewStyle style = {});

    void setItems(std::vector<std::string> items);
    void setVirtualItems(
        std::size_t itemCount,
        ValueCallback<std::string_view, std::size_t> labelProvider) noexcept;
    [[nodiscard]] std::size_t itemCount() const noexcept;
    [[nodiscard]] std::string_view item(std::size_t index) const;
    void setSelectedIndex(std::optional<std::size_t> index) noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedIndex() const noexcept;
    void setScrollOffset(float offset) noexcept;
    [[nodiscard]] float scrollOffset() const noexcept;
    [[nodiscard]] std::size_t lastPaintedRowCount() const noexcept;
    void setStyle(ListViewStyle style) noexcept;
    void setOnSelectionChanged(Callback<std::size_t> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    [[nodiscard]] float maximumScrollOffset() const noexcept;
    [[nodiscard]] bool updateScroll(float offset) noexcept;
    [[nodiscard]] bool select(std::size_t index, bool notify);
    void reveal(std::size_t index) noexcept;

    std::vector<std::string> mItems;
    ValueCallback<std::string_view, std::size_t> mLabelProvider{};
    ListViewStyle mStyle{};
    Callback<std::size_t> mOnSelectionChanged{};
    std::optional<std::size_t> mSelected;
    std::size_t mVirtualItemCount = 0;
    std::size_t mLastPaintedRows = 0;
    float mScrollOffset = 0.0F;
};

} // namespace henia::ui
