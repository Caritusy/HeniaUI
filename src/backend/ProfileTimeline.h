#pragma once

#include "henia/RenderProfile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace henia::detail {

struct RenderProfileInput final {
    std::uint64_t frameAttemptId = 0;
    std::uint64_t producerIdentity = 0;
    std::uint64_t producerRevision = 0;
    std::uint64_t producerBuildNanoseconds = 0;
    std::uint64_t cpuUploadNanoseconds = 0;
    std::uint64_t cpuDrawSubmitNanoseconds = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint32_t uploadRangeCount = 0;
    std::uint32_t submissionSlot = kNoSubmissionSlot;
    InstanceUploadKind uploadKind = InstanceUploadKind::None;
};

// Fixed-size, allocation-free association between successful submissions and
// timestamps that may resolve several frames later.
class ProfileTimeline final {
public:
    static constexpr std::size_t kHistoryCapacity = 64;
    static constexpr std::size_t kRollingWindow = 60;

    [[nodiscard]] std::uint64_t complete(RenderProfileInput input) noexcept {
        RenderProfileSample sample{};
        sample.identity = {
            .sampleId = nextSampleId(),
            .frameAttemptId = input.frameAttemptId,
            .producerIdentity = input.producerIdentity,
            .producerRevision = input.producerRevision,
            .submissionSlot = input.submissionSlot,
        };
        sample.cpuUploadNanoseconds = input.cpuUploadNanoseconds;
        sample.cpuDrawSubmitNanoseconds = input.cpuDrawSubmitNanoseconds;
        sample.uploadedInstanceBytes = input.uploadedInstanceBytes;
        sample.uploadRangeCount = input.uploadRangeCount;
        sample.uploadKind = normalizedUploadKind(input);
        if (input.producerBuildNanoseconds != 0
            && remember(mBuildRevisions, mBuildRevisionCursor,
                input.producerIdentity, input.producerRevision)) {
            sample.cpuProducerBuildNanoseconds = input.producerBuildNanoseconds;
            sample.producerBuildRecorded = true;
        }

        mHistory[mHistoryCursor] = sample;
        mHistoryCursor = (mHistoryCursor + 1U) % mHistory.size();
        mHistoryCount = std::min(mHistoryCount + 1U, mHistory.size());
        mProfile.latestSample = sample;
        addSampleToTotals(sample);
        recalculateRollingAverage();
        return sample.identity.sampleId;
    }

    [[nodiscard]] bool reportGpuTime(
        std::uint64_t sampleId,
        std::uint64_t nanoseconds) noexcept {
        if (sampleId == 0) return false;
        RenderProfileSample* sample = find(sampleId);
        if (sample == nullptr || sample->gpuTimingAvailable) return false;
        sample->gpuNanoseconds = nanoseconds;
        sample->gpuTimingAvailable = true;
        if (mProfile.latestSample.identity.sampleId == sampleId) {
            mProfile.latestSample = *sample;
        }
        mProfile.latestGpuSample = *sample;
        ++mProfile.cumulative.gpuSamples;
        addSaturated(nanoseconds, mProfile.cumulative.gpuNanoseconds);
        recalculateRollingAverage();
        return true;
    }

    void reset() noexcept {
        mHistory = {};
        mBuildRevisions = {};
        mZeroWorkRevisions = {};
        mHistoryCursor = 0;
        mHistoryCount = 0;
        mBuildRevisionCursor = 0;
        mZeroWorkRevisionCursor = 0;
        mProfile = {};
    }

    [[nodiscard]] const RenderProfile& profile() const noexcept { return mProfile; }

private:
    struct RevisionKey final {
        std::uint64_t identity = 0;
        std::uint64_t revision = 0;
    };

    [[nodiscard]] std::uint64_t nextSampleId() noexcept {
        const std::uint64_t result = mNextSampleId++;
        if (result != 0) return result;
        return mNextSampleId++;
    }

    template <std::size_t Size>
    [[nodiscard]] static bool remember(
        std::array<RevisionKey, Size>& keys,
        std::size_t& cursor,
        std::uint64_t identity,
        std::uint64_t revision) noexcept {
        if (identity == 0 || revision == 0) return false;
        for (const RevisionKey& key : keys) {
            if (key.identity == identity && key.revision == revision) return false;
        }
        keys[cursor] = {identity, revision};
        cursor = (cursor + 1U) % keys.size();
        return true;
    }

