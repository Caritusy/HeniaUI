#include "henia/ui/widget/controls/PopupLayer.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

class PopupBackdrop final : public Widget {
public:
    explicit PopupBackdrop(PopupLayer& owner) noexcept : mOwner(&owner) {}
    [[nodiscard]] bool acceptsPointerInput() const noexcept override { return true; }
    [[nodiscard]] bool handleInput(const InputEvent& event) override {
        switch (event.kind) {
            case InputEventKind::PointerDown:
            case InputEventKind::PointerMove:
            case InputEventKind::PointerScroll:
                return true;
            case InputEventKind::PointerUp:
                if (event.button == PointerButton::Primary
                    && !mOwner->popupContains(event.position)) {
                    mOwner->backdropActivated();
                }
                return true;
            default:
                return false;
        }
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

class PopupSurface final : public Widget {
public:
    [[nodiscard]] bool acceptsPointerInput() const noexcept override { return true; }
    [[nodiscard]] bool handleInput(const InputEvent& event) override {
        switch (event.kind) {
            case InputEventKind::PointerDown:
            case InputEventKind::PointerUp:
            case InputEventKind::PointerMove:
            case InputEventKind::PointerScroll:
                return true;
            default:
                return false;
        }
    }
protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter&, Constraints constraints) override {
        return constraints.maximum;
    }
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
    auto surface = std::make_unique<PopupSurface>();
    mSurface = surface.get();
    mSurface->setVisible(false);
    addChild(std::move(surface));
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
    mSurface->setVisible(mOpen);
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

Widget* PopupLayer::hitTest(Vec2 point) noexcept {
    if (!visible() || !enabled() || !contains(point)) {
        return nullptr;
    }
    if (allowsChildInteraction()) {
        for (auto iterator = mChildren.rbegin(); iterator != mChildren.rend(); ++iterator) {
            if (!allowsInteractionForChild(**iterator)) {
                continue;
            }
            if (Widget* hit = (*iterator)->hitTest(point)) {
                return hit;
            }
        }
    }
    return blocksUnhandledPointerInput(point) ? this : nullptr;
}

bool PopupLayer::acceptsPointerInput() const noexcept { return mOpen; }

bool PopupLayer::allowsInteractionForChild(const Widget& child) const noexcept {
    if (!mOpen || !mStyle.modal) {
        return true;
    }
    return &child == mBackdrop || &child == mSurface || &child == mPopup;
}

bool PopupLayer::blocksUnhandledPointerInput(Vec2 point) const noexcept {
    return mOpen && (mStyle.modal || popupContains(point));
}

bool PopupLayer::handleInput(const InputEvent& event) {
    if (!blocksUnhandledPointerInput(event.position)) {
        return false;
    }
    switch (event.kind) {
        case InputEventKind::PointerDown:
        case InputEventKind::PointerUp:
        case InputEventKind::PointerMove:
        case InputEventKind::PointerScroll:
            return true;
        default:
            return false;
    }
}

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
    Rect popupFrame{};
    bool hasPopupFrame = false;
    if (mOpen) {
        const float width = std::clamp(mPopupBounds.width(), 0.0F, std::max(arrangedFrame.width(), 0.0F));
        const float height = std::clamp(mPopupBounds.height(), 0.0F, std::max(arrangedFrame.height(), 0.0F));
        const float x = std::clamp(arrangedFrame.min.x + mPopupBounds.min.x,
            arrangedFrame.min.x, arrangedFrame.max.x - width);
        const float y = std::clamp(arrangedFrame.min.y + mPopupBounds.min.y,
            arrangedFrame.min.y, arrangedFrame.max.y - height);
        popupFrame = {{x, y}, {x + width, y + height}};
        hasPopupFrame = true;
    }
    if (mSurface != nullptr && mSurface->visible() && hasPopupFrame) {
        const Vec2 size{popupFrame.width(), popupFrame.height()};
        static_cast<void>(mSurface->measure(text, {size, size}));
        mSurface->arrange(text, popupFrame);
    }
    if (mPopup != nullptr && mPopup->visible()) {
        const Vec2 size{popupFrame.width(), popupFrame.height()};
        static_cast<void>(mPopup->measure(text, {size, size}));
        mPopup->arrange(text, popupFrame);
    }
}

bool PopupLayer::popupContains(Vec2 point) const noexcept {
    if (!mOpen || mSurface == nullptr || !mSurface->visible()) {
        return false;
    }
    const Rect bounds = mSurface->frame();
    return point.x >= bounds.min.x && point.y >= bounds.min.y
        && point.x < bounds.max.x && point.y < bounds.max.y;
}

void PopupLayer::backdropActivated() {
    if (!mOpen || !mStyle.dismissOnBackdrop) return;
    setOpen(false);
    mOnDismissed();
}

} // namespace henia::ui
