#include "henia/ui/BatchCompiler.h"
#include "henia/ui/Validation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace henia::ui {
namespace {

constexpr std::uint32_t kNoTextureSlot = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] constexpr std::uint8_t compactTextureSlot(std::uint32_t slot) noexcept {
    return slot == kNoTextureSlot ? 0xFFU : static_cast<std::uint8_t>(slot);
}

[[nodiscard]] DrawBatch makeBatch(
    ClipRect clip,
    BlendMode blend,
    std::uint32_t firstInstance) noexcept {
    DrawBatch batch{};
    batch.clip = clip;
    batch.blend = blend;
    batch.firstInstance = firstInstance;
    return batch;
}

} // namespace

void BatchCompiler::setFragmentAreaTracking(bool enabled) noexcept {
    mTrackFragmentArea = enabled;
}

bool BatchCompiler::fragmentAreaTracking() const noexcept { return mTrackFragmentArea; }

bool BatchCompiler::compile(
    const DisplayList& displayList,
    RenderPacketBuilder& output) const noexcept {
    if (!output.active()) {
        return false;
    }
    output.clear();
    output.setSourceCommands(displayList.size());

    for (const DrawCommand& command : displayList.commands()) {
        if (!validateDrawCommand(command).empty()) {
            return output.rejectPacket(true);
        }
        if (!commandOverlapsClip(command)) {
            continue;
        }
        if (command.kind == PrimitiveKind::StrokeRect) {
            std::array<PreparedCommand, 8> prepared{};
            const std::size_t preparedCount = prepareCommand(command, prepared);
            for (std::size_t index = 0; index < preparedCount; ++index) {
                if (!append(prepared[index], output, mTrackFragmentArea)) return false;
            }
            continue;
        }

        if (output.instanceCount() >= std::numeric_limits<std::uint32_t>::max()) {
            return output.rejectPacket();
        }
        DrawBatch* batch = output.lastBatch();
        if (batch == nullptr || !compatible(*batch, command)) {
            batch = output.appendBatch(makeBatch(
                command.clip,
                command.blend,
                static_cast<std::uint32_t>(output.instanceCount())));
            if (batch == nullptr) return output.rejectPacket();
        }
        std::uint32_t textureSlot = kNoTextureSlot;
        if (!resolveTextureSlot(*batch, command.texture, textureSlot)) {
            batch = output.appendBatch(makeBatch(
                command.clip,
                command.blend,
                static_cast<std::uint32_t>(output.instanceCount())));
            if (batch == nullptr || !resolveTextureSlot(*batch, command.texture, textureSlot)) {
                return output.rejectPacket();
            }
        }
        const DrawInstance instance = makeInstance(command, textureSlot);
        if (!output.appendInstance(instance)) return output.rejectPacket();
        if (mTrackFragmentArea) {
            output.addEstimatedFragmentArea(estimateFragmentArea(instance));
        }
        ++batch->instanceCount;
    }

    output.completePacket();
    return true;
}

bool BatchCompiler::compile(
    std::span<const DisplayListSegment> segments,
    RenderPacketBuilder& output) const noexcept {
    if (!output.active()) {
        return false;
    }
    output.clear();

    std::size_t sourceCommands = 0;
    for (const DisplayListSegment& segment : segments) {
        if (segment.commands.size() > std::numeric_limits<std::size_t>::max() - sourceCommands) {
            return output.rejectPacket();
        }
        sourceCommands += segment.commands.size();
    }
    output.setSourceCommands(sourceCommands);

    try {
        if (mPreparedSegments.size() < segments.size()) {
            mPreparedSegments.resize(segments.size());
        }
    } catch (...) {
        return output.rejectPacket();
    }

    for (std::size_t index = 0; index < segments.size(); ++index) {
        const DisplayListSegment& segment = segments[index];
        PreparedSegment& prepared = mPreparedSegments[index];
        if (prepared.identity != segment.identity || prepared.revision != segment.revision) {
            const PrepareResult result = prepare(segment, prepared);
            if (result != PrepareResult::Ready) {
                return output.rejectPacket(result == PrepareResult::InvalidInput);
            }
        }
        for (const PreparedCommand& command : prepared.commands) {
            if (!append(command, output, mTrackFragmentArea)) {
                return false;
            }
        }
    }

    output.completePacket();
    return true;
}

