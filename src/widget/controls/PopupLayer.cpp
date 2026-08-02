#include "henia/ui/widget/controls/PopupLayer.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

class PopupBackdrop final : public Widget {
public:
    explicit PopupBackdrop(PopupLayer& owner) noexcept : mOwner(&owner) {}
    [[nodiscard]] bool acceptsPointerInput() const noexcept override { return true; }
    [[nodiscard]] bool handleInput(const InputEvent& event) override {
        if (event.kind == InputEventKind::PointerDown
            && event.button == PointerButton::Primary) return true;
        if (event.kind == InputEventKind::PointerUp
            && event.button == PointerButton::Primary) {
            mOwner->backdropActivated();
            return true;
        }
        return false;
    }
protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter&, Constraints constraints) override {
        return constraints.maximum;
    }
    void onPaint(Canvas& canvas, TextPainter&, const Theme&) override {
        canvas.fillRect(frame(), mOwner->backdropColor(), 0.0F);
    }
private:
    PopupLayer* mOwner = nullptr;
};

PopupLayer::PopupLayer(
    std::unique_ptr<Widget> contentValue,
    std::unique_ptr<Widget> popupValue,
    Rect popupBoundsValue,
    PopupLayerStyle style)
    : Widget(WidgetKind::PopupLayer), mStyle(style), mPopupBounds(popupBoundsValue) {
    if (contentValue != nullptr) {
        mContent = contentValue.get();
        addChild(std::move(contentValue));
    }
    auto backdrop = std::make_unique<PopupBackdrop>(*this);
    mBackdrop = backdrop.get();
    mBackdrop->setVisible(false);
    addChild(std::move(backdrop));
    if (popupValue != nullptr) {
        mPopup = popupValue.get();
        mPopup->setVisible(false);
        addChild(std::move(popupValue));
    }
}

Widget* PopupLayer::content() const noexcept { return mContent; }
Widget* PopupLayer::popup() const noexcept { return mPopup; }
void PopupLayer::setPopupBounds(Rect bounds) noexcept {
    if (mPopupBounds == bounds) return;
    mPopupBounds = bounds;
    markLayoutDirty();
}
Rect PopupLayer::popupBounds() const noexcept { return mPopupBounds; }
void PopupLayer::setOpen(bool openValue) {
    openValue = openValue && mPopup != nullptr;
    if (mOpen == openValue) return;
    mOpen = openValue;
    mBackdrop->setVisible(mOpen && mStyle.modal);
    if (mPopup != nullptr) mPopup->setVisible(mOpen);
    markLayoutDirty();
}
bool PopupLayer::open() const noexcept { return mOpen; }
void PopupLayer::setModal(bool modalValue) {
    if (mStyle.modal == modalValue) return;
    mStyle.modal = modalValue;
    mBackdrop->setVisible(mOpen && mStyle.modal);
    markPaintDirty();
}
bool PopupLayer::modal() const noexcept { return mStyle.modal; }
void PopupLayer::setDismissOnBackdrop(bool dismiss) noexcept { mStyle.dismissOnBackdrop = dismiss; }
void PopupLayer::setBackdropColor(Color color) noexcept {
    if (mStyle.backdrop == color) return;
    mStyle.backdrop = color;
    mBackdrop->markPaintDirty();
}
Color PopupLayer::backdropColor() const noexcept { return mStyle.backdrop; }
void PopupLayer::setOnDismissed(Callback<> callback) noexcept { mOnDismissed = callback; }

Vec2 PopupLayer::onMeasure(TextPainter&, Constraints constraints) { return constraints.maximum; }

void PopupLayer::onArrange(TextPainter& text, Rect arrangedFrame) {
    if (mContent != nullptr) {
        const Vec2 size{std::max(arrangedFrame.width(), 0.0F), std::max(arrangedFrame.height(), 0.0F)};
        static_cast<void>(mContent->measure(text, {size, size}));
        mContent->arrange(text, arrangedFrame);
    }
    if (mBackdrop != nullptr && mBackdrop->visible()) {
        const Vec2 size{std::max(arrangedFrame.width(), 0.0F), std::max(arrangedFrame.height(), 0.0F)};
        static_cast<void>(mBackdrop->measure(text, {size, size}));
        mBackdrop->arrange(text, arrangedFrame);
    }
    if (mPopup != nullptr && mPopup->visible()) {
        const float width = std::clamp(mPopupBounds.width(), 0.0F, std::max(arrangedFrame.width(), 0.0F));
        const float height = std::clamp(mPopupBounds.height(), 0.0F, std::max(arrangedFrame.height(), 0.0F));
        const float x = std::clamp(arrangedFrame.min.x + mPopupBounds.min.x,
            arrangedFrame.min.x, arrangedFrame.max.x - width);
        const float y = std::clamp(arrangedFrame.min.y + mPopupBounds.min.y,
            arrangedFrame.min.y, arrangedFrame.max.y - height);
        const Vec2 size{width, height};
        static_cast<void>(mPopup->measure(text, {size, size}));
        mPopup->arrange(text, {{x, y}, {x + width, y + height}});
    }
}

void PopupLayer::backdropActivated() {
    if (!mOpen || !mStyle.dismissOnBackdrop) return;
    setOpen(false);
    mOnDismissed();
}

} // namespace henia::ui
