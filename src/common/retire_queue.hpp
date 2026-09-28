#pragma once

// Objects that may still be in use by queued GPU work, destroyed once a later point on a monotonic
// counter (a timeline semaphore's value) has been reached.
//
// The presenter retires the present semaphores of a destroyed swapchain, and a semaphore whose present
// failed, this way (T-081: destroyed only after a later fence on the queue has signalled), instead of
// keeping every one of them until the device goes away.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace evr {

template <typename T>
class RetireQueue {
public:
    // `item` may be destroyed once the counter has reached `safeAt`.
    void retire(T item, std::uint64_t safeAt) { entries_.push_back({std::move(item), safeAt}); }

    // Calls destroy(item) for every item whose point the counter has reached (`completed`); returns how
    // many were destroyed. The others keep their order.
    template <typename Destroy>
    std::size_t collect(std::uint64_t completed, Destroy&& destroy) {
        std::size_t kept = 0;
        std::size_t destroyed = 0;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].safeAt <= completed) {
                destroy(entries_[i].item);
                ++destroyed;
            } else {
                if (kept != i) {
                    entries_[kept] = std::move(entries_[i]);
                }
                ++kept;
            }
        }
        entries_.resize(kept);
        return destroyed;
    }

    // Destroys everything (the device is idle).
    template <typename Destroy>
    void drain(Destroy&& destroy) {
        for (Entry& e : entries_) {
            destroy(e.item);
        }
        entries_.clear();
    }

    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    [[nodiscard]] bool empty() const { return entries_.empty(); }

private:
    struct Entry {
        T item;
        std::uint64_t safeAt;
    };
    std::vector<Entry> entries_;
};

} // namespace evr
