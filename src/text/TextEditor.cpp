#include "henia/ui/text/TextEditor.h"

#include "henia/ui/text/Utf8.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

bool MemoryTextClipboard::writeText(std::string_view text) {
    mText.assign(text);
    return true;
}

std::optional<std::string> MemoryTextClipboard::readText() { return mText; }

TextEditorState::TextEditorState(std::string textValue) { setText(std::move(textValue)); }

void TextEditorState::setText(std::string textValue) {
    mText = validUtf8(textValue) ? std::move(textValue) : sanitizeUtf8(textValue);
    mSelection = {mText.size(), mText.size()};
    mComposition = {};
    mUndo.clear();
    mRedo.clear();
    changed(true);
}

std::string_view TextEditorState::text() const noexcept { return mText; }
std::uint64_t TextEditorState::revision() const noexcept { return mRevision; }
std::uint64_t TextEditorState::textRevision() const noexcept { return mTextRevision; }
TextSelection TextEditorState::selection() const noexcept { return mSelection; }
std::size_t TextEditorState::selectionBegin() const noexcept { return mSelection.begin(); }
std::size_t TextEditorState::selectionEnd() const noexcept { return mSelection.end(); }
bool TextEditorState::hasSelection() const noexcept { return !mSelection.empty(); }

std::string TextEditorState::selectedText() const {
    return mText.substr(selectionBegin(), selectionEnd() - selectionBegin());
}

bool TextEditorState::setSelection(std::size_t anchor, std::size_t caret) noexcept {
    const TextSelection next{
        clampUtf8Boundary(mText, anchor),
        clampUtf8Boundary(mText, caret),
    };
    if (next == mSelection) return false;
    mSelection = next;
    static_cast<void>(cancelComposition());
    changed(false);
    return true;
}

bool TextEditorState::setCaret(std::size_t caret, bool extend) noexcept {
    caret = clampUtf8Boundary(mText, caret);
    return setSelection(extend ? mSelection.anchor : caret, caret);
}

bool TextEditorState::selectAll() noexcept {
    return setSelection(0, mText.size());
}

bool TextEditorState::moveLeft(bool extend) noexcept {
    const std::size_t next = !extend && hasSelection()
        ? selectionBegin()
        : previousUtf8Boundary(mText, mSelection.caret);
    return setCaret(next, extend);
}

bool TextEditorState::moveRight(bool extend) noexcept {
    const std::size_t next = !extend && hasSelection()
        ? selectionEnd()
        : nextUtf8Boundary(mText, mSelection.caret);
    return setCaret(next, extend);
}

bool TextEditorState::moveLineStart(bool extend) noexcept {
    const std::size_t position = mSelection.caret == 0
        ? std::string::npos
        : mText.rfind('\n', mSelection.caret - 1U);
    return setCaret(position == std::string::npos ? 0 : position + 1U, extend);
}

bool TextEditorState::moveLineEnd(bool extend) noexcept {
    const std::size_t position = mText.find('\n', mSelection.caret);
    return setCaret(position == std::string::npos ? mText.size() : position, extend);
}

bool TextEditorState::insert(std::string_view utf8) {
    return replaceSelection(utf8);
}

bool TextEditorState::insert(char32_t codepoint) {
    std::string encoded;
    if (!appendUtf8(encoded, codepoint)) return false;
    return replaceSelection(encoded);
}

bool TextEditorState::overwrite(std::string_view utf8) {
    if (hasSelection()) return replaceSelection(utf8);
    const std::string replacement = validUtf8(utf8)
        ? std::string(utf8)
        : sanitizeUtf8(utf8);
    std::size_t replacementOffset = 0;
    std::size_t replaceEnd = mSelection.caret;
    while (replacementOffset < replacement.size() && replaceEnd < mText.size()) {
        replacementOffset = nextUtf8Boundary(replacement, replacementOffset);
        replaceEnd = nextUtf8Boundary(mText, replaceEnd);
    }
    return replaceRange(mSelection.caret, replaceEnd, replacement, true);
}

bool TextEditorState::overwrite(char32_t codepoint) {
    std::string encoded;
    if (!appendUtf8(encoded, codepoint)) return false;
    return overwrite(encoded);
}

bool TextEditorState::backspace() {
    if (hasSelection()) return replaceSelection({});
    const std::size_t begin = previousUtf8Boundary(mText, mSelection.caret);
    return begin != mSelection.caret
        && replaceRange(begin, mSelection.caret, {}, true);
}

bool TextEditorState::deleteForward() {
    if (hasSelection()) return replaceSelection({});
    const std::size_t end = nextUtf8Boundary(mText, mSelection.caret);
    return end != mSelection.caret
        && replaceRange(mSelection.caret, end, {}, true);
}

bool TextEditorState::copy(TextClipboard& clipboard) const {
    return hasSelection() && clipboard.writeText(selectedText());
}

bool TextEditorState::cut(TextClipboard& clipboard) {
    if (!copy(clipboard)) return false;
    return replaceSelection({});
}

bool TextEditorState::paste(TextClipboard& clipboard) {
    const std::optional<std::string> value = clipboard.readText();
    return value.has_value() && replaceSelection(*value);
}

