#include "henia/ui/widget/controls/TextInput.h"

#include "henia/ui/text/Utf8.h"

#include <algorithm>
#include <array>
#include <utility>

namespace henia::ui {
namespace {

[[nodiscard]] constexpr bool isControlCharacter(char32_t value) noexcept {
    return value < U' ' || (value >= 0x7FU && value <= 0x9FU);
}

[[nodiscard]] constexpr Color mixColor(
    Color first,
    Color second,
    float amount) noexcept {
    const float value = std::clamp(amount, 0.0F, 1.0F);
    return {
        first.red + (second.red - first.red) * value,
        first.green + (second.green - first.green) * value,
        first.blue + (second.blue - first.blue) * value,
        first.alpha + (second.alpha - first.alpha) * value,
    };
}

} // namespace

TextInput::TextInput(std::string textValue, TextInputStyle style)
    : Widget(WidgetKind::TextInput), mStyle(std::move(style)), mEditor(std::move(textValue)) {
    rebuildFontChain();
}

void TextInput::setText(std::string textValue) {
    if (mEditor.text() == textValue) return;
    mEditor.setText(std::move(textValue));
    mScrollX = 0.0F;
    markPaintDirty();
}

std::string_view TextInput::text() const noexcept { return mEditor.text(); }

void TextInput::setPlaceholder(std::string placeholderValue) {
    if (mPlaceholder == placeholderValue) return;
    mPlaceholder = std::move(placeholderValue);
    markPaintDirty();
}

std::string_view TextInput::placeholder() const noexcept { return mPlaceholder; }

void TextInput::setStyle(TextInputStyle styleValue) {
    mStyle = std::move(styleValue);
    rebuildFontChain();
    mScrollX = 0.0F;
    markLayoutDirty();
}

const TextInputStyle& TextInput::style() const noexcept { return mStyle; }
void TextInput::setClipboard(TextClipboard* clipboardValue) noexcept { mClipboard = clipboardValue; }
TextClipboard* TextInput::clipboard() const noexcept { return mClipboard; }
void TextInput::setOnTextChanged(Callback<std::string_view> callback) noexcept {
    mOnTextChanged = callback;
}
TextEditorState& TextInput::editor() noexcept { return mEditor; }
const TextEditorState& TextInput::editor() const noexcept { return mEditor; }

bool TextInput::acceptsPointerInput() const noexcept { return true; }
bool TextInput::acceptsKeyboardFocus() const noexcept { return true; }

bool TextInput::handleInput(const InputEvent& event) {
    if (event.kind == InputEventKind::FocusLost) {
        mPointerSelecting = false;
        if (mEditor.cancelComposition()) markPaintDirty();
        return true;
    }
    if (event.kind == InputEventKind::PointerCancel) {
        mPointerSelecting = false;
        return true;
    }
    if (!enabled()) return false;

    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) {
        static_cast<void>(mEditor.cancelComposition());
        const bool changed = mEditor.setCaret(caretAt(event.position), event.shift);
        mPointerSelecting = true;
        if (changed) markPaintDirty();
        return true;
    }
    if (event.kind == InputEventKind::PointerMove && mPointerSelecting) {
        if (mEditor.setCaret(caretAt(event.position), true)) markPaintDirty();
        return true;
    }
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary) {
        mPointerSelecting = false;
        return true;
    }
    if (!focused()) return false;

    const std::uint64_t textRevision = mEditor.textRevision();
    bool changed = false;
    switch (event.kind) {
        case InputEventKind::TextInput:
            if (!event.textUtf8.empty()) {
                const std::string textValue = filtered(event.textUtf8);
                if (!textValue.empty()) {
                    changed = mOverwriteMode
                        ? mEditor.overwrite(textValue)
                        : mEditor.insert(textValue);
                }
            } else if (mStyle.multiline && event.text == U'\n') {
                changed = mEditor.insert("\n");
            } else if (event.text != U'\0' && !isControlCharacter(event.text)) {
                changed = mOverwriteMode
                    ? mEditor.overwrite(event.text)
                    : mEditor.insert(event.text);
            }
            finishEdit(textRevision, changed);
            return true;
        case InputEventKind::CompositionStart:
            changed = mEditor.beginComposition();
            finishEdit(textRevision, changed);
            return true;
        case InputEventKind::CompositionUpdate: {
            const std::string textValue = filtered(event.textUtf8);
            changed = mEditor.updateComposition(
                textValue,
                std::min(event.compositionSelectionStart, textValue.size()),
                event.compositionSelectionLength);
            finishEdit(textRevision, changed);
            return true;
        }
        case InputEventKind::CompositionCommit: {
            const std::string textValue = filtered(event.textUtf8);
            const TextComposition composition = mEditor.composition();
            if (mOverwriteMode
                && (!composition.active
                    || composition.replaceBegin == composition.replaceEnd)) {
                const std::size_t caret = composition.active
                    ? composition.replaceBegin
                    : mEditor.selection().caret;
                changed = mEditor.cancelComposition();
                static_cast<void>(mEditor.setCaret(caret));
                if (!textValue.empty()) {
                    changed = mEditor.overwrite(textValue) || changed;
                }
            } else {
                changed = mEditor.commitComposition(textValue);
            }
            finishEdit(textRevision, changed);
            return true;
        }
        case InputEventKind::CompositionCancel:
            changed = mEditor.cancelComposition();
            finishEdit(textRevision, changed);
            return true;
        default:
            break;
    }
    if (event.kind != InputEventKind::KeyDown) return false;

    if (event.control) {
        switch (event.key) {
            case KeyCode::A: changed = mEditor.selectAll(); break;
            case KeyCode::C:
                return mClipboard != nullptr && mEditor.copy(*mClipboard);
            case KeyCode::X:
                changed = mClipboard != nullptr && mEditor.cut(*mClipboard);
                break;
            case KeyCode::V:
                if (mClipboard != nullptr) {
                    if (mStyle.multiline) {
                        changed = mEditor.paste(*mClipboard);
                    } else if (const std::optional<std::string> value = mClipboard->readText()) {
                        changed = mEditor.insert(filtered(*value));
                    }
                }
                break;
            case KeyCode::Z:
                changed = event.shift ? mEditor.redo() : mEditor.undo();
                break;
            case KeyCode::Y: changed = mEditor.redo(); break;
            default: return false;
        }
        finishEdit(textRevision, changed);
        return true;
    }

    switch (event.key) {
        case KeyCode::Backspace: changed = mEditor.backspace(); break;
        case KeyCode::Delete: changed = mEditor.deleteForward(); break;
        case KeyCode::Left: changed = mEditor.moveLeft(event.shift); break;
        case KeyCode::Right: changed = mEditor.moveRight(event.shift); break;
        case KeyCode::Home: changed = mEditor.moveLineStart(event.shift); break;
        case KeyCode::End: changed = mEditor.moveLineEnd(event.shift); break;
        case KeyCode::Insert:
            mOverwriteMode = !mOverwriteMode;
            return true;
        case KeyCode::Enter:
            if (!mStyle.multiline) return false;
            changed = mEditor.insert("\n");
            break;
        case KeyCode::Escape: changed = mEditor.cancelComposition(); break;
        default: return false;
    }
    finishEdit(textRevision, changed);
    return true;
}

