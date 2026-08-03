#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/UiDocument.h"
#include "henia/ui/widget/controls/Button.h"
#include "henia/ui/widget/controls/ColorPicker.h"
#include "henia/ui/widget/controls/ComboBox.h"
#include "henia/ui/widget/controls/KeyBindingEditor.h"
#include "henia/ui/widget/controls/Label.h"
#include "henia/ui/widget/controls/ListView.h"
#include "henia/ui/widget/controls/NumericInput.h"
#include "henia/ui/widget/controls/Panel.h"
#include "henia/ui/widget/controls/PopupLayer.h"
#include "henia/ui/widget/controls/ScrollContainer.h"
#include "henia/ui/widget/controls/Slider.h"
#include "henia/ui/widget/controls/TabBar.h"
#include "henia/ui/widget/controls/TextInput.h"
#include "henia/ui/widget/controls/Toggle.h"
#include "henia/ui/widget/controls/Tooltip.h"
#include "henia/ui/widget/controls/TreeView.h"

#include <algorithm>
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

struct TextRecorder final {
    void changed(std::string_view next) {
        value.assign(next);
        ++calls;
    }
    std::string value;
    int calls = 0;
};

struct BoolRecorder final {
    void changed(bool next) { value = next; ++calls; }
    bool value = false;
    int calls = 0;
};

struct IndexRecorder final {
    void changed(std::size_t next) { value = next; ++calls; }
    std::size_t value = 0;
    int calls = 0;
};

struct KeyRecorder final {
    void changed(KeyCode next) { value = next; ++calls; }
    KeyCode value = KeyCode::Unknown;
    int calls = 0;
};

struct ColorRecorder final {
    void changed(Color next) { value = next; ++calls; }
    Color value{};
    int calls = 0;
};

struct ExpansionRecorder final {
    void changed(std::size_t next, bool open) { index = next; expanded = open; ++calls; }
    std::size_t index = 0;
    bool expanded = false;
    int calls = 0;
};

struct ItemSelectionRecorder final {
    void changed(std::size_t nextIndex, ListItemKey nextKey) {
        index = nextIndex;
        key = nextKey;
        ++calls;
    }
    std::size_t index = 0;
    ListItemKey key = 0;
    int calls = 0;
};

struct VirtualLabels final {
    [[nodiscard]] std::string_view label(std::size_t index) {
        return labels[index % labels.size()];
    }
    std::array<std::string_view, 4> labels{"Alpha", "Bravo", "Charlie", "Delta"};
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
static_assert(!noexcept(std::declval<TextInput&>().handleInput(std::declval<const InputEvent&>())));
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

class TallPaintProbe final : public Widget {
public:
    explicit TallPaintProbe(float height) noexcept : mHeight(height) {}
protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter&, Constraints constraints) override {
        return {constraints.maximum.x, mHeight};
    }
    void onPaint(Canvas& canvas, TextPainter&, const Theme&) override {
        canvas.fillRect(frame(), {0.2F, 0.4F, 0.6F, 1.0F}, 0.0F);
    }
private:
    float mHeight = 0.0F;
};

class RecycledRowProbe final : public Widget {
public:
    void bind(std::size_t index, ListItemKey key, bool selected) noexcept {
        if (mIndex == index && mKey == key && mSelected == selected) return;
        mIndex = index;
        mKey = key;
        mSelected = selected;
        markPaintDirty();
    }

    [[nodiscard]] std::size_t index() const noexcept { return mIndex; }
    [[nodiscard]] ListItemKey key() const noexcept { return mKey; }
    [[nodiscard]] bool selected() const noexcept { return mSelected; }
    [[nodiscard]] bool acceptsPointerInput() const noexcept override { return true; }
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override { return true; }
    [[nodiscard]] bool handleInput(const InputEvent&) override {
        ++unexpectedInputCalls;
        return true;
    }

    int unexpectedInputCalls = 0;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter&, Constraints constraints) override {
        return constraints.maximum;
    }
    void onPaint(Canvas& canvas, TextPainter&, const Theme&) override {
        const Color color = mSelected
            ? Color{0.18F, 0.58F, 0.74F, 1.0F}
            : Color{0.10F, 0.16F, 0.22F, 1.0F};
        canvas.fillRect(
            {{frame().min.x + 4.0F, frame().min.y + 2.0F},
             {frame().max.x - 4.0F, frame().max.y - 2.0F}},
            color, 2.0F);
    }

