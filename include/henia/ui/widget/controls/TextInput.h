#pragma once

#include "henia/ui/text/TextEditor.h"
#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <string>
#include <string_view>
#include <vector>

namespace henia::ui {

struct TextInputStyle final {
    FontHandle font{};
    std::vector<FontHandle> fallbackFonts;
    float fontSize = 14.0F;
    Color textColor{0.90F, 0.95F, 0.98F, 1.0F};
    Color placeholderColor{0.42F, 0.50F, 0.58F, 1.0F};
    Color background{0.032F, 0.047F, 0.071F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    Color focus{0.10F, 0.72F, 0.91F, 1.0F};
    Color selection{0.10F, 0.45F, 0.78F, 0.65F};
    Color caret{0.88F, 0.96F, 1.0F, 1.0F};
    Color composition{0.20F, 0.82F, 0.96F, 1.0F};
    float borderWidth = 1.0F;
    float radius = 7.0F;
    float controlWidth = 240.0F;
    float controlHeight = 38.0F;
    Insets padding{10.0F, 8.0F, 10.0F, 8.0F};
    bool multiline = false;
};

class TextInput final : public Widget {
public:
    explicit TextInput(std::string text = {}, TextInputStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setPlaceholder(std::string placeholder);
    [[nodiscard]] std::string_view placeholder() const noexcept;
    void setStyle(TextInputStyle style);
    [[nodiscard]] const TextInputStyle& style() const noexcept;
    void setClipboard(TextClipboard* clipboard) noexcept;
    [[nodiscard]] TextClipboard* clipboard() const noexcept;
    void setOnTextChanged(Callback<std::string_view> callback) noexcept;
    [[nodiscard]] TextEditorState& editor() noexcept;
    [[nodiscard]] const TextEditorState& editor() const noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    void rebuildFontChain();
    [[nodiscard]] std::string filtered(std::string_view text) const;
    [[nodiscard]] std::size_t caretAt(Vec2 point);
    void finishEdit(std::uint64_t previousTextRevision, bool changed);

    TextInputStyle mStyle{};
    TextEditorState mEditor;
    std::string mPlaceholder;
    std::string mDisplayText;
    std::vector<FontHandle> mFontChain;
    Callback<std::string_view> mOnTextChanged{};
    TextClipboard* mClipboard = nullptr;
    TextPainter* mLastPainter = nullptr;
    Vec2 mTextOrigin{};
    float mScrollX = 0.0F;
    bool mPointerSelecting = false;
    bool mOverwriteMode = false;
};

} // namespace henia::ui
