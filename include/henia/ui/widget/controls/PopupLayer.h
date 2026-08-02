#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <memory>

namespace henia::ui {

class PopupBackdrop;

struct PopupLayerStyle final {
    Color backdrop{0.0F, 0.0F, 0.0F, 0.48F};
    bool modal = true;
    bool dismissOnBackdrop = true;
};

// Owns content, backdrop, and popup in that paint order. popupBounds are local
// to the layer and clamped to its arranged viewport.
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

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onArrange(TextPainter& text, Rect frame) override;

private:
    friend class PopupBackdrop;
    void backdropActivated();

    PopupLayerStyle mStyle{};
    Callback<> mOnDismissed{};
    Widget* mContent = nullptr;
    PopupBackdrop* mBackdrop = nullptr;
    Widget* mPopup = nullptr;
    Rect mPopupBounds{};
    bool mOpen = false;
};

} // namespace henia::ui
