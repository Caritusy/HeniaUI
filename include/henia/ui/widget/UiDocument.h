#pragma once

#include "henia/ui/Frame.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace henia::ui {

struct UiDocumentStatistics final {
    std::uint64_t layoutPasses = 0;
    std::uint64_t paintPasses = 0;
    std::uint64_t cachedFrames = 0;
    std::uint64_t rejectedCompositions = 0;
    std::uint64_t inputEvents = 0;
    std::uint64_t rejectedNestedDispatches = 0;
    // A rebuilt subtree was visited because it contained dirty paint output;
    // a reused subtree was skipped as one stable retained range.
    std::uint64_t rebuiltSubtrees = 0;
    std::uint64_t reusedSubtrees = 0;
    std::uint64_t rebuiltSegments = 0;
    std::uint64_t reusedSegments = 0;
    std::uint64_t revision = 0;
};

class UiDocument final {
public:
    explicit UiDocument(TextPainter& text, Theme theme = {});

    void reserve(
        std::size_t commandCapacity,
        std::size_t batchCapacity,
        CapacityPolicy capacityPolicy = CapacityPolicy::Grow,
        std::size_t snapshotSlots = RenderPacketBuilder::kDefaultSnapshotSlots);
    void reserve(
        std::size_t commandCapacity,
        std::size_t instanceCapacity,
        std::size_t batchCapacity,
        CapacityPolicy capacityPolicy = CapacityPolicy::Grow,
        std::size_t snapshotSlots = RenderPacketBuilder::kDefaultSnapshotSlots);
    void setRoot(std::unique_ptr<Widget> root);
    // Structural changes requested from an input/focus callback are applied in
    // request order after the outer callback returns. A false result means the
    // widget is not currently owned by this document or the reparent is invalid.
    [[nodiscard]] bool removeWidget(Widget& widget);
    [[nodiscard]] bool reparentWidget(Widget& widget, Widget& newParent);
    [[nodiscard]] Widget* root() const noexcept;
    void setViewport(Vec2 viewport) noexcept;
    [[nodiscard]] Vec2 viewport() const noexcept;
    void setTheme(Theme theme) noexcept;
    [[nodiscard]] const Theme& theme() const noexcept;
    void setFragmentAreaTracking(bool enabled) noexcept;
    [[nodiscard]] bool fragmentAreaTracking() const noexcept;

    // Stable documents return a handle to the same immutable snapshot storage.
    [[nodiscard]] RenderPacket compose();
    // Exceptions raised by client callbacks propagate to the host boundary.
    // Recursive dispatch is rejected and counted; compose() remains valid during
    // callbacks. Deferred structural mutations are discarded if a callback throws.
    [[nodiscard]] bool dispatch(const InputEvent& event);
    void clearInteraction();

    [[nodiscard]] UiDocumentStatistics statistics() const noexcept;

private:
    friend class Widget;

    enum class MutationKind : std::uint8_t {
        SetRoot,
        Remove,
        Reparent,
    };

    struct PendingMutation final {
        MutationKind kind = MutationKind::SetRoot;
        std::unique_ptr<Widget> root;
        std::uint64_t widgetIdentity = 0;
        std::uint64_t parentIdentity = 0;
    };

    [[nodiscard]] bool dispatchEvent(const InputEvent& event);
    void drainMutations();
    void applyMutation(PendingMutation mutation);
    [[nodiscard]] bool mustDeferMutation() const noexcept;
    [[nodiscard]] Widget* resolve(std::uint64_t identity) const noexcept;
    [[nodiscard]] static Widget* findInSubtree(Widget* root, std::uint64_t identity) noexcept;
    [[nodiscard]] static bool subtreeContains(const Widget& root, std::uint64_t identity) noexcept;
    [[nodiscard]] static bool interactive(const Widget& widget) noexcept;
    [[nodiscard]] Widget* adjacentFocusable(bool backwards) const noexcept;
    void widgetBecameNonInteractive(Widget& subtree);
    void clearPointerInteraction() noexcept;
    void clearInteractionImpl();
    void clearInteractionForSubtree(Widget& subtree);
    void resetInteractionWithoutCallbacks() noexcept;
    void sanitizeInteraction();
    void updateHover(Vec2 position) noexcept;
    void setFocus(std::uint64_t identity);
    void rebuildPaintSegment(Widget& widget);
    void rebuildSegmentTopology(Widget& widget);
    void updateDirtySubtree(Widget& widget);

    TextPainter* mText = nullptr;
    Theme mTheme{};
    Frame mFrame;
    std::unique_ptr<Widget> mRoot;
    std::vector<DisplayListSegment> mRetainedSegments;
    std::vector<PendingMutation> mPendingMutations;
    std::uint64_t mHoveredIdentity = 0;
    std::uint64_t mCapturedIdentity = 0;
    std::uint64_t mFocusedIdentity = 0;
    Vec2 mViewport{};
    UiDocumentStatistics mStatistics{};
    bool mDispatching = false;
    bool mApplyingMutations = false;
    bool mClearingInteraction = false;
    bool mPointerSequenceActive = false;
    bool mHasPublishedPacket = false;
    bool mPacketRepresentsEmptyDocument = false;
};

} // namespace henia::ui
