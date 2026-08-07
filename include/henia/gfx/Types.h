#pragma once

#include "henia/RenderProfile.h"

#include <array>
#include <bit>
#include <cstdint>

namespace henia::gfx {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;
    [[nodiscard]] constexpr bool operator==(const Vec2&) const noexcept = default;
};

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

struct LinearColor final {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
    [[nodiscard]] constexpr bool operator==(const LinearColor&) const noexcept = default;
};

struct Mat4 final {
    // Column-major clip transform: clip = viewProjection * vec4(position, 1).
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    [[nodiscard]] constexpr bool operator==(const Mat4&) const noexcept = default;
};

enum class ClipDepthRange : std::uint8_t {
    MinusOneToOne,
    ZeroToOne,
};

struct ViewParameters final {
    Mat4 viewProjection{};
    Vec2 viewport{};
    float timeSeconds = 0.0F;
    // Per-frame translation scale for BoxEffect::MotionTranslation. Keeping
    // this in the view avoids rebuilding immutable instance data every frame.
    float motionScale = 0.0F;
    ClipDepthRange clipDepthRange = ClipDepthRange::ZeroToOne;
};

enum class CompareOp : std::uint8_t {
    Never,
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
    Always,
};

struct DepthState final {
    bool enabled = false;
    bool writeEnabled = false;
    CompareOp compare = CompareOp::LessEqual;
    [[nodiscard]] constexpr bool operator==(const DepthState&) const noexcept = default;
};

enum class BoxEffect : std::uint32_t {
    None = 0,
    HueCycle = 1U << 0U,
    MotionTranslation = 1U << 1U,
    // Storage-format flags used by BoxInstance. They are public so serialized
    // instances and custom backends can decode the same 64-byte layout.
    PackedMotionTranslation = 1U << 2U,
    ExplicitVisibilityMask = 1U << 3U,
    Filled = 1U << 4U,
    OutlineDisabled = 1U << 5U,
};

