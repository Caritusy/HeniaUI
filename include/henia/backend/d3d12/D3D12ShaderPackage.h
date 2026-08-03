#pragma once

#include <cstddef>
#include <string_view>

namespace henia::backend::d3d12 {

enum class ShaderPackage {
    Ui,
    Gfx,
};

struct ShaderPackageInfo final {
    std::string_view version;
    std::size_t vertexBytes = 0;
    std::size_t pixelBytes = 0;
    std::size_t pixelVariantBytes = 0;
    bool runtimeCompilationEnabled = false;
};

[[nodiscard]] ShaderPackageInfo shaderPackageInfo(ShaderPackage package) noexcept;

} // namespace henia::backend::d3d12
