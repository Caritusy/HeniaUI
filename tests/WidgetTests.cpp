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
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace henia::ui;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

struct ClickCounter final {
    void clicked() { ++count; }
    int count = 0;
};

struct ValueRecorder final {
    void changed(double next) {
        value = next;
        ++calls;
    }
    double value = 0.0;
    int calls = 0;
};

struct ThrowingCallbacks final {
    void clicked() { throw std::runtime_error("button callback"); }
    void changed(double) { throw std::runtime_error("numeric callback"); }
};

struct RootReplacingCallback final {
    void clicked() {
        nestedDispatchRejected = !document->dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Enter});
        static_cast<void>(document->compose());
        composedDuringCallback = true;
        document->setRoot(std::make_unique<Panel>());
    }

    UiDocument* document = nullptr;
    bool nestedDispatchRejected = false;
    bool composedDuringCallback = false;
};

struct ReparentingCallback final {
    void clicked() { accepted = document->reparentWidget(*widget, *newParent); }

    UiDocument* document = nullptr;
    Widget* widget = nullptr;
    Widget* newParent = nullptr;
    bool accepted = false;
};

struct RemovingCallback final {
    void clicked() { accepted = document->removeWidget(*widget); }

    UiDocument* document = nullptr;
    Widget* widget = nullptr;
    bool accepted = false;
};

struct RemoveOnValueChange final {
    void changed(double) {
        ++calls;
        accepted = document->removeWidget(*widget);
    }

    UiDocument* document = nullptr;
    Widget* widget = nullptr;
    int calls = 0;
    bool accepted = false;
};

static_assert(!noexcept(std::declval<Button&>().handleInput(std::declval<const InputEvent&>())));
static_assert(!noexcept(std::declval<NumericInput&>().handleInput(std::declval<const InputEvent&>())));
static_assert(!noexcept(std::declval<UiDocument&>().dispatch(std::declval<const InputEvent&>())));
static_assert(!noexcept(std::declval<Widget&>().setVisible(false)));
static_assert(!noexcept(std::declval<Widget&>().setEnabled(false)));

[[nodiscard]] Vec2 rectCenter(Rect rect) noexcept {
    return {
        (rect.min.x + rect.max.x) * 0.5F,
        (rect.min.y + rect.max.y) * 0.5F,
    };
}

void click(UiDocument& document, Vec2 position) {
    static_cast<void>(document.dispatch({.kind = InputEventKind::PointerMove, .position = position}));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = position,
        .button = PointerButton::Primary,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = position,
        .button = PointerButton::Primary,
    }));
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

class LayoutProbe final : public Widget {
public:
    explicit LayoutProbe(Vec2 preferred) noexcept : mPreferred(preferred) {}

    void setPreferred(Vec2 preferred) noexcept {
        mPreferred = preferred;
        markLayoutDirty();
    }

    Constraints lastConstraints{};
    std::uint64_t measureCalls = 0;
    std::uint64_t arrangeCalls = 0;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter&, Constraints constraints) override {
        lastConstraints = constraints;
        ++measureCalls;
        return mPreferred;
    }

    void onArrange(TextPainter&, Rect) override { ++arrangeCalls; }

private:
    Vec2 mPreferred{};
};

class InteractionProbe final : public Widget {
public:
    [[nodiscard]] bool acceptsPointerInput() const noexcept override { return true; }
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override { return true; }

    [[nodiscard]] bool handleInput(const InputEvent& event) override {
        if (event.kind == InputEventKind::FocusLost) {
            ++focusLostCalls;
            if (disableOnFocusLost != nullptr) {
                disableOnFocusLost->setEnabled(false);
            }
            return true;
        }
        return event.kind == InputEventKind::PointerDown
            && event.button == PointerButton::Primary;
    }

    Widget* disableOnFocusLost = nullptr;
    int focusLostCalls = 0;
};

