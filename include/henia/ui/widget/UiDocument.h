#pragma once

#include "henia/ui/Frame.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace henia::ui {

struct UiDocumentStatistics final {
    std::uint64_t layoutPasses = 0;
    std::uint64_t paintPasses = 0;
    std::uint64_t cachedFrames = 0;
    std::uint64_t inputEvents = 0;
    std::uint64_t revision = 0;
};

class UiDocument final {
public:
    explicit UiDocument(TextPainter& text, Theme theme = {});

    void reserve(std::size_t commandCapacity, std::size_t batchCapacity);
    void setRoot(std::unique_ptr<Widget> root) noexcept;
    [[nodiscard]] Widget* root() const noexcept;
    void setViewport(Vec2 viewport) noexcept;
    [[nodiscard]] Vec2 viewport() const noexcept;
    void setTheme(Theme theme) noexcept;
    [[nodiscard]] const Theme& theme() const noexcept;

    [[nodiscard]] const RenderPacket& compose();
    [[nodiscard]] bool dispatch(const InputEvent& event) noexcept;
    void clearInteraction() noexcept;

    [[nodiscard]] UiDocumentStatistics statistics() const noexcept;

private:
    void updateHover(Vec2 position) noexcept;
    void setFocus(Widget* widget) noexcept;

    TextPainter* mText = nullptr;
    Theme mTheme{};
    Frame mFrame;
    std::unique_ptr<Widget> mRoot;
    Widget* mHovered = nullptr;
    Widget* mCaptured = nullptr;
    Widget* mFocused = nullptr;
    Vec2 mViewport{};
    UiDocumentStatistics mStatistics{};
};

} // namespace henia::ui
