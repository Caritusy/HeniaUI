#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/UiDocument.h"
#include "henia/ui/widget/controls/Button.h"
#include "henia/ui/widget/controls/Label.h"
#include "henia/ui/widget/controls/Panel.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace henia::ui;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] FontHandle createFont(TextureStore& textures, FontStore& fonts) {
    const std::array<std::byte, 16> pixels{};
    const TextureHandle atlas = textures.create(TextureFormat::Alpha8, 4, 4, 4, pixels);
    std::vector<GlyphMetrics> glyphs;
    for (char32_t codepoint = U' '; codepoint <= U'~'; ++codepoint) {
        glyphs.push_back({
            codepoint,
            {{0.0F, 0.0F}, {0.25F, 0.25F}},
            codepoint == U' ' ? Vec2{} : Vec2{6.0F, 9.0F},
            {0.0F, 8.0F},
            7.0F,
        });
    }
    return fonts.add({
        .atlas = atlas,
        .pixelSize = 12.0F,
        .ascent = 9.0F,
        .descent = 3.0F,
        .lineGap = 0.0F,
        .glyphs = std::move(glyphs),
    });
}

class ProbeWidget final : public Widget {
public:
    ProbeWidget(Vec2 preferred, Color color, bool nestedClip = false) noexcept
        : mPreferred(preferred), mColor(color), mNestedClip(nestedClip) {}

    void setColor(Color color) noexcept {
        mColor = color;
        markPaintDirty();
    }

    void setPreferred(Vec2 preferred) noexcept {
        mPreferred = preferred;
        markLayoutDirty();
    }

    std::uint64_t measureCalls = 0;
    std::uint64_t arrangeCalls = 0;
    std::uint64_t paintCalls = 0;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter&, Constraints) override {
        ++measureCalls;
        return mPreferred;
    }

    void onArrange(TextPainter&, Rect) override { ++arrangeCalls; }

    void onPaint(Canvas& canvas, TextPainter&, const Theme&) override {
        ++paintCalls;
        if (!mNestedClip) {
            canvas.fillRect(frame(), mColor);
            return;
        }
        require(canvas.pushClip(frame()), "probe outer clip was rejected");
        const Rect inner{
            {frame().min.x + 2.0F, frame().min.y + 2.0F},
            {frame().max.x - 2.0F, frame().max.y - 2.0F},
        };
        require(canvas.pushClip(inner), "probe nested clip was rejected");
        canvas.fillRect(frame(), mColor);
        require(canvas.popClip() && canvas.popClip(), "probe clip stack did not balance");
    }

private:
    Vec2 mPreferred{};
    Color mColor{};
    bool mNestedClip = false;
};

[[nodiscard]] Vec2 center(Rect rect) noexcept {
    return {(rect.min.x + rect.max.x) * 0.5F, (rect.min.y + rect.max.y) * 0.5F};
}

void verifyPartialPaint(TextPainter& painter) {
    UiDocument document(painter);
    document.reserve(32, 8);
    document.setViewport({120.0F, 40.0F});
    auto root = std::make_unique<Panel>(PanelStyle{.direction = LayoutDirection::Row});
    ProbeWidget& changed = root->emplaceChild<ProbeWidget>(
        Vec2{40.0F, 20.0F}, Color{1.0F, 0.0F, 0.0F, 1.0F}, true);
    ProbeWidget& stable = root->emplaceChild<ProbeWidget>(
        Vec2{40.0F, 20.0F}, Color{0.0F, 1.0F, 0.0F, 1.0F});
    document.setRoot(std::move(root));

    const RenderPacket first = document.compose();
    require(first.instances().size() == 2, "initial retained segment order is incomplete");
    const std::uint64_t changedIdentity = changed.paintSegmentIdentity();
    const std::uint64_t changedRevision = changed.paintRevision();
    const std::uint64_t stableIdentity = stable.paintSegmentIdentity();
    const std::uint64_t stableRevision = stable.paintRevision();
    const UiDocumentStatistics before = document.statistics();

    const Color blue{0.0F, 0.25F, 1.0F, 1.0F};
    changed.setColor(blue);
    const RenderPacket partial = document.compose();
    const UiDocumentStatistics after = document.statistics();
    require(changed.paintCalls == 2 && stable.paintCalls == 1,
        "paint-only leaf change repainted an unrelated sibling subtree");
    require(changed.paintSegmentIdentity() == changedIdentity
            && changed.paintRevision() == changedRevision + 1
            && stable.paintSegmentIdentity() == stableIdentity
            && stable.paintRevision() == stableRevision,
        "retained segment identity or revision was not stable");
    require(after.rebuiltSegments == before.rebuiltSegments + 1
            && after.reusedSubtrees > before.reusedSubtrees
            && after.layoutPasses == before.layoutPasses,
        "partial-paint statistics or layout isolation is incorrect");
    require(partial.instances().size() == 2
            && partial.instances()[0].color == blue
            && partial.instances()[1].color == Color{0.0F, 1.0F, 0.0F, 1.0F},
        "partial paint changed global draw order");
    require(partial.batches().size() == 2 && partial.batches()[0].clip.enabled
            && partial.batches()[0].clip.area.min == Vec2{2.0F, 2.0F}
            && partial.batches()[0].clip.area.max == Vec2{38.0F, 38.0F},
        "nested clipping was not preserved after partial paint");
}