private:
    std::size_t mIndex = std::numeric_limits<std::size_t>::max();
    ListItemKey mKey = 0;
    bool mSelected = false;
};

struct RecycledListModel final {
    [[nodiscard]] ListItemKey key(std::size_t index) const noexcept {
        return 10'000U + static_cast<ListItemKey>((index + rotation) % itemCount);
    }
    [[nodiscard]] float extent(std::size_t index) const noexcept {
        return 18.0F + static_cast<float>(key(index) % 3U) * 6.0F;
    }
    [[nodiscard]] std::unique_ptr<Widget> create() {
        ++creations;
        return std::make_unique<RecycledRowProbe>();
    }
    void bind(Widget& widget, std::size_t index, ListItemKey itemKey, bool selected) {
        static_cast<RecycledRowProbe&>(widget).bind(index, itemKey, selected);
        ++binds;
    }

    std::size_t itemCount = 50'000;
    std::size_t rotation = 0;
    std::uint64_t creations = 0;
    std::uint64_t binds = 0;
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

void verifyEditorGradeTextInput(TextPainter& painter, FontHandle font) {
    UiDocument document(painter);
    document.reserve(256, 16, CapacityPolicy::Fixed);
    document.setViewport({300.0F, 80.0F});
    auto root = std::make_unique<Panel>();
    TextInput& input = root->emplaceChild<TextInput>(
        "seed",
        TextInputStyle{.font = font, .controlWidth = 260.0F});
    MemoryTextClipboard clipboard;
    input.setClipboard(&clipboard);
    TextRecorder recorder;
    input.setOnTextChanged(
        Callback<std::string_view>::bind<TextRecorder, &TextRecorder::changed>(recorder));
    document.setRoot(std::move(root));
    static_cast<void>(document.compose());
    click(document, rectCenter(input.frame()));

    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::A,
        .control = true,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::TextInput,
        .textUtf8 = "\xE4\xB8\xAD",
    }));
    if (input.text() != "\xE4\xB8\xAD" || recorder.calls != 1) {
        fail("TextInput did not replace a UTF-8 selection or invoke its callback");
    }

    static_cast<void>(document.dispatch({.kind = InputEventKind::CompositionStart}));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::CompositionUpdate,
        .textUtf8 = "\xE3\x81\x82",
        .compositionSelectionStart = 3,
    }));
    if (input.text() != "\xE4\xB8\xAD" || recorder.calls != 1
        || !input.editor().composition().active) {
        fail("TextInput IME preedit changed committed storage");
    }
    const RenderPacket composing = document.compose();
    if (composing.instances().empty()) {
        fail("TextInput IME composition did not produce retained paint output");
    }
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::CompositionCommit,
        .textUtf8 = "\xE3\x81\x82",
    }));
    if (input.text() != "\xE4\xB8\xAD\xE3\x81\x82" || recorder.calls != 2) {
        fail("TextInput IME commit was not applied as one edit");
    }

    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::A,
        .control = true,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::C,
        .control = true,
    }));
    static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::End}));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::V,
        .control = true,
    }));
    if (input.text() != "\xE4\xB8\xAD\xE3\x81\x82\xE4\xB8\xAD\xE3\x81\x82") {
        fail("TextInput clipboard copy/paste lost multilingual UTF-8 text");
    }
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Z,
        .control = true,
    }));
    if (input.text() != "\xE4\xB8\xAD\xE3\x81\x82") {
        fail("TextInput Ctrl+Z did not restore the previous committed text");
    }

    input.setText("A\xE4\xB8\xAD");
    const int callbacksBeforeBackspace = recorder.calls;
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Backspace,
    }));
    const int callbacksAfterBackspace = recorder.calls;
    if (input.text() != "A" || callbacksAfterBackspace != callbacksBeforeBackspace + 1) {
        fail("TextInput Backspace did not remove exactly one UTF-8 codepoint");
    }
    constexpr std::array controlCharacters{
        U'\b', U'\t', U'\r', U'\x1B', U'\x7F', U'\x85'};
    for (char32_t control : controlCharacters) {
        if (!document.dispatch({
                .kind = InputEventKind::TextInput,
                .text = control,
            })) {
            fail("TextInput did not consume a non-text control scalar");
        }
    }
    if (input.text() != "A" || recorder.calls != callbacksAfterBackspace) {
        fail("A duplicate control character replaced the text deleted by Backspace");
    }

    input.setText("A\xE4\xB8\xAD" "B");
    static_cast<void>(input.editor().setCaret(1));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Insert,
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::TextInput,
        .text = U'\u6587',
    }));
    if (input.text() != "A\xE6\x96\x87" "B") {
        fail("TextInput Insert mode did not overwrite one UTF-8 codepoint");
    }
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Z,
        .control = true,
    }));
    if (input.text() != "A\xE4\xB8\xAD" "B" || input.editor().selection().caret != 1) {
        fail("TextInput overwrite was not restored by one undo step");
    }

    static_cast<void>(document.dispatch({.kind = InputEventKind::CompositionStart}));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::CompositionUpdate,
        .textUtf8 = "\xE6\x96\x87\xE5\xAD\x97",
    }));
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::CompositionCommit,
        .textUtf8 = "\xE6\x96\x87\xE5\xAD\x97",
    }));
    if (input.text() != "A\xE6\x96\x87\xE5\xAD\x97") {
        fail("TextInput overwrite mode did not apply to a Chinese IME commit");
    }
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Z,
        .control = true,
    }));
    if (input.text() != "A\xE4\xB8\xAD" "B") {
        fail("Chinese IME overwrite was not restored by one undo step");
    }
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::KeyDown,
        .key = KeyCode::Insert,
    }));

    static_cast<void>(input.editor().setSelection(1, 4));
    const int callbacksBeforeFilteredSelection = recorder.calls;
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::TextInput,
        .textUtf8 = "\b\t\x7F",
    }));
    if (input.text() != "A\xE4\xB8\xAD" "B"
        || recorder.calls != callbacksBeforeFilteredSelection) {
        fail("Filtered control-only UTF-8 input deleted the active selection");
    }

    input.setText("");
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::TextInput,
        .textUtf8 = "a\r\nb\rc\n\t\x7F",
    }));
    if (input.text() != "abc") {
        fail("Single-line TextInput retained control characters from UTF-8 input");
    }

    input.setStyle(TextInputStyle{
        .font = font,
        .controlWidth = 260.0F,
        .multiline = true,
    });
    input.setText("");
    static_cast<void>(document.compose());
    static_cast<void>(document.dispatch({
        .kind = InputEventKind::TextInput,
        .textUtf8 = "a\r\nb\rc\n\t\x7F",
    }));
    if (input.text() != "a\nb\nc\n") {
        fail("Multiline TextInput did not normalize line endings or filter controls");
    }
}

