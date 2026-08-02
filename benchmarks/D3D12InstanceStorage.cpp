#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/backend/d3d12/D3D12RenderDevice.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
using henia::backend::d3d12::InstanceStorageStrategy;
using namespace henia::gfx;

struct Options final {
    std::uint32_t iterations = 15;
    std::uint32_t warmup = 5;
};

struct Scenario final {
    const char* name = nullptr;
    std::size_t instances = 0;
    bool dynamic = false;
};

struct Result final {
    std::string adapter;
    std::string scenario;
    const char* strategy = nullptr;
    bool architectureKnown = false;
    bool uma = true;
    double medianGpuMicroseconds = 0.0;
    double medianRecordMicroseconds = 0.0;
    std::uint64_t copiedBytes = 0;
    std::uint64_t uploadHeapReadBytes = 0;
    std::uint64_t residentBytes = 0;
    std::uint64_t gpuLocalFrames = 0;
    std::uint64_t directFrames = 0;
};

[[noreturn]] void fail(std::string_view message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

[[nodiscard]] bool parseUnsigned(std::string_view value, std::uint32_t& result) noexcept {
    const char* first = value.data();
    const char* last = first + value.size();
    const auto parsed = std::from_chars(first, last, result);
    return parsed.ec == std::errc{} && parsed.ptr == last && result != 0;
}

[[nodiscard]] Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if ((argument == "--iterations" || argument == "--warmup") && index + 1 < argc) {
            std::uint32_t value = 0;
            if (!parseUnsigned(argv[++index], value)) fail("invalid benchmark iteration count");
            if (argument == "--iterations") options.iterations = value;
            else options.warmup = value;
        } else {
            fail("usage: HeniaUID3D12InstanceBenchmarks [--iterations N] [--warmup N]");
        }
    }
    return options;
}

[[nodiscard]] D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

[[nodiscard]] D3D12_RESOURCE_DESC bufferDescription(std::uint64_t bytes) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

[[nodiscard]] std::string adapterName(const DXGI_ADAPTER_DESC1& description) {
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, description.Description, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return "unknown adapter";
    std::string result(static_cast<std::size_t>(length), '\0');
    static_cast<void>(WideCharToMultiByte(
        CP_UTF8,
        0,
        description.Description,
        -1,
        result.data(),
        length,
        nullptr,
        nullptr));
    result.pop_back();
    return result;
}

[[nodiscard]] double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    return values.size() % 2U == 0
        ? (values[middle - 1U] + values[middle]) * 0.5
        : values[middle];
}

[[nodiscard]] const char* strategyName(InstanceStorageStrategy strategy) noexcept {
    switch (strategy) {
    case InstanceStorageStrategy::Automatic: return "automatic";
    case InstanceStorageStrategy::DirectUpload: return "direct-upload";
    case InstanceStorageStrategy::GpuLocal: return "gpu-local";
    }
    return "invalid";
}

