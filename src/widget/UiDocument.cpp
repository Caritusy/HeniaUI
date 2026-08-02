#include "henia/ui/widget/UiDocument.h"

#include <utility>

namespace henia::ui {

UiDocument::UiDocument(TextPainter& text, Theme theme) : mText(&text), mTheme(theme) {}

void UiDocument::reserve(
    std::size_t commandCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy,
    std::size_t snapshotSlots) {
    mFrame.reserve(commandCapacity, batchCapacity, capacityPolicy, snapshotSlots);
}

void UiDocument::setRoot(std::unique_ptr<Widget> rootWidget) {
    PendingMutation mutation;
    mutation.kind = MutationKind::SetRoot;
    mutation.root = std::move(rootWidget);
    mPendingMutations.push_back(std::move(mutation));
    if (!mustDeferMutation()) {
        drainMutations();
    }
}

bool UiDocument::removeWidget(Widget& widget) {
    if (resolve(widget.identity()) != &widget) {
        return false;
    }
    PendingMutation mutation;
    mutation.kind = MutationKind::Remove;
    mutation.widgetIdentity = widget.identity();
    mPendingMutations.push_back(std::move(mutation));
    if (!mustDeferMutation()) {
        drainMutations();
    }
    return true;
}

bool UiDocument::reparentWidget(Widget& widget, Widget& newParent) {
    if (resolve(widget.identity()) != &widget || resolve(newParent.identity()) != &newParent
        || widget.parent() == nullptr || widget.identity() == newParent.identity()
        || subtreeContains(widget, newParent.identity())) {
        return false;
    }
    if (widget.parent() == &newParent) {
        return true;
    }
    PendingMutation mutation;
    mutation.kind = MutationKind::Reparent;
    mutation.widgetIdentity = widget.identity();
    mutation.parentIdentity = newParent.identity();
    mPendingMutations.push_back(std::move(mutation));
    if (!mustDeferMutation()) {
        drainMutations();
    }
    return true;
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

RenderPacket UiDocument::compose() {
    if (mRoot == nullptr || mViewport.x <= 0.0F || mViewport.y <= 0.0F) {
        Canvas& canvas = mFrame.begin();
        static_cast<void>(canvas);
        RenderPacket packet = mFrame.finish();
        if (!mFrame.lastBuildPublished()) {
            ++mStatistics.rejectedCompositions;
        }
        return packet;
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
        RenderPacket packet = mFrame.finish();
        if (!mFrame.lastBuildPublished()) {
            ++mStatistics.rejectedCompositions;
            return packet;
        }
        mRoot->clearDirtyRecursive();
        ++mStatistics.paintPasses;
        ++mStatistics.revision;
        return packet;
    }

    ++mStatistics.cachedFrames;
    return mFrame.packet();
}

bool UiDocument::dispatch(const InputEvent& event) {
    if (mDispatching || mApplyingMutations || mClearingInteraction) {
        ++mStatistics.rejectedNestedDispatches;
        return false;
    }

    mDispatching = true;
    try {
        sanitizeInteraction();
        const bool handled = dispatchEvent(event);
        sanitizeInteraction();
        mDispatching = false;
        drainMutations();
        sanitizeInteraction();
        return handled;
    } catch (...) {
        resetInteractionWithoutCallbacks();
        mPendingMutations.clear();
        mDispatching = false;
        throw;
    }
}

bool UiDocument::dispatchEvent(const InputEvent& event) {
    if (mRoot == nullptr) {
        return false;
    }
    ++mStatistics.inputEvents;

    switch (event.kind) {
        case InputEventKind::PointerMove: {
            updateHover(event.position);
            Widget* target = resolve(mCapturedIdentity != 0 ? mCapturedIdentity : mHoveredIdentity);
            return target != nullptr && target->handleInput(event);
        }
        case InputEventKind::PointerDown: {
            updateHover(event.position);
            Widget* target = mRoot->hitTest(event.position);
            if (target == nullptr) {
                setFocus(0);
                return false;
            }
            const std::uint64_t targetIdentity = target->identity();
            mCapturedIdentity = targetIdentity;
            target->setPressed(true);
            setFocus(targetIdentity);
            target = resolve(targetIdentity);
            return target != nullptr && interactive(*target) && target->handleInput(event);
        }
        case InputEventKind::PointerUp: {
            Widget* target = resolve(mCapturedIdentity);
            if (target == nullptr) {
                target = mRoot->hitTest(event.position);
            }
            const bool handled = target != nullptr && target->handleInput(event);
            if (Widget* captured = resolve(mCapturedIdentity)) {
                captured->setPressed(false);
            }
            mCapturedIdentity = 0;
            updateHover(event.position);
            return handled;
        }
        case InputEventKind::KeyDown:
        case InputEventKind::KeyUp:
        case InputEventKind::TextInput: {
            Widget* focused = resolve(mFocusedIdentity);
            return focused != nullptr && focused->handleInput(event);
        }
        case InputEventKind::PointerScroll: {
            updateHover(event.position);
            Widget* hovered = resolve(mHoveredIdentity);
            return hovered != nullptr && hovered->handleInput(event);
        }
        case InputEventKind::FocusLost:
            clearInteraction();
            return false;
    }
    return false;
}

void UiDocument::clearInteraction() {
    if (mClearingInteraction) {
        return;
    }
    mClearingInteraction = true;
    try {
        clearInteractionImpl();
        mClearingInteraction = false;
        if (!mustDeferMutation()) {
            drainMutations();
        }
    } catch (...) {
        mClearingInteraction = false;
        mPendingMutations.clear();
        sanitizeInteraction();
        throw;
    }
}

void UiDocument::clearInteractionImpl() {
    Widget* hovered = resolve(mHoveredIdentity);
    Widget* captured = resolve(mCapturedIdentity);
    Widget* focused = resolve(mFocusedIdentity);
    mHoveredIdentity = 0;
    mCapturedIdentity = 0;
    mFocusedIdentity = 0;

    if (hovered != nullptr) {
        hovered->setHovered(false);
    }
    if (captured != nullptr) {
        captured->setPressed(false);
    }
    if (focused != nullptr) {
        focused->setFocused(false);
        static_cast<void>(focused->handleInput({.kind = InputEventKind::FocusLost}));
    }
}

UiDocumentStatistics UiDocument::statistics() const noexcept { return mStatistics; }

void UiDocument::drainMutations() {
    if (mustDeferMutation() || mPendingMutations.empty()) {
        return;
    }
    mApplyingMutations = true;
    try {
        std::size_t index = 0;
        while (index < mPendingMutations.size()) {
            PendingMutation mutation = std::move(mPendingMutations[index++]);
            applyMutation(std::move(mutation));
        }
        mPendingMutations.clear();
        mApplyingMutations = false;
        sanitizeInteraction();
    } catch (...) {
        mPendingMutations.clear();
        mApplyingMutations = false;
        sanitizeInteraction();
        throw;
    }
}

void UiDocument::applyMutation(PendingMutation mutation) {
    switch (mutation.kind) {
        case MutationKind::SetRoot:
            clearInteraction();
            mRoot = std::move(mutation.root);
            if (mRoot != nullptr) {
                mRoot->mParent = nullptr;
                mRoot->markLayoutDirty();
            }
            return;
        case MutationKind::Remove: {
            Widget* widget = resolve(mutation.widgetIdentity);
            if (widget == nullptr) {
                return;
            }
            clearInteractionForSubtree(*widget);
            if (widget == mRoot.get()) {
                mRoot.reset();
                return;
            }
            if (widget->mParent != nullptr) {
                static_cast<void>(widget->mParent->detachChild(widget->identity()));
            }
            return;
        }
        case MutationKind::Reparent: {
            Widget* widget = resolve(mutation.widgetIdentity);
            Widget* newParent = resolve(mutation.parentIdentity);
            if (widget == nullptr || newParent == nullptr || widget == mRoot.get()
                || widget->mParent == nullptr || widget == newParent
                || subtreeContains(*widget, newParent->identity())) {
                return;
            }
            if (widget->mParent == newParent) {
                return;
            }
            newParent->mChildren.reserve(newParent->mChildren.size() + 1U);
            Widget* oldParent = widget->mParent;
            std::unique_ptr<Widget> moved = oldParent->detachChild(widget->identity());
            if (moved != nullptr) {
                newParent->addChild(std::move(moved));
            }
            return;
        }
    }
}

bool UiDocument::mustDeferMutation() const noexcept {
    return mDispatching || mApplyingMutations || mClearingInteraction;
}

Widget* UiDocument::resolve(std::uint64_t identity) const noexcept {
    return identity == 0 ? nullptr : findInSubtree(mRoot.get(), identity);
}

Widget* UiDocument::findInSubtree(Widget* rootWidget, std::uint64_t identity) noexcept {
    if (rootWidget == nullptr || identity == 0) {
        return nullptr;
    }
    if (rootWidget->identity() == identity) {
        return rootWidget;
    }
    for (const std::unique_ptr<Widget>& child : rootWidget->mChildren) {
        if (Widget* found = findInSubtree(child.get(), identity)) {
            return found;
        }
    }
    return nullptr;
}

bool UiDocument::subtreeContains(const Widget& rootWidget, std::uint64_t identity) noexcept {
    if (identity == 0) {
        return false;
    }
    if (rootWidget.identity() == identity) {
        return true;
    }
    for (const std::unique_ptr<Widget>& child : rootWidget.mChildren) {
        if (subtreeContains(*child, identity)) {
            return true;
        }
    }
    return false;
}

bool UiDocument::interactive(const Widget& widget) noexcept {
    const Widget* current = &widget;
    while (current != nullptr) {
        if (!current->visible() || !current->enabled()) {
            return false;
        }
        current = current->parent();
    }
    return true;
}

void UiDocument::clearInteractionForSubtree(Widget& subtree) {
    const bool clearHovered = subtreeContains(subtree, mHoveredIdentity);
    const bool clearCaptured = subtreeContains(subtree, mCapturedIdentity);
    const bool clearFocused = subtreeContains(subtree, mFocusedIdentity);
    Widget* hovered = clearHovered ? resolve(mHoveredIdentity) : nullptr;
    Widget* captured = clearCaptured ? resolve(mCapturedIdentity) : nullptr;
    Widget* focused = clearFocused ? resolve(mFocusedIdentity) : nullptr;

    if (clearHovered) {
        mHoveredIdentity = 0;
    }
    if (clearCaptured) {
        mCapturedIdentity = 0;
    }
    if (clearFocused) {
        mFocusedIdentity = 0;
    }
    if (hovered != nullptr) {
        hovered->setHovered(false);
    }
    if (captured != nullptr) {
        captured->setPressed(false);
    }
    if (focused != nullptr) {
        focused->setFocused(false);
        static_cast<void>(focused->handleInput({.kind = InputEventKind::FocusLost}));
    }
}

void UiDocument::resetInteractionWithoutCallbacks() noexcept {
    Widget* hovered = resolve(mHoveredIdentity);
    Widget* captured = resolve(mCapturedIdentity);
    Widget* focused = resolve(mFocusedIdentity);
    mHoveredIdentity = 0;
    mCapturedIdentity = 0;
    mFocusedIdentity = 0;
    if (hovered != nullptr) {
        hovered->setHovered(false);
    }
    if (captured != nullptr) {
        captured->setPressed(false);
    }
    if (focused != nullptr) {
        focused->setFocused(false);
    }
}

void UiDocument::sanitizeInteraction() noexcept {
    Widget* hovered = resolve(mHoveredIdentity);
    if (hovered == nullptr || !interactive(*hovered)) {
        if (hovered != nullptr) {
            hovered->setHovered(false);
        }
        mHoveredIdentity = 0;
    }

    Widget* captured = resolve(mCapturedIdentity);
    if (captured == nullptr || !interactive(*captured)) {
        if (captured != nullptr) {
            captured->setPressed(false);
        }
        mCapturedIdentity = 0;
    }

    Widget* focused = resolve(mFocusedIdentity);
    if (focused == nullptr || !interactive(*focused)) {
        if (focused != nullptr) {
            focused->setFocused(false);
        }
        mFocusedIdentity = 0;
    }
}

void UiDocument::updateHover(Vec2 position) noexcept {
    Widget* next = mRoot == nullptr ? nullptr : mRoot->hitTest(position);
    const std::uint64_t nextIdentity = next == nullptr ? 0 : next->identity();
    if (nextIdentity == mHoveredIdentity) {
        return;
    }
    if (Widget* hovered = resolve(mHoveredIdentity)) {
        hovered->setHovered(false);
    }
    mHoveredIdentity = nextIdentity;
    if (next != nullptr) {
        next->setHovered(true);
    }
}

void UiDocument::setFocus(std::uint64_t identity) {
    if (identity == mFocusedIdentity) {
        return;
    }
    Widget* previous = resolve(mFocusedIdentity);
    mFocusedIdentity = 0;
    if (previous != nullptr) {
        previous->setFocused(false);
        static_cast<void>(previous->handleInput({.kind = InputEventKind::FocusLost}));
    }

    Widget* next = resolve(identity);
    if (next != nullptr && interactive(*next)) {
        mFocusedIdentity = identity;
        next->setFocused(true);
    }
}

} // namespace henia::ui