Vec2 TextInput::onMeasure(TextPainter& textPainter, Constraints) {
    mLastPainter = &textPainter;
    return {mStyle.controlWidth, mStyle.controlHeight};
}

void TextInput::onPaint(Canvas& canvas, TextPainter& textPainter, const Theme&) {
    mLastPainter = &textPainter;
    const Rect bounds = frame();
    const bool active = focused() || pressed();
    const Color background = active
        ? mixColor(mStyle.background, mStyle.focus, 0.13F)
        : hovered()
            ? mixColor(mStyle.background, mStyle.focus, 0.07F)
            : mStyle.background;
    const Color border = focused()
        ? mStyle.focus
        : hovered() || pressed()
            ? mixColor(mStyle.border, mStyle.focus, pressed() ? 0.72F : 0.48F)
            : mStyle.border;
    if (hovered() || active) {
        Color glow = mStyle.focus;
        glow.alpha *= active ? 0.16F : 0.08F;
        canvas.roundedGlow(bounds, glow, mStyle.radius, active ? 5.0F : 3.0F);
    }
    canvas.fillRect(bounds, background, mStyle.radius);
    canvas.strokeRect(
        bounds,
        border,
        mStyle.radius,
        mStyle.borderWidth + (active ? 0.5F : 0.0F));

    const Rect content{
        {bounds.min.x + mStyle.padding.left, bounds.min.y + mStyle.padding.top},
        {bounds.max.x - mStyle.padding.right, bounds.max.y - mStyle.padding.bottom},
    };
    Canvas::ClipScope clip = canvas.scopedClip(content);
    mDisplayText = mEditor.displayText();
    const std::string_view visibleText = mDisplayText.empty() && !mPlaceholder.empty()
        ? std::string_view(mPlaceholder)
        : std::string_view(mDisplayText);
    const Color textColor = mDisplayText.empty() && !mPlaceholder.empty()
        ? mStyle.placeholderColor
        : mStyle.textColor;
    const TextLayoutResult* layout = layoutText(textPainter, visibleText);
    float textY = content.min.y;
    if (!mStyle.multiline && layout != nullptr) {
        textY = TextPainter::centeredVisualOrigin(*layout, content).y;
    }
    if (layout != nullptr && focused() && !mDisplayText.empty()) {
        const Vec2 caret = TextPainter::caretPosition(*layout, mEditor.displayCaret());
        const float visibleWidth = std::max(content.width(), 0.0F);
        if (caret.x - mScrollX > visibleWidth - 1.0F) {
            mScrollX = caret.x - visibleWidth + 1.0F;
        } else if (caret.x < mScrollX) {
            mScrollX = caret.x;
        }
    } else if (!focused()) {
        mScrollX = 0.0F;
    }
    mTextOrigin = {content.min.x - mScrollX, textY};

    if (layout != nullptr && !mDisplayText.empty()) {
        const TextSelection selection = mEditor.displaySelection();
        for (Rect rectangle : TextPainter::selectionRects(
                 *layout, selection.begin(), selection.end())) {
            rectangle.min.x += mTextOrigin.x;
            rectangle.min.y += mTextOrigin.y;
            rectangle.max.x += mTextOrigin.x;
            rectangle.max.y += mTextOrigin.y;
            canvas.fillRect(rectangle, mStyle.selection, 1.0F);
        }
        textPainter.drawLayout(canvas, *layout, mTextOrigin, textColor);

        const TextSelection composition = mEditor.compositionDisplayRange();
        for (Rect rectangle : TextPainter::selectionRects(
                 *layout, composition.begin(), composition.end())) {
            const float y = rectangle.max.y + mTextOrigin.y - 1.0F;
            canvas.line(
                {rectangle.min.x + mTextOrigin.x, y},
                {rectangle.max.x + mTextOrigin.x, y},
                mStyle.composition,
                1.0F,
                LineCap::Butt);
        }
    } else if (layout != nullptr) {
        textPainter.drawLayout(canvas, *layout, mTextOrigin, textColor);
    }

    if (focused()) {
        Vec2 caret{};
        float caretHeight = mStyle.fontSize;
        if (layout != nullptr && !mDisplayText.empty()) {
            caret = TextPainter::caretPosition(*layout, mEditor.displayCaret());
            for (const TextCaretStop& stop : layout->caretStops) {
                if (stop.byteOffset == mEditor.displayCaret()) {
                    caretHeight = stop.lineHeight;
                    break;
                }
            }
        }
        caret.x += mTextOrigin.x;
        caret.y += mTextOrigin.y;
        canvas.line(
            caret,
            {caret.x, caret.y + caretHeight},
            mStyle.caret,
            1.0F,
            LineCap::Butt);
    }
}

