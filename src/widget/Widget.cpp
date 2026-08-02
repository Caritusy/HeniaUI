#include "henia/ui/widget/Widget.h"
#include "henia/ui/widget/UiDocument.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace henia::ui {
namespace {

std::atomic_uint64_t gNextWidgetIdentity{1};

[[nodiscard]] std::uint64_t nextWidgetIdentity() noexcept {
    std::uint64_t identity = gNextWidgetIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0) {
        identity = gNextWidgetIdentity.fetch_add(1, std::memory_order_relaxed);
    }
    return identity;
}

[[nodiscard]] float normalizedMinimum(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] float normalizedMaximum(float value, float minimum) noexcept {
    if (std::isinf(value) && value > 0.0F) {
        return std::numeric_limits<float>::max();
    }
    if (!std::isfinite(value)) {
        return minimum;
    }
    return std::max(std::max(value, 0.0F), minimum);
}

[[nodiscard]] Constraints normalized(Constraints constraints) noexcept {
    Constraints result{};
    result.minimum.x = normalizedMinimum(constraints.minimum.x);
    result.minimum.y = normalizedMinimum(constraints.minimum.y);
    result.maximum.x = normalizedMaximum(constraints.maximum.x, result.minimum.x);
    result.maximum.y = normalizedMaximum(constraints.maximum.y, result.minimum.y);
    return result;
}

[[nodiscard]] float clampMeasured(float value, float minimum, float maximum) noexcept {
    if (std::isinf(value) && value > 0.0F) {
        return maximum;
    }
    if (!std::isfinite(value)) {
        return minimum;
    }
    return std::clamp(value, minimum, maximum);
}

} // namespace

Widget::Widget(WidgetKind kind) noexcept
    : mKind(kind),
      mIdentity(nextWidgetIdentity()) {}

Widget::~Widget() = default;

WidgetKind Widget::kind() const noexcept { return mKind; }

std::uint64_t Widget::identity() const noexcept { return mIdentity; }

Widget* Widget::parent() const noexcept { return mParent; }

std::span<const std::unique_ptr<Widget>> Widget::children() const noexcept { return mChildren; }

Rect Widget::frame() const noexcept { return mFrame; }

const LayoutParameters& Widget::layoutParameters() const noexcept { return mLayout; }

bool Widget::visible() const noexcept { return mVisible; }

bool Widget::enabled() const noexcept { return mEnabled; }

bool Widget::hovered() const noexcept { return mHovered; }

bool Widget::pressed() const noexcept { return mPressed; }

bool Widget::focused() const noexcept { return mFocused; }

bool Widget::layoutDirty() const noexcept { return mLayoutDirty; }

bool Widget::paintDirty() const noexcept { return mPaintDirty; }

bool Widget::subtreePaintDirty() const noexcept { return mSubtreePaintDirty; }

std::uint64_t Widget::paintSegmentIdentity() const noexcept { return mIdentity; }

std::uint64_t Widget::paintRevision() const noexcept { return mPaintRevision; }

void Widget::setLayoutParameters(LayoutParameters parameters) noexcept {
    if (mLayout.width == parameters.width && mLayout.height == parameters.height
        && mLayout.flexGrow == parameters.flexGrow
        && mLayout.margin.left == parameters.margin.left
        && mLayout.margin.top == parameters.margin.top
        && mLayout.margin.right == parameters.margin.right
        && mLayout.margin.bottom == parameters.margin.bottom) {
        return;
    }
    mLayout = parameters;
    markLayoutDirty();
}

void Widget::setVisible(bool visibleValue) {
    if (mVisible == visibleValue) {
        return;
    }
    mVisible = visibleValue;
    markPaintTopologyDirty();
    markLayoutDirty();
    if (!mVisible) {
        if (mDocument != nullptr) {
            mDocument->widgetBecameNonInteractive(*this);
        } else {
            mHovered = false;
            mPressed = false;
            mFocused = false;
        }
    }
}

void Widget::setEnabled(bool enabledValue) {
    if (mEnabled == enabledValue) {
        return;
    }
    mEnabled = enabledValue;
    markPaintDirty();
    if (!mEnabled) {
        if (mDocument != nullptr) {
            mDocument->widgetBecameNonInteractive(*this);
        } else {
            mHovered = false;
            mPressed = false;
            mFocused = false;
        }
    }
}

