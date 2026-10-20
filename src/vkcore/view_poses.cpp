// Motion controllers: the view's hand poses as a tick of the game reads them (controllers_impl.hpp,
// features/input/view_pose_clock.hpp).

#include "vkcore/controllers_impl.hpp"

#include <windows.h>

#include <cstdint>
#include <mutex>

namespace evr::vkcore::controllers {

namespace {

LONGLONG countsOf(double seconds) {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return static_cast<LONGLONG>(static_cast<double>(frequency.QuadPart) * seconds);
}

LONGLONG staleCounts() {
    static const LONGLONG counts = countsOf(kWorldStaleSeconds);
    return counts;
}

LONGLONG capCounts() {
    static const LONGLONG counts = countsOf(kWorldHitchCapSeconds);
    return counts;
}

TickView readLocked(const State& s, LONGLONG now) {
    TickView v;
    v.world = s.world;
    v.model = s.model;
    v.worldCurrent = v.world.valid && s.viewClock.current(v.world.qpc, now, staleCounts(), capCounts());
    v.modelCurrent = v.model.valid && s.viewClock.current(v.model.qpc, now, staleCounts(), capCounts());
    return v;
}

} // namespace

TickView tickView() {
    State& s = state();
    const LONGLONG now = nowQpc();
    std::lock_guard lock(s.viewMutex);
    return readLocked(s, now);
}

input::ViewClockRead viewClockRead() {
    State& s = state();
    const LONGLONG now = nowQpc();
    std::lock_guard lock(s.viewMutex);
    return {s.viewClock, now, staleCounts(), capCounts()};
}

TickView handsTick() {
    State& s = state();
    const LONGLONG now = nowQpc();
    std::lock_guard lock(s.viewMutex);
    s.viewClock.tick(now);
    return readLocked(s, now);
}

} // namespace evr::vkcore::controllers
