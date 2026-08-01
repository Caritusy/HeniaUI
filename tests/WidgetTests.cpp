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

    const RenderPacket& first = document.compose();
    UiDocumentStatistics statistics = document.statistics();
    if (statistics.layoutPasses != 1 || statistics.paintPasses != 1 || first.batches().size() != 1) {
        fail("First retained document composition is incorrect");
    }
    const std::uint64_t firstInstances = first.statistics().instances;
    const RenderPacket& cached = document.compose();
    statistics = document.statistics();
    if (&cached != &first || statistics.cachedFrames != 1 || statistics.paintPasses != 1
        || cached.statistics().instances != firstInstances) {
        fail("Stable retained document did not reuse the render packet");
    }

    const Rect buttonFrame = button.frame();
    const Vec2 center{
        (buttonFrame.min.x + buttonFrame.max.x) * 0.5F,
        (buttonFrame.min.y + buttonFrame.max.y) * 0.5F,
    };
    static_cast<void>(document.dispatch({.kind = InputEventKind::PointerMove, .position = center}));
    const RenderPacket& hovered = document.compose();
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

    std::cout << "HeniaUI retained widget tests passed\n";
    return EXIT_SUCCESS;
}
