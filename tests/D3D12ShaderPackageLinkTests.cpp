#include "henia/backend/d3d12/D3D12ShaderPackage.h"
#include "henia/gfx/backend/d3d12/D3D12RenderDevice.h"
#include "henia/ui/backend/d3d12/D3D12Renderer.h"

#include <cstdlib>

#if defined(HENIAUI_EXPECT_RUNTIME_SHADER_COMPILATION)
constexpr bool kExpectedRuntimeShaderCompilation = true;
#else
constexpr bool kExpectedRuntimeShaderCompilation = false;
#endif

int main() {
    const auto ui = henia::backend::d3d12::shaderPackageInfo(
        henia::backend::d3d12::ShaderPackage::Ui);
    const auto gfx = henia::backend::d3d12::shaderPackageInfo(
        henia::backend::d3d12::ShaderPackage::Gfx);
    henia::ui::D3D12Renderer uiRenderer;
    henia::gfx::D3D12RenderDevice gfxRenderer;
    return ui.version.size() == 64 && gfx.version.size() == 64
            && ui.vertexBytes != 0 && ui.pixelBytes != 0
            && ui.pixelVariantBytes != 0
            && gfx.vertexBytes != 0 && gfx.pixelBytes != 0
            && ui.runtimeCompilationEnabled == kExpectedRuntimeShaderCompilation
            && gfx.runtimeCompilationEnabled == kExpectedRuntimeShaderCompilation
            && !uiRenderer.initialized()
            && !gfxRenderer.initialized()
        ? EXIT_SUCCESS : EXIT_FAILURE;
}
