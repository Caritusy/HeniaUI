#include "henia/backend/d3d12/D3D12ShaderPackage.h"

#include "GfxShaders.generated.h"
#include "UiShaders.generated.h"

namespace henia::backend::d3d12 {

ShaderPackageInfo shaderPackageInfo(ShaderPackage package) noexcept {
#if defined(HENIAUI_D3D12_RUNTIME_SHADER_COMPILATION)
    constexpr bool runtimeCompilation = true;
#else
    constexpr bool runtimeCompilation = false;
#endif
    if (package == ShaderPackage::Ui) {
        return {
            .version = generated::ui::kVersion,
            .vertexBytes = sizeof(generated::ui::kVertexShader),
            .pixelBytes = sizeof(generated::ui::kPixelShader),
            .pixelVariantBytes = sizeof(generated::ui::kPixelVariantShader),
            .runtimeCompilationEnabled = runtimeCompilation,
        };
    }
    return {
        .version = generated::gfx::kVersion,
        .vertexBytes = sizeof(generated::gfx::kVertexShader),
        .pixelBytes = sizeof(generated::gfx::kPixelShader),
        .runtimeCompilationEnabled = runtimeCompilation,
    };
}

} // namespace henia::backend::d3d12
