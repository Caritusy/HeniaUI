#pragma once

#include "henia/ui/Canvas.h"
#include "henia/ui/input/InputEvent.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/theme/Theme.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace henia::ui {

struct Insets final {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

struct Constraints final {
    Vec2 minimum{};
    Vec2 maximum{};
};

struct LayoutParameters final {
    float width = -1.0F;
    float height = -1.0F;
    float flexGrow = 0.0F;
    Insets margin{};
};

enum class WidgetKind : std::uint8_t {
    Generic,
    Panel,
    Label,
    Button,
    NumericInput,
};

class Widget {
public:
    explicit Widget(WidgetKind kind = WidgetKind::Generic) noexcept;
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    [[nodiscard]] WidgetKind kind() const noexcept;
    [[nodiscard]] Widget* parent() const noexcept;
    [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const noexcept;
    [[nodiscard]] Rect frame() const noexcept;
    [[nodiscard]] const LayoutParameters& layoutParameters() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool hovered() const noexcept;
    [[nodiscard]] bool pressed() const noexcept;
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] bool layoutDirty() const noexcept;
    [[nodiscard]] bool paintDirty() const noexcept;

    void setLayoutParameters(LayoutParameters parameters) noexcept;
    void setVisible(bool visible) noexcept;
    void setEnabled(bool enabled) noexcept;
    Widget& addChild(std::unique_ptr<Widget> child);

    template <typename Type, typename... Arguments>
    Type& emplaceChild(Arguments&&... arguments) {
        auto child = std::make_unique<Type>(std::forward<Arguments>(arguments)...);
        Type& reference = *child;
        addChild(std::move(child));
        return reference;
    }

    [[nodiscard]] Vec2 measure(TextPainter& text, Constraints constraints);
    void arrange(TextPainter& text, Rect frame);
    void paint(Canvas& canvas, TextPainter& text, const Theme& theme);
    [[nodiscard]] Widget* hitTest(Vec2 point) noexcept;
    [[nodiscard]] virtual bool handleInput(const InputEvent& event);

    void markLayoutDirty() noexcept;
    void markPaintDirty() noexcept;

protected:
    [[nodiscard]] virtual Vec2 onMeasure(TextPainter& text, Constraints constraints);
    virtual void onArrange(TextPainter& text, Rect frame);
    virtual void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme);
    [[nodiscard]] bool contains(Vec2 point) const noexcept;

    std::vector<std::unique_ptr<Widget>> mChildren;

private:
    friend class UiDocument;

    void setHovered(bool hovered) noexcept;
    void setPressed(bool pressed) noexcept;
    void setFocused(bool focused) noexcept;
    void clearDirtyRecursive() noexcept;

    WidgetKind mKind = WidgetKind::Generic;
    Widget* mParent = nullptr;
    Rect mFrame{};
    LayoutParameters mLayout{};
    Vec2 mMeasured{};
    bool mVisible = true;
    bool mEnabled = true;
    bool mHovered = false;
    bool mPressed = false;
    bool mFocused = false;
    bool mLayoutDirty = true;
    bool mPaintDirty = true;
};

} // namespace henia::ui
