#pragma once

// Parallel Eye Rendering keeps both eyes of a present from the same frame. Each eye used to be copied at
// present time from its view's one image (eye 0 the presented swapchain image, eye 1 view 1's screen-pass
// image), and that copy runs on the GPU after the next frame may already have written the image (measured:
// the eyes a frame apart in most capture bursts). Instead, each eye is copied into a small ring by a batch
// after the game's submit of the command buffer its view's screen pass is recorded into (the engine's
// screen command contexts, [[context + 0x118]]): view 0's swapchain image (the one its command buffer moves
// to PRESENT_SRC) after the submit with view 0's, view 1's image after the one with view 1's. The two copies
// of a frame share a slot, and a present shows the newest pair both of whose copies were submitted, waiting
// on their timeline values, with that frame's view record: the eyes are of one frame by construction
// (snapshot_ring::pickPair). With no new pair the headset keeps the last one; with none for a while, or on
// frames without view 1, eye 0 is the presented image and eye 1 view 1's image (or the presented one).
//
// ETERNALVR_TEST_PE_PAIRING=guess (A/B runs) keeps the guess this replaced: eye 0 the presented image, eye
// 1 the copy of view 1's image tagged with the semaphore the submit signals that the present waits on, or
// the copy before it (the game usually presents the image its previous frame drew), told by which frame drew
// the presented image (snapshot_ring::pickShown). ETERNALVR_TEST_EYE_SNAPSHOT=0 turns the copies off, =1
// always reads the copy before, =2 the tagged one also when both frames drew the presented image (guess).

#include "vkcore/frame_repeat.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {
struct DeviceData;
}

namespace evr::vkcore::view_snapshot {

// The snapshots are on: Parallel Eye Rendering's eye copy runs and ETERNALVR_TEST_EYE_SNAPSHOT is not 0
// (logged once; under Route S false, and nothing is logged). Cached: one check.
bool enabled();

// The engine hook (view_one_passes.cpp): view 1's screen pass is being recorded into `cb` and draws into
// `cloneImage` (an engine image pointer).
void arm(std::uintptr_t cloneImage, VkCommandBuffer cb);

// The swapchain watch (view_swap_watch.hpp) at the engine's swapchain destroy: the next presents of the new
// swapchain's images that no view 0 pass has drawn yet keep the last pair (at most 4; snapshot_ring.hpp
// NewImageHold) when eye 0 is the presented image.
void holdNewSwapchainImages();

// The presenter: the head-tracked view record (its seq) written into this frame's view 0, before its screen
// passes. The copies keep it, so a present carries the pose its eyes were rendered with.
void noteFrameView(std::uint64_t view);

// vkCreateSwapchainKHR: the game's swapchain images, their format and size (view 0's copies).
void noteSwapchain(const VkImage* images, std::uint32_t count, VkFormat format, VkExtent2D extent);

// The presenter: an in-headset capture of `frames` frames fired (bug_capture.hpp). The trace of the last
// presents, with which copies each showed, goes into the log once the burst's presents are in it.
void captureFired(std::uint32_t frames);

// The same hook for view 0's screen pass, recorded into `cb` just before view 1's of the same frame, and the
// UI layer's command buffer hooks (ui_vulkan.cpp, cheap; called only while enabled()): which swapchain image
// a frame draws.
void noteView0Pass(VkCommandBuffer cb);
void noteBegin(VkCommandBuffer cb);
void noteBarrier(VkCommandBuffer cb, const VkImageMemoryBarrier& barrier);

// The UI layer's submit hook: with a frame's view 0 or view 1 command buffer in `submits`, one batch after
// them runs the copies (`cbs`, in order), waiting first on `waits` (the slots' last copies and reads, on the
// GPU) and signalling `signals`; then copiesSubmitted() with whether that submit worked. With an eye 0 copy
// the signals of the game's batches from `moveFrom` on (their semaphores and timeline values) move onto it
// as well, so the present of that image, and anything else the game orders after its batches, waits for
// the copy (submit_order.hpp; UINT32_MAX: none move). `order` keeps other copies from being given timeline
// values until the submit carrying these has returned (each timeline is signalled in order); released with
// the Append.
struct Append {
    std::array<VkCommandBuffer, 4> cbs{};
    std::uint32_t cbCount = 0;
    std::array<VkSemaphore, 3> waits{};
    std::array<std::uint64_t, 3> waitValues{};
    std::uint32_t waitCount = 0;
    std::array<VkSemaphore, 2> signals{};
    std::array<std::uint64_t, 2> signalValues{};
    std::uint32_t signalCount = 0;
    // The pair slots' copies in it (slot * 2 + eye), for copiesSubmitted.
    std::array<std::uint8_t, 4> halves{};
    std::uint32_t halfCount = 0;
    std::uint32_t moveFrom = UINT32_MAX;
    std::unique_lock<std::mutex> order;
};
std::optional<Append> submitted(VkQueue queue, std::uint32_t submitCount, const VkSubmitInfo* submits);
void copiesSubmitted(const Append& append, bool ok);

// One eye's copy, in TRANSFER_SRC once `wait` is signalled on `semaphore`; a null image: none.
struct Copy {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::uint64_t wait = 0;
};

// The presenter: the copies for a present waiting on `waits` of the swapchain image `presented`. With
// `drop` the present is not handed to the headset (it keeps the last pair; why: `dropWhy`). A pick holds its
// slot: once the copy reading it is submitted, read() with that copy's timeline value; notRead() when it was
// not.
struct Pick {
    std::array<Copy, 2> eyes{}; // [0] view 0's copy (pairs only), [1] view 1's
    bool drop = false;
    FrameRepeat dropWhy = FrameRepeat::Kept; // the frames table's reason (Kept, Dropped or Held)
    bool paired = false;                     // the eyes are a pair of one frame (else the guess)
    std::size_t slot = 0;
    std::uint64_t heldRead = 0; // the slot's last read before this pick held it
    std::uint64_t frame = 0;    // the frame shown (view 0 screen passes counted)
    std::uint64_t matched = 0;  // guess: the copy of the frame whose submit the present waits on
    std::uint64_t lastRead = 0; // guess: the copy the last present read
    bool repeat = false;        // guess: the copy the last present read again
    std::uint8_t shows =
        0; // guess: the presented image is the one 1: the matched frame, 2: before, 3: both drew
    std::uint64_t view = 0;    // the view record the frame was rendered with (0: not known)
    std::uint64_t present = 0; // the present's number (the trace)
    // Pairs: the frame and view record of eye 1's copy (the shown frame's, or under
    // ETERNALVR_TEST_PE_EYE1_LAG=1 the pair shown before it, whose slot is then `eye1Slot`).
    std::uint64_t eye1Frame = 0;
    std::uint64_t eye1View = 0;
    std::size_t eye1Slot = SIZE_MAX;
};
std::optional<Pick> forPresent(const VkSemaphore* waits, std::uint32_t waitCount, VkImage presented);
void read(const Pick& pick, VkSemaphore presenterTimeline, std::uint64_t value);
void notRead(const Pick& pick);

// A capture burst's line on which copies the eyes show (`pick`: forPresent's result).
std::string describe(const std::optional<Pick>& pick);

// vkDestroyDevice (after the presenter's shutdown): the rings' objects of that device.
void onDeviceDestroyed(DeviceData& dev);

} // namespace evr::vkcore::view_snapshot
