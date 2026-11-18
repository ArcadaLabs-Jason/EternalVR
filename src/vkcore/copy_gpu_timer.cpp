// The interop copies' GPU time (copy_gpu_timer.hpp).

#include "vkcore/copy_gpu_timer.hpp"

#include "gpu_timing/stats.hpp"
#include "vkcore/log.hpp"

#include <cstring>
#include <string>

namespace evr::vkcore {

namespace gt = gpu_timing;

void CopyGpuTimer::setup(DeviceData& dev) {
    if (setUp_.exchange(true)) {
        return;
    }
    std::wstring value;
    if (!readEnv(L"ETERNALVR_TEST_COPY_GPU", value) || !gt::parseEnabled(value)) {
        return;
    }
    const auto load = [&dev](const char* name) {
        return dev.nextGetDeviceProcAddr(dev.device, name);
    };
    fn_.createQueryPool = reinterpret_cast<PFN_vkCreateQueryPool>(load("vkCreateQueryPool"));
    fn_.destroyQueryPool = reinterpret_cast<PFN_vkDestroyQueryPool>(load("vkDestroyQueryPool"));
    fn_.getQueryPoolResults = reinterpret_cast<PFN_vkGetQueryPoolResults>(load("vkGetQueryPoolResults"));
    fn_.cmdResetQueryPool = reinterpret_cast<PFN_vkCmdResetQueryPool>(load("vkCmdResetQueryPool"));
    fn_.cmdWriteTimestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(load("vkCmdWriteTimestamp"));
    if (!fn_.createQueryPool || !fn_.destroyQueryPool || !fn_.getQueryPoolResults || !fn_.cmdResetQueryPool ||
        !fn_.cmdWriteTimestamp) {
        EVR_LOG("copy-gpu: the query functions are missing; the copies go untimed");
        return;
    }
    VkPhysicalDeviceProperties props{};
    dev.instance->vk.GetPhysicalDeviceProperties(dev.physicalDevice, &props);
    periodNs_ = props.limits.timestampPeriod;
    std::uint32_t count = 0;
    dev.instance->vk.GetPhysicalDeviceQueueFamilyProperties(dev.physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    dev.instance->vk.GetPhysicalDeviceQueueFamilyProperties(dev.physicalDevice, &count, families.data());
    familyBits_.clear();
    for (const VkQueueFamilyProperties& f : families) {
        familyBits_.push_back(f.timestampValidBits);
    }
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = kCopies * kStamps;
    if (fn_.createQueryPool(dev.device, &info, nullptr, &pool_) != VK_SUCCESS) {
        pool_ = VK_NULL_HANDLE;
        EVR_LOG("copy-gpu: no query pool; the copies go untimed");
        return;
    }
    on_.store(true);
    EVR_LOG(
        "copy-gpu: the interop copies are timed (ETERNALVR_TEST_COPY_GPU): three timestamps around each copy "
        "into the ring and each D3D12 copy into the headset's images; a 10 s line");
}

void CopyGpuTimer::destroy(DeviceData& dev, bool xrWorkerStopped) {
    std::lock_guard lock(vkMutex_);
    if (pool_) {
        fn_.destroyQueryPool(dev.device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    on_.store(false);
    setUp_.store(false);
    pending_ = {};
    recording_.reset();
    if (xrWorkerStopped) {
        releaseD3d(); // the worker is joined: nothing else touches the heap and readback
    }
}

void CopyGpuTimer::begin(VkCommandBuffer cb, std::uint32_t family) {
    if (!on()) {
        return;
    }
    std::lock_guard lock(vkMutex_);
    recording_.reset();
    const std::uint32_t bits = family < familyBits_.size() ? familyBits_[family] : 0;
    if (bits == 0) {
        return;
    }
    for (std::uint32_t i = 0; i < kCopies; ++i) {
        const std::uint32_t c = (next_ + i) % kCopies;
        if (pending_[c].value == 0) {
            next_ = (c + 1) % kCopies;
            recording_ = c;
            pending_[c] = {kRecording, bits}; // taken until submitted
            fn_.cmdResetQueryPool(cb, pool_, c * kStamps, kStamps);
            fn_.cmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, c * kStamps);
            return;
        }
    }
}

void CopyGpuTimer::mark(VkCommandBuffer cb) {
    if (!on()) {
        return;
    }
    std::lock_guard lock(vkMutex_);
    if (recording_) {
        fn_.cmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, *recording_ * kStamps + 1);
    }
}

void CopyGpuTimer::end(VkCommandBuffer cb) {
    if (!on()) {
        return;
    }
    std::lock_guard lock(vkMutex_);
    if (recording_) {
        fn_.cmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, *recording_ * kStamps + 2);
    }
}

void CopyGpuTimer::submitted(std::uint64_t value) {
    if (!on()) {
        return;
    }
    std::lock_guard lock(vkMutex_);
    if (recording_) {
        pending_[*recording_].value = value; // 0 frees it
    }
    recording_.reset();
}

void CopyGpuTimer::poll(DeviceData& dev, std::uint64_t completed) {
    if (!on()) {
        return;
    }
    std::lock_guard lock(vkMutex_);
    for (std::uint32_t c = 0; c < kCopies; ++c) {
        Pending& p = pending_[c];
        if (p.value == 0 || p.value > completed) {
            continue;
        }
        std::array<std::uint64_t, kStamps> stamps{};
        const VkResult r =
            fn_.getQueryPoolResults(dev.device, pool_, c * kStamps, kStamps, sizeof(stamps), stamps.data(),
                                    sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        const std::uint32_t bits = p.bits;
        p = {};
        if (r != VK_SUCCESS) {
            continue;
        }
        if (const std::optional<gt::CopyParts> parts = gt::copyParts(stamps, bits, periodNs_)) {
            std::lock_guard windowLock(windowMutex_);
            window_.addVulkan(*parts);
        }
    }
}

void CopyGpuTimer::beginList(ID3D12Device* device,
                             ID3D12CommandQueue* queue,
                             ID3D12GraphicsCommandList* list) {
    listArmed_ = false;
    if (!on() || d3dFailed_) {
        return;
    }
    if (heap_ && heapDevice_.Get() != device) {
        releaseD3d();
    }
    if (!heap_) {
        UINT64 frequency = 0;
        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = kStamps;
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kStamps * sizeof(std::uint64_t);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0 ||
            FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&heap_))) ||
            FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&readback_)))) {
            d3dFailed_ = true;
            releaseD3d();
            EVR_LOG("copy-gpu: no D3D12 timestamps; the copies into the headset's images go untimed");
            return;
        }
        d3dPeriodNs_ = static_cast<float>(1e9 / static_cast<double>(frequency));
        heapDevice_ = device;
    }
    list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    listArmed_ = true;
}