void TextInput::rebuildFontChain() {
    mFontChain.clear();
    if (mStyle.font.valid()) mFontChain.push_back(mStyle.font);
    for (FontHandle fallback : mStyle.fallbackFonts) {
        if (fallback.valid()
            && std::find(mFontChain.begin(), mFontChain.end(), fallback) == mFontChain.end()) {
            mFontChain.push_back(fallback);
        }
    }
}

std::string TextInput::filtered(std::string_view textValue) const {
    std::string result;
    result.reserve(textValue.size());
    bool previousWasCarriageReturn = false;
    for (std::size_t offset = 0; offset < textValue.size();) {
        const Utf8Codepoint decoded = decodeUtf8(textValue, offset);
        if (decoded.bytes == 0) break;
        offset += decoded.bytes;
        const char32_t value = decoded.valid ? decoded.value : U'\uFFFD';
        if (value == U'\r') {
            if (mStyle.multiline) result.push_back('\n');
            previousWasCarriageReturn = true;
            continue;
        }
        if (value == U'\n') {
            if (mStyle.multiline && !previousWasCarriageReturn) {
                result.push_back('\n');
            }
            previousWasCarriageReturn = false;
            continue;
        }
        previousWasCarriageReturn = false;
        if (!isControlCharacter(value)) {
            static_cast<void>(appendUtf8(result, value));
        }
    }
    return result;
}

const TextLayoutResult* TextInput::layoutText(
    TextPainter& painter,
    std::string_view text) {
    if (mStyle.fallbackFonts.empty()) {
        return painter.layout(mStyle.font, mStyle.fontSize, text);
    }
    return painter.layout(mFontChain, mStyle.fontSize, text);
}

std::size_t TextInput::caretAt(Vec2 point) {
    if (mLastPainter == nullptr) return mEditor.text().size();
    mDisplayText = mEditor.displayText();
    const TextLayoutResult* layout = layoutText(*mLastPainter, mDisplayText);
    return layout == nullptr ? 0 : TextPainter::hitTest(
        *layout,
        {point.x - mTextOrigin.x, point.y - mTextOrigin.y});
}

void TextInput::finishEdit(std::uint64_t previousTextRevision, bool changedValue) {
    if (!changedValue) return;
    markPaintDirty();
    if (mEditor.textRevision() != previousTextRevision) {
        mOnTextChanged(mEditor.text());
    }
}

} // namespace henia::ui
