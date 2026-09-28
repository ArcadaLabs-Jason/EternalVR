#include "vkcore/window_timing.hpp"

#include <windows.h>

#include <atomic>

namespace evr::vkcore::window_timing {

namespace {

struct AtomicCalls {
    std::atomic<std::uint64_t> count{0};
    std::atomic<std::uint64_t> micros{0};
    std::atomic<std::uint64_t> maxMicros{0};

    void add(std::uint64_t us) {
        count.fetch_add(1, std::memory_order_relaxed);
        micros.fetch_add(us, std::memory_order_relaxed);
        std::uint64_t seen = maxMicros.load(std::memory_order_relaxed);
        while (us > seen && !maxMicros.compare_exchange_weak(seen, us, std::memory_order_relaxed)) {
        }
    }
    Calls take() {
        Calls c;
        c.count = count.exchange(0, std::memory_order_relaxed);
        c.micros = micros.exchange(0, std::memory_order_relaxed);
        c.maxMicros = maxMicros.exchange(0, std::memory_order_relaxed);
        return c;
    }
};

AtomicCalls g_acquire;
AtomicCalls g_present;

} // namespace

void addAcquire(std::uint64_t micros) {
    g_acquire.add(micros);
}

void addPresent(std::uint64_t micros) {
    g_present.add(micros);
}

Totals take() {
    return Totals{g_acquire.take(), g_present.take()};
}

std::uint64_t nowMicros() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<std::uint64_t>(t.QuadPart / frequency * 1000000 +
                                      t.QuadPart % frequency * 1000000 / frequency);
}

} // namespace evr::vkcore::window_timing
