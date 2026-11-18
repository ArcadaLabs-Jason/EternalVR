#pragma once

// The interop copies' GPU time (ETERNALVR_TEST_COPY_GPU=1, a rig switch, off by default; T-075's budget line,
// docs/rig-findings/perf-targets.md): timestamps around the present hook's copy into the ring (Vulkan, on the
// game's queue) and around the XR worker's copy into the headset's images (D3D12), summed into one 10 s line
// (gpu_timing/copy_window.hpp). Each copy gets three timestamps: before its first command (when the work
// before it on the queue and its waits are done), between its two parts and after its last command, so a
// part's time is from the end of the work before it to its own end, which the GPU's other work running
// alongside can only lengthen. Off, nothing is made or recorded: every call returns on one flag.

#include "gpu_timing/copy_window.hpp"
#include "vkcore/dispatch.hpp"

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace evr::vkcore {

class CopyGpuTimer {
public:
    // Reads the switch and makes the query pool; once per presenter (present hook).
    void setup(DeviceData& dev);
    // Frees the query pool and, once the XR worker has stopped (xrWorkerStopped), its D3D12 heap and
    // readback; a worker left behind keeps them until the presenter's own teardown.
    void destroy(DeviceData& dev, bool xrWorkerStopped);
    bool on() const { return on_.load(std::memory_order_relaxed); }

    // Present hook, around one copy command buffer: begin before its first command, mark between the ring's
    // part and the mirror's, end after its last; then submitted with the copy's timeline value (0: not
    // submitted, the stamps are dropped).
    void begin(VkCommandBuffer cb, std::uint32_t family);
    void mark(VkCommandBuffer cb);
    void end(VkCommandBuffer cb);
    void submitted(std::uint64_t value);
    // Reads the stamps of the copies whose timeline value has completed.
    void poll(DeviceData& dev, std::uint64_t completed);

    // XR worker, around the D3D12 copy list: begin after its reset, mark after the colour copy, end before
    // its close; done once the copy's fence was waited (copied: it finished; otherwise the stamps are
    // dropped).
    void beginList(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list);
    void markList(ID3D12GraphicsCommandList* list);
    void endList(ID3D12GraphicsCommandList* list);
    void listDone(bool copied);
    void releaseD3d();

    // The 10 s line (XR worker).
    void logSummary();

private:
    static constexpr std::uint32_t kStamps = 3;  // timestamps per copy: before, between the parts, after
    static constexpr std::uint32_t kCopies = 32; // copies in flight at once, kStamps queries each

    struct Functions {
        PFN_vkCreateQueryPool createQueryPool = nullptr;
        PFN_vkDestroyQueryPool destroyQueryPool = nullptr;
        PFN_vkGetQueryPoolResults getQueryPoolResults = nullptr;
        PFN_vkCmdResetQueryPool cmdResetQueryPool = nullptr;
        PFN_vkCmdWriteTimestamp cmdWriteTimestamp = nullptr;
    };
    static constexpr std::uint64_t kRecording = ~std::uint64_t{0}; // recorded, not submitted yet
    struct Pending {
        std::uint64_t value = 0; // the copy's timeline value; 0: free, kRecording: not submitted yet
        std::uint32_t bits = 0;  // the family's valid timestamp bits
    };

    std::atomic<bool> on_{false};
    std::atomic<bool> setUp_{false}; // set by the present hook, cleared by destroy() (presenter teardown)
    Functions fn_;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    float periodNs_ = 1.0f;
    std::vector<std::uint32_t> familyBits_;
    std::mutex vkMutex_; // the present hook's state below
    std::array<Pending, kCopies> pending_{};
    std::optional<std::uint32_t> recording_; // the copy between begin and submitted
    std::uint32_t next_ = 0;

    // XR worker only.
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    // The device they were made on (a reconnect makes a new one); held, so a new device cannot reuse its
    // address.
    Microsoft::WRL::ComPtr<ID3D12Device> heapDevice_;
    float d3dPeriodNs_ = 0.0f;
    bool d3dFailed_ = false;
    bool listArmed_ = false;

    std::mutex windowMutex_;
    gpu_timing::CopyWindow window_;
};

} // namespace evr::vkcore
