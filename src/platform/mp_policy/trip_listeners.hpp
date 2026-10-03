#pragma once

// The functions the multiplayer guard calls when it trips (vkcore/mp_guard.hpp,
// docs/rig-findings/mp-guard.md). Portable so the once-only rule can be tested without the game.
//
// A fixed list of kCapacity slots, lock-free: add() claims the next slot with an atomic counter and publishes
// its function, fire() marks the list fired and calls every function published so far. Each slot has its own
// "called" flag, taken with an atomic exchange, so a listener runs exactly once even when add() and fire()
// race on two threads: the publication and the fired flag are sequentially consistent, so at least one of
// the two sees the other, and the flag lets only one of them call. Listeners are never removed.

#include <array>
#include <atomic>
#include <cstddef>

namespace evr::mp_policy {

class TripListeners {
public:
    using Listener = void (*)();
    static constexpr std::size_t kCapacity = 8;

    // Adds `listener`. Once fire() has been called it is called at once, on this thread. False (not added,
    // not called) when the list is full or `listener` is null.
    bool add(Listener listener);

    // Calls every listener added so far, once each, in the order added, on this thread; a listener added
    // later is called by add(). Calling it again calls nothing twice.
    void fire();

    [[nodiscard]] bool fired() const { return fired_.load(); }
    [[nodiscard]] std::size_t size() const { return claimed_.load(); }

private:
    void callOnce(std::size_t slot);

    std::atomic<std::size_t> claimed_{0};
    std::array<std::atomic<Listener>, kCapacity> slots_{};
    std::array<std::atomic<bool>, kCapacity> called_{};
    std::atomic<bool> fired_{false};
};

} // namespace evr::mp_policy