[[nodiscard]] constexpr BoxEffect operator|(BoxEffect left, BoxEffect right) noexcept {
    return static_cast<BoxEffect>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

struct BoxInstance final {
    static constexpr std::uint32_t kPackedComponentBits = 21U;
    static constexpr std::uint32_t kPackedMantissaBits = 12U;
    static constexpr std::uint32_t kPackedComponentMask =
        (std::uint32_t{1} << kPackedComponentBits) - 1U;
    static constexpr std::uint32_t kFillOpacityShift = 8U;
    static constexpr std::uint32_t kFillOpacityMask = 0xFFU << kFillOpacityShift;

    Vec3 minimum{};
    float lineWidth = 1.5F;
    Vec3 maximum{1.0F, 1.0F, 1.0F};
    float hueOffset = 0.0F;
    LinearColor color{};
    BoxEffect effects = BoxEffect::None;
    std::array<std::uint32_t, 3> reserved{};

    // New instances keep a full 32-bit visibility mask in reserved[0] and
    // three compact, full-range floating-point deltas in reserved[1..2]. The
    // compact format retains sign/exponent and twelve mantissa bits. Legacy
    // MotionTranslation instances without PackedMotionTranslation continue to
    // decode their three original float words. This preserves the 64-byte GPU
    // layout while making visibility and motion independent.
    constexpr void setVisibilityMask(std::uint32_t mask) noexcept {
        if (hasEffect(BoxEffect::MotionTranslation)
            && !hasEffect(BoxEffect::PackedMotionTranslation)) {
            const Vec3 legacy = motionDelta();
            storePackedMotion(legacy);
            addEffect(BoxEffect::PackedMotionTranslation);
        }
        reserved[0] = mask;
        addEffect(BoxEffect::ExplicitVisibilityMask);
    }
    constexpr void clearVisibilityMask() noexcept {
        if (hasEffect(BoxEffect::MotionTranslation)
            && !hasEffect(BoxEffect::PackedMotionTranslation)) {
            const Vec3 legacy = motionDelta();
            storePackedMotion(legacy);
            addEffect(BoxEffect::PackedMotionTranslation);
        }
        reserved[0] = 0;
        removeEffect(BoxEffect::ExplicitVisibilityMask);
    }
    [[nodiscard]] constexpr std::uint32_t visibilityMask() const noexcept {
        return hasEffect(BoxEffect::ExplicitVisibilityMask)
            ? reserved[0] : ~std::uint32_t{0};
    }
    constexpr void setMotionDelta(Vec3 value) noexcept {
        storePackedMotion(value);
        addEffect(BoxEffect::MotionTranslation);
        addEffect(BoxEffect::PackedMotionTranslation);
    }
    constexpr void clearMotionDelta() noexcept {
        removeEffect(BoxEffect::MotionTranslation);
        removeEffect(BoxEffect::PackedMotionTranslation);
        reserved[1] = 0;
        reserved[2] = 0;
    }
    [[nodiscard]] constexpr Vec3 motionDelta() const noexcept {
        if (!hasEffect(BoxEffect::MotionTranslation)) {
            return {};
        }
        if (hasEffect(BoxEffect::PackedMotionTranslation)) return loadPackedMotion();
        return {
            std::bit_cast<float>(reserved[0]),
            std::bit_cast<float>(reserved[1]),
            std::bit_cast<float>(reserved[2]),
        };
    }
    constexpr void setFillOpacity(float opacity) noexcept {
        const float finiteOpacity = opacity == opacity ? opacity : 0.0F;
        const float clamped = finiteOpacity < 0.0F
            ? 0.0F : finiteOpacity > 1.0F ? 1.0F : finiteOpacity;
        const std::uint32_t quantized = static_cast<std::uint32_t>(clamped * 255.0F + 0.5F);
        std::uint32_t raw = static_cast<std::uint32_t>(effects) & ~kFillOpacityMask;
        raw |= quantized << kFillOpacityShift;
        if (quantized != 0U) raw |= static_cast<std::uint32_t>(BoxEffect::Filled);
        else raw &= ~static_cast<std::uint32_t>(BoxEffect::Filled);
        effects = static_cast<BoxEffect>(raw);
    }
    constexpr void clearFill() noexcept {
        effects = static_cast<BoxEffect>(static_cast<std::uint32_t>(effects)
            & ~(kFillOpacityMask | static_cast<std::uint32_t>(BoxEffect::Filled)));
    }
    [[nodiscard]] constexpr float fillOpacity() const noexcept {
        const std::uint32_t quantized =
            (static_cast<std::uint32_t>(effects) & kFillOpacityMask)
            >> kFillOpacityShift;
        return static_cast<float>(quantized) / 255.0F;
    }
    [[nodiscard]] constexpr bool fillEnabled() const noexcept {
        return hasEffect(BoxEffect::Filled);
    }
    constexpr void setOutlineEnabled(bool enabled) noexcept {
        if (enabled) removeEffect(BoxEffect::OutlineDisabled);
        else addEffect(BoxEffect::OutlineDisabled);
    }
    [[nodiscard]] constexpr bool outlineEnabled() const noexcept {
        return !hasEffect(BoxEffect::OutlineDisabled);
    }
    [[nodiscard]] constexpr bool operator==(const BoxInstance&) const noexcept = default;

private:
    [[nodiscard]] constexpr bool hasEffect(BoxEffect effect) const noexcept {
        return (static_cast<std::uint32_t>(effects)
            & static_cast<std::uint32_t>(effect)) != 0U;
    }
    constexpr void addEffect(BoxEffect effect) noexcept {
        effects = static_cast<BoxEffect>(static_cast<std::uint32_t>(effects)
            | static_cast<std::uint32_t>(effect));
    }
    constexpr void removeEffect(BoxEffect effect) noexcept {
        effects = static_cast<BoxEffect>(static_cast<std::uint32_t>(effects)
            & ~static_cast<std::uint32_t>(effect));
    }
    [[nodiscard]] static constexpr std::uint32_t packComponent(float value) noexcept {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        return ((bits >> 31U) << 20U)
            | (((bits >> 23U) & 0xFFU) << kPackedMantissaBits)
            | ((bits >> (23U - kPackedMantissaBits))
                & ((std::uint32_t{1} << kPackedMantissaBits) - 1U));
    }
    [[nodiscard]] static constexpr float unpackComponent(std::uint32_t value) noexcept {
        const std::uint32_t bits = ((value >> 20U) << 31U)
            | (((value >> kPackedMantissaBits) & 0xFFU) << 23U)
            | ((value & ((std::uint32_t{1} << kPackedMantissaBits) - 1U))
                << (23U - kPackedMantissaBits));
        return std::bit_cast<float>(bits);
    }
    constexpr void storePackedMotion(Vec3 value) noexcept {
        const std::uint64_t packed = static_cast<std::uint64_t>(packComponent(value.x))
            | (static_cast<std::uint64_t>(packComponent(value.y)) << kPackedComponentBits)
            | (static_cast<std::uint64_t>(packComponent(value.z))
                << (kPackedComponentBits * 2U));
        reserved[1] = static_cast<std::uint32_t>(packed);
        reserved[2] = static_cast<std::uint32_t>(packed >> 32U);
    }
    [[nodiscard]] constexpr Vec3 loadPackedMotion() const noexcept {
        const std::uint64_t packed = static_cast<std::uint64_t>(reserved[1])
            | (static_cast<std::uint64_t>(reserved[2]) << 32U);
        return {
            unpackComponent(static_cast<std::uint32_t>(packed) & kPackedComponentMask),
            unpackComponent(static_cast<std::uint32_t>(
                packed >> kPackedComponentBits) & kPackedComponentMask),
            unpackComponent(static_cast<std::uint32_t>(
                packed >> (kPackedComponentBits * 2U)) & kPackedComponentMask),
        };
    }
};

using RenderProfile = henia::RenderProfile;

} // namespace henia::gfx
