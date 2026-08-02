#include "henia/ui/widget/controls/TextInput.h"

#include <algorithm>
#include <array>
#include <utility>

namespace henia::ui {

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
                changed = mEditor.insert(filtered(event.textUtf8));
            } else if (event.text != U'\0'
                && (mStyle.multiline || (event.text != U'\r' && event.text != U'\n'))) {
                changed = mEditor.insert(event.text);
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
        case InputEventKind::CompositionCommit:
            changed = mEditor.commitComposition(filtered(event.textUtf8));
            finishEdit(textRevision, changed);
            return true;
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
    canvas.fillRect(bounds, mStyle.background, mStyle.radius);
    canvas.strokeRect(
        bounds,
        focused() ? mStyle.focus : mStyle.border,
        mStyle.radius,
        mStyle.borderWidth);

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
    const TextLayoutResult* layout = textPainter.layout(
        mFontChain,
        mStyle.fontSize,
        visibleText);
    float textY = content.min.y;
    if (!mStyle.multiline && layout != nullptr) {
        textY += std::max((content.height() - layout->metrics.height) * 0.5F, 0.0F);
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
    std::string result(textValue);
    if (!mStyle.multiline) {
        result.erase(
            std::remove_if(result.begin(), result.end(), [](char value) {
                return value == '\r' || value == '\n';
            }),
            result.end());
    }
    return result;
}

std::size_t TextInput::caretAt(Vec2 point) {
    if (mLastPainter == nullptr || mFontChain.empty()) return mEditor.text().size();
    mDisplayText = mEditor.displayText();
    const TextLayoutResult* layout = mLastPainter->layout(
        mFontChain,
        mStyle.fontSize,
        mDisplayText);
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