void verifyProductionOverlayWidgets(TextPainter& painter, FontHandle font) {
    {
        UiDocument document(painter);
        document.reserve(1024, 64);
        document.setViewport({360.0F, 420.0F});
        auto root = std::make_unique<Panel>(PanelStyle{
            .padding = {8.0F, 8.0F, 8.0F, 8.0F},
            .gap = 6.0F,
            .direction = LayoutDirection::Column,
        });
        Checkbox& checkbox = root->emplaceChild<Checkbox>(
            "Enable overlay", false, ToggleStyle{.font = font});
        Toggle& toggle = root->emplaceChild<Toggle>(
            "Streamer mode", false, ToggleStyle{.font = font});
        Slider& slider = root->emplaceChild<Slider>(0.0, 0.0, 100.0, 1.0);
        ComboBox& combo = root->emplaceChild<ComboBox>(
            std::vector<std::string>{"Low", "Medium", "High"}, 0,
            ComboBoxStyle{.font = font});
        TabBar& tabs = root->emplaceChild<TabBar>(
            std::vector<std::string>{"Aim", "Visual", "World"}, 0,
            TabBarStyle{.font = font});
        ColorPicker& picker = root->emplaceChild<ColorPicker>(
            Color{1.0F, 0.0F, 0.0F, 1.0F}, ColorPickerStyle{.height = 110.0F});
        BoolRecorder checkboxRecorder;
        BoolRecorder toggleRecorder;
        ValueRecorder sliderRecorder;
        IndexRecorder comboRecorder;
        IndexRecorder tabRecorder;
        ColorRecorder colorRecorder;
        checkbox.setOnChanged(Callback<bool>::bind<BoolRecorder, &BoolRecorder::changed>(checkboxRecorder));
        toggle.setOnChanged(Callback<bool>::bind<BoolRecorder, &BoolRecorder::changed>(toggleRecorder));
        slider.setOnValueChanged(Callback<double>::bind<ValueRecorder, &ValueRecorder::changed>(sliderRecorder));
        combo.setOnSelectionChanged(Callback<std::size_t>::bind<IndexRecorder, &IndexRecorder::changed>(comboRecorder));
        tabs.setOnSelectionChanged(Callback<std::size_t>::bind<IndexRecorder, &IndexRecorder::changed>(tabRecorder));
        picker.setOnColorChanged(Callback<Color>::bind<ColorRecorder, &ColorRecorder::changed>(colorRecorder));
        document.setRoot(std::move(root));
        static_cast<void>(document.compose());

        if (!document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Tab})
            || !checkbox.focused()) {
            fail("Tab traversal did not focus the first production control");
        }
        static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Space}));
        static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Tab}));
        if (!checkbox.checked() || checkboxRecorder.calls != 1 || !toggle.focused()) {
            fail("Checkbox activation or forward keyboard traversal failed");
        }
        static_cast<void>(document.dispatch({
            .kind = InputEventKind::KeyDown, .key = KeyCode::Tab, .shift = true}));
        if (!checkbox.focused()) fail("Shift+Tab did not traverse focus backwards");

        click(document, rectCenter(toggle.frame()));
        click(document, {slider.frame().min.x + slider.frame().width() * 0.75F,
                         (slider.frame().min.y + slider.frame().max.y) * 0.5F});
        click(document, rectCenter(combo.frame()));
        static_cast<void>(document.compose());
        click(document, {combo.frame().min.x + 20.0F,
                         combo.frame().min.y + combo.style().rowHeight * 2.5F});
        click(document, rectCenter(tabs.frame()));
        click(document, {picker.frame().min.x + picker.frame().width() * 0.5F,
                         picker.frame().max.y - 5.0F});
        if (!toggle.checked() || toggleRecorder.calls != 1
            || slider.value() < 70.0 || sliderRecorder.calls == 0
            || combo.selectedIndex() != 1 || comboRecorder.value != 1
            || tabRecorder.calls == 0 || colorRecorder.calls == 0) {
            fail("Production toggle/slider/combo/tab/color interactions failed");
        }
    }

    {
        UiDocument document(painter);
        document.reserve(256, 24);
        document.setViewport({260.0F, 100.0F});
        auto root = std::make_unique<Panel>(PanelStyle{.gap = 6.0F, .direction = LayoutDirection::Column});
        KeyBindingEditor& editor = root->emplaceChild<KeyBindingEditor>(
            KeyCode::F1, KeyBindingEditorStyle{.font = font});
        Button& next = root->emplaceChild<Button>("Next", ButtonStyle{.font = font});
        KeyRecorder recorder;
        editor.setOnBindingChanged(Callback<KeyCode>::bind<KeyRecorder, &KeyRecorder::changed>(recorder));
        document.setRoot(std::move(root));
        static_cast<void>(document.compose());
        click(document, rectCenter(editor.frame()));
        static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Tab}));
        if (editor.binding() != KeyCode::Tab || recorder.value != KeyCode::Tab || recorder.calls != 1) {
            fail("Key binding capture did not retain Tab instead of traversing focus");
        }
        static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::Tab}));
        if (!next.focused()) fail("Focus traversal did not resume after key capture");
    }

    {
        UiDocument document(painter);
        document.reserve(256, 24);
        document.setViewport({140.0F, 100.0F});
        auto list = std::make_unique<ListView>(std::vector<std::string>{}, ListViewStyle{
            .font = font, .width = 140.0F, .height = 100.0F, .rowHeight = 20.0F});
        ListView* listPointer = list.get();
        VirtualLabels labels;
        IndexRecorder recorder;
        list->setVirtualItems(10'000,
            ValueCallback<std::string_view, std::size_t>::bind<VirtualLabels, &VirtualLabels::label>(labels));
        list->setOnSelectionChanged(
            Callback<std::size_t>::bind<IndexRecorder, &IndexRecorder::changed>(recorder));
        document.setRoot(std::move(list));
        const RenderPacket packet = document.compose();
        if (listPointer->lastPaintedRowCount() > 6 || packet.instances().size() > 64) {
            fail("Virtual ListView rendered rows outside its viewport");
        }
        click(document, {30.0F, 50.0F});
        static_cast<void>(document.dispatch({.kind = InputEventKind::KeyDown, .key = KeyCode::End}));
        if (listPointer->selectedIndex() != 9'999 || recorder.value != 9'999
            || listPointer->scrollOffset() <= 0.0F) {
            fail("Virtual ListView selection/reveal keyboard path failed");
        }
    }

    {
        UiDocument document(painter);
        document.reserve(256, 256, 24, CapacityPolicy::Fixed);
        document.setViewport({180.0F, 120.0F});
        RecycledListModel model;
        ItemSelectionRecorder selection;
        auto list = std::make_unique<ListView>(std::vector<std::string>{}, ListViewStyle{
            .width = 180.0F,
            .height = 120.0F,
            .rowHeight = 24.0F,
            .overscanRows = 2,
        });
        ListView* listPointer = list.get();
        list->setRecycledItems({
            .itemCount = model.itemCount,
            .itemKey = ValueCallback<ListItemKey, std::size_t>::bind<
                RecycledListModel, &RecycledListModel::key>(model),
            .itemExtent = ValueCallback<float, std::size_t>::bind<
                RecycledListModel, &RecycledListModel::extent>(model),
            .createWidget = ValueCallback<std::unique_ptr<Widget>>::bind<
                RecycledListModel, &RecycledListModel::create>(model),
            .bindWidget = Callback<Widget&, std::size_t, ListItemKey, bool>::bind<
                RecycledListModel, &RecycledListModel::bind>(model),
        });
        list->setOnItemSelectionChanged(
            Callback<std::size_t, ListItemKey>::bind<
                ItemSelectionRecorder, &ItemSelectionRecorder::changed>(selection));
        document.setRoot(std::move(list));
        RenderPacket packet = document.compose();

        for (std::size_t sample = 0; sample < 128; ++sample) {
            listPointer->setScrollOffset(static_cast<float>(sample * 173U));
            packet = document.compose();
        }
        const std::size_t warmPoolSize = listPointer->pooledWidgetCount();
        const std::uint64_t warmCreationCount = listPointer->widgetCreationCount();
        std::vector<std::uint64_t> physicalIdentities;
        physicalIdentities.reserve(warmPoolSize);
        for (const std::unique_ptr<Widget>& itemWidget : listPointer->children()) {
            physicalIdentities.push_back(itemWidget->identity());
        }

        for (std::size_t sample = 0; sample < 1'000; ++sample) {
            listPointer->setScrollOffset(static_cast<float>((sample * 7'919U) % 1'100'000U));
            packet = document.compose();
        }
        if (listPointer->pooledWidgetCount() != warmPoolSize
            || listPointer->widgetCreationCount() != warmCreationCount
            || model.creations != warmCreationCount || warmPoolSize > 12
            || listPointer->lastPaintedRowCount() > 8 || packet.instances().size() > 40) {
            fail("Recycled ListView pool or render work grew with the 50,000-item data set");
        }
        for (std::size_t slot = 0; slot < physicalIdentities.size(); ++slot) {
            if (listPointer->children()[slot]->identity() != physicalIdentities[slot]) {
                fail("Recycled ListView replaced a physical widget while scrolling");
            }
        }
        const bool clippedRows = std::any_of(packet.batches().begin(), packet.batches().end(),
            [](const DrawBatch& batch) { return batch.clip.enabled; });
        if (!clippedRows) fail("Recycled ListView descendants were not clipped to the viewport");

        const ListItemKey preserved = model.key(43'210);
        listPointer->setSelectedItemKey(preserved);
        static_cast<void>(document.compose());
        model.rotation = 123;
        listPointer->refreshRecycledItems(model.itemCount);
        static_cast<void>(document.compose());
        if (listPointer->selectedItemKey() != preserved
            || listPointer->selectedIndex() != 43'087) {
            fail("Recycled ListView did not preserve stable-key selection across reordering");
        }
        const auto selectedRow = std::find_if(
            listPointer->realizedItems().begin(), listPointer->realizedItems().end(),
            [preserved](const RealizedListItem& itemValue) {
                return itemValue.key == preserved && itemValue.selected;
            });
        if (selectedRow == listPointer->realizedItems().end()) {
            fail("Recycled ListView did not realize/rebind its revealed selected item");
        }

        click(document, {40.0F, 50.0F});
        if (!listPointer->focused() || selection.calls != 1
            || selection.key != model.key(selection.index)) {
            fail("Recycled ListView input selected an obsolete physical-widget identity");
        }
        for (const RealizedListItem& itemValue : listPointer->realizedItems()) {
            if (static_cast<RecycledRowProbe*>(itemValue.widget)->unexpectedInputCalls != 0) {
                fail("Presentation-only recycled item intercepted list input");
            }
        }
    }

    {
        UiDocument document(painter);
        document.reserve(128, 16);
        document.setViewport({120.0F, 80.0F});
        auto scroll = std::make_unique<ScrollContainer>(
            std::make_unique<TallPaintProbe>(400.0F),
            ScrollContainerStyle{.width = 120.0F, .height = 80.0F});
        ScrollContainer* scrollPointer = scroll.get();
        document.setRoot(std::move(scroll));
        const RenderPacket packet = document.compose();
        const bool clippedChild = std::any_of(packet.batches().begin(), packet.batches().end(),
            [](const DrawBatch& batch) { return batch.clip.enabled; });
        static_cast<void>(document.dispatch({
            .kind = InputEventKind::PointerScroll,
            .position = {50.0F, 40.0F},
            .scrollY = -1.0F,
        }));
        static_cast<void>(document.compose());
        if (!clippedChild || scrollPointer->contentExtent() != 400.0F
            || scrollPointer->scrollOffset() <= 0.0F) {
            fail("ScrollContainer did not clip descendants or consume bubbled wheel input");
        }
    }

    {
        UiDocument document(painter);
        document.reserve(256, 24);
        document.setViewport({200.0F, 140.0F});
        auto content = std::make_unique<Panel>(PanelStyle{.background = {0.08F, 0.10F, 0.14F, 1.0F}});
        auto popup = std::make_unique<Panel>(PanelStyle{.background = {0.16F, 0.22F, 0.28F, 1.0F}});
        auto layer = std::make_unique<PopupLayer>(
            std::move(content), std::move(popup), Rect{{50.0F, 40.0F}, {150.0F, 100.0F}});
        PopupLayer* layerPointer = layer.get();
        ClickCounter dismissed;
        layer->setOnDismissed(Callback<>::bind<ClickCounter, &ClickCounter::clicked>(dismissed));
        layer->setOpen(true);
        document.setRoot(std::move(layer));
        const RenderPacket open = document.compose();
        click(document, {10.0F, 10.0F});
        if (open.instances().size() < 3 || layerPointer->open() || dismissed.count != 1) {
            fail("Modal PopupLayer paint order or backdrop dismissal failed");
        }
    }

    {
        UiDocument document(painter);
        document.reserve(256, 24);
        document.setViewport({180.0F, 80.0F});
        std::vector<TreeViewNode> nodes{
            {"Root", kTreeRoot, true},
            {"Child A", 0, true},
            {"Child B", 0, true},
            {"Other", kTreeRoot, true},
        };
        auto tree = std::make_unique<TreeView>(std::move(nodes), TreeViewStyle{
            .font = font, .width = 180.0F, .height = 80.0F, .rowHeight = 20.0F});
        TreeView* treePointer = tree.get();
        ExpansionRecorder expansion;
        tree->setOnExpansionChanged(
            Callback<std::size_t, bool>::bind<ExpansionRecorder, &ExpansionRecorder::changed>(expansion));
        document.setRoot(std::move(tree));
        static_cast<void>(document.compose());
        click(document, {12.0F, 10.0F});
        static_cast<void>(document.compose());
        if (treePointer->visibleNodeCount() != 2 || treePointer->lastPaintedRowCount() > 4
            || expansion.index != 0 || expansion.expanded || expansion.calls != 1) {
            fail("TreeView expansion or visible-row virtualization failed");
        }
    }

    {
        UiDocument document(painter);
        document.reserve(64, 8);
        document.setViewport({160.0F, 40.0F});
        document.setRoot(std::make_unique<Tooltip>(
            "Explicit hover delay", TooltipStyle{.font = font}));
        if (document.compose().instances().empty()) fail("Tooltip did not produce a passive bubble");
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
    verifyEditorGradeTextInput(painter, font);
    verifyProductionOverlayWidgets(painter, font);
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
