#pragma once

// The depth layer (ETERNALVR_DEPTH_LAYER, off by default; docs/ARCHITECTURE.md section 6 "Depth", T-057,
// T-080): each eye's depth goes to the runtime with its projection view (XR_KHR_composition_layer_depth), so
// a runtime that reprojects a late frame (Meta's ASW, SteamVR's motion smoothing, Virtual Desktop's SSW) can
// move it by depth. The plain parts (the switch, the format, the units, the checks) are in
// features/depth/depth_layer.hpp.
//
// What crosses, per ring slot, beside the colour image (presenter_ring.cpp):
// - Vulkan (presenter_depth_vk.cpp; its objects in presenter_depth_setup.cpp): in the present hook's copy of
//   each Route S pair's eye, a compute shader reads the game's
//   `_upscaledOpaqueDepth` (the scene depth at the output size, the eye image's size) and writes its floats
//   into the slot's shared buffer, at the D3D12 footprint of the depth swapchain image (both eyes side by
//   side, as in the colour image), at most the value of depth::kNearestMetres. A compute shader because a
//   depth aspect cannot be copied on the compute queue the game presents from in a level (Vulkan allows depth
//   copies on graphics queues only) and a D3D12 compute shader cannot write a depth swapchain. The copy is in
//   the same command buffer as the eye's colour copy: same game frame, eye, slot and timeline value. The
//   game's next submit on any other queue after the copy's own waits for it (ui_vulkan::holdQueues), so the
//   game's later work there does not write the image before the read has run; a submit already made is not
//   ordered by it.
//   D32_FLOAT_S8X24_UINT (the fallback format) is not tried on a runtime yet.
// - D3D12 (presenter_depth_xr.cpp): with the slot's colour copy, a CopyTextureRegion of the whole buffer (a
//   placed footprint) into an image of a depth swapchain of the colour image's size (D32_FLOAT, else
//   D32_FLOAT_S8X24_UINT), acquired, waited and released with the colour image.
// - OpenXR: each projection view of an image whose depth came along gets an XrCompositionLayerDepthInfoKHR
//   with the depth swapchain, the view's own rectangle, minDepth 0, maxDepth 1, and nearZ / farZ in metres
//   from the depth row the eye was drawn with (depth::planesOf: reversed, nearZ the far plane). A repeated
//   image keeps its depth; a new image without one (a check failed) is submitted without.
//
// Only Route S pairs: Route S's mono frames, alternate eyes (on or auto), Parallel Eye Rendering, mono head
// tracking, the cinema screen, menus, loading screens, cutscenes on the screen and every quad (HUD, menus,
// reticle) carry no depth. A frame whose source fails a check goes without depth (counted in the 10 s line);
// the extension or a format missing, the swapchain, shader or import failing, the runtime refusing a frame
// with depth (xrEndFrame failing) or holding the depth image through kMaxWaitTimeouts waits in a row stop the
// layer for the game with one log line, and the projection is colour only from the next frame. Off, nothing
// of this is made, recorded or submitted: the hooks return on one flag, and no game image is changed either
// way.

