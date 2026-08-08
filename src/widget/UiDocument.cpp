#include "henia/ui/widget/UiDocument.h"

#include <optional>
#include <utility>

namespace henia::ui {

UiDocument::UiDocument(TextPainter& text, Theme theme) : mText(&text), mTheme(theme) {}

void UiDocument::reserve(
    std::size_t commandCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy,
    std::size_t snapshotSlots) {
    reserve(
        commandCapacity,
        commandCapacity,
        batchCapacity,
        capacityPolicy,
        snapshotSlots);
}

void UiDocument::reserve(
    std::size_t commandCapacity,
    std::size_t instanceCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy,
    std::size_t snapshotSlots) {
    mFrame.reserve(
        commandCapacity,
        instanceCapacity,
        batchCapacity,
        capacityPolicy,
        snapshotSlots);
    mRetainedSegments.reserve(commandCapacity);
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
    mCoordinateSpace.logicalViewport = value;
    if (mRoot != nullptr) {
        mRoot->markLayoutDirty();
    }
}

Vec2 UiDocument::viewport() const noexcept { return mViewport; }

bool UiDocument::setCoordinateSpace(UiCoordinateSpace space) noexcept {
    if (!valid(space)) return false;
    if (mCoordinateSpace == space) return true;
    const bool viewportChanged = mViewport != space.logicalViewport;
    const bool dpiChanged = mCoordinateSpace.dpiScale != space.dpiScale;
    const bool inputChanged = mCoordinateSpace.inputToLogical != space.inputToLogical;
    const bool renderChanged = mCoordinateSpace.render != space.render;
    mCoordinateSpace = space;
    mViewport = space.logicalViewport;
    ++mStatistics.coordinateSpaceChanges;
    mStatistics.dpiChanges += dpiChanged ? 1U : 0U;
    mStatistics.inputTransformChanges += inputChanged ? 1U : 0U;
    mStatistics.renderTransformChanges += renderChanged ? 1U : 0U;
    if (mRoot != nullptr) {
        if (viewportChanged) {
            mRoot->markLayoutDirty();
        } else if (dpiChanged || inputChanged || renderChanged) {
            mRoot->markPaintDirtyRecursive();
        }
    }
    return true;
}

const UiCoordinateSpace& UiDocument::coordinateSpace() const noexcept {
    return mCoordinateSpace;
}

void UiDocument::setTheme(Theme themeValue) noexcept {
    if (mTheme == themeValue) {
        return;
    }
    const bool layoutChanged = !mTheme.layoutEquivalent(themeValue);
    mTheme = themeValue;
    if (mRoot != nullptr) {
        if (layoutChanged) {
            mRoot->markLayoutDirtyRecursive();
        } else {
            mRoot->markPaintDirtyRecursive();
        }
    }
}

const Theme& UiDocument::theme() const noexcept { return mTheme; }

void UiDocument::invalidateTypography() noexcept {
    ++mStatistics.typographyInvalidations;
    if (mRoot != nullptr) mRoot->markLayoutDirtyRecursive();
}

void UiDocument::setFragmentAreaTracking(bool enabled) noexcept {
    mFrame.setFragmentAreaTracking(enabled);
}

bool UiDocument::fragmentAreaTracking() const noexcept {
    return mFrame.fragmentAreaTracking();
}

RenderPacket UiDocument::compose() {
    if (mRoot == nullptr || mViewport.x <= 0.0F || mViewport.y <= 0.0F) {
        if (mHasPublishedPacket && mPacketRepresentsEmptyDocument) {
            ++mStatistics.cachedFrames;
            return mFrame.packet();
        }
        const RenderPacket packet = mFrame.finish(std::span<const DisplayListSegment>{});
        if (!mFrame.lastBuildPublished()) {
            ++mStatistics.rejectedCompositions;
            return packet;
        }
        mHasPublishedPacket = true;
        mPacketRepresentsEmptyDocument = true;
        ++mStatistics.paintPasses;
        ++mStatistics.revision;
        return packet;
    }

    if (mRoot->layoutDirty()) {
        const Constraints constraints{{0.0F, 0.0F}, mViewport};
        static_cast<void>(mRoot->measure(*mText, constraints));
        mRoot->arrange(*mText, {{0.0F, 0.0F}, mViewport});
        ++mStatistics.layoutPasses;
    }
    if (mRoot->mSubtreePaintDirty || mRoot->mSubtreePaintTopologyDirty
        || !mHasPublishedPacket || mPacketRepresentsEmptyDocument) {
        if (mRoot->mSubtreePaintTopologyDirty || mRetainedSegments.empty()) {
            mRetainedSegments.clear();
            rebuildSegmentTopology(*mRoot);
        } else {
            updateDirtySubtree(*mRoot);
        }
        const RenderPacket packet = mFrame.finish(mRetainedSegments);
        if (!mFrame.lastBuildPublished()) {
            ++mStatistics.rejectedCompositions;
            return packet;
        }
        mRoot->clearPaintDirtyRecursive();
        mHasPublishedPacket = true;
        mPacketRepresentsEmptyDocument = false;
        ++mStatistics.paintPasses;
        ++mStatistics.revision;
        return packet;
    }

    ++mStatistics.cachedFrames;
    return mFrame.packet();
}

void UiDocument::rebuildPaintSegment(Widget& widget) {
    widget.mPaintSegment.clear();
    Canvas canvas(widget.mPaintSegment);
    std::optional<Rect> inheritedClip;
    for (Widget* ancestor = widget.mParent; ancestor != nullptr; ancestor = ancestor->mParent) {
        if (!ancestor->clipsChildren()) continue;
        inheritedClip = inheritedClip.has_value()
            ? intersect(*inheritedClip, ancestor->childrenClipRect())
            : ancestor->childrenClipRect();
    }
    if (inheritedClip.has_value() && !inheritedClip->valid()) {
        ++widget.mPaintRevision;
        if (widget.mPaintRevision == 0) ++widget.mPaintRevision;
        ++mStatistics.rebuiltSegments;
        return;
    }
    Canvas::ClipScope clip = inheritedClip.has_value()
        ? canvas.scopedClip(*inheritedClip) : Canvas::ClipScope{};
    widget.onPaint(canvas, *mText, mTheme);
    ++widget.mPaintRevision;
    if (widget.mPaintRevision == 0) {
        ++widget.mPaintRevision;
    }
    ++mStatistics.rebuiltSegments;
}

void UiDocument::rebuildSegmentTopology(Widget& widget) {
    widget.mRetainedSegmentBegin = mRetainedSegments.size();
    if (!widget.mVisible) {
        widget.mRetainedSegmentEnd = widget.mRetainedSegmentBegin;
        return;
    }

    ++mStatistics.rebuiltSubtrees;
    if (widget.mPaintDirty || widget.mPaintRevision == 0) {
        rebuildPaintSegment(widget);
    } else {
        ++mStatistics.reusedSegments;
    }
    mRetainedSegments.push_back({
        .identity = widget.mIdentity,
        .revision = widget.mPaintRevision,
        .commands = widget.mPaintSegment.commands(),
    });
    for (const std::unique_ptr<Widget>& child : widget.mChildren) {
        rebuildSegmentTopology(*child);
    }
    widget.mRetainedSegmentEnd = mRetainedSegments.size();
}

void UiDocument::updateDirtySubtree(Widget& widget) {
    if (!widget.mVisible) {
        return;
    }
    if (!widget.mSubtreePaintDirty) {
        ++mStatistics.reusedSubtrees;
        mStatistics.reusedSegments += widget.mRetainedSegmentEnd - widget.mRetainedSegmentBegin;
        return;
    }

    ++mStatistics.rebuiltSubtrees;
    if (widget.mPaintDirty) {
        rebuildPaintSegment(widget);
        mRetainedSegments[widget.mRetainedSegmentBegin] = {
            .identity = widget.mIdentity,
            .revision = widget.mPaintRevision,
            .commands = widget.mPaintSegment.commands(),
        };
    } else {
        ++mStatistics.reusedSegments;
    }
    for (const std::unique_ptr<Widget>& child : widget.mChildren) {
        updateDirtySubtree(*child);
    }
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
            Widget* target = resolve(
                mPointerSequenceActive ? mCapturedIdentity : mHoveredIdentity);
            if (target == nullptr || !interactive(*target)
                || !target->acceptsPointerInput()) {
                return false;
            }
            if (target->handleInput(event)) {
                return true;
            }
            Widget* barrier = pointerBarrier(target->parent(), event.position);
            return barrier != nullptr && barrier->handleInput(event);
        }
        case InputEventKind::PointerDown: {
            if (Widget* captured = resolve(mCapturedIdentity)) {
                captured->setPressed(false);
            }
            mCapturedIdentity = 0;
            mPointerSequenceActive = true;
            updateHover(event.position);
            Widget* target = mRoot->hitTest(event.position);
            if (target == nullptr) {
                setFocus(0);
                return false;
            }
            std::uint64_t targetIdentity = target->identity();
            bool handled = target->handleInput(event);
            if (!handled) {
                if (Widget* barrier = pointerBarrier(target->parent(), event.position)) {
                    target = barrier;
                    targetIdentity = barrier->identity();
                    handled = barrier->handleInput(event);
                }
            }
            target = resolve(targetIdentity);
            if (!handled || target == nullptr || !interactive(*target)
                || !target->acceptsPointerInput()) {
                if (!handled && event.button == PointerButton::Primary) {
                    setFocus(0);
                }
                return handled;
            }
            setFocus(target->acceptsKeyboardFocus() ? targetIdentity : 0);
            target = resolve(targetIdentity);
            if (target == nullptr || !interactive(*target) || !target->acceptsPointerInput()) {
                return true;
            }
            mCapturedIdentity = targetIdentity;
            target->setPressed(true);
            return true;
        }
        case InputEventKind::PointerUp: {
            const bool capturedSequence = mPointerSequenceActive;
            Widget* target = resolve(mCapturedIdentity);
            if (target == nullptr && !capturedSequence) {
                target = mRoot->hitTest(event.position);
            }
            bool handled = target != nullptr && interactive(*target)
                && target->acceptsPointerInput() && target->handleInput(event);
            if (!handled && target != nullptr) {
                Widget* barrier = pointerBarrier(target->parent(), event.position);
                handled = barrier != nullptr && barrier->handleInput(event);
            }
            if (Widget* captured = resolve(mCapturedIdentity)) {
                captured->setPressed(false);
            }
            mCapturedIdentity = 0;
            mPointerSequenceActive = false;
            updateHover(event.position);
            return handled;
        }
        case InputEventKind::KeyDown:
        case InputEventKind::KeyUp:
        case InputEventKind::TextInput:
        case InputEventKind::CompositionStart:
        case InputEventKind::CompositionUpdate:
        case InputEventKind::CompositionCommit:
        case InputEventKind::CompositionCancel: {
            Widget* focused = resolve(mFocusedIdentity);
            if (event.kind == InputEventKind::KeyDown && event.key == KeyCode::Tab
                && (focused == nullptr || !focused->wantsTabKey())) {
                Widget* next = adjacentFocusable(event.shift);
                if (next == nullptr) return false;
                setFocus(next->identity());
                return true;
            }
            return focused != nullptr && interactive(*focused)
                && focused->acceptsKeyboardFocus() && focused->handleInput(event);
        }
        case InputEventKind::PointerScroll: {
            updateHover(event.position);
            Widget* hovered = resolve(mHoveredIdentity);
            for (Widget* current = hovered; current != nullptr; current = current->parent()) {
                if (interactive(*current) && current->acceptsPointerInput()
                    && current->handleInput(event)) {
                    return true;
                }
            }
            return false;
        }
        case InputEventKind::FocusLost:
            clearInteraction();
            return false;
        case InputEventKind::PointerCancel:
            clearPointerInteraction();
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
        resetInteractionWithoutCallbacks();
        throw;
    }
}

