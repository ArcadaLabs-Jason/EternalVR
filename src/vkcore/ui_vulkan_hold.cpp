// The depth layer's part of the UI layer's Vulkan hooks (ui_vulkan.hpp: holdQueues, watchDestroy): which
// queue's next submit waits for a depth read, and whether the game destroyed the depth layer's source image.

#include "vkcore/ui_vulkan_hold.hpp"

#include "vkcore/ui_vulkan.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace evr::vkcore::ui_vulkan {

namespace {

// After a depth read on g_holdQueue, the game's next submit on every other queue first waits for g_holdValue
// on g_holdTimeline. Allocated once and never destroyed (no teardown at process exit, as ui_vulkan.cpp's).
std::mutex& g_holdMutex = *new std::mutex;
VkQueue g_holdQueue = VK_NULL_HANDLE;
VkSemaphore g_holdTimeline = VK_NULL_HANDLE;
std::uint64_t g_holdValue = 0;
auto& g_heldUntil = *new std::unordered_map<VkQueue, std::uint64_t>;
std::atomic<bool> g_holdOn{false};
std::atomic<std::uint64_t> g_holdWaits{0};
// The depth layer's source image, and whether the game destroyed it since (takeDestroyed).
std::atomic<VkImage> g_depthWatch{VK_NULL_HANDLE};
std::atomic<bool> g_depthGone{false};

} // namespace

std::optional<HoldWait> holdFor(VkQueue queue) {
    if (!g_holdOn.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::lock_guard lock(g_holdMutex);
    const auto it = g_heldUntil.find(queue);
    if (queue == g_holdQueue || (it != g_heldUntil.end() && g_holdValue <= it->second)) {
        return std::nullopt;
    }
    return HoldWait{g_holdTimeline, g_holdValue};
}

void noteHoldAdded(VkQueue queue, const HoldWait& wait) {
    std::lock_guard lock(g_holdMutex);
    if (wait.timeline == g_holdTimeline) {
        std::uint64_t& waited = g_heldUntil[queue];
        waited = std::max(waited, wait.value);
    }
    g_holdWaits.fetch_add(1, std::memory_order_relaxed);
}

void noteDestroyed(VkImage image) {
    if (image != VK_NULL_HANDLE && g_depthWatch.load(std::memory_order_relaxed) == image) {
        g_depthGone.store(true, std::memory_order_release); // the depth layer drops its view
    }
}

void holdQueues(VkQueue except, VkSemaphore timeline, std::uint64_t value) {
    std::lock_guard lock(g_holdMutex);
    if (timeline != g_holdTimeline) {
        g_heldUntil.clear(); // values of another timeline
    }
    g_holdQueue = except;
    g_holdTimeline = timeline;
    g_holdValue = value;
    g_holdOn.store(true, std::memory_order_release);
}

void stopHolding() {
    std::lock_guard lock(g_holdMutex);
    g_holdOn.store(false, std::memory_order_release);
    // A timeline made again later (the same handle value, counting from 1 again) starts with no queue waited.
    g_heldUntil.clear();
    g_holdTimeline = VK_NULL_HANDLE;
    g_holdValue = 0;
}

std::uint64_t holdWaits() {
    return g_holdWaits.load(std::memory_order_relaxed);
}

void watchDestroy(VkImage image) {
    g_depthWatch.store(image, std::memory_order_relaxed);
}

bool takeDestroyed() {
    return g_depthGone.exchange(false, std::memory_order_acq_rel);
}

} // namespace evr::vkcore::ui_vulkan