void CopyGpuTimer::markList(ID3D12GraphicsCommandList* list) {
    if (listArmed_) {
        list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    }
}

void CopyGpuTimer::endList(ID3D12GraphicsCommandList* list) {
    if (listArmed_) {
        list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
        list->ResolveQueryData(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, kStamps, readback_.Get(), 0);
    }
}

void CopyGpuTimer::listDone(bool copied) {
    if (!listArmed_) {
        return;
    }
    listArmed_ = false;
    if (!copied) {
        return;
    }
    void* mapped = nullptr;
    const D3D12_RANGE read{0, kStamps * sizeof(std::uint64_t)};
    if (FAILED(readback_->Map(0, &read, &mapped)) || !mapped) {
        return;
    }
    std::array<std::uint64_t, kStamps> stamps{};
    std::memcpy(stamps.data(), mapped, sizeof(stamps));
    const D3D12_RANGE written{0, 0};
    readback_->Unmap(0, &written);
    if (const std::optional<gt::CopyParts> parts = gt::copyParts(stamps, 64, d3dPeriodNs_)) {
        std::lock_guard lock(windowMutex_);
        window_.addD3d(*parts);
    }
}

void CopyGpuTimer::releaseD3d() {
    heap_.Reset();
    readback_.Reset();
    heapDevice_.Reset();
    listArmed_ = false;
}

void CopyGpuTimer::logSummary() {
    if (!on()) {
        return;
    }
    gt::CopyWindow::Report r;
    {
        std::lock_guard lock(windowMutex_);
        r = window_.take();
    }
    EVR_LOG("%s", gt::formatCopyReport(r).c_str());
}

} // namespace evr::vkcore
