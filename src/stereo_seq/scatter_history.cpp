#include "stereo_seq/scatter_history.hpp"

#include <cstring>

namespace evr::stereo_seq {

void ScatterHistory::reset(const std::array<ScatterPair, 2>& engine, const std::array<ScatterPair, 2>& eyeR) {
    eyes_ = {};
    eyes_[0].pairs = engine;
    eyes_[1].pairs = eyeR;
    // Each eye clears both of its pairs once: eye R's images are new, and eye L's hold what the engine
    // last filtered, before the eyes had pairs of their own.
    eyes_[0].clears = 2;
    eyes_[1].clears = 2;
    active_ = 0;
    ready_ = engine[0].packed0 && engine[0].packed1 && engine[1].packed0 && engine[1].packed1 &&
             eyeR[0].packed0 && eyeR[0].packed1 && eyeR[1].packed0 && eyeR[1].packed1;
}

void ScatterHistory::invalidate() {
    eyes_[0].clears = 2;
    eyes_[1].clears = 2;
}

ScatterPlan ScatterHistory::beforeRender(Eye eye, std::uint32_t counter, const ScatterState& current) {
    ScatterPlan plan;
    const int e = eye == Eye::Right ? 1 : 0;
    PerEye& p = eyes_[e];
    if (e != active_) {
        eyes_[active_].state = current;
        // The eye's first render takes the other eye's state (then with no last frame, below).
        ScatterState load = p.state ? *p.state : current;
        // The engine resets the filter when the last frame is not the render just before; an eye's own last
        // render is always two back (the other eye's in between), so it is passed off as the previous one.
        std::uint32_t last = 0;
        std::memcpy(&last, load.data() + kScatterLastFrame, sizeof(last));
        if (last != 0) {
            last = counter - 1u;
            std::memcpy(load.data() + kScatterLastFrame, &last, sizeof(last));
        }
        plan.load = load;
        active_ = e;
    }
    if (p.clears > 0) {
        // No last frame: the engine clears the pair this render reads before it reads it.
        ScatterState fresh = plan.load ? *plan.load : current;
        std::memset(fresh.data() + kScatterLastFrame, 0, sizeof(std::uint32_t));
        plan.load = fresh;
        --p.clears;
    }
    const int read = p.written < 0 ? 0 : p.written;
    const int write = read ^ 1;
    const unsigned parity = counter & 1u;
    plan.slots[parity] = p.pairs[write];
    plan.slots[parity ^ 1u] = p.pairs[read];
    p.written = write;
    return plan;
}

} // namespace evr::stereo_seq
