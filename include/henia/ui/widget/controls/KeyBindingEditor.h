#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <string_view>

namespace henia::ui {

[[nodiscard]] std::string_view keyCodeName(KeyCode key) noexcept;

struct KeyBindingEditorStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color text{0.90F, 0.95F, 0.98F, 1.0F};
    Color muted{0.48F, 0.57F, 0.66F, 1.0F};
    Color background{0.035F, 0.052F, 0.078F, 1.0F};
    Color hover{0.055F, 0.082F, 0.115F, 1.0F};
    Color capture{0.08F, 0.19F, 0.25F, 1.0F};
    Color border{0.14F, 0.22F, 0.30F, 1.0F};
    Color focus{0.10F, 0.72F, 0.91F, 1.0F};
    float width = 180.0F;
    float height = 36.0F;
    float radius = 7.0F;
};

class KeyBindingEditor final : public Widget {
public:
    explicit KeyBindingEditor(KeyCode binding = KeyCode::Unknown,
        KeyBindingEditorStyle style = {}) noexcept;

    void setBinding(KeyCode binding) noexcept;
    [[nodiscard]] KeyCode binding() const noexcept;
    [[nodiscard]] bool capturing() const noexcept;
    void setStyle(KeyBindingEditorStyle style) noexcept;
    void setOnBindingChanged(Callback<KeyCode> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool wantsTabKey() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    void beginCapture() noexcept;
    void finishCapture(KeyCode key, bool notify);

    KeyBindingEditorStyle mStyle{};
    Callback<KeyCode> mOnBindingChanged{};
    KeyCode mBinding = KeyCode::Unknown;
    bool mCapturing = false;
};

} // namespace henia::ui