Widget& Widget::addChild(std::unique_ptr<Widget> child) {
    if (child == nullptr) {
        return *this;
    }
    Widget* pointer = child.get();
    mChildren.push_back(std::move(child));
    pointer->mParent = this;
    pointer->setDocumentRecursive(mDocument);
    markPaintTopologyDirty();
    markLayoutDirty();
    return *pointer;
}

Vec2 Widget::measure(TextPainter& text, Constraints constraints) {
    if (!mVisible) {
        mMeasured = {};
        mMeasurementDirty = false;
        return {};
    }
    constraints = normalized(constraints);
    if (mMeasurementDirty) {
        for (MeasurementCacheEntry& entry : mMeasurementCache) {
            entry.valid = false;
        }
        mNextMeasurementCacheEntry = 0;
        mMeasurementDirty = false;
    }
    const auto cached = std::find_if(
        mMeasurementCache.begin(),
        mMeasurementCache.end(),
        [constraints](const MeasurementCacheEntry& entry) {
            return entry.valid && entry.constraints == constraints;
        });
    if (cached != mMeasurementCache.end()) {
        mMeasured = cached->measured;
    } else {
        mMeasured = onMeasure(text, constraints);
        if (mLayout.width >= 0.0F) {
            mMeasured.x = mLayout.width;
        }
        if (mLayout.height >= 0.0F) {
            mMeasured.y = mLayout.height;
        }
        mMeasured.x = clampMeasured(
            mMeasured.x, constraints.minimum.x, constraints.maximum.x);
        mMeasured.y = clampMeasured(
            mMeasured.y, constraints.minimum.y, constraints.maximum.y);
        MeasurementCacheEntry& entry = mMeasurementCache[mNextMeasurementCacheEntry];
        entry = {.constraints = constraints, .measured = mMeasured, .valid = true};
        mNextMeasurementCacheEntry = (mNextMeasurementCacheEntry + 1U) % mMeasurementCache.size();
    }
    mMeasuredConstraints = constraints;
    mHasMeasuredConstraints = true;
    return mMeasured;
}

void Widget::arrange(TextPainter& text, Rect arrangedFrame) {
    if (!mVisible) {
        // Visibility restoration marks layout dirty again. Clearing the hidden
        // root here prevents an otherwise stable empty document from running a
        // layout pass on every compose().
        mLayoutDirty = false;
        return;
    }
    const bool frameChanged = !(mFrame == arrangedFrame);
    const bool measurementContextChanged = mHasMeasuredConstraints
        && (!mHasArrangedMeasurementConstraints
            || !(mArrangedMeasurementConstraints == mMeasuredConstraints));
    if (!mLayoutDirty && !frameChanged && !measurementContextChanged) {
        return;
    }
    if (frameChanged) {
        mFrame = arrangedFrame;
        markPaintDirty();
        if (clipsChildren()) {
            for (const std::unique_ptr<Widget>& child : mChildren) {
                child->markPaintDirtyRecursive();
            }
        }
    }
    onArrange(text, arrangedFrame);
    if (mHasMeasuredConstraints) {
        mArrangedMeasurementConstraints = mMeasuredConstraints;
        mHasArrangedMeasurementConstraints = true;
    }
    mLayoutDirty = false;
}

void Widget::paint(Canvas& canvas, TextPainter& text, const Theme& theme) {
    if (!mVisible) {
        return;
    }
    onPaint(canvas, text, theme);
    for (const std::unique_ptr<Widget>& child : mChildren) {
        child->paint(canvas, text, theme);
    }
}

Widget* Widget::hitTest(Vec2 point) noexcept {
    if (!mVisible || !mEnabled || !contains(point)) {
        return nullptr;
    }
    if (allowsChildInteraction()) {
        for (auto iterator = mChildren.rbegin(); iterator != mChildren.rend(); ++iterator) {
            if (Widget* hit = (*iterator)->hitTest(point)) {
                return hit;
            }
        }
    }
    return acceptsPointerInput() ? this : nullptr;
}

bool Widget::acceptsPointerInput() const noexcept { return false; }

bool Widget::acceptsKeyboardFocus() const noexcept { return false; }

bool Widget::allowsChildInteraction() const noexcept { return true; }