#include "features/depth/depth_layer.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/presenter_swapchains.hpp"
#include "vkcore/wait_stats.hpp"

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <openxr/openxr.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore {

struct XrFunctions;

class DepthLayer {
public:
    static constexpr std::uint32_t kSlots = 4; // kRingSize (static_assert in presenter_depth_glue.cpp)
    static constexpr std::uint32_t kCommandBuffers = 8; // FamilyCommands: two per slot (the same)
    static constexpr std::uint32_t kRowRecords = 16;    // game ticks whose depth row is kept (noteRows)

    // Both out of line (presenter_depth_setup.cpp), where Functions is complete.
    DepthLayer(bool switchOn, float unitsPerMetre);
    ~DepthLayer();
    DepthLayer(const DepthLayer&) = delete;
    DepthLayer& operator=(const DepthLayer&) = delete;

    // ---- Start-up (XR worker) ----

    // At instance creation: names XR_KHR_composition_layer_depth in `extensions` when switched on and listed.
    // A new instance (after a lost session) without it stops a running layer (colour only, one line).
    void addExtension(const std::vector<XrExtensionProperties>& listed, std::vector<const char*>& extensions);
    // The instance was made again without the optional extensions: the same.
    void extensionRefused();
    // While the ring is built (no present copies meanwhile): decides whether the layer runs (one log line),
    // and if it does makes the shared buffers for a ring of `ring` (eyes of `eye`), and the shader's objects
    // once. False (logged) when it stays off; buffers that cannot be made leave it off for this ring only.
    bool createBuffers(DeviceData& dev,
                       ID3D12Device* d3d,
                       const depth::StartState& start,
                       const std::vector<std::int64_t>& formats,
                       VkExtent2D ring,
                       VkExtent2D eye);
    // While no present copies (ring rebuild, shutdown): the buffers; with `all` the shader's objects too.
    void destroyBuffers(DeviceData& dev, bool all);
    [[nodiscard]] bool active() const { return active_.load(std::memory_order_acquire); }

    // ---- Render jobs ----

    // The depth row Route S's eye of game tick `tick` was drawn with (its projection's [10] and [11]).
    void noteRows(std::uint64_t tick, float m10, float m11);

    // ---- Present hook, under the presenter's mutex ----

    // Before a copy into `slot`: unless it adds the second eye of a pair (`keepOther`), the slot holds no
    // depth (any copy without depth clears it, so an older pair's depth never goes with new colour).
    void beginSlot(std::uint32_t slot, bool keepOther);
    // Into the copy command buffer `cbIndex` (FamilyCommands' index) on a queue of `family`: halves
    // `firstEye` to `firstEye + eyeCount - 1` of `slot` from the game's depth of game tick `tick`. False when
    // nothing was recorded (counted).
    bool record(DeviceData& dev,
                VkCommandBuffer cb,
                std::uint32_t family,
                std::uint32_t slot,
                std::uint32_t cbIndex,
                std::uint32_t firstEye,
                std::uint32_t eyeCount,
                std::uint64_t tick);
    // The copy in `cbIndex` was submitted on `queue` with `timeline` at `value`, or not (`value` 0). After a
    // depth read the game's next submit on any other queue waits for that value (ui_vulkan::holdQueues): the
    // game's work submitted after this on another queue does not write the depth image before the read.
    void submitted(std::uint32_t cbIndex, std::uint64_t value, VkQueue queue, VkSemaphore timeline);
    // The shared timeline at `completed`: GPU times read, views retired.
    void poll(DeviceData& dev, std::uint64_t completed);

    // ---- XR worker ----

    // The slot taken for this frame holds both eyes' depth.
    [[nodiscard]] bool slotComplete(std::uint32_t slot) const;
    // The depth swapchain's image for this slot's copy, acquired and waited (the swapchain made on first
    // use); false: the image goes without depth (a timeout keeps the image acquired for the next frame; the
    // first is logged, and kMaxWaitTimeouts in a row stop the layer).
    bool acquire(const XrFunctions& xr, XrSession session, XrSwapchainSet& swapchains, WaitStats& waits);
    // Into the colour copy's command list: the slot's buffer into the acquired image.
    void recordXrCopy(ID3D12GraphicsCommandList* list, std::uint32_t slot);
    // The colour copy's list was not executed: nothing went into the acquired image.
    void copyAbandoned() {
        image_.onCopyAbandoned();
        readBackPending_ = false;
    }
    // The colour copy finished (completeCopy): the image's depth is the shown one, or the new image has none.
    void finish(const XrFunctions& xr);
    // The projection's views of the shown image get their depth (`infos` chained to them); false: none.
    bool fill(std::array<XrCompositionLayerProjectionView, 2>& views,
              std::array<XrCompositionLayerDepthInfoKHR, 2>& infos);
    // xrEndFrame failed with depth chained (`result`): the layer stops, the next frames are colour only.
    void refused(int result);
    // ETERNALVR_TEST_DEPTH_REFUSE=fake: the result xrEndFrame is taken to have returned with depth chained.
    [[nodiscard]] bool fakeRefusal() const { return refuse_ == Refuse::Fake; }
    // The ring is rebuilt: the depth swapchain is handed back (destroyed by the caller after the new one is
    // made).
    XrSwapchain detachSwapchain();
    // The session ends.
    void destroyXr(const XrFunctions& xr, XrSwapchainSet& swapchains);
    // Every 10 s with the xr line.
    void logStats();

private:
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        std::array<bool, 2> eyes{}; // halves written for the slot's image (present hook while it writes)
        float m10 = 0.0f;           // the depth row of their game tick
        float m11 = 0.0f;
    };
    struct Functions; // the Vulkan entry points the shader needs (presenter_depth_functions.hpp)
    struct Timing {
        std::uint64_t value = 0; // the submit that writes the pair, 0: none pending
        bool recorded = false;   // timestamps recorded
        bool read = false;       // a depth read recorded
    };
    struct RetiredView {
        VkImageView view = VK_NULL_HANDLE;
        std::uint64_t value = 0; // the last submit that read it (0: never submitted, destroyed at once)
    };
    struct RowRecord {
        std::uint64_t tick = 0;
        float m10 = 0.0f;
        float m11 = 0.0f;
    };
    enum class Refuse { None, Invalid, Fake }; // ETERNALVR_TEST_DEPTH_REFUSE: 1 / fake

    bool createShaderObjects(DeviceData& dev);
    void destroyShaderObjects(DeviceData& dev);
    bool externalBufferImportable(DeviceData& dev);
    bool createSlotBuffer(DeviceData& dev, ID3D12Device* d3d, Slot& slot);
    VkImageView viewFor(DeviceData& dev, VkImage image, std::uint64_t serial, std::int32_t format);
    void retireView();
    std::optional<RowRecord> rowsOf(std::uint64_t tick);
    void notCopied(const char* why);
    void fail(const char* what); // off for the rest of the process, one line
    bool createSwapchain(const XrFunctions& xr, XrSession session, XrSwapchainSet& swapchains);
    void recordReadBack(ID3D12GraphicsCommandList* list, ID3D12Resource* image, const Slot& slot);
    void readBack(); // ETERNALVR_TEST_DEPTH_READBACK
    void writeReadBack(const float* texels);

    const bool switchOn_;
    const float unitsPerMetre_;
    bool listed_ = false;    // the runtime lists the extension
    bool extension_ = false; // enabled on the instance
    bool decided_ = false;
    std::atomic<bool> active_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> stopped_{false}; // fail() ran: the XR worker shows no more depth
    bool ringFailed_ = false;          // this ring's buffers could not be made (tried again with the next)

    // Vulkan (present hook; made and freed while no present copies)
    std::unique_ptr<Functions> fn_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kCommandBuffers> sets_{};
    VkQueryPool queries_ = VK_NULL_HANDLE;
    double timestampNs_ = 0.0; // the device's timestamp period
    std::vector<std::uint32_t> timestampBits_;
    std::vector<bool> graphicsFamily_; // per queue family: has graphics (a depth layout change is allowed)
    std::array<Timing, kCommandBuffers> timing_{};
    VkImage viewImage_ = VK_NULL_HANDLE; // the source the current view is of
    std::uint64_t viewSerial_ = 0;
    VkImageView view_ = VK_NULL_HANDLE;
    std::uint64_t viewValue_ = 0; // the last submit that read the current view
    std::vector<RetiredView> retired_;
    std::array<Slot, kSlots> slots_{};
    VkExtent2D ring_{};
    VkExtent2D eye_{};
    std::uint64_t rowTexels_ = 0; // the footprint's row pitch in floats
    std::uint64_t bufferBytes_ = 0;
    std::mutex rowsMutex_; // render jobs write, the present hook reads
    std::array<RowRecord, kRowRecords> rows_{};
    std::uint32_t nextRow_ = 0;
    bool loggedFirst_ = false;

    // XR worker
    std::int64_t format_ = 0;
    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    XrSwapchainSet::Images images_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    bool swapchainFailed_ = false;
    depth::SwapImageState image_;
    depth::WaitTimeouts waits_;
    Refuse refuse_ = Refuse::None;
    bool readBackOn_ = false;
    std::wstring readBackDir_; // ETERNALVR_TEST_DEPTH_READBACK=<dir>: the first read-backs also as images
    std::uint32_t readBackFiles_ = 0;
    bool fault_ = false; // ETERNALVR_TEST_DEPTH_FAULT=1: eye R's copy also into eye L's half (planted)
    Microsoft::WRL::ComPtr<ID3D12Resource> readBack_;
    std::uint64_t readBackBytes_ = 0;
    bool readBackPending_ = false;
    ULONGLONG lastReadBack_ = 0;
    std::optional<depth::DepthModel> readBackModel_;

    // Statistics: present hook (atomics, read by the worker's 10 s line) and worker
    std::atomic<std::uint64_t> eyesCopied_{0};
    std::atomic<std::uint64_t> eyesNotCopied_{0};
    std::atomic<const char*> lastWhy_{"none"};
    std::atomic<std::uint64_t> gpuSamples_{0};
    std::atomic<std::uint64_t> gpuSumNs_{0};
    std::atomic<std::uint64_t> gpuMaxNs_{0};
    std::array<std::atomic<std::uint64_t>, 3> layouts_{}; // shader read-only, depth read-only, attachment
    std::uint64_t framesWith_ = 0;
    std::uint64_t framesWithout_ = 0;
    std::uint64_t imagesWith_ = 0;
    std::uint64_t imagesWithout_ = 0;
    bool loggedFirstFrame_ = false;
};

} // namespace evr::vkcore