void verifyPartialLayoutAndVisibility(TextPainter& painter) {
    UiDocument document(painter);
    document.reserve(32, 8);
    document.setViewport({120.0F, 40.0F});
    auto root = std::make_unique<Panel>(PanelStyle{.direction = LayoutDirection::Row});
    ProbeWidget& stable = root->emplaceChild<ProbeWidget>(
        Vec2{30.0F, 20.0F}, Color{1.0F, 0.0F, 0.0F, 1.0F});
    ProbeWidget& changed = root->emplaceChild<ProbeWidget>(
        Vec2{30.0F, 20.0F}, Color{0.0F, 1.0F, 0.0F, 1.0F});
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());

    const std::uint64_t stableMeasures = stable.measureCalls;
    const std::uint64_t stableArranges = stable.arrangeCalls;
    const std::uint64_t stablePaints = stable.paintCalls;
    const std::uint64_t changedMeasures = changed.measureCalls;
    const std::uint64_t changedArranges = changed.arrangeCalls;
    const std::uint64_t changedPaints = changed.paintCalls;
    changed.setPreferred({40.0F, 20.0F});
    static_cast<void>(document.compose());
    require(stable.measureCalls == stableMeasures && stable.arrangeCalls == stableArranges
            && stable.paintCalls == stablePaints && changed.measureCalls > changedMeasures
            && changed.arrangeCalls == changedArranges + 1
            && changed.paintCalls == changedPaints + 1,
        "layout dirtiness did not stay on the affected branch");

    changed.setVisible(false);
    const RenderPacket hidden = document.compose();
    require(hidden.instances().size() == 1 && stable.paintCalls == stablePaints,
        "visibility topology change rebuilt or retained the wrong segment");
    changed.setVisible(true);
    const RenderPacket visible = document.compose();
    require(visible.instances().size() == 2 && stable.paintCalls == stablePaints
            && changed.paintCalls == 3,
        "restoring visibility did not rebuild only the revealed segment");

    require(document.removeWidget(changed), "retained widget removal was rejected");
    const RenderPacket removed = document.compose();
    require(removed.instances().size() == 1 && stable.paintCalls == stablePaints,
        "widget removal left a stale segment or rebuilt a stable sibling");
}

void verifyHoverAndPaintOnlyStyles(TextPainter& painter, FontHandle font) {
    UiDocument document(painter);
    document.reserve(64, 8);
    document.setViewport({240.0F, 80.0F});
    auto root = std::make_unique<Panel>(PanelStyle{.direction = LayoutDirection::Row});
    Button& button = root->emplaceChild<Button>("Hover", ButtonStyle{.font = font});
    ProbeWidget& sibling = root->emplaceChild<ProbeWidget>(
        Vec2{40.0F, 20.0F}, Color{0.0F, 1.0F, 0.0F, 1.0F});
    Label& label = root->emplaceChild<Label>("Color", LabelStyle{font, 14.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());

    const std::uint64_t siblingPaints = sibling.paintCalls;
    const std::uint64_t buttonRevision = button.paintRevision();
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerMove,
        .position = center(button.frame()),
    }));
    static_cast<void>(document.compose());
    require(button.hovered() && button.paintRevision() == buttonRevision + 1
            && sibling.paintCalls == siblingPaints,
        "hovering one button rebuilt an unrelated widget");

    const std::uint64_t layoutPasses = document.statistics().layoutPasses;
    LabelStyle style = label.style();
    style.color = {0.2F, 0.6F, 1.0F, 1.0F};
    const std::uint64_t labelRevision = label.paintRevision();
    label.setStyle(style);
    static_cast<void>(document.compose());
    require(document.statistics().layoutPasses == layoutPasses
            && label.paintRevision() == labelRevision + 1,
        "a paint-only control style change forced layout");
}

