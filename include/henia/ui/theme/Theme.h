#pragma once

#include "henia/ui/Types.h"

#include <concepts>
#include <optional>
#include <utility>

namespace henia::ui {

template <typename Value>
class ThemeProperty final {
public:
    constexpr ThemeProperty() noexcept = default;
    constexpr ThemeProperty(std::nullopt_t) noexcept {}
    constexpr ThemeProperty(const Value& value) : mValue(value) {}
    constexpr ThemeProperty(Value&& value) : mValue(std::move(value)) {}

    template <typename... Arguments>
        requires std::constructible_from<Value, Arguments...>
    constexpr ThemeProperty(Arguments&&... arguments)
        : mValue(std::in_place, std::forward<Arguments>(arguments)...) {}

    [[nodiscard]] constexpr bool hasValue() const noexcept { return mValue.has_value(); }
    constexpr void reset() noexcept { mValue.reset(); }

    template <typename Fallback>
    [[nodiscard]] constexpr Value value_or(Fallback&& fallback) const {
        return mValue.value_or(std::forward<Fallback>(fallback));
    }

    friend constexpr bool operator==(
        const ThemeProperty&,
        const ThemeProperty&) noexcept = default;

private:
    std::optional<Value> mValue;
};

struct Theme final {
    Color canvas{0.018F, 0.027F, 0.043F, 1.0F};
    // Panels remain transparent by default for compatibility, but their
    // class defaults still participate in the document theme cascade.
    Color panelBackground{0.0F, 0.0F, 0.0F, 0.0F};
    Color panelBorder{0.0F, 0.0F, 0.0F, 0.0F};
    Color surface{0.032F, 0.047F, 0.071F, 1.0F};
    Color surfaceRaised{0.046F, 0.064F, 0.092F, 1.0F};
    Color surfaceHover{0.060F, 0.092F, 0.125F, 1.0F};
    Color surfacePressed{0.075F, 0.125F, 0.165F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    Color accent{0.10F, 0.72F, 0.91F, 1.0F};
    Color accentStrong{0.06F, 0.58F, 0.82F, 1.0F};
    Color textPrimary{0.90F, 0.95F, 0.98F, 1.0F};
    Color textMuted{0.48F, 0.59F, 0.67F, 1.0F};
    Color danger{0.93F, 0.31F, 0.36F, 1.0F};
    FontHandle font{};
    float fontSize = 14.0F;
    float cornerRadius = 8.0F;
    float borderWidth = 1.0F;
    float controlWidth = 176.0F;
    float controlHeight = 36.0F;
    float stepButtonWidth = 40.0F;
    float controlPaddingHorizontal = 14.0F;
    float controlPaddingVertical = 9.0F;
    float panelPadding = 0.0F;
    float panelGap = 0.0F;
    // Styling density only. Input/framebuffer transforms remain a host
    // integration contract rather than hidden document state.
    float scale = 1.0F;

    [[nodiscard]] constexpr bool layoutEquivalent(const Theme& other) const noexcept {
        return font == other.font
            && fontSize == other.fontSize
            && controlWidth == other.controlWidth
            && controlHeight == other.controlHeight
            && stepButtonWidth == other.stepButtonWidth
            && controlPaddingHorizontal == other.controlPaddingHorizontal
            && controlPaddingVertical == other.controlPaddingVertical
            && panelPadding == other.panelPadding
            && panelGap == other.panelGap
            && scale == other.scale;
    }

    friend constexpr bool operator==(const Theme&, const Theme&) noexcept = default;
};

} // namespace henia::ui
