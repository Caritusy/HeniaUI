#include "henia/ui/Frame.h"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>

int main() {
    using namespace henia::ui;

    Frame frame;
    frame.reserve(8192, 32);
    Canvas& canvas = frame.begin();

    canvas.fillRect({{24.0F, 24.0F}, {1000.0F, 680.0F}}, {0.025F, 0.035F, 0.055F, 0.98F}, 16.0F);
    canvas.strokeRect(
        {{24.0F, 24.0F}, {1000.0F, 680.0F}},
        {0.18F, 0.72F, 0.92F, 0.85F},
        16.0F,
        1.0F);

    std::array<GlyphQuad, 4096> glyphs{};
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        const float x = 48.0F + static_cast<float>(index % 96) * 9.0F;
        const float y = 70.0F + static_cast<float>(index / 96) * 13.0F;
        glyphs[index] = {{{x, y}, {x + 8.0F, y + 12.0F}}, {{0.0F, 0.0F}, {0.0625F, 0.0625F}}};
    }
    canvas.glyphs(TextureHandle{1}, glyphs, {0.91F, 0.95F, 0.98F, 1.0F});

    for (int row = 0; row < 80; ++row) {
        const float y = 80.0F + static_cast<float>(row) * 7.0F;
        canvas.fillRect(
            {{820.0F, y}, {960.0F, y + 4.0F}},
            {0.12F, 0.56F + static_cast<float>(row % 3) * 0.05F, 0.78F, 0.75F},
            2.0F);
    }

    const RenderPacket packet = frame.finish();
    const PacketStatistics& stats = packet.statistics();

    std::cout << "HeniaUI batching sandbox\n"
              << "  source commands : " << stats.sourceCommands << '\n'
              << "  GPU instances   : " << stats.instances << '\n'
              << "  draw batches    : " << stats.batches << '\n'
              << "  merged commands : " << stats.mergedCommands << '\n'
              << "  compression     : " << std::fixed << std::setprecision(2)
              << (stats.sourceCommands == 0
                      ? 0.0
                      : 100.0 * static_cast<double>(stats.mergedCommands)
                            / static_cast<double>(stats.sourceCommands))
              << "%\n";

    return stats.batches == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
}
