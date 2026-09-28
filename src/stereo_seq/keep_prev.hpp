#pragma once

// Eye R keeps the previous model matrix eye L's commit set (docs/rig-findings/stereo-moved-flag.md, "Eye R's
// previous model matrix").
//
// The world's commit of an entity (0x1C8ADE0, from the commit 0x18D9FA0) first copies the entity's current
// model matrix over its previous one, then computes the new current one. In mono that runs once per game
// frame, so previous-to-current is one frame of motion. Under Route S an entity the engine commits again in
// eye R's world frame (status 2: render-time transforms, geometry caches) runs it a second time in the same
// tick: its previous becomes eye L's current, the motion eye R's TAA reprojects with is zero, and the demon
// smears in eye R. For an entity eye L committed in the same tick, eye R puts back the previous matrix eye L
// set; an entity eye R commits on its own keeps the engine's behaviour.

#include "stereo_seq/eye_tags.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace evr::stereo_seq {

enum class KeepPrevMode {
    Off,   // the engine's behaviour: eye R's recommit sets previous = current
    Count, // as Off, and the recommits are counted
    On,    // eye R keeps eye L's previous matrix for entities eye L committed in the same tick
};

// ETERNALVR_STEREO_KEEP_PREV: "0"/"off"/"false"/"no" Off, "count" Count, anything else (or unset) On.
KeepPrevMode keepPrevMode(std::string_view value);

// Numbers the stereo pairs so eye R can tell whether eye L committed an entity in the same tick. Eye L stamps
// each entity it commits with leftStamp(); eye R asks rightPair(tick) for the stamp eye L used in that tick.
// Stamps start at 1, so a zeroed stamp table matches nothing. Safe to call from several threads: a caller
// racing a new tick may see the previous pair's number, which only means no keep (the engine's behaviour).
class PairClock {
public:
    std::uint32_t leftStamp() const { return next_.load(std::memory_order_acquire); }
    std::uint32_t rightPair(std::uint64_t tick);

private:
    std::atomic<std::uint64_t> lastTick_{~std::uint64_t{0}};
    std::atomic<std::uint32_t> next_{1};
    std::atomic<std::uint32_t> right_{0};
};

// Whether eye R's recommit of an entity stamped `leftStamp` puts back the previous matrix: mode On, eye R,
// and eye L committed the entity in this pair (`pair`, from PairClock::rightPair).
bool keepPrevious(KeepPrevMode mode, Eye eye, std::uint32_t leftStamp, std::uint32_t pair);

} // namespace evr::stereo_seq
