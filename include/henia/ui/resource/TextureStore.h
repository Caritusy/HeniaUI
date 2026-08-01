#pragma once

#include "henia/ui/ResourceHandle.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace henia::ui {

enum class TextureFormat : std::uint8_t {
    Alpha8,
    Rgba8,
};

struct TextureView final {
    TextureHandle handle{};
    TextureFormat format = TextureFormat::Rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    std::uint64_t revision = 0;
    std::span<const std::byte> pixels{};
};

class TextureStore final {
public:
    [[nodiscard]] TextureHandle create(
        TextureFormat format,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t rowPitch,
        std::span<const std::byte> pixels);
    [[nodiscard]] bool update(
        TextureHandle handle,
        std::uint32_t rowPitch,
        std::span<const std::byte> pixels);

    [[nodiscard]] TextureView view(TextureHandle handle) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry final {
        TextureFormat format = TextureFormat::Rgba8;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t rowPitch = 0;
        std::uint64_t revision = 1;
        std::vector<std::byte> pixels;
    };

    [[nodiscard]] static std::size_t bytesPerPixel(TextureFormat format) noexcept;
    [[nodiscard]] Entry* find(TextureHandle handle) noexcept;
    [[nodiscard]] const Entry* find(TextureHandle handle) const noexcept;

    std::vector<Entry> mEntries;
};

} // namespace henia::ui
