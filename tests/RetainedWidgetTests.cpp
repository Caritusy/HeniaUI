#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/UiDocument.h"
#include "henia/ui/widget/controls/Button.h"
#include "henia/ui/widget/controls/Label.h"
#include "henia/ui/widget/controls/NumericInput.h"
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

[[nodiscard]] bool containsColor(const RenderPacket& packet, Color color) noexcept {
    return std::any_of(
        packet.instances().begin(),
        packet.instances().end(),
        [color](const DrawInstance& instance) { return instance.color == color; });
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

void verifyThemeCascade(TextPainter& painter, FontHandle font) {
    Theme theme{};
    theme.font = font;
    theme.panelBackground = {0.04F, 0.06F, 0.09F, 1.0F};
    theme.panelGap = 4.0F;

    UiDocument document(painter, theme);
    document.reserve(512, 64);
    document.setViewport({320.0F, 240.0F});
    auto root = std::make_unique<Panel>();
    Panel* panel = root.get();
    Label& label = root->emplaceChild<Label>("Theme label");
    Button& button = root->emplaceChild<Button>("Theme button");
    NumericInput& numeric = root->emplaceChild<NumericInput>(42.0);
    document.setRoot(std::move(root));

    const RenderPacket initial = document.compose();
    require(containsColor(initial, theme.panelBackground)
            && containsColor(initial, theme.textPrimary)
            && containsColor(initial, theme.surfaceRaised)
            && containsColor(initial, theme.surface),
        "default built-in styles did not inherit document theme colors");
    require(numeric.frame().height() == theme.controlHeight,
        "numeric input did not inherit the theme control height");
    const float initialLabelHeight = label.frame().height();
    const UiDocumentStatistics initialStatistics = document.statistics();
    const std::uint64_t panelRevision = panel->paintRevision();
    const std::uint64_t labelRevision = label.paintRevision();
    const std::uint64_t buttonRevision = button.paintRevision();
    const std::uint64_t numericRevision = numeric.paintRevision();

    Theme recolored = theme;
    recolored.panelBackground = {0.20F, 0.04F, 0.08F, 1.0F};
    recolored.textPrimary = {1.0F, 0.82F, 0.30F, 1.0F};
    recolored.surfaceRaised = {0.08F, 0.28F, 0.18F, 1.0F};
    recolored.surface = {0.04F, 0.12F, 0.24F, 1.0F};
    document.setTheme(recolored);
    const RenderPacket colorPacket = document.compose();
    const UiDocumentStatistics colorStatistics = document.statistics();
    require(colorStatistics.layoutPasses == initialStatistics.layoutPasses
            && colorStatistics.paintPasses == initialStatistics.paintPasses + 1
            && panel->paintRevision() == panelRevision + 1
            && label.paintRevision() == labelRevision + 1
            && button.paintRevision() == buttonRevision + 1
            && numeric.paintRevision() == numericRevision + 1,
        "color-only theme change did not stay paint-only");
    require(containsColor(colorPacket, recolored.panelBackground)
            && containsColor(colorPacket, recolored.textPrimary)
            && containsColor(colorPacket, recolored.surfaceRaised)
            && containsColor(colorPacket, recolored.surface),
        "color-only theme change did not repaint built-in controls");

    Theme resized = recolored;
    resized.fontSize = 20.0F;
    resized.controlHeight = 52.0F;
    resized.panelGap = 9.0F;
    document.setTheme(resized);
    static_cast<void>(document.compose());
    const UiDocumentStatistics resizedStatistics = document.statistics();
    require(resizedStatistics.layoutPasses == colorStatistics.layoutPasses + 1
            && numeric.frame().height() == resized.controlHeight
            && label.frame().height() > initialLabelHeight,
        "layout-affecting theme metrics did not invalidate inherited measurement");

    const Color panelOverride{0.30F, 0.05F, 0.35F, 1.0F};
    const Color labelOverride{0.25F, 0.95F, 0.65F, 1.0F};
    const Color buttonOverride{0.75F, 0.18F, 0.10F, 1.0F};
    const Color numericOverride{0.06F, 0.40F, 0.48F, 1.0F};
    UiDocument overrideDocument(painter, theme);
    overrideDocument.reserve(512, 64);
    overrideDocument.setViewport({320.0F, 260.0F});
    auto overrideRoot = std::make_unique<Panel>(PanelStyle{
        .background = panelOverride,
        .padding = Insets{5.0F, 5.0F, 5.0F, 5.0F},
        .gap = 7.0F,
    });
    Label& overrideLabel = overrideRoot->emplaceChild<Label>(
        "Override label",
        LabelStyle{.font = font, .size = 17.0F, .color = labelOverride});
    Button& overrideButton = overrideRoot->emplaceChild<Button>(
        "Override button",
        ButtonStyle{
            .font = font,
            .fontSize = 16.0F,
            .textColor = labelOverride,
            .background = buttonOverride,
            .padding = Insets{11.0F, 8.0F, 11.0F, 8.0F},
            .controlHeight = 44.0F,
        });
    NumericInput& overrideNumeric = overrideRoot->emplaceChild<NumericInput>(
        7.0,
        NumericInputStyle{
            .font = font,
            .fontSize = 16.0F,
            .textColor = labelOverride,
            .mutedText = labelOverride,
            .background = numericOverride,
            .controlWidth = 190.0F,
            .controlHeight = 48.0F,
            .stepButtonWidth = 42.0F,
            .padding = Insets{10.0F, 8.0F, 10.0F, 8.0F},
        });
    Panel* overridePanel = overrideRoot.get();
    overrideDocument.setRoot(std::move(overrideRoot));
    const RenderPacket overridden = overrideDocument.compose();
    const Rect labelFrame = overrideLabel.frame();
    const Rect buttonFrame = overrideButton.frame();
    const Rect numericFrame = overrideNumeric.frame();
    require(containsColor(overridden, panelOverride)
            && containsColor(overridden, labelOverride)
            && containsColor(overridden, buttonOverride)
            && containsColor(overridden, numericOverride),
        "widget-local style overrides were not applied");

    Theme changed = theme;
    changed.panelBackground = {0.85F, 0.85F, 0.85F, 1.0F};
    changed.textPrimary = {0.05F, 0.05F, 0.05F, 1.0F};
    changed.surfaceRaised = {0.90F, 0.90F, 0.90F, 1.0F};
    changed.surface = {0.72F, 0.72F, 0.72F, 1.0F};
    changed.fontSize = 28.0F;
    changed.controlWidth = 240.0F;
    changed.controlHeight = 64.0F;
    changed.stepButtonWidth = 54.0F;
    changed.controlPaddingHorizontal = 20.0F;
    changed.controlPaddingVertical = 14.0F;
    changed.panelPadding = 18.0F;
    changed.panelGap = 15.0F;
    changed.scale = 1.5F;
    overrideDocument.setTheme(changed);
    const RenderPacket stableOverrides = overrideDocument.compose();
    require(overrideLabel.frame() == labelFrame
            && overrideButton.frame() == buttonFrame
            && overrideNumeric.frame() == numericFrame
            && containsColor(stableOverrides, panelOverride)
            && containsColor(stableOverrides, labelOverride)
            && containsColor(stableOverrides, buttonOverride)
            && containsColor(stableOverrides, numericOverride),
        "widget-local overrides changed when the document theme changed");

    ButtonStyle inheritedButtonStyle = overrideButton.style();
    inheritedButtonStyle.background.reset();
    overrideButton.setStyle(inheritedButtonStyle);
    const RenderPacket resetOverride = overrideDocument.compose();
    require(containsColor(resetOverride, changed.surfaceRaised)
            && overridePanel->paintRevision() != 0,
        "clearing a widget override did not restore theme inheritance");
}

void verifyCoordinateSpaceInvalidation(TextPainter& painter) {
    UiDocument document(painter);
    document.reserve(32, 8);
    document.setViewport({200.0F, 100.0F});
    auto root = std::make_unique<ProbeWidget>(
        Vec2{200.0F, 100.0F},
        Color{0.2F, 0.4F, 0.8F, 1.0F});
    ProbeWidget* probe = root.get();
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());

    const UiDocumentStatistics before = document.statistics();
    require(document.setCoordinateSpace(makeUiCoordinateSpace(
            {200.0F, 100.0F},
            {250.0F, 125.0F},
            300,
            150,
            1.5F)),
        "document rejected an explicit coordinate space");
    static_cast<void>(document.compose());
    const UiDocumentStatistics transformed = document.statistics();
    require(transformed.coordinateSpaceChanges == before.coordinateSpaceChanges + 1
            && transformed.dpiChanges == before.dpiChanges + 1
            && transformed.inputTransformChanges == before.inputTransformChanges + 1
            && transformed.renderTransformChanges == before.renderTransformChanges + 1
            && transformed.layoutPasses == before.layoutPasses
            && transformed.paintPasses == before.paintPasses + 1,
        "transform-only coordinate change did not stay paint-only");

    const std::uint64_t arrangeBeforeResize = probe->arrangeCalls;
    require(document.setCoordinateSpace(makeUiCoordinateSpace(
            {240.0F, 120.0F},
            {300.0F, 150.0F},
            360,
            180,
            1.5F)),
        "document rejected a resized logical viewport");
    static_cast<void>(document.compose());
    require(document.statistics().layoutPasses == transformed.layoutPasses + 1
            && probe->arrangeCalls == arrangeBeforeResize + 1,
        "logical viewport change did not invalidate layout");
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
    verifyThemeCascade(painter, font);
    verifyCoordinateSpaceInvalidation(painter);

    std::cout << "HeniaUI retained subtree tests passed\n";
    return EXIT_SUCCESS;
}
