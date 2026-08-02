#include "henia/ui/BatchCompiler.h"

#include <algorithm>
#include <limits>

namespace henia::ui {
namespace {

constexpr std::uint32_t kNoTextureSlot = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] DrawBatch makeBatch(const DrawCommand& command, std::uint32_t firstInstance) noexcept {
    DrawBatch batch{};
    batch.clip = command.clip;
    batch.blend = command.blend;
    batch.firstInstance = firstInstance;
    return batch;
}

} // namespace

bool BatchCompiler::compile(
    const DisplayList& displayList,
    RenderPacketBuilder& output) const noexcept {
    if (!output.active()) {
        return false;
    }
    output.clear();
    output.setSourceCommands(displayList.size());

    for (const DrawCommand& command : displayList.commands()) {
        DrawBatch* batch = output.lastBatch();
        if (batch == nullptr || !compatible(*batch, command)) {
            batch = output.appendBatch(makeBatch(
                command,
                static_cast<std::uint32_t>(output.instanceCount())));
            if (batch == nullptr) {
                return output.rejectPacket();
            }
        }

        std::uint32_t textureSlot = kNoTextureSlot;
        if (!resolveTextureSlot(*batch, command.texture, textureSlot)) {
            batch = output.appendBatch(makeBatch(
                command,
                static_cast<std::uint32_t>(output.instanceCount())));
            if (batch == nullptr) {
                return output.rejectPacket();
            }
            const bool resolved = resolveTextureSlot(*batch, command.texture, textureSlot);
            if (!resolved) {
                return output.rejectPacket();
            }
        }

        if (!output.appendInstance(makeInstance(command, textureSlot))) {
            return output.rejectPacket();
        }
        ++batch->instanceCount;
    }

    output.completePacket();
    return true;
}

bool BatchCompiler::compatible(const DrawBatch& batch, const DrawCommand& command) noexcept {
    return batch.clip == command.clip && batch.blend == command.blend;
}

bool BatchCompiler::resolveTextureSlot(
    DrawBatch& batch,
    TextureHandle texture,
    std::uint32_t& slot) noexcept {
    if (!texture.valid()) {
        slot = kNoTextureSlot;
        return true;
    }

    const auto begin = batch.textures.begin();
    const auto end = begin + batch.textureCount;
    const auto iterator = std::find(begin, end, texture);
    if (iterator != end) {
        slot = static_cast<std::uint32_t>(iterator - begin);
        return true;
    }
    if (batch.textureCount == batch.textures.size()) {
        return false;
    }

    slot = batch.textureCount;
    batch.textures[batch.textureCount++] = texture;
    return true;
}

DrawInstance BatchCompiler::makeInstance(
    const DrawCommand& command,
    std::uint32_t textureSlot) noexcept {
    return {
        .bounds = command.bounds,
        .uv = command.uv,
        .pointA = command.pointA,
        .pointB = command.pointB,
        .color = command.color,
        .radius = command.radius,
        .thickness = command.thickness,
        .textureSlot = textureSlot,
        .kind = command.kind,
    };
}

} // namespace henia::ui