void UiDocument::clearInteractionImpl() {
    Widget* focused = resolve(mFocusedIdentity);
    mFocusedIdentity = 0;
    clearPointerInteraction();
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
        resetInteractionWithoutCallbacks();
        throw;
    }
}

void UiDocument::applyMutation(PendingMutation mutation) {
    switch (mutation.kind) {
        case MutationKind::SetRoot:
            clearInteraction();
            if (mRoot != nullptr) {
                mRoot->setDocumentRecursive(nullptr);
            }
            mRoot = std::move(mutation.root);
            if (mRoot != nullptr) {
                mRoot->mParent = nullptr;
                mRoot->setDocumentRecursive(this);
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
                widget->setDocumentRecursive(nullptr);
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
    const Widget* child = &widget;
    for (const Widget* current = &widget; current != nullptr; current = current->parent()) {
        if (!current->visible() || !current->enabled()) {
            return false;
        }
        if (current != &widget && !current->allowsInteractionForChild(*child)) {
            return false;
        }
        child = current;
    }
    return true;
}

Widget* UiDocument::pointerBarrier(Widget* target, Vec2 point) noexcept {
    for (Widget* current = target; current != nullptr; current = current->parent()) {
        if (interactive(*current) && current->acceptsPointerInput()
            && current->blocksUnhandledPointerInput(point)) {
            return current;
        }
    }
    return nullptr;
}

Widget* UiDocument::adjacentFocusable(bool backwards) const noexcept {
    Widget* first = nullptr;
    Widget* last = nullptr;
    Widget* previous = nullptr;
    Widget* next = nullptr;
    bool foundCurrent = mFocusedIdentity == 0;
    const auto visit = [&](const auto& self, Widget* widget) -> void {
        if (widget == nullptr || !widget->visible() || !widget->enabled()) return;
        if (widget->acceptsKeyboardFocus()) {
            if (first == nullptr) first = widget;
            if (widget->identity() == mFocusedIdentity) {
                foundCurrent = true;
                previous = last;
            } else if (foundCurrent && next == nullptr) {
                next = widget;
            }
            last = widget;
        }
        if (widget->allowsChildInteraction()) {
            for (const std::unique_ptr<Widget>& child : widget->children()) {
                if (widget->allowsInteractionForChild(*child)) {
                    self(self, child.get());
                }
            }
        }
    };
    visit(visit, mRoot.get());
    if (first == nullptr) return nullptr;
    if (mFocusedIdentity == 0) return backwards ? last : first;
    return backwards ? (previous == nullptr ? last : previous)
                     : (next == nullptr ? first : next);
}

void UiDocument::clearPointerInteraction() noexcept {
    Widget* hovered = resolve(mHoveredIdentity);
    Widget* captured = resolve(mCapturedIdentity);
    mHoveredIdentity = 0;
    mCapturedIdentity = 0;
    mPointerSequenceActive = false;
    if (hovered != nullptr) {
        hovered->setHovered(false);
    }
    if (captured != nullptr) {
        captured->setPressed(false);
    }
}

void UiDocument::widgetBecameNonInteractive(Widget& subtree) {
    if (subtree.mDocument != this || mClearingInteraction) {
        return;
    }
    mClearingInteraction = true;
    try {
        clearInteractionForSubtree(subtree);
        // A FocusLost callback can hide or disable another interactive subtree
        // while direct invalidation is guarded. Revalidate before leaving that
        // boundary so nested invalidations cannot leave stale identities and
        // callback-requested structural mutations remain deferred.
        sanitizeInteraction();
        mClearingInteraction = false;
        if (!mustDeferMutation()) {
            drainMutations();
        }
    } catch (...) {
        mClearingInteraction = false;
        mPendingMutations.clear();
        resetInteractionWithoutCallbacks();
        throw;
    }
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
    mPointerSequenceActive = false;
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

void UiDocument::sanitizeInteraction() {
    Widget* hovered = resolve(mHoveredIdentity);
    if (hovered == nullptr || !interactive(*hovered) || !hovered->acceptsPointerInput()) {
        if (hovered != nullptr) {
            hovered->setHovered(false);
        }
        mHoveredIdentity = 0;
    }

    Widget* captured = resolve(mCapturedIdentity);
    if (captured == nullptr || !interactive(*captured) || !captured->acceptsPointerInput()) {
        if (captured != nullptr) {
            captured->setPressed(false);
        }
        mCapturedIdentity = 0;
    }

    Widget* focused = resolve(mFocusedIdentity);
    if (focused == nullptr) {
        mFocusedIdentity = 0;
    } else if (!interactive(*focused) || !focused->acceptsKeyboardFocus()) {
        setFocus(0);
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
    if (next != nullptr && interactive(*next) && next->acceptsKeyboardFocus()) {
        mFocusedIdentity = identity;
        next->setFocused(true);
    }
}

} // namespace henia::ui
