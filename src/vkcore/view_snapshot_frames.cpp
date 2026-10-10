#include "vkcore/view_snapshot_frames.hpp"

#include "vkcore/view_snapshot.hpp"
#include "vkcore/view_snapshot_impl.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace evr::vkcore::view_snapshot {

namespace {

// Command buffers being recorded that moved a swapchain image to PRESENT_SRC since their begin, and that
// image.
// Written under g_framesMutex; `cb` read without it first, so a begin of any other command buffer takes no
// lock.
struct FrameOf {
    std::atomic<VkCommandBuffer> cb{VK_NULL_HANDLE};
    VkImage frame = VK_NULL_HANDLE;
    bool released = false; // that barrier hands the image to another queue family
};
// Allocated once and never destroyed (layer_entry.cpp: no teardown at process exit).
std::mutex& g_framesMutex = *new std::mutex;
std::array<FrameOf, 8> g_frames{};
std::size_t g_framesNext = 0;

FrameSubmits& g_submits = *new FrameSubmits;

bool carries(std::uint32_t submitCount, const VkSubmitInfo* submits, VkCommandBuffer cb) {
    for (std::uint32_t s = 0; s < submitCount; ++s) {
        for (std::uint32_t i = 0; i < submits[s].commandBufferCount; ++i) {
            if (submits[s].pCommandBuffers[i] == cb) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

FrameSubmits& frameSubmits() {
    return g_submits;
}

std::uint64_t FrameSubmits::start(VkCommandBuffer cb, std::uint64_t view, Counters& count) {
    Entry& e = entries_[++frames_ % kKept];
    if (e.frame != 0 && e.call1 != 0 && e.call0 == 0) {
        ++count[Count::V0Never];
    }
    e.frame = frames_;
    e.view = view;
    e.cb.store(cb, std::memory_order_relaxed);
    e.call0 = 0;
    e.call1 = 0;
    return frames_;
}

bool FrameSubmits::carriesView0(std::uint32_t submitCount, const VkSubmitInfo* submits) const {
    for (const Entry& e : entries_) {
        const VkCommandBuffer cb = e.cb.load(std::memory_order_relaxed);
        if (cb && carries(submitCount, submits, cb)) {
            return true;
        }
    }
    return false;
}

std::size_t FrameSubmits::submitted(std::uint32_t submitCount,
                                    const VkSubmitInfo* submits,
                                    std::uint64_t view1,
                                    View0s& view0,
                                    Counters& count) {
    ++calls_;
    std::size_t found = 0;
    for (Entry& e : entries_) {
        const VkCommandBuffer cb = e.cb.load(std::memory_order_relaxed);
        if (cb && carries(submitCount, submits, cb)) {
            e.cb.store(VK_NULL_HANDLE, std::memory_order_relaxed);
            e.call0 = calls_;
            classify(e, count);
            view0[found++] = View0{e.frame, e.view, cb};
        }
        if (view1 != 0 && e.frame == view1 && e.call1 == 0) {
            e.call1 = calls_;
            newest1_ = std::max(newest1_, view1);
            classify(e, count);
        }
    }
    std::sort(view0.begin(), view0.begin() + static_cast<std::ptrdiff_t>(found),
              [](const View0& a, const View0& b) { return a.frame < b.frame; });
    return found;
}

bool FrameSubmits::holds(VkCommandBuffer cb) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [cb](const Entry& e) { return e.cb.load(std::memory_order_relaxed) == cb; });
}

void FrameSubmits::begun(VkCommandBuffer cb) {
    for (Entry& e : entries_) {
        if (e.cb.load(std::memory_order_relaxed) == cb) {
            e.cb.store(VK_NULL_HANDLE,
                       std::memory_order_relaxed); // never submitted: V0Never once view 1's is
        }
    }
}

const FrameSubmits::Entry* FrameSubmits::find(std::uint64_t frame) const {
    const Entry& e = entries_[frame % kKept];
    return frame != 0 && e.frame == frame ? &e : nullptr;
}

char FrameSubmits::view0(std::uint64_t frame) const {
    const Entry* e = find(frame);
    return !e ? '?' : e->call0 != 0 ? 's' : 'w';
}

char FrameSubmits::order(std::uint64_t frame) const {
    const Entry* e = find(frame);
    if (!e || e->call0 == 0 || e->call1 == 0) {
        return '?';
    }
    return e->call0 < e->call1 ? 'b' : e->call0 == e->call1 ? 'w' : 'a';
}

void FrameSubmits::classify(const Entry& e, Counters& count) const {
    if (e.call0 != 0 && e.call1 != 0) {
        ++count[e.call0 < e.call1 ? Count::V0Before : e.call0 == e.call1 ? Count::V0With : Count::V0After];
    }
}

void FrameSubmits::reset() {
    for (Entry& e : entries_) {
        e.frame = 0;
        e.cb.store(VK_NULL_HANDLE, std::memory_order_relaxed);
        e.call0 = 0;
        e.call1 = 0;
    }
    newest1_ = 0;
}

VkImage frameOf(VkCommandBuffer cb, bool* released) {
    std::lock_guard lock(g_framesMutex);
    for (const FrameOf& f : g_frames) {
        if (f.cb.load(std::memory_order_relaxed) == cb) {
            if (released) {
                *released = f.released;
            }
            return f.frame;
        }
    }
    return VK_NULL_HANDLE;
}

void noteBegin(VkCommandBuffer cb) {
    if (g_submits.holds(cb)) {
        std::lock_guard lock(mutex());
        g_submits.begun(cb);
    }
    if (std::none_of(g_frames.begin(), g_frames.end(),
                     [cb](const FrameOf& f) { return f.cb.load(std::memory_order_relaxed) == cb; })) {
        return;
    }
    std::lock_guard lock(g_framesMutex);
    for (FrameOf& f : g_frames) {
        if (f.cb.load(std::memory_order_relaxed) == cb) {
            f.cb.store(VK_NULL_HANDLE, std::memory_order_relaxed);
            f.frame = VK_NULL_HANDLE;
        }
    }
}

void noteBarrier(VkCommandBuffer cb, const VkImageMemoryBarrier& barrier) {
    if (barrier.newLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        return;
    }
    const bool released = barrier.srcQueueFamilyIndex != barrier.dstQueueFamilyIndex &&
                          barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
                          barrier.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED;
    std::lock_guard lock(g_framesMutex);
    for (FrameOf& f : g_frames) {
        if (f.cb.load(std::memory_order_relaxed) == cb) {
            f.frame = barrier.image;
            f.released = released;
            return;
        }
    }
    FrameOf& f = g_frames[g_framesNext++ % g_frames.size()];
    f.cb.store(cb, std::memory_order_relaxed);
    f.frame = barrier.image;
    f.released = released;
}

} // namespace evr::vkcore::view_snapshot
