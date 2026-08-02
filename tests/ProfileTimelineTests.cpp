#include "ProfileTimeline.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using henia::InstanceUploadKind;
using henia::detail::ProfileTimeline;
using henia::detail::RenderProfileInput;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) fail(message);
}

void stableRevisionIsBuiltOnce() {
    ProfileTimeline timeline;
    std::uint64_t firstSampleId = 0;
    for (std::uint64_t frame = 1; frame <= 100; ++frame) {
        const std::uint64_t sampleId = timeline.complete({
            .frameAttemptId = frame,
            .producerIdentity = 7,
            .producerRevision = 11,
            .producerBuildNanoseconds = 500,
            .cpuUploadNanoseconds = frame == 1 ? 20U : 0U,
            .cpuDrawSubmitNanoseconds = 30,
            .uploadedInstanceBytes = frame == 1 ? 4096U : 0U,
            .uploadRangeCount = frame == 1 ? 1U : 0U,
            .submissionSlot = static_cast<std::uint32_t>((frame - 1U) % 3U),
            .uploadKind = frame == 1
                ? InstanceUploadKind::Full
                : InstanceUploadKind::None,
        });
        if (frame == 1) firstSampleId = sampleId;
    }
    const henia::RenderProfile& profile = timeline.profile();
    require(profile.cumulative.samples == 100, "stable sample total is incorrect");
    require(profile.cumulative.producerBuilds == 1,
        "stable revision was reported as multiple producer builds");
    require(profile.cumulative.cpuProducerBuildNanoseconds == 500,
        "stable revision repeated its producer build duration");
    require(!profile.latestSample.producerBuildRecorded
            && profile.latestSample.cpuProducerBuildNanoseconds == 0,
        "latest stable sample retained stale producer build work");
    require(profile.cumulative.fullUploadSamples == 1
            && profile.cumulative.uploadedInstanceBytes == 4096,
        "stable revision upload totals are incorrect");
    require(profile.rollingAverage.windowSamples == ProfileTimeline::kRollingWindow,
        "rolling window did not clamp to its documented size");
    require(firstSampleId != 0, "first sample ID is invalid");
}

void delayedGpuSamplesRemainAssociated() {
    ProfileTimeline timeline;
    const std::uint64_t first = timeline.complete({
        .frameAttemptId = 102,
        .producerIdentity = 21,
        .producerRevision = 4,
        .producerBuildNanoseconds = 40,
        .cpuUploadNanoseconds = 10,
        .cpuDrawSubmitNanoseconds = 20,
        .uploadedInstanceBytes = 640,
        .uploadRangeCount = 1,
        .submissionSlot = 0,
        .uploadKind = InstanceUploadKind::Full,
    });
    const std::uint64_t second = timeline.complete({
        .frameAttemptId = 104,
        .producerIdentity = 21,
        .producerRevision = 5,
        .producerBuildNanoseconds = 50,
        .cpuUploadNanoseconds = 11,
        .cpuDrawSubmitNanoseconds = 21,
        .uploadedInstanceBytes = 64,
        .uploadRangeCount = 1,
        .submissionSlot = 0,
        .uploadKind = InstanceUploadKind::DirtyRanges,
    });
    require(timeline.reportGpuTime(first, 1000), "delayed GPU sample was rejected");
    const henia::RenderProfile& delayed = timeline.profile();
    require(delayed.latestSample.identity.sampleId == second
            && delayed.latestSample.identity.frameAttemptId == 104
            && delayed.latestSample.identity.producerRevision == 5
            && delayed.latestSample.identity.submissionSlot == 0,
        "delayed timestamp replaced the current CPU sample");
    require(!delayed.latestSample.gpuTimingAvailable,
        "old GPU duration leaked into the current sample");
    require(delayed.latestGpuSample.identity.sampleId == first
            && delayed.latestGpuSample.identity.frameAttemptId == 102
            && delayed.latestGpuSample.identity.producerRevision == 4
            && delayed.latestGpuSample.identity.submissionSlot == 0
            && delayed.latestGpuSample.gpuNanoseconds == 1000,
        "delayed timestamp lost its frame/revision/slot association");
    require(!timeline.reportGpuTime(first, 2000), "duplicate GPU sample was accepted");
    require(!timeline.reportGpuTime(999999, 2000), "unknown GPU sample was accepted");
    require(timeline.reportGpuTime(second, 3000), "current GPU sample was rejected");
    require(timeline.profile().latestSample.gpuTimingAvailable
            && timeline.profile().latestSample.gpuNanoseconds == 3000
            && timeline.profile().cumulative.gpuSamples == 2
            && timeline.profile().rollingAverage.gpuWindowSamples == 2
            && timeline.profile().rollingAverage.gpuNanoseconds == 2000,
        "GPU totals or rolling average are incorrect");
}

void staleAndZeroWorkSamplesAreExplicit() {
    ProfileTimeline timeline;
    const std::uint64_t stale = timeline.complete({
        .frameAttemptId = 1,
        .producerIdentity = 4,
        .producerRevision = 1,
        .uploadKind = InstanceUploadKind::ZeroWorkRevision,
    });
    static_cast<void>(timeline.complete({
        .frameAttemptId = 2,
        .producerIdentity = 4,
        .producerRevision = 1,
        .uploadKind = InstanceUploadKind::ZeroWorkRevision,
    }));
    require(timeline.profile().cumulative.zeroWorkRevisionSamples == 1,
        "stable empty revision repeated zero-work upload accounting");
    for (std::uint64_t frame = 3;
         frame < 3 + ProfileTimeline::kHistoryCapacity;
         ++frame) {
        static_cast<void>(timeline.complete({
            .frameAttemptId = frame,
            .producerIdentity = frame,
            .producerRevision = 1,
        }));
    }
    require(!timeline.reportGpuTime(stale, 10),
        "timestamp older than the fixed history window was accepted");

    const std::uint64_t beforeReset = timeline.profile().latestSample.identity.sampleId;
    timeline.reset();
    require(!timeline.reportGpuTime(beforeReset, 20),
        "timestamp from a reset renderer lifetime was accepted");
    const std::uint64_t afterReset = timeline.complete({.frameAttemptId = 1});
    require(afterReset > beforeReset, "sample IDs were reused after reset");
}

} // namespace

int main() {
    stableRevisionIsBuiltOnce();
    delayedGpuSamplesRemainAssociated();
    staleAndZeroWorkSamplesAreExplicit();
    std::cout << "HeniaUI profile timeline tests passed\n";
    return 0;
}
