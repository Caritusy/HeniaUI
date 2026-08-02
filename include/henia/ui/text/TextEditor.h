#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace henia::ui {

class TextClipboard {
public:
    virtual ~TextClipboard() = default;
    [[nodiscard]] virtual bool writeText(std::string_view text) = 0;
    [[nodiscard]] virtual std::optional<std::string> readText() = 0;
};

class MemoryTextClipboard final : public TextClipboard {
public:
    [[nodiscard]] bool writeText(std::string_view text) override;
    [[nodiscard]] std::optional<std::string> readText() override;

private:
    std::string mText;
};

struct TextSelection final {
    std::size_t anchor = 0;
    std::size_t caret = 0;

    [[nodiscard]] constexpr std::size_t begin() const noexcept {
        return anchor < caret ? anchor : caret;
    }
    [[nodiscard]] constexpr std::size_t end() const noexcept {
        return anchor < caret ? caret : anchor;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return anchor == caret; }
    friend constexpr bool operator==(TextSelection, TextSelection) noexcept = default;
};

struct TextComposition final {
    bool active = false;
    std::size_t replaceBegin = 0;
    std::size_t replaceEnd = 0;
    std::size_t caret = 0;
    std::size_t selectionLength = 0;
    std::string text;
};

// UTF-8 byte offsets are always clamped to codepoint boundaries. Composition
// remains separate from committed storage until commit, which makes cancel and
// host IME lifecycle loss deterministic.
class TextEditorState final {
public:
    TextEditorState() = default;
    explicit TextEditorState(std::string text);

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::uint64_t textRevision() const noexcept;

    [[nodiscard]] TextSelection selection() const noexcept;
    [[nodiscard]] std::size_t selectionBegin() const noexcept;
    [[nodiscard]] std::size_t selectionEnd() const noexcept;
    [[nodiscard]] bool hasSelection() const noexcept;
    [[nodiscard]] std::string selectedText() const;
    [[nodiscard]] bool setSelection(std::size_t anchor, std::size_t caret) noexcept;
    [[nodiscard]] bool setCaret(std::size_t caret, bool extend = false) noexcept;
    [[nodiscard]] bool selectAll() noexcept;
    [[nodiscard]] bool moveLeft(bool extend = false) noexcept;
    [[nodiscard]] bool moveRight(bool extend = false) noexcept;
    [[nodiscard]] bool moveLineStart(bool extend = false) noexcept;
    [[nodiscard]] bool moveLineEnd(bool extend = false) noexcept;

    [[nodiscard]] bool insert(std::string_view utf8);
    [[nodiscard]] bool insert(char32_t codepoint);
    [[nodiscard]] bool backspace();
    [[nodiscard]] bool deleteForward();

    [[nodiscard]] bool copy(TextClipboard& clipboard) const;
    [[nodiscard]] bool cut(TextClipboard& clipboard);
    [[nodiscard]] bool paste(TextClipboard& clipboard);

    void setMaximumUndoEntries(std::size_t entries);
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] bool beginComposition();
    [[nodiscard]] bool updateComposition(
        std::string_view utf8,
        std::size_t caret,
        std::size_t selectionLength = 0);
    [[nodiscard]] bool commitComposition(std::string_view utf8);
    [[nodiscard]] bool cancelComposition() noexcept;
    [[nodiscard]] const TextComposition& composition() const noexcept;
    [[nodiscard]] std::string displayText() const;
    [[nodiscard]] std::size_t displayCaret() const noexcept;
    [[nodiscard]] TextSelection displaySelection() const noexcept;
    [[nodiscard]] TextSelection compositionDisplayRange() const noexcept;

private:
    struct Snapshot final {
        std::string text;
        TextSelection selection{};
    };

    [[nodiscard]] bool replaceSelection(std::string_view utf8);
    [[nodiscard]] bool replaceRange(
        std::size_t begin,
        std::size_t end,
        std::string_view utf8,
        bool recordUndo);
    void pushUndo();
    void restore(Snapshot snapshot);
    void changed(bool textChanged) noexcept;

    std::string mText;
    TextSelection mSelection{};
    TextComposition mComposition{};
    std::vector<Snapshot> mUndo;
    std::vector<Snapshot> mRedo;
    std::size_t mMaximumUndoEntries = 128;
    std::uint64_t mRevision = 1;
    std::uint64_t mTextRevision = 1;
};

} // namespace henia::ui