BatchCompiler::PrepareResult BatchCompiler::prepare(
    const DisplayListSegment& segment,
    PreparedSegment& output) const noexcept {
    output.identity = 0;
    output.revision = 0;
    output.commands.clear();
    try {
        output.commands.reserve(segment.commands.size());
        for (const DrawCommand& command : segment.commands) {
            if (!validateDrawCommand(command).empty()) {
                return PrepareResult::InvalidInput;
            }
            if (!commandOverlapsClip(command)) {
                continue;
            }
            if (command.kind != PrimitiveKind::StrokeRect) {
                output.commands.push_back({
                    .instance = makeInstance(command, kNoTextureSlot),
                    .clip = command.clip,
                    .blend = command.blend,
                    .texture = command.texture,
                });
            } else {
                std::array<PreparedCommand, 8> prepared{};
                const std::size_t preparedCount = prepareCommand(command, prepared);
                output.commands.insert(
                    output.commands.end(),
                    prepared.begin(),
                    prepared.begin() + static_cast<std::ptrdiff_t>(preparedCount));
            }
        }
    } catch (...) {
        output.commands.clear();
        return PrepareResult::OutOfMemory;
    }
    output.identity = segment.identity;
    output.revision = segment.revision;
    return PrepareResult::Ready;
}

std::size_t BatchCompiler::prepareCommand(
    const DrawCommand& command,
    std::span<PreparedCommand, 8> output) noexcept {
    const auto prepared = [&command](DrawInstance instance) noexcept {
        return PreparedCommand{
            .instance = instance,
            .clip = command.clip,
            .blend = command.blend,
            .texture = command.texture,
        };
    };

    DrawInstance instance = makeInstance(command, kNoTextureSlot);
    instance.uv = command.bounds;
    const Rect outer{
        {command.bounds.min.x - kAnalyticAaFringe, command.bounds.min.y - kAnalyticAaFringe},
        {command.bounds.max.x + kAnalyticAaFringe, command.bounds.max.y + kAnalyticAaFringe},
    };
    const float cornerMetric = std::max(command.radius, command.thickness) + kAnalyticAaFringe;
    const float cornerWidth = std::min(cornerMetric, outer.width() * 0.5F);
    const float cornerHeight = std::min(cornerMetric, outer.height() * 0.5F);
    const float topEdgeBottom = std::min(
        outer.max.y,
        command.bounds.min.y + command.thickness + kAnalyticAaFringe);
    const float bottomEdgeTop = std::max(
        outer.min.y,
        command.bounds.max.y - command.thickness - kAnalyticAaFringe);
    const float leftEdgeRight = std::min(
        outer.max.x,
        command.bounds.min.x + command.thickness + kAnalyticAaFringe);
    const float rightEdgeLeft = std::max(
        outer.min.x,
        command.bounds.max.x - command.thickness - kAnalyticAaFringe);

    const std::array regions{
        Rect{outer.min, {outer.min.x + cornerWidth, outer.min.y + cornerHeight}},
        Rect{{outer.max.x - cornerWidth, outer.min.y}, {outer.max.x, outer.min.y + cornerHeight}},
        Rect{{outer.min.x, outer.max.y - cornerHeight}, {outer.min.x + cornerWidth, outer.max.y}},
        Rect{{outer.max.x - cornerWidth, outer.max.y - cornerHeight}, outer.max},
        Rect{{outer.min.x + cornerWidth, outer.min.y}, {outer.max.x - cornerWidth, topEdgeBottom}},
        Rect{{outer.min.x + cornerWidth, bottomEdgeTop}, {outer.max.x - cornerWidth, outer.max.y}},
        Rect{{outer.min.x, outer.min.y + cornerHeight}, {leftEdgeRight, outer.max.y - cornerHeight}},
        Rect{{rightEdgeLeft, outer.min.y + cornerHeight}, {outer.max.x, outer.max.y - cornerHeight}},
    };

    std::size_t count = 0;
    for (const Rect region : regions) {
        if (!region.valid()
            || (command.clip.enabled && !intersect(region, command.clip.area).valid())) {
            continue;
        }
        instance.bounds = region;
        output[count++] = prepared(instance);
    }
    return count;
}