void verifyReparentedSegmentIdentity(TextPainter& painter) {
    UiDocument document(painter);
    document.reserve(16, 4);
    document.setViewport({80.0F, 40.0F});
    auto root = std::make_unique<Widget>();
    Widget& source = root->emplaceChild<Widget>();
    Widget& destination = root->emplaceChild<Widget>();
    ProbeWidget& moving = source.emplaceChild<ProbeWidget>(
        Vec2{20.0F, 20.0F}, Color{0.1F, 0.5F, 1.0F, 1.0F});
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());
    const std::uint64_t identity = moving.paintSegmentIdentity();
    const std::uint64_t revision = moving.paintRevision();
    const std::uint64_t paints = moving.paintCalls;

    require(document.reparentWidget(moving, destination), "retained reparent was rejected");
    const RenderPacket reparented = document.compose();
    require(moving.parent() == &destination && source.children().empty()
            && destination.children().size() == 1 && reparented.instances().size() == 1
            && moving.paintSegmentIdentity() == identity && moving.paintRevision() == revision
            && moving.paintCalls == paints,
        "reparenting discarded stable local paint output");
}

void verifyStableEmptySnapshots(TextPainter& painter) {
    UiDocument document(painter);
    const RenderPacket firstEmpty = document.compose();
    const std::uint64_t revision = document.statistics().revision;
    const RenderPacket cachedEmpty = document.compose();
    require(cachedEmpty.identity() == firstEmpty.identity()
            && cachedEmpty.revision() == firstEmpty.revision()
            && document.statistics().revision == revision,
        "null-root composition rebuilt a stable empty snapshot");

    auto root = std::make_unique<ProbeWidget>(
        Vec2{20.0F, 20.0F}, Color{1.0F, 0.0F, 0.0F, 1.0F});
    ProbeWidget* rootPointer = root.get();
    document.setRoot(std::move(root));
    const RenderPacket invalidViewport = document.compose();
    require(invalidViewport.identity() == firstEmpty.identity()
            && invalidViewport.revision() == firstEmpty.revision(),
        "nonpositive viewport rebuilt an already-empty snapshot");

    document.setViewport({40.0F, 40.0F});
    const RenderPacket populated = document.compose();
    require(populated.instances().size() == 1, "positive viewport did not populate the document");
    rootPointer->setVisible(false);
    const RenderPacket hiddenRoot = document.compose();
    const std::uint64_t hiddenLayoutPasses = document.statistics().layoutPasses;
    const RenderPacket cachedHiddenRoot = document.compose();
    require(hiddenRoot.instances().empty()
            && cachedHiddenRoot.identity() == hiddenRoot.identity()
            && cachedHiddenRoot.revision() == hiddenRoot.revision()
            && document.statistics().layoutPasses == hiddenLayoutPasses,
        "hidden root did not become a stable empty document");
    rootPointer->setVisible(true);
    static_cast<void>(document.compose());
    document.setViewport({0.0F, 40.0F});
    const RenderPacket cleared = document.compose();
    const RenderPacket cachedCleared = document.compose();
    require(cleared.instances().empty() && cachedCleared.identity() == cleared.identity()
            && cachedCleared.revision() == cleared.revision(),
        "nonpositive viewport did not retain its empty snapshot");
}

} // namespace

int main() {
    TextureStore textures;
    FontStore fonts;
    const FontHandle font = createFont(textures, fonts);
    TextRunCache cache(fonts);
    cache.reserve(64, 128);
    TextPainter painter(cache);

    verifyPartialPaint(painter);
    verifyPartialLayoutAndVisibility(painter);
    verifyHoverAndPaintOnlyStyles(painter, font);
    verifyReparentedSegmentIdentity(painter);
    verifyStableEmptySnapshots(painter);

    std::cout << "HeniaUI retained subtree tests passed\n";
    return EXIT_SUCCESS;
}