bool Widget::wantsTabKey() const noexcept { return false; }

bool Widget::handleInput(const InputEvent&) { return false; }

void Widget::markLayoutDirty() noexcept {
    mPaintDirty = true;
    for (Widget* current = this; current != nullptr; current = current->mParent) {
        current->mLayoutDirty = true;
        current->mMeasurementDirty = true;
        current->mSubtreePaintDirty = true;
    }
}

void Widget::markPaintDirty() noexcept {
    mPaintDirty = true;
    for (Widget* current = this; current != nullptr; current = current->mParent) {
        current->mSubtreePaintDirty = true;
    }
}

Vec2 Widget::onMeasure(TextPainter& text, Constraints constraints) {
    Vec2 measured{};
    for (const std::unique_ptr<Widget>& child : mChildren) {
        const Vec2 childSize = child->measure(text, constraints);
        measured.x = std::max(measured.x, childSize.x);
        measured.y = std::max(measured.y, childSize.y);
    }
    return measured;
}

void Widget::onArrange(TextPainter& text, Rect arrangedFrame) {
    for (const std::unique_ptr<Widget>& child : mChildren) {
        child->arrange(text, arrangedFrame);
    }
}

void Widget::onPaint(Canvas&, TextPainter&, const Theme&) {}

bool Widget::clipsChildren() const noexcept { return false; }

Rect Widget::childrenClipRect() const noexcept { return mFrame; }

void Widget::clearChildren() {
    if (mChildren.empty()) return;
    if (mDocument != nullptr) {
        for (const std::unique_ptr<Widget>& child : mChildren) {
            mDocument->clearInteractionForSubtree(*child);
        }
    }
    for (const std::unique_ptr<Widget>& child : mChildren) {
        child->mParent = nullptr;
        child->setDocumentRecursive(nullptr);
    }
    mChildren.clear();
    markPaintTopologyDirty();
    markLayoutDirty();
}

bool Widget::contains(Vec2 point) const noexcept {
    return point.x >= mFrame.min.x && point.y >= mFrame.min.y
        && point.x < mFrame.max.x && point.y < mFrame.max.y;
}

std::unique_ptr<Widget> Widget::detachChild(std::uint64_t identityValue) noexcept {
    const auto iterator = std::find_if(
        mChildren.begin(),
        mChildren.end(),
        [identityValue](const std::unique_ptr<Widget>& child) {
            return child->mIdentity == identityValue;
        });
    if (iterator == mChildren.end()) {
        return {};
    }
    std::unique_ptr<Widget> child = std::move(*iterator);
    mChildren.erase(iterator);
    child->mParent = nullptr;
    child->setDocumentRecursive(nullptr);
    markPaintTopologyDirty();
    markLayoutDirty();
    return child;
}

void Widget::setDocumentRecursive(UiDocument* document) noexcept {
    mDocument = document;
    for (const std::unique_ptr<Widget>& child : mChildren) {
        child->setDocumentRecursive(document);
    }
}

void Widget::setHovered(bool hoveredValue) noexcept {
    if (mHovered != hoveredValue) {
        mHovered = hoveredValue;
        markPaintDirty();
    }
}

void Widget::setPressed(bool pressedValue) noexcept {
    if (mPressed != pressedValue) {
        mPressed = pressedValue;
        markPaintDirty();
    }
}

void Widget::setFocused(bool focusedValue) noexcept {
    if (mFocused != focusedValue) {
        mFocused = focusedValue;
        markPaintDirty();
    }
}

void Widget::markPaintTopologyDirty() noexcept {
    for (Widget* current = this; current != nullptr; current = current->mParent) {
        current->mSubtreePaintTopologyDirty = true;
        current->mSubtreePaintDirty = true;
    }
}

void Widget::markPaintDirtyRecursive() noexcept {
    mPaintDirty = true;
    mSubtreePaintDirty = true;
    for (const std::unique_ptr<Widget>& child : mChildren) {
        child->markPaintDirtyRecursive();
    }
}

void Widget::clearPaintDirtyRecursive() noexcept {
    mPaintDirty = false;
    mSubtreePaintDirty = false;
    mSubtreePaintTopologyDirty = false;
    for (const std::unique_ptr<Widget>& child : mChildren) {
        child->clearPaintDirtyRecursive();
    }
}

} // namespace henia::ui