class AdapterBenchmark final {
public:
    AdapterBenchmark(ComPtr<IDXGIAdapter1> adapter, std::string name)
        : mAdapter(std::move(adapter)), mName(std::move(name)) {
        if (FAILED(D3D12CreateDevice(
                mAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)))) {
            fail("failed to create benchmark D3D12 device");
        }
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(mDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&mQueue)))
            || FAILED(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)))) {
            fail("failed to create benchmark queue/fence");
        }
        mFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (mFenceEvent == nullptr || FAILED(mQueue->GetTimestampFrequency(&mTimestampFrequency))) {
            fail("failed to initialize benchmark synchronization");
        }
    }

    ~AdapterBenchmark() {
        if (mFenceEvent != nullptr) CloseHandle(mFenceEvent);
    }

    AdapterBenchmark(const AdapterBenchmark&) = delete;
    AdapterBenchmark& operator=(const AdapterBenchmark&) = delete;

    [[nodiscard]] Result run(
        const Scenario& scenario,
        InstanceStorageStrategy strategy,
        const Options& options) {
        ShapeBatch3D builder;
        std::vector<BoxInstance> boxes(scenario.instances);
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            const float offset = static_cast<float>(index % 7U) * 0.001F;
            boxes[index] = {
                .minimum = {2.0F + offset, 2.0F, 2.0F},
                .lineWidth = 1.0F,
                .maximum = {3.0F + offset, 3.0F, 3.0F},
                .color = {0.8F, 0.2F, 0.1F, 1.0F},
            };
        }
        builder.replaceBoxes(boxes);
        InstanceBatch batch = builder.snapshot();

        D3D12RenderDevice renderer;
        if (!renderer.initialize(*mDevice.Get(), {
                .boxCapacity = scenario.instances,
                .submissionCapacity = 1,
                .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
                .instanceStorage = strategy,
            })) {
            fail(renderer.lastError());
        }

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        ComPtr<ID3D12QueryHeap> queryHeap;
        ComPtr<ID3D12Resource> queryReadback;
        if (FAILED(mDevice->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
            || FAILED(mDevice->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&commandList)))) {
            fail("failed to create benchmark command recording objects");
        }
        D3D12_QUERY_HEAP_DESC queryDescription{};
        queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDescription.Count = 2;
        const D3D12_HEAP_PROPERTIES readbackHeap = heapProperties(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC readbackDescription = bufferDescription(2U * sizeof(std::uint64_t));
        if (FAILED(mDevice->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&queryHeap)))
            || FAILED(mDevice->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDescription,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&queryReadback)))
            || FAILED(commandList->Close())) {
            fail("failed to create benchmark timestamp resources");
        }

        const ViewParameters view{.viewport = {64.0F, 64.0F}};
        const auto recordFrame = [&](bool mutate) {
            if (mutate) {
                boxes[0].hueOffset += 1.0F;
                builder.replaceBoxes(boxes);
                batch = builder.snapshot();
            }
            if (FAILED(allocator->Reset())
                || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
                fail("failed to reset benchmark command recording objects");
            }
            commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            const auto started = std::chrono::steady_clock::now();
            if (!renderer.record(batch, view, *commandList.Get(), 0)) fail(renderer.lastError());
            const double recordMicroseconds = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count()) / 1000.0;
            commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            commandList->ResolveQueryData(
                queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, queryReadback.Get(), 0);
            if (FAILED(commandList->Close())) fail("failed to close benchmark command list");
            ID3D12CommandList* lists[]{commandList.Get()};
            mQueue->ExecuteCommandLists(1, lists);
            wait();

            const D3D12_RANGE readRange{0, 2U * sizeof(std::uint64_t)};
            const std::uint64_t* timestamps = nullptr;
            if (FAILED(queryReadback->Map(
                    0,
                    &readRange,
                    reinterpret_cast<void**>(const_cast<std::uint64_t**>(&timestamps))))
                || timestamps == nullptr) {
                fail("failed to read benchmark timestamps");
            }
            const std::uint64_t elapsed = timestamps[1] - timestamps[0];
            queryReadback->Unmap(0, nullptr);
            const double gpuMicroseconds = static_cast<double>(elapsed) * 1'000'000.0
                / static_cast<double>(mTimestampFrequency);
            return std::array{gpuMicroseconds, recordMicroseconds};
        };

        for (std::uint32_t index = 0; index < options.warmup; ++index) {
            static_cast<void>(recordFrame(scenario.dynamic));
        }
        const D3D12GfxStatistics before = renderer.statistics();
        std::vector<double> gpuSamples;
        std::vector<double> recordSamples;
        gpuSamples.reserve(options.iterations);
        recordSamples.reserve(options.iterations);
        for (std::uint32_t index = 0; index < options.iterations; ++index) {
            const auto sample = recordFrame(scenario.dynamic);
            gpuSamples.push_back(sample[0]);
            recordSamples.push_back(sample[1]);
        }
        const D3D12GfxStatistics after = renderer.statistics();
        const std::uint64_t gpuLocalFrames = after.gpuLocalFrames - before.gpuLocalFrames;
        const std::uint64_t directFrames = after.directUploadFrames - before.directUploadFrames;
        const std::uint64_t copiedBytes = after.copiedInstanceBytes - before.copiedInstanceBytes;
        const std::uint64_t uploadHeapReadBytes =
            after.uploadHeapReadBytes - before.uploadHeapReadBytes;
        const std::uint64_t bytesPerFrame = scenario.instances * sizeof(BoxInstance);
        bool expectedGpuLocal = strategy == InstanceStorageStrategy::GpuLocal;
        if (strategy == InstanceStorageStrategy::Automatic) {
            expectedGpuLocal = after.adapterArchitectureKnown && !after.adapterUma
                && bytesPerFrame
                    >= henia::backend::d3d12::kDefaultGpuLocalInstanceThresholdBytes;
        }
        const std::uint64_t expectedCopiedBytes = expectedGpuLocal && scenario.dynamic
            ? bytesPerFrame * options.iterations
            : 0;
        const std::uint64_t expectedUploadHeapReads = expectedGpuLocal
            ? 0
            : bytesPerFrame * options.iterations;
        if (gpuLocalFrames != (expectedGpuLocal ? options.iterations : 0U)
            || directFrames != (expectedGpuLocal ? 0U : options.iterations)
            || copiedBytes != expectedCopiedBytes
            || uploadHeapReadBytes != expectedUploadHeapReads) {
            fail("instance-storage benchmark observed an incorrect strategy or byte count");
        }
        return {
            .adapter = mName,
            .scenario = scenario.name,
            .strategy = strategyName(strategy),
            .architectureKnown = after.adapterArchitectureKnown,
            .uma = after.adapterUma,
            .medianGpuMicroseconds = median(std::move(gpuSamples)),
            .medianRecordMicroseconds = median(std::move(recordSamples)),
            .copiedBytes = copiedBytes,
            .uploadHeapReadBytes = uploadHeapReadBytes,
            .residentBytes = after.gpuLocalResidentBytes,
            .gpuLocalFrames = gpuLocalFrames,
            .directFrames = directFrames,
        };
    }

