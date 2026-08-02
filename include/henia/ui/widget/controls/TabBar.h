#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace henia::ui {

struct TabBarStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color text{0.58F, 0.66F, 0.74F, 1.0F};
    Color activeText{0.94F, 0.98F, 1.0F, 1.0F};
    Color background{0.035F, 0.05F, 0.075F, 1.0F};
    Color active{0.08F, 0.16F, 0.22F, 1.0F};
    Color accent{0.10F, 0.72F, 0.91F, 1.0F};
    Color focus{0.35F, 0.84F, 1.0F, 1.0F};
    float width = 320.0F;
    float height = 36.0F;
    float radius = 7.0F;
};

class TabBar final : public Widget {
public:
    explicit TabBar(std::vector<std::string> tabs = {}, std::size_t selected = 0,
        TabBarStyle style = {});

    void setTabs(std::vector<std::string> tabs);
    [[nodiscard]] std::size_t tabCount() const noexcept;
    [[nodiscard]] std::string_view tab(std::size_t index) const noexcept;
    void setSelectedIndex(std::size_t index) noexcept;
    [[nodiscard]] std::size_t selectedIndex() const noexcept;
    void setStyle(TabBarStyle style) noexcept;
    void setOnSelectionChanged(Callback<std::size_t> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    [[nodiscard]] bool select(std::size_t index, bool notify);
    [[nodiscard]] std::size_t indexAt(float x) const noexcept;

    std::vector<std::string> mTabs;
    TabBarStyle mStyle{};
    Callback<std::size_t> mOnSelectionChanged{};
    std::size_t mSelected = 0;
};

} // namespace henia::ui