bool BatchCompiler::append(
    const PreparedCommand& command,
    RenderPacketBuilder& output,
    bool trackFragmentArea) noexcept {
    if (output.instanceCount() >= std::numeric_limits<std::uint32_t>::max()) {
        return output.rejectPacket();
    }
    DrawBatch* batch = output.lastBatch();
    if (batch == nullptr || !compatible(*batch, command)) {
        batch = output.appendBatch(makeBatch(
            command.clip,
            command.blend,
            static_cast<std::uint32_t>(output.instanceCount())));
        if (batch == nullptr) {
            return output.rejectPacket();
        }
    }

    std::uint32_t textureSlot = kNoTextureSlot;
    if (!resolveTextureSlot(*batch, command.texture, textureSlot)) {
        batch = output.appendBatch(makeBatch(
            command.clip,
            command.blend,
            static_cast<std::uint32_t>(output.instanceCount())));
        if (batch == nullptr) {
            return output.rejectPacket();
        }
        if (!resolveTextureSlot(*batch, command.texture, textureSlot)) {
            return output.rejectPacket();
        }
    }

    DrawInstance instance = command.instance;
    instance.textureSlot = compactTextureSlot(textureSlot);
    if (!output.appendInstance(instance)) {
        return output.rejectPacket();
    }
    if (trackFragmentArea) {
        output.addEstimatedFragmentArea(estimateFragmentArea(instance));
    }
    ++batch->instanceCount;
    return true;
}

bool BatchCompiler::compatible(
    const DrawBatch& batch,
    const DrawCommand& command) noexcept {
    return batch.clip == command.clip && batch.blend == command.blend;
}

bool BatchCompiler::compatible(
    const DrawBatch& batch,
    const PreparedCommand& command) noexcept {
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
    DrawInstance instance{
        .bounds = command.bounds,
        .uv = command.uv,
        .color = command.color,
        .radius = command.radius,
        .thickness = command.thickness,
        .kind = command.kind,
        .textureSlot = compactTextureSlot(textureSlot),
        .lineCap = command.lineCap,
    };
    instance.setLineStyle(command.lineJoin, command.lineFlags);
    return instance;
}

std::uint64_t BatchCompiler::estimateFragmentArea(const DrawInstance& instance) noexcept {
    double area = 0.0;
    if (instance.kind == PrimitiveKind::Line) {
        const double deltaX = static_cast<double>(instance.bounds.max.x - instance.bounds.min.x);
        const double deltaY = static_cast<double>(instance.bounds.max.y - instance.bounds.min.y);
        const double length = std::hypot(deltaX, deltaY);
        const double halfWidth = static_cast<double>(instance.thickness) * 0.5;
        const auto endpointExtension = [halfWidth](bool internal, LineCap cap, LineJoin join) {
            if (internal) {
                static_cast<void>(join);
                return halfWidth;
            }
            return cap == LineCap::Butt ? 0.0 : halfWidth;
        };
        const double start = endpointExtension(
            (instance.lineFlags() & kLineHasPrevious) != 0,
            instance.lineCap,
            instance.lineJoin());
        const double end = endpointExtension(
            (instance.lineFlags() & kLineHasNext) != 0,
            instance.lineCap,
            instance.lineJoin());
        area = (length + start + end + kAnalyticAaFringe * 2.0)
            * (static_cast<double>(instance.thickness) + kAnalyticAaFringe * 2.0);
    } else if (instance.kind == PrimitiveKind::SolidRect) {
        area = static_cast<double>(instance.bounds.width() + kAnalyticAaFringe * 2.0F)
            * (instance.bounds.height() + kAnalyticAaFringe * 2.0F);
    } else {
        area = static_cast<double>(instance.bounds.width()) * instance.bounds.height();
    }
    if (!std::isfinite(area) || area <= 0.0) {
        return 0;
    }
    if (area >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(std::ceil(area));
}

} // namespace henia::ui