private:
    void wait() {
        const std::uint64_t value = ++mFenceValue;
        if (FAILED(mQueue->Signal(mFence.Get(), value))
            || FAILED(mFence->SetEventOnCompletion(value, mFenceEvent))
            || WaitForSingleObject(mFenceEvent, 30000) != WAIT_OBJECT_0) {
            fail("benchmark queue wait failed");
        }
    }

    ComPtr<IDXGIAdapter1> mAdapter;
    std::string mName;
    ComPtr<ID3D12Device> mDevice;
    ComPtr<ID3D12CommandQueue> mQueue;
    ComPtr<ID3D12Fence> mFence;
    HANDLE mFenceEvent = nullptr;
    std::uint64_t mFenceValue = 0;
    std::uint64_t mTimestampFrequency = 0;
};

void printResult(const Result& result) {
    std::cout << std::left << std::setw(31) << result.adapter.substr(0, 30)
              << std::setw(14) << (result.architectureKnown
                      ? (result.uma ? "UMA" : "discrete")
                      : "unknown")
              << std::setw(15) << result.scenario
              << std::setw(15) << result.strategy
              << std::right << std::fixed << std::setprecision(2)
              << std::setw(11) << result.medianGpuMicroseconds
              << std::setw(12) << result.medianRecordMicroseconds
              << std::setw(14) << result.copiedBytes
              << std::setw(14) << result.uploadHeapReadBytes
              << std::setw(14) << result.residentBytes
              << std::setw(8) << result.gpuLocalFrames
              << std::setw(8) << result.directFrames << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        fail("failed to create DXGI factory");
    }

    constexpr std::array scenarios{
        Scenario{"small-static", 64, false},
        Scenario{"small-dynamic", 64, true},
        Scenario{"large-static", 32768, false},
        Scenario{"large-dynamic", 32768, true},
    };
    constexpr std::array strategies{
        InstanceStorageStrategy::DirectUpload,
        InstanceStorageStrategy::GpuLocal,
        InstanceStorageStrategy::Automatic,
    };

    std::cout << "HeniaUI D3D12 instance-storage benchmark (GPU timestamps)\n"
              << "iterations=" << options.iterations << ", warmup=" << options.warmup << "\n"
              << std::left << std::setw(31) << "adapter"
              << std::setw(14) << "architecture"
              << std::setw(15) << "workload"
              << std::setw(15) << "strategy"
              << std::right << std::setw(11) << "GPU us"
              << std::setw(12) << "record us"
              << std::setw(14) << "copied B"
              << std::setw(14) << "upload-read B"
              << std::setw(14) << "resident B"
              << std::setw(8) << "local"
              << std::setw(8) << "direct" << '\n';

    std::uint32_t testedAdapters = 0;
    std::vector<std::uint64_t> testedHardwareIds;
    for (std::uint32_t index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapterByGpuPreference(
                index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))
            || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0
            || FAILED(D3D12CreateDevice(
                adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
            continue;
        }
        const std::uint64_t hardwareId =
            (static_cast<std::uint64_t>(description.VendorId) << 32U)
            | description.DeviceId;
        if (std::find(
                testedHardwareIds.begin(), testedHardwareIds.end(), hardwareId)
            != testedHardwareIds.end()) {
            continue;
        }
        testedHardwareIds.push_back(hardwareId);
        AdapterBenchmark benchmark(adapter, adapterName(description));
        for (const Scenario& scenario : scenarios) {
            for (const InstanceStorageStrategy strategy : strategies) {
                printResult(benchmark.run(scenario, strategy, options));
            }
        }
        ++testedAdapters;
    }
    if (testedAdapters == 0) fail("no hardware D3D12 adapters were available");
    return EXIT_SUCCESS;
}
