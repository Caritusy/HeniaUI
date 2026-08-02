#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace henia::ui {

struct ComboBoxStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color text{0.90F, 0.95F, 0.98F, 1.0F};
    Color muted{0.48F, 0.57F, 0.66F, 1.0F};
    Color background{0.035F, 0.052F, 0.078F, 1.0F};
    Color hover{0.055F, 0.085F, 0.12F, 1.0F};
    Color selected{0.08F, 0.19F, 0.25F, 1.0F};
    Color border{0.14F, 0.22F, 0.30F, 1.0F};
    Color focus{0.10F, 0.72F, 0.91F, 1.0F};
    float width = 220.0F;
    float rowHeight = 36.0F;
    float radius = 7.0F;
    float horizontalPadding = 10.0F;
    std::size_t maximumVisibleItems = 6;
};

class ComboBox final : public Widget {
public:
    explicit ComboBox(std::vector<std::string> items = {}, std::size_t selected = 0,
        ComboBoxStyle style = {});

    void setItems(std::vector<std::string> items);
    [[nodiscard]] std::size_t itemCount() const noexcept;
    [[nodiscard]] std::string_view item(std::size_t index) const noexcept;
    void setSelectedIndex(std::size_t index) noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept;
    [[nodiscard]] std::string_view selectedText() const noexcept;
    void setOpen(bool open) noexcept;
    [[nodiscard]] bool open() const noexcept;
    void setStyle(ComboBoxStyle style) noexcept;
    [[nodiscard]] const ComboBoxStyle& style() const noexcept;
    void setOnSelectionChanged(Callback<std::size_t> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    [[nodiscard]] std::size_t visibleCount() const noexcept;
    void ensureHighlightedVisible() noexcept;
    [[nodiscard]] bool select(std::size_t index, bool notify);

    std::vector<std::string> mItems;
    ComboBoxStyle mStyle{};
    Callback<std::size_t> mOnSelectionChanged{};
    std::size_t mSelected = 0;
    std::size_t mHighlighted = 0;
    std::size_t mFirstVisible = 0;
    bool mOpen = false;
};

} // namespace henia::ui
