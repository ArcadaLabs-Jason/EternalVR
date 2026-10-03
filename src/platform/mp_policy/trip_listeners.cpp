#include "platform/mp_policy/trip_listeners.hpp"

namespace evr::mp_policy {

bool TripListeners::add(Listener listener) {
    if (!listener) {
        return false;
    }
    std::size_t slot = claimed_.load();
    do {
        if (slot >= kCapacity) {
            return false;
        }
    } while (!claimed_.compare_exchange_weak(slot, slot + 1));
    // Published before the fired flag is read; fire() sets the flag before it reads the slots (both
    // sequentially consistent), so either fire() sees this listener or this call sees the flag.
    slots_[slot].store(listener);
    if (fired_.load()) {
        callOnce(slot);
    }
    return true;
}

void TripListeners::fire() {
    fired_.store(true);
    for (std::size_t slot = 0; slot < kCapacity; ++slot) {
        callOnce(slot);
    }
}

void TripListeners::callOnce(std::size_t slot) {
    const Listener listener = slots_[slot].load();
    if (listener && !called_[slot].exchange(true)) {
        listener();
    }
}

} // namespace evr::mp_policy