void verifyConstraintSensitiveLayout(TextPainter& painter) {
    LayoutProbe cacheProbe({100.0F, 100.0F});
    Vec2 measured = cacheProbe.measure(painter, {{}, {100.0F, 80.0F}});
    cacheProbe.arrange(painter, {{0.0F, 0.0F}, measured});
    static_cast<void>(cacheProbe.measure(painter, {{}, {100.0F, 80.0F}}));
    if (cacheProbe.measureCalls != 1) {
        fail("Measurement cache did not reuse an identical normalized constraint key");
    }
    measured = cacheProbe.measure(painter, {{}, {40.0F, 30.0F}});
    if (!(measured == Vec2{40.0F, 30.0F}) || cacheProbe.measureCalls != 2) {
        fail("Changed constraints reused a stale widget measurement");
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float negativeInfinity = -std::numeric_limits<float>::infinity();
    measured = cacheProbe.measure(painter, {{50.0F, nan}, {10.0F, negativeInfinity}});
    if (!(measured == Vec2{50.0F, 0.0F})
        || !(cacheProbe.lastConstraints == Constraints{{50.0F, 0.0F}, {50.0F, 0.0F}})) {
        fail("Invalid constraints were not normalized before clamping and measurement");
    }

    UiDocument siblingDocument(painter);
    siblingDocument.reserve(16, 4);
    siblingDocument.setViewport({100.0F, 40.0F});
    auto siblingRoot = std::make_unique<Panel>(PanelStyle{
        .padding = {5.0F, 5.0F, 5.0F, 5.0F},
        .gap = 4.0F,
        .direction = LayoutDirection::Row,
        .stretchCrossAxis = true,
    });
    LayoutProbe& leading = siblingRoot->emplaceChild<LayoutProbe>(Vec2{60.0F, 12.0F});
    leading.setLayoutParameters({
        .height = 10.0F,
        .margin = {.right = 3.0F},
    });
    LayoutProbe& trailing = siblingRoot->emplaceChild<LayoutProbe>(Vec2{1000.0F, 12.0F});
    trailing.setLayoutParameters({.margin = {.left = 2.0F}});
    siblingDocument.setRoot(std::move(siblingRoot));
    static_cast<void>(siblingDocument.compose());
    if (!(leading.frame() == Rect{{5.0F, 5.0F}, {65.0F, 15.0F}})
        || !(trailing.frame() == Rect{{74.0F, 5.0F}, {95.0F, 35.0F}})) {
        fail("Margins, remaining-space measurement, or explicit cross-size precedence is incorrect");
    }

    leading.setPreferred({30.0F, 12.0F});
    static_cast<void>(siblingDocument.compose());
    if (!(leading.frame() == Rect{{5.0F, 5.0F}, {35.0F, 15.0F}})
        || !(trailing.frame() == Rect{{44.0F, 5.0F}, {95.0F, 35.0F}})) {
        fail("A sibling size change did not remeasure the constrained remainder");
    }

    siblingDocument.setViewport({70.0F, 40.0F});
    static_cast<void>(siblingDocument.compose());
    if (!(trailing.frame() == Rect{{44.0F, 5.0F}, {65.0F, 35.0F}})) {
        fail("Shrinking the viewport left a stale child measurement");
    }
    siblingDocument.setViewport({120.0F, 40.0F});
    static_cast<void>(siblingDocument.compose());
    if (!(trailing.frame() == Rect{{44.0F, 5.0F}, {115.0F, 35.0F}})) {
        fail("Growing the viewport left a stale child measurement");
    }
    leading.setPreferred({80.0F, 12.0F});
    static_cast<void>(siblingDocument.compose());
    if (!(trailing.frame() == Rect{{94.0F, 5.0F}, {115.0F, 35.0F}})) {
        fail("Growing one sibling did not shrink the constrained remainder");
    }

    UiDocument fixedCrossDocument(painter);
    fixedCrossDocument.reserve(8, 2);
    fixedCrossDocument.setViewport({40.0F, 60.0F});
    auto fixedCrossRoot = std::make_unique<Panel>(PanelStyle{
        .padding = {5.0F, 5.0F, 5.0F, 5.0F},
        .direction = LayoutDirection::Column,
        .stretchCrossAxis = true,
    });
    LayoutProbe& fixedWidth = fixedCrossRoot->emplaceChild<LayoutProbe>(Vec2{20.0F, 20.0F});
    fixedWidth.setLayoutParameters({.width = 12.0F});
    LayoutProbe& stretchedWidth = fixedCrossRoot->emplaceChild<LayoutProbe>(Vec2{10.0F, 20.0F});
    fixedCrossDocument.setRoot(std::move(fixedCrossRoot));
    static_cast<void>(fixedCrossDocument.compose());
    if (fixedWidth.frame().width() != 12.0F || stretchedWidth.frame().width() != 30.0F) {
        fail("Explicit width did not take precedence over column cross-axis stretching");
    }

    UiDocument nestedDocument(painter);
    nestedDocument.reserve(16, 4);
    nestedDocument.setViewport({120.0F, 80.0F});
    auto outer = std::make_unique<Panel>(PanelStyle{
        .padding = {5.0F, 5.0F, 5.0F, 5.0F},
        .direction = LayoutDirection::Column,
    });
    Panel& inner = outer->emplaceChild<Panel>(PanelStyle{
        .gap = 4.0F,
        .direction = LayoutDirection::Row,
    });
    inner.setLayoutParameters({.flexGrow = 1.0F});
    LayoutProbe& one = inner.emplaceChild<LayoutProbe>(Vec2{20.0F, 10.0F});
    one.setLayoutParameters({.flexGrow = 1.0F});
    LayoutProbe& two = inner.emplaceChild<LayoutProbe>(Vec2{20.0F, 10.0F});
    two.setLayoutParameters({.flexGrow = 2.0F});
    nestedDocument.setRoot(std::move(outer));
    static_cast<void>(nestedDocument.compose());
    if (!(inner.frame() == Rect{{5.0F, 5.0F}, {115.0F, 75.0F}})
        || !(one.frame() == Rect{{5.0F, 5.0F}, {47.0F, 75.0F}})
        || !(two.frame() == Rect{{51.0F, 5.0F}, {115.0F, 75.0F}})
        || !(one.lastConstraints == Constraints{{42.0F, 70.0F}, {42.0F, 70.0F}})
        || !(two.lastConstraints == Constraints{{64.0F, 70.0F}, {64.0F, 70.0F}})) {
        fail("Nested flex allocation was not remeasured under its final arranged size");
    }
}

void verifyInteractionCapabilities(TextPainter& painter, FontHandle font) {
    UiDocument passiveDocument(painter);
    passiveDocument.reserve(32, 4);
    passiveDocument.setViewport({120.0F, 60.0F});
    auto passiveRoot = std::make_unique<Panel>();
    Panel* passivePanel = passiveRoot.get();
    Label& passiveLabel = passiveRoot->emplaceChild<Label>(
        "Passive", LabelStyle{font, 14.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
    passiveDocument.setRoot(std::move(passiveRoot));
    const RenderPacket passiveBefore = passiveDocument.compose();
    const std::uint64_t paintPasses = passiveDocument.statistics().paintPasses;
    const Vec2 passivePosition = rectCenter(passiveLabel.frame());
    const bool passiveMove = passiveDocument.dispatch({
        .kind = InputEventKind::PointerMove,
        .position = passivePosition,
    });
    const bool passiveDown = passiveDocument.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = passivePosition,
        .button = PointerButton::Primary,
    });
    const bool passiveUp = passiveDocument.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = passivePosition,
        .button = PointerButton::Primary,
    });
    const RenderPacket passiveAfter = passiveDocument.compose();
    if (passiveMove || passiveDown || passiveUp
        || passiveLabel.hovered() || passiveLabel.pressed() || passiveLabel.focused()
        || passivePanel->hovered() || passivePanel->pressed() || passivePanel->focused()
        || passiveDocument.statistics().paintPasses != paintPasses
        || passiveAfter.identity() != passiveBefore.identity()
        || passiveAfter.revision() != passiveBefore.revision()) {
        fail("Passive label/panel input changed interaction state or repainted the document");
    }

    UiDocument focusDocument(painter);
    focusDocument.reserve(64, 8);
    focusDocument.setViewport({220.0F, 70.0F});
    auto focusRoot = std::make_unique<Panel>();
    NumericInput& numeric = focusRoot->emplaceChild<NumericInput>(
        7.0, NumericInputStyle{.font = font});
    ValueRecorder commits;
    numeric.setOnValueChanged(
        Callback<double>::bind<ValueRecorder, &ValueRecorder::changed>(commits));
    focusDocument.setRoot(std::move(focusRoot));
    static_cast<void>(focusDocument.compose());
    click(focusDocument, rectCenter(numeric.frame()));
    static_cast<void>(focusDocument.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Backspace,
    }));
    static_cast<void>(focusDocument.dispatch({
        .kind = InputEventKind::TextInput,
        .text = U'4',
    }));
    static_cast<void>(focusDocument.dispatch({
        .kind = InputEventKind::TextInput,
        .text = U'2',
    }));
    numeric.setVisible(false);
    if (numeric.value() != 42.0 || commits.calls != 1
        || numeric.hovered() || numeric.pressed() || numeric.focused()
        || focusDocument.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Right})) {
        fail("Hiding a focused numeric input did not commit and invalidate interaction exactly once");
    }

    numeric.setVisible(true);
    static_cast<void>(focusDocument.compose());
    click(focusDocument, rectCenter(numeric.frame()));
    static_cast<void>(focusDocument.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Backspace,
    }));
    static_cast<void>(focusDocument.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Backspace,
    }));
    static_cast<void>(focusDocument.dispatch({
        .kind = InputEventKind::TextInput,
        .text = U'9',
    }));
    numeric.setEnabled(false);
    if (numeric.value() != 9.0 || commits.calls != 2
        || numeric.hovered() || numeric.pressed() || numeric.focused()
        || focusDocument.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Right})) {
        fail("Disabling a focused numeric input did not commit and stop keyboard input");
    }

    UiDocument captureDocument(painter);
    captureDocument.reserve(32, 4);
    captureDocument.setViewport({160.0F, 60.0F});
    auto captureRoot = std::make_unique<Panel>();
    Button& captured = captureRoot->emplaceChild<Button>(
        "Capture", ButtonStyle{.font = font});
    ClickCounter clicks;
    captured.setOnClick(Callback<>::bind<ClickCounter, &ClickCounter::clicked>(clicks));
    captureDocument.setRoot(std::move(captureRoot));
    static_cast<void>(captureDocument.compose());
    Vec2 capturePosition = rectCenter(captured.frame());
    static_cast<void>(captureDocument.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = {159.0F, 59.0F},
        .button = PointerButton::Primary,
    }));
    const bool retargetedRelease = captureDocument.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = capturePosition,
        .button = PointerButton::Primary,
    });
    if (retargetedRelease || clicks.count != 0 || captured.pressed() || captured.focused()) {
        fail("A passive pointer-down sequence retargeted its release to a control");
    }
    static_cast<void>(captureDocument.dispatch({
        .kind = InputEventKind::PointerMove,
        .position = capturePosition,
    }));
    static_cast<void>(captureDocument.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = capturePosition,
        .button = PointerButton::Primary,
    }));
    if (!captured.pressed() || !captured.focused()) {
        fail("Interactive pointer down did not establish capture and focus");
    }
    captured.setVisible(false);
    const bool hiddenRelease = captureDocument.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = capturePosition,
        .button = PointerButton::Primary,
    });
    if (hiddenRelease || clicks.count != 0
        || captured.hovered() || captured.pressed() || captured.focused()) {
        fail("Hiding a captured widget left a pressed state or retargeted pointer up");
    }

    captured.setVisible(true);
    static_cast<void>(captureDocument.compose());
    capturePosition = rectCenter(captured.frame());
    static_cast<void>(captureDocument.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = capturePosition,
        .button = PointerButton::Primary,
    }));
    captured.setEnabled(false);
    const bool disabledRelease = captureDocument.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = capturePosition,
        .button = PointerButton::Primary,
    });
    if (disabledRelease || clicks.count != 0
        || captured.hovered() || captured.pressed() || captured.focused()) {
        fail("Disabling a captured widget left a pressed state or delivered pointer up");
    }

    UiDocument nestedInvalidationDocument(painter);
    nestedInvalidationDocument.reserve(32, 4);
    nestedInvalidationDocument.setViewport({160.0F, 80.0F});
    auto nestedRoot = std::make_unique<Panel>(
        PanelStyle{.direction = LayoutDirection::Column});
    InteractionProbe& firstProbe = nestedRoot->emplaceChild<InteractionProbe>();
    InteractionProbe& secondProbe = nestedRoot->emplaceChild<InteractionProbe>();
    firstProbe.setLayoutParameters({.height = 40.0F});
    secondProbe.setLayoutParameters({.height = 40.0F});
    nestedInvalidationDocument.setRoot(std::move(nestedRoot));
    static_cast<void>(nestedInvalidationDocument.compose());
    static_cast<void>(nestedInvalidationDocument.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = rectCenter(firstProbe.frame()),
        .button = PointerButton::Primary,
    }));
    static_cast<void>(nestedInvalidationDocument.dispatch({
        .kind = InputEventKind::PointerMove,
        .position = rectCenter(secondProbe.frame()),
    }));
    if (!firstProbe.focused() || !firstProbe.pressed() || !secondProbe.hovered()) {
        fail("Nested interaction invalidation test did not establish its initial state");
    }
    firstProbe.disableOnFocusLost = &secondProbe;
    firstProbe.setVisible(false);
    firstProbe.setVisible(false);
    if (firstProbe.focusLostCalls != 1 || firstProbe.focused() || firstProbe.pressed()
        || secondProbe.enabled() || secondProbe.hovered() || secondProbe.pressed()
        || secondProbe.focused()) {
        fail("Nested interaction invalidation left stale state or repeated FocusLost");
    }

    UiDocument removalDocument(painter);
    removalDocument.reserve(32, 4);
    removalDocument.setViewport({180.0F, 60.0F});
    auto removalRoot = std::make_unique<Panel>();
    Panel* removalRootPointer = removalRoot.get();
    NumericInput& removingNumeric = removalRoot->emplaceChild<NumericInput>(
        1.0, NumericInputStyle{.font = font});
    RemoveOnValueChange removalCallback{
        .document = &removalDocument,
        .widget = &removingNumeric,
    };
    removingNumeric.setOnValueChanged(
        Callback<double>::bind<RemoveOnValueChange, &RemoveOnValueChange::changed>(
            removalCallback));
    removalDocument.setRoot(std::move(removalRoot));
    static_cast<void>(removalDocument.compose());
    click(removalDocument, rectCenter(removingNumeric.frame()));
    static_cast<void>(removalDocument.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Backspace,
    }));
    static_cast<void>(removalDocument.dispatch({
        .kind = InputEventKind::TextInput,
        .text = U'2',
    }));
    removingNumeric.setVisible(false);
    if (!removalCallback.accepted || removalCallback.calls != 1
        || !removalRootPointer->children().empty()) {
        fail("Focus-loss removal during visibility invalidation was not deferred safely");
    }
}

} // namespace

