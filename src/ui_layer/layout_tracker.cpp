#include "ui_layer/layout_tracker.hpp"

#include <utility>

namespace evr::ui_layer {

bool LayoutTracker::listed(Handle image) const {
    if (image == 0) {
        return false;
    }
    for (const auto& slot : fast_) {
        if (slot.load(std::memory_order_acquire) == image) {
            return true;
        }
    }
    return false;
}

bool LayoutTracker::addCandidate(Handle image) {
    std::lock_guard lock(mutex_);
    if (image == 0) {
        return false;
    }
    if (candidates_.count(image) == 0) {
        auto* free = static_cast<std::atomic<Handle>*>(nullptr);
        for (auto& slot : fast_) {
            if (slot.load(std::memory_order_relaxed) == 0) {
                free = &slot;
                break;
            }
        }
        if (!free) {
            return false;
        }
        free->store(image, std::memory_order_release);
        candidates_.insert(image);
    }
    current_.erase(image);
    return true;
}

void LayoutTracker::removeCandidate(Handle image) {
    std::lock_guard lock(mutex_);
    for (auto& slot : fast_) {
        if (slot.load(std::memory_order_relaxed) == image) {
            slot.store(0, std::memory_order_release);
        }
    }
    candidates_.erase(image);
    current_.erase(image);
    for (auto& [cb, images] : byCommandBuffer_) {
        if (images.erase(image) != 0) {
            pending_.erase({cb, image});
        }
    }
}

bool LayoutTracker::isCandidate(Handle image) const {
    std::lock_guard lock(mutex_);
    return candidates_.count(image) != 0;
}

void LayoutTracker::onBarrier(Handle commandBuffer, Handle image, std::int32_t newLayout) {
    if (!listed(image)) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (candidates_.count(image) == 0) {
        return;
    }
    if (pending_.size() >= kMaxPending) {
        pending_.clear();
        byCommandBuffer_.clear();
    }
    pending_[{commandBuffer, image}] = newLayout;
    byCommandBuffer_[commandBuffer].insert(image);
}

void LayoutTracker::onBegin(Handle commandBuffer) {
    std::lock_guard lock(mutex_);
    const auto it = byCommandBuffer_.find(commandBuffer);
    if (it == byCommandBuffer_.end()) {
        return;
    }
    for (const Handle image : it->second) {
        pending_.erase({commandBuffer, image});
    }
    byCommandBuffer_.erase(it);
}

void LayoutTracker::onExecute(Handle primary, const Handle* secondaries, std::size_t count) {
    std::lock_guard lock(mutex_);
    for (std::size_t i = 0; i < count; ++i) {
        const auto it = byCommandBuffer_.find(secondaries[i]);
        if (it == byCommandBuffer_.end()) {
            continue;
        }
        // A secondary can be executed more than once, so its own entries stay until it is re-recorded.
        for (const Handle image : it->second) {
            const auto p = pending_.find({secondaries[i], image});
            if (p != pending_.end()) {
                pending_[{primary, image}] = p->second;
                byCommandBuffer_[primary].insert(image);
            }
        }
    }
}

void LayoutTracker::onSubmit(Handle queue, const Handle* commandBuffers, std::size_t count) {
    std::lock_guard lock(mutex_);
    if (pending_.empty()) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto it = byCommandBuffer_.find(commandBuffers[i]);
        if (it == byCommandBuffer_.end()) {
            continue;
        }
        for (const Handle image : it->second) {
            const auto p = pending_.find({commandBuffers[i], image});
            if (p != pending_.end() && candidates_.count(image) != 0) {
                current_[image] = State{p->second, queue};
            }
        }
        // A command buffer may be submitted again without being re-recorded: its entries stay.
    }
}

std::optional<LayoutTracker::State> LayoutTracker::stateOf(Handle image) const {
    std::lock_guard lock(mutex_);
    const auto it = current_.find(image);
    if (it == current_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t LayoutTracker::pendingCount() const {
    std::lock_guard lock(mutex_);
    return pending_.size();
}

} // namespace evr::ui_layer
