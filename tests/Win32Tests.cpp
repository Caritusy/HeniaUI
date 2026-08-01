#include "henia/ui/Frame.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/text/TextLayout.h"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace henia::ui;

    TextureStore textures;
    FontStore fonts;
    constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
    const FontHandle font = Win32FontLoader::load(
        textures,
        fonts,
        {.family = L"Segoe UI", .pixelHeight = 24, .atlasWidth = 512, .atlasHeight = 256, .ranges = ranges});
    if (!font.valid() || textures.size() != 1 || fonts.size() != 1) {
        std::cerr << "Win32 font atlas construction failed\n";
        return EXIT_FAILURE;
    }

    const TextureView atlas = textures.view(fonts.find(font)->atlas());
    if (atlas.format != TextureFormat::Alpha8 || atlas.width != 512 || atlas.height != 256
        || atlas.pixels.empty()) {
        std::cerr << "Win32 font atlas texture is invalid\n";
        return EXIT_FAILURE;
    }

    TextRunCache cache(fonts);
    cache.reserve(16, 64);
    TextPainter painter(cache);
    const TextMetrics metrics = painter.measure(font, 18.0F, "HeniaUI 0123456789");
    if (metrics.width <= 0.0F || metrics.height <= 0.0F) {
        std::cerr << "Win32 font text metrics are invalid\n";
        return EXIT_FAILURE;
    }

    Frame frame;
    frame.reserve(128, 8);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{0.0F, 0.0F}, {220.0F, 40.0F}}, {}, 6.0F);
    painter.draw(canvas, font, 18.0F, {8.0F, 8.0F}, {}, "HeniaUI 0123456789");
    const RenderPacket& packet = frame.finish();
    if (packet.batches().size() != 1 || packet.instances().size() <= 1) {
        std::cerr << "Win32 text did not merge with the UI batch\n";
        return EXIT_FAILURE;
    }

    std::cout << "HeniaUI Win32 font tests passed\n";
    return EXIT_SUCCESS;
}
