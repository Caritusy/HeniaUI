#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <memory>

namespace henia::ui {

class PopupBackdrop;
class PopupSurface;

struct PopupLayerStyle final {
    Color backdrop{0.0F, 0.0F, 0.0F, 0.48F};
    bool modal = true;
    bool dismissOnBackdrop = true;
};

// Owns content, backdrop, a passive input surface, and popup in that order.
// popupBounds are local to the layer and clamped to its arranged viewport.
class PopupLayer final : public Widget {
public:
    explicit PopupLayer(
        std::unique_ptr<Widget> content = {},
        std::unique_ptr<Widget> popup = {},
        Rect popupBounds = {},
        PopupLayerStyle style = {});

    [[nodiscard]] Widget* content() const noexcept;
    [[nodiscard]] Widget* popup() const noexcept;
    void setPopupBounds(Rect bounds) noexcept;
    [[nodiscard]] Rect popupBounds() const noexcept;
    void setOpen(bool open);
    [[nodiscard]] bool open() const noexcept;
    void setModal(bool modal);
    [[nodiscard]] bool modal() const noexcept;
    void setDismissOnBackdrop(bool dismiss) noexcept;
    void setBackdropColor(Color color) noexcept;
    [[nodiscard]] Color backdropColor() const noexcept;
    void setOnDismissed(Callback<> callback) noexcept;

    [[nodiscard]] Widget* hitTest(Vec2 point) noexcept override;
    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool allowsInteractionForChild(
        const Widget& child) const noexcept override;
    [[nodiscard]] bool blocksUnhandledPointerInput(
        Vec2 point) const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onArrange(TextPainter& text, Rect frame) override;

private:
    friend class PopupBackdrop;
    [[nodiscard]] bool popupContains(Vec2 point) const noexcept;
    void backdropActivated();

    PopupLayerStyle mStyle{};
    Callback<> mOnDismissed{};
    Widget* mContent = nullptr;
    PopupBackdrop* mBackdrop = nullptr;
    PopupSurface* mSurface = nullptr;
    Widget* mPopup = nullptr;
    Rect mPopupBounds{};
    bool mOpen = false;
};

} // namespace henia::ui