    [[nodiscard]] InstanceUploadKind normalizedUploadKind(
        const RenderProfileInput& input) noexcept {
        if (input.uploadKind != InstanceUploadKind::ZeroWorkRevision) {
            return input.uploadKind;
        }
        return remember(mZeroWorkRevisions, mZeroWorkRevisionCursor,
                   input.producerIdentity, input.producerRevision)
            ? InstanceUploadKind::ZeroWorkRevision
            : InstanceUploadKind::None;
    }

    static void addSaturated(std::uint64_t value, std::uint64_t& total) noexcept {
        const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        total = value > maximum - total ? maximum : total + value;
    }

    void addSampleToTotals(const RenderProfileSample& sample) noexcept {
        RenderProfileTotals& totals = mProfile.cumulative;
        ++totals.samples;
        if (sample.producerBuildRecorded) ++totals.producerBuilds;
        addSaturated(sample.cpuProducerBuildNanoseconds, totals.cpuProducerBuildNanoseconds);
        addSaturated(sample.cpuUploadNanoseconds, totals.cpuUploadNanoseconds);
        addSaturated(sample.cpuDrawSubmitNanoseconds, totals.cpuDrawSubmitNanoseconds);
        addSaturated(sample.uploadedInstanceBytes, totals.uploadedInstanceBytes);
        addSaturated(sample.uploadRangeCount, totals.uploadRanges);
        if (sample.uploadKind == InstanceUploadKind::Full) ++totals.fullUploadSamples;
        if (sample.uploadKind == InstanceUploadKind::DirtyRanges) ++totals.dirtyUploadSamples;
        if (sample.uploadKind == InstanceUploadKind::ZeroWorkRevision) {
            ++totals.zeroWorkRevisionSamples;
        }
    }

    [[nodiscard]] RenderProfileSample* find(std::uint64_t sampleId) noexcept {
        for (std::size_t offset = 0; offset < mHistoryCount; ++offset) {
            const std::size_t index = (mHistoryCursor + mHistory.size() - 1U - offset)
                % mHistory.size();
            if (mHistory[index].identity.sampleId == sampleId) return &mHistory[index];
        }
        return nullptr;
    }

    void recalculateRollingAverage() noexcept {
        const std::size_t count = std::min(mHistoryCount, kRollingWindow);
        std::uint64_t producerBuild = 0;
        std::uint64_t upload = 0;
        std::uint64_t drawSubmit = 0;
        std::uint64_t uploadedBytes = 0;
        std::uint64_t gpu = 0;
        std::uint32_t gpuCount = 0;
        for (std::size_t offset = 0; offset < count; ++offset) {
            const std::size_t index = (mHistoryCursor + mHistory.size() - 1U - offset)
                % mHistory.size();
            const RenderProfileSample& sample = mHistory[index];
            addSaturated(sample.cpuProducerBuildNanoseconds, producerBuild);
            addSaturated(sample.cpuUploadNanoseconds, upload);
            addSaturated(sample.cpuDrawSubmitNanoseconds, drawSubmit);
            addSaturated(sample.uploadedInstanceBytes, uploadedBytes);
            if (sample.gpuTimingAvailable) {
                addSaturated(sample.gpuNanoseconds, gpu);
                ++gpuCount;
            }
        }
        RenderProfileRollingAverage& average = mProfile.rollingAverage;
        average = {};
        average.windowSamples = static_cast<std::uint32_t>(count);
        average.gpuWindowSamples = gpuCount;
        if (count != 0) {
            average.cpuProducerBuildNanoseconds = producerBuild / count;
            average.cpuUploadNanoseconds = upload / count;
            average.cpuDrawSubmitNanoseconds = drawSubmit / count;
            average.uploadedInstanceBytes = uploadedBytes / count;
        }
        if (gpuCount != 0) average.gpuNanoseconds = gpu / gpuCount;
    }

    std::array<RenderProfileSample, kHistoryCapacity> mHistory{};
    std::array<RevisionKey, kHistoryCapacity> mBuildRevisions{};
    std::array<RevisionKey, kHistoryCapacity> mZeroWorkRevisions{};
    std::size_t mHistoryCursor = 0;
    std::size_t mHistoryCount = 0;
    std::size_t mBuildRevisionCursor = 0;
    std::size_t mZeroWorkRevisionCursor = 0;
    std::uint64_t mNextSampleId = 1;
    RenderProfile mProfile{};
};

} // namespace henia::detail