void TextEditorState::setMaximumUndoEntries(std::size_t entries) {
    mMaximumUndoEntries = entries;
    if (entries == 0) {
        mUndo.clear();
        mRedo.clear();
        return;
    }
    if (mUndo.size() > entries) {
        mUndo.erase(mUndo.begin(), mUndo.end() - static_cast<std::ptrdiff_t>(entries));
    }
    if (mRedo.size() > entries) {
        mRedo.erase(mRedo.begin(), mRedo.end() - static_cast<std::ptrdiff_t>(entries));
    }
}

bool TextEditorState::canUndo() const noexcept { return !mUndo.empty(); }
bool TextEditorState::canRedo() const noexcept { return !mRedo.empty(); }

bool TextEditorState::undo() {
    if (mUndo.empty()) return false;
    mRedo.push_back({mText, mSelection});
    Snapshot snapshot = std::move(mUndo.back());
    mUndo.pop_back();
    restore(std::move(snapshot));
    return true;
}

bool TextEditorState::redo() {
    if (mRedo.empty()) return false;
    mUndo.push_back({mText, mSelection});
    Snapshot snapshot = std::move(mRedo.back());
    mRedo.pop_back();
    restore(std::move(snapshot));
    return true;
}

bool TextEditorState::beginComposition() {
    if (mComposition.active) return false;
    mComposition = {
        .active = true,
        .replaceBegin = selectionBegin(),
        .replaceEnd = selectionEnd(),
    };
    changed(false);
    return true;
}

bool TextEditorState::updateComposition(
    std::string_view utf8,
    std::size_t caret,
    std::size_t selectionLength) {
    std::string next = validUtf8(utf8) ? std::string(utf8) : sanitizeUtf8(utf8);
    if (!mComposition.active) static_cast<void>(beginComposition());
    caret = clampUtf8Boundary(next, caret);
    const std::size_t selectionEnd = clampUtf8Boundary(
        next,
        std::min(next.size(), caret + std::min(selectionLength, next.size() - caret)));
    selectionLength = selectionEnd - caret;
    if (next == mComposition.text && caret == mComposition.caret
        && selectionLength == mComposition.selectionLength) {
        return false;
    }
    mComposition.text = std::move(next);
    mComposition.caret = caret;
    mComposition.selectionLength = selectionLength;
    changed(false);
    return true;
}

bool TextEditorState::commitComposition(std::string_view utf8) {
    if (!mComposition.active) return replaceSelection(utf8);
    const std::size_t begin = mComposition.replaceBegin;
    const std::size_t end = mComposition.replaceEnd;
    if (begin == end && utf8.empty()) {
        mComposition = {};
        changed(false);
        return true;
    }
    return replaceRange(begin, end, utf8, true);
}

bool TextEditorState::cancelComposition() noexcept {
    if (!mComposition.active) return false;
    mComposition = {};
    changed(false);
    return true;
}

const TextComposition& TextEditorState::composition() const noexcept {
    return mComposition;
}

std::string TextEditorState::displayText() const {
    if (!mComposition.active) return mText;
    std::string result;
    result.reserve(
        mText.size() - (mComposition.replaceEnd - mComposition.replaceBegin)
        + mComposition.text.size());
    result.append(mText, 0, mComposition.replaceBegin);
    result.append(mComposition.text);
    result.append(mText, mComposition.replaceEnd, std::string::npos);
    return result;
}

std::size_t TextEditorState::displayCaret() const noexcept {
    return mComposition.active
        ? mComposition.replaceBegin + mComposition.caret
        : mSelection.caret;
}

TextSelection TextEditorState::displaySelection() const noexcept {
    if (mComposition.active) {
        const std::size_t begin = displayCaret();
        return {begin, begin + mComposition.selectionLength};
    }
    return mSelection;
}

TextSelection TextEditorState::compositionDisplayRange() const noexcept {
    if (!mComposition.active) return {};
    return {
        mComposition.replaceBegin,
        mComposition.replaceBegin + mComposition.text.size(),
    };
}

bool TextEditorState::replaceSelection(std::string_view utf8) {
    return replaceRange(selectionBegin(), selectionEnd(), utf8, true);
}

bool TextEditorState::replaceRange(
    std::size_t begin,
    std::size_t end,
    std::string_view utf8,
    bool recordUndo) {
    begin = clampUtf8Boundary(mText, begin);
    end = clampUtf8Boundary(mText, end);
    if (begin > end) std::swap(begin, end);
    const std::string replacement = validUtf8(utf8) ? std::string(utf8) : sanitizeUtf8(utf8);
    if (begin == end && replacement.empty()) return false;
    if (recordUndo) pushUndo();
    mText.replace(begin, end - begin, replacement);
    const std::size_t caret = begin + replacement.size();
    mSelection = {caret, caret};
    mComposition = {};
    mRedo.clear();
    changed(true);
    return true;
}

void TextEditorState::pushUndo() {
    if (mMaximumUndoEntries == 0) return;
    if (mUndo.size() == mMaximumUndoEntries) mUndo.erase(mUndo.begin());
    mUndo.push_back({mText, mSelection});
}

void TextEditorState::restore(Snapshot snapshot) {
    mText = std::move(snapshot.text);
    mSelection = snapshot.selection;
    mComposition = {};
    changed(true);
}

void TextEditorState::changed(bool textChanged) noexcept {
    ++mRevision;
    if (mRevision == 0) mRevision = 1;
    if (textChanged) {
        ++mTextRevision;
        if (mTextRevision == 0) mTextRevision = 1;
    }
}

} // namespace henia::ui
