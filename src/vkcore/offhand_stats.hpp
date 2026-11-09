#pragma once

// The free off hand's 10 s line in the log (features/input/offhand_watch.hpp), on by default: "offhand: last
// 10 s: ..." with the arm policy's decisions and reasons, the layer's share, the ticks refused, the off
// hand's tracking and how far from where it was meant the glove was drawn (features/arm/drawn_wrist.hpp). A
// line only for 10 s in which the player's hands ticked with the weapon in the right hand; nothing per frame.
//
// Threads: the viewmodel hook notes each placement (under a lock of this file's); everything else runs on the
// off-hand hook's thread.

#include "features/input/offhand_watch.hpp"
#include "vkcore/controllers_impl.hpp"

#include <cstdint>

namespace evr::vkcore::controllers::offhand_stats {

// Viewmodel hook, after it placed the arms model: the placement of this game frame.
void notePlacement(Vec3 eye, const xr_math::EyeRelativePose& placed);

// Off-hand hook, after the layer posed the free off hand this tick: the game's read of the wrist this tick
// (before the write) and the wrist written (model space, game units), the placement it was posed under, and
// the stamps of the hands snapshot it was posed from and of the one that placement came from.
struct Posed {
    Vec3 readWrist;
    Vec3 writtenWrist;
    ModelPlacement placement;
    LONGLONG worldQpc = 0;
    float unitsPerMetre = 1.0f;
};
void notePosed(const Posed& posed);

// Off-hand hook, once per tick of the player's hands after the arm was posed or given back: the tick, the eye
// (the player's first-person origin) and the hook's refused count so far. Writes the line every 10 s.
void tick(const input::OffhandWatchTick& tick, Vec3 eye, std::uint64_t refused);

} // namespace evr::vkcore::controllers::offhand_stats
