#include "henia/ui/widget/UiDocument.h"

namespace henia::ui {

UiDocument::UiDocument(TextPainter& text, Theme theme) : mText(&text), mTheme(theme) {}

void UiDocument::reserve(
    std::size_t commandCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy) {
    mFrame.reserve(commandCapacity, batchCapacity, capacityPolicy);
}

void UiDocument::setRoot(std::unique_ptr<Widget> rootWidget) {
    clearInteraction();
    mRoot = std::move(rootWidget);
    if (mRoot != nullptr) {
        mRoot->markLayoutDirty();
    }
}

Widget* UiDocument::root() const noexcept { return mRoot.get(); }

void UiDocument::setViewport(Vec2 value) noexcept {
    if (mViewport == value) {
        return;
    }
    mViewport = value;
    if (mRoot != nullptr) {
        mRoot->markLayoutDirty();
    }
}

Vec2 UiDocument::viewport() const noexcept { return mViewport; }

void UiDocument::setTheme(Theme themeValue) noexcept {
    mTheme = themeValue;
    if (mRoot != nullptr) {
        mRoot->markPaintDirty();
    }
}

const Theme& UiDocument::theme() const noexcept { return mTheme; }

const RenderPacket& UiDocument::compose() {
    if (mRoot == nullptr || mViewport.x <= 0.0F || mViewport.y <= 0.0F) {
        Canvas& canvas = mFrame.begin();
        static_cast<void>(canvas);
        return mFrame.finish();
    }

    if (mRoot->layoutDirty()) {
        const Constraints constraints{{0.0F, 0.0F}, mViewport};
        static_cast<void>(mRoot->measure(*mText, constraints));
        mRoot->arrange(*mText, {{0.0F, 0.0F}, mViewport});
        ++mStatistics.layoutPasses;
    }
    if (mRoot->paintDirty() || mStatistics.revision == 0) {
        Canvas& canvas = mFrame.begin();
        mRoot->paint(canvas, *mText, mTheme);
        const RenderPacket& packet = mFrame.finish();
        mRoot->clearDirtyRecursive();
        ++mStatistics.paintPasses;
        ++mStatistics.revision;
        return packet;
    }

    ++mStatistics.cachedFrames;
    return mFrame.packet();
}

bool UiDocument::dispatch(const InputEvent& event) {
    if (mRoot == nullptr) {
        return false;
    }
    ++mStatistics.inputEvents;

    switch (event.kind) {
        case InputEventKind::PointerMove:
            updateHover(event.position);
            return (mCaptured != nullptr ? mCaptured : mHovered) != nullptr
                && (mCaptured != nullptr ? mCaptured : mHovered)->handleInput(event);
        case InputEventKind::PointerDown: {
            updateHover(event.position);
            Widget* target = mRoot->hitTest(event.position);
            if (target == nullptr) {
                setFocus(nullptr);
                return false;
            }
            mCaptured = target;
            target->setPressed(true);
            setFocus(target);
            return target->handleInput(event);
        }
        case InputEventKind::PointerUp: {
            Widget* target = mCaptured != nullptr ? mCaptured : mRoot->hitTest(event.position);
            const bool handled = target != nullptr && target->handleInput(event);
            if (mCaptured != nullptr) {
                mCaptured->setPressed(false);
            }
            mCaptured = nullptr;
            updateHover(event.position);
            return handled;
        }
        case InputEventKind::KeyDown:
        case InputEventKind::KeyUp:
        case InputEventKind::TextInput:
            return mFocused != nullptr && mFocused->handleInput(event);
        case InputEventKind::PointerScroll:
            updateHover(event.position);
            return mHovered != nullptr && mHovered->handleInput(event);
        case InputEventKind::FocusLost:
            clearInteraction();
            return false;
    }
    return false;
}

void UiDocument::clearInteraction() {
    if (mHovered != nullptr) {
        mHovered->setHovered(false);
    }
    if (mCaptured != nullptr) {
        mCaptured->setPressed(false);
    }
    if (mFocused != nullptr) {
        static_cast<void>(mFocused->handleInput({.kind = InputEventKind::FocusLost}));
        mFocused->setFocused(false);
    }
    mHovered = nullptr;
    mCaptured = nullptr;
    mFocused = nullptr;
}

UiDocumentStatistics UiDocument::statistics() const noexcept { return mStatistics; }

void UiDocument::updateHover(Vec2 position) noexcept {
    Widget* next = mRoot == nullptr ? nullptr : mRoot->hitTest(position);
    if (next == mHovered) {
        return;
    }
    if (mHovered != nullptr) {
        mHovered->setHovered(false);
    }
    mHovered = next;
    if (mHovered != nullptr) {
        mHovered->setHovered(true);
    }
}

void UiDocument::setFocus(Widget* widget) {
    if (widget == mFocused) {
        return;
    }
    if (mFocused != nullptr) {
        static_cast<void>(mFocused->handleInput({.kind = InputEventKind::FocusLost}));
        mFocused->setFocused(false);
    }
    mFocused = widget;
    if (mFocused != nullptr) {
        mFocused->setFocused(true);
    }
}

} // namespace henia::ui