int main() {
    TextureStore textures;
    FontStore fonts;
    const FontHandle font = createFont(textures, fonts);
    if (!font.valid()) {
        fail("Unable to create the widget test font");
    }

    TextRunCache cache(fonts);
    cache.reserve(32, 64);
    TextPainter painter(cache);
    verifyConstraintSensitiveLayout(painter);
    verifyInteractionCapabilities(painter, font);
    UiDocument document(painter);
    document.reserve(512, 32);
    document.setViewport({320.0F, 200.0F});

    auto root = std::make_unique<Panel>(PanelStyle{
        .background = {0.02F, 0.03F, 0.05F, 1.0F},
        .border = {0.12F, 0.20F, 0.28F, 1.0F},
        .borderWidth = 1.0F,
        .radius = 10.0F,
        .padding = {16.0F, 16.0F, 16.0F, 16.0F},
        .gap = 10.0F,
        .direction = LayoutDirection::Column,
    });
    Label& label = root->emplaceChild<Label>(
        "HeniaUI retained document",
        LabelStyle{font, 16.0F, {0.9F, 0.95F, 1.0F, 1.0F}});
    static_cast<void>(label);
    Button& button = root->emplaceChild<Button>(
        "Continue",
        ButtonStyle{.font = font, .fontSize = 14.0F});
    button.setLayoutParameters({.height = 38.0F});
    ClickCounter counter;
    button.setOnClick(Callback<>::bind<ClickCounter, &ClickCounter::clicked>(counter));
    NumericInput& numeric = root->emplaceChild<NumericInput>(
        7.0,
        NumericInputStyle{.font = font, .fontSize = 14.0F});
    numeric.setRange(0.0, 255.0);
    numeric.setStep(1.0);
    ValueRecorder recorder;
    numeric.setOnValueChanged(Callback<double>::bind<ValueRecorder, &ValueRecorder::changed>(recorder));
    document.setRoot(std::move(root));

    const RenderPacket first = document.compose();
    UiDocumentStatistics statistics = document.statistics();
    if (statistics.layoutPasses != 1 || statistics.paintPasses != 1 || first.batches().size() != 1) {
        fail("First retained document composition is incorrect");
    }
    const std::uint64_t firstInstances = first.statistics().instances;
    const RenderPacket cached = document.compose();
    statistics = document.statistics();
    if (cached.identity() != first.identity() || cached.revision() != first.revision()
        || statistics.cachedFrames != 1 || statistics.paintPasses != 1
        || cached.statistics().instances != firstInstances) {
        fail("Stable retained document did not reuse the render packet");
    }

    const Rect buttonFrame = button.frame();
    const Vec2 center{
        (buttonFrame.min.x + buttonFrame.max.x) * 0.5F,
        (buttonFrame.min.y + buttonFrame.max.y) * 0.5F,
    };
    static_cast<void>(document.dispatch({.kind = InputEventKind::PointerMove, .position = center}));
    const RenderPacket hovered = document.compose();
    statistics = document.statistics();
    if (!button.hovered() || statistics.layoutPasses != 1 || statistics.paintPasses != 2
        || hovered.batches().size() != 1) {
        fail("Hover state did not invalidate paint only");
    }

    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = center,
        .button = PointerButton::Primary,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = center,
        .button = PointerButton::Primary,
    }));
    if (counter.count != 1 || button.pressed()) {
        fail("Button capture or direct callback failed");
    }
    static_cast<void>(document.compose());
    statistics = document.statistics();
    if (statistics.layoutPasses != 1 || statistics.paintPasses != 3) {
        fail("Button interaction caused an unnecessary layout pass");
    }

    const Rect numericFrame = numeric.frame();
    const Vec2 increment{
        numericFrame.max.x - 10.0F,
        (numericFrame.min.y + numericFrame.max.y) * 0.5F,
    };
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = increment,
        .button = PointerButton::Primary,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = increment,
        .button = PointerButton::Primary,
    }));
    if (numeric.value() != 8.0 || recorder.value != 8.0 || recorder.calls != 1) {
        fail("Numeric input step button or direct callback failed");
    }

    const Vec2 valueCenter{
        (numericFrame.min.x + numericFrame.max.x) * 0.5F,
        (numericFrame.min.y + numericFrame.max.y) * 0.5F,
    };
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerDown,
        .position = valueCenter,
        .button = PointerButton::Primary,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::PointerUp,
        .position = valueCenter,
        .button = PointerButton::Primary,
    }));
    static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Backspace}));
    static_cast<void>(document.dispatch({.kind = InputEventKind::TextInput, .text = U'4'}));
    static_cast<void>(document.dispatch({.kind = InputEventKind::TextInput, .text = U'2'}));
    static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Enter}));
    if (numeric.value() != 42.0 || recorder.value != 42.0 || recorder.calls != 2) {
        fail("Numeric input keyboard editing failed");
    }

    ThrowingCallbacks throwingCallbacks;
    Button throwingButton("Throw", ButtonStyle{.font = font});
    throwingButton.arrange(painter, {{0.0F, 0.0F}, {80.0F, 30.0F}});
    throwingButton.setOnClick(
        Callback<>::bind<ThrowingCallbacks, &ThrowingCallbacks::clicked>(throwingCallbacks));
    try {
        static_cast<void>(throwingButton.handleInput({
            .kind = InputEventKind::PointerUp,
            .position = {10.0F, 10.0F},
            .button = PointerButton::Primary,
        }));
        fail("Button callback exception was swallowed");
    } catch (const std::runtime_error&) {
    }

    NumericInput throwingNumeric(1.0, NumericInputStyle{.font = font});
    throwingNumeric.arrange(painter, {{0.0F, 0.0F}, {176.0F, 38.0F}});
    throwingNumeric.setOnValueChanged(
        Callback<double>::bind<ThrowingCallbacks, &ThrowingCallbacks::changed>(throwingCallbacks));
    try {
        static_cast<void>(throwingNumeric.handleInput({
            .kind = InputEventKind::PointerScroll,
            .position = {80.0F, 10.0F},
            .scrollY = 1.0F,
        }));
        fail("Numeric callback exception was swallowed");
    } catch (const std::runtime_error&) {
    }

    {
        UiDocument replacingDocument(painter);
        replacingDocument.reserve(128, 16, CapacityPolicy::Fixed);
        replacingDocument.setViewport({240.0F, 120.0F});
        auto replacingRoot = std::make_unique<Panel>();
        Button& replacingButton = replacingRoot->emplaceChild<Button>(
            "Replace root",
            ButtonStyle{.font = font});
        RootReplacingCallback callback{.document = &replacingDocument};
        replacingButton.setOnClick(
            Callback<>::bind<RootReplacingCallback, &RootReplacingCallback::clicked>(callback));
        const std::uint64_t oldRootIdentity = replacingRoot->identity();
        replacingDocument.setRoot(std::move(replacingRoot));
        static_cast<void>(replacingDocument.compose());
        click(replacingDocument, rectCenter(replacingButton.frame()));
        if (!callback.nestedDispatchRejected || !callback.composedDuringCallback
            || replacingDocument.root() == nullptr
            || replacingDocument.root()->identity() == oldRootIdentity
            || replacingDocument.statistics().rejectedNestedDispatches != 1) {
            fail("Root replacement or nested-dispatch policy is unsafe");
        }
        static_cast<void>(replacingDocument.compose());
    }

    {
        UiDocument reparentingDocument(painter);
        reparentingDocument.reserve(128, 16, CapacityPolicy::Fixed);
        reparentingDocument.setViewport({240.0F, 160.0F});
        auto reparentingRoot = std::make_unique<Panel>();
        Panel& source = reparentingRoot->emplaceChild<Panel>();
        Panel& destination = reparentingRoot->emplaceChild<Panel>();
        Button& movingButton = source.emplaceChild<Button>("Move", ButtonStyle{.font = font});
        ReparentingCallback callback{
            .document = &reparentingDocument,
            .widget = &movingButton,
            .newParent = &destination,
        };
        movingButton.setOnClick(
            Callback<>::bind<ReparentingCallback, &ReparentingCallback::clicked>(callback));
        reparentingDocument.setRoot(std::move(reparentingRoot));
        static_cast<void>(reparentingDocument.compose());
        click(reparentingDocument, rectCenter(movingButton.frame()));
        if (!callback.accepted || movingButton.parent() != &destination
            || !source.children().empty() || destination.children().size() != 1
            || movingButton.pressed()) {
            fail("Reparenting an interacted widget was unsafe");
        }
        static_cast<void>(reparentingDocument.compose());
    }

    {
        UiDocument removingDocument(painter);
        removingDocument.reserve(128, 16, CapacityPolicy::Fixed);
        removingDocument.setViewport({240.0F, 120.0F});
        auto removingRoot = std::make_unique<Panel>();
        Panel* rootPointer = removingRoot.get();
        Button& removingButton = removingRoot->emplaceChild<Button>(
            "Remove",
            ButtonStyle{.font = font});
        RemovingCallback callback{
            .document = &removingDocument,
            .widget = &removingButton,
        };
        removingButton.setOnClick(
            Callback<>::bind<RemovingCallback, &RemovingCallback::clicked>(callback));
        removingDocument.setRoot(std::move(removingRoot));
        static_cast<void>(removingDocument.compose());
        click(removingDocument, rectCenter(removingButton.frame()));
        if (!callback.accepted || !rootPointer->children().empty()) {
            fail("Removing a hovered, focused, captured widget was unsafe");
        }
    }

    {
        UiDocument focusDocument(painter);
        focusDocument.reserve(256, 16, CapacityPolicy::Fixed);
        focusDocument.setViewport({240.0F, 160.0F});
        auto focusRoot = std::make_unique<Panel>(PanelStyle{.direction = LayoutDirection::Column});
        Panel* rootPointer = focusRoot.get();
        NumericInput& focusedNumeric = focusRoot->emplaceChild<NumericInput>(
            1.0,
            NumericInputStyle{.font = font});
        Button& focusTarget = focusRoot->emplaceChild<Button>("Focus next", ButtonStyle{.font = font});
        RemoveOnValueChange callback{
            .document = &focusDocument,
            .widget = &focusedNumeric,
        };
        focusedNumeric.setOnValueChanged(
            Callback<double>::bind<RemoveOnValueChange, &RemoveOnValueChange::changed>(callback));
        focusDocument.setRoot(std::move(focusRoot));
        static_cast<void>(focusDocument.compose());

        click(focusDocument, rectCenter(focusedNumeric.frame()));
        static_cast<void>(focusDocument.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Backspace}));
        static_cast<void>(focusDocument.dispatch({.kind = InputEventKind::TextInput, .text = U'2'}));
        static_cast<void>(focusDocument.dispatch({
            .kind = InputEventKind::PointerDown,
            .position = rectCenter(focusTarget.frame()),
            .button = PointerButton::Primary,
        }));
        if (!callback.accepted || callback.calls != 1 || rootPointer->children().size() != 1
            || rootPointer->children().front().get() != &focusTarget || !focusTarget.focused()) {
            fail("Focus-loss callback removal left stale interaction state");
        }
    }

    {
        UiDocument snapshotDocument(painter);
        snapshotDocument.reserve(128, 16, CapacityPolicy::Fixed, 2);
        snapshotDocument.setViewport({240.0F, 120.0F});
        auto snapshotRoot = std::make_unique<Panel>();
        Button& snapshotButton = snapshotRoot->emplaceChild<Button>(
            "Snapshot pressure",
            ButtonStyle{.font = font});
        snapshotDocument.setRoot(std::move(snapshotRoot));
        RenderPacket firstSnapshot = snapshotDocument.compose();
        const Vec2 position = rectCenter(snapshotButton.frame());

        static_cast<void>(snapshotDocument.dispatch({
            .kind = InputEventKind::PointerMove,
            .position = position,
        }));
        const RenderPacket secondSnapshot = snapshotDocument.compose();
        static_cast<void>(snapshotDocument.dispatch({
            .kind = InputEventKind::PointerDown,
            .position = position,
            .button = PointerButton::Primary,
        }));
        const RenderPacket rejectedSnapshot = snapshotDocument.compose();
        if (rejectedSnapshot.identity() != secondSnapshot.identity()
            || rejectedSnapshot.revision() != secondSnapshot.revision()
            || !snapshotButton.paintDirty()) {
            fail("Rejected snapshot publication lost retained paint dirtiness");
        }

        firstSnapshot = {};
        const RenderPacket recoveredSnapshot = snapshotDocument.compose();
        if (recoveredSnapshot.revision() <= secondSnapshot.revision()
            || snapshotButton.paintDirty()
            || snapshotDocument.statistics().paintPasses != 3
            || snapshotDocument.statistics().rejectedCompositions != 1) {
            fail("Retained document did not retry a rejected snapshot publication");
        }
    }

    std::cout << "HeniaUI retained widget tests passed\n";
    return EXIT_SUCCESS;
}
