#pragma once

#include <cstdint>
#include <limits>

namespace henia {

inline constexpr std::uint32_t kNoSubmissionSlot =
    std::numeric_limits<std::uint32_t>::max();

enum class InstanceUploadKind : std::uint8_t {
    None,
    Full,
    DirtyRanges,
    ZeroWorkRevision,
};

struct RenderSampleIdentity final {
    std::uint64_t sampleId = 0;
    std::uint64_t frameAttemptId = 0;
    // RenderPacket::identity/revision or InstanceBatch::identity/revision.
    std::uint64_t producerIdentity = 0;
    std::uint64_t producerRevision = 0;
    std::uint32_t submissionSlot = kNoSubmissionSlot;
};

struct RenderProfileSample final {
    RenderSampleIdentity identity{};
    std::uint64_t cpuProducerBuildNanoseconds = 0;
    std::uint64_t cpuUploadNanoseconds = 0;
    std::uint64_t cpuDrawSubmitNanoseconds = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint32_t uploadRangeCount = 0;
    InstanceUploadKind uploadKind = InstanceUploadKind::None;
    std::uint64_t gpuNanoseconds = 0;
    bool producerBuildRecorded = false;
    bool gpuTimingAvailable = false;
};

struct RenderProfileTotals final {
    std::uint64_t samples = 0;
    std::uint64_t producerBuilds = 0;
    std::uint64_t cpuProducerBuildNanoseconds = 0;
    std::uint64_t cpuUploadNanoseconds = 0;
    std::uint64_t cpuDrawSubmitNanoseconds = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint64_t uploadRanges = 0;
    std::uint64_t fullUploadSamples = 0;
    std::uint64_t dirtyUploadSamples = 0;
    std::uint64_t zeroWorkRevisionSamples = 0;
    std::uint64_t gpuSamples = 0;
    std::uint64_t gpuNanoseconds = 0;
};

struct RenderProfileRollingAverage final {
    std::uint32_t windowSamples = 0;
    std::uint32_t gpuWindowSamples = 0;
    std::uint64_t cpuProducerBuildNanoseconds = 0;
    std::uint64_t cpuUploadNanoseconds = 0;
    std::uint64_t cpuDrawSubmitNanoseconds = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint64_t gpuNanoseconds = 0;
};

struct RenderProfile final {
    // The most recent successful submission. GPU availability applies only to
    // this exact sample and returns to false on the next successful submission.
    RenderProfileSample latestSample{};
    // May identify an older sample when a timestamp resolves after later frames.
    RenderProfileSample latestGpuSample{};
    RenderProfileTotals cumulative{};
    // Arithmetic averages across at most the latest 60 successful samples.
    RenderProfileRollingAverage rollingAverage{};
};

} // namespace henia
