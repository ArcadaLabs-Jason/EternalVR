#pragma once

// CPU particles and effects in both eyes of a Route S tick (stereo_seq/fx_sync.hpp,
// docs/rig-findings/stereo-fx-lag.md; Steam build 25216728). On by default; ETERNALVR_STEREO_FX_SYNC=0
// patches nothing, =count only counts.
//
// The world's per-render prepare (0x18E78D0) advances the particle vertex ring (call 0x1A0F5D0 at RVA
// 0x18E791C) and resets the particle light pool's count (call 0x1953D80 at RVA 0x18E7ABD); the particle
// update (0x1955150) and the effect update (0x19511E0) then bind the vertices the render before generated,
// the particles' lights with them, and generate new ones (call 0x1953D90 at RVA 0x195526F; from RVA
// 0x195166E). In eye R's render the hooks keep the ring where eye L left it (its previous slot's pointer
// reopened, as the advance would have) and the pool's count with it, and skip the generation of every model
// eye L generated in the tick, so eye R draws what eye L bound and shows eye L's particle lights. A model eye
// L did not generate runs the engine's code.
//
// Located by signature and cross-checked; anything missing installs nothing and logs why. All four hooks act
// or none does.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installFxSyncHooks();

} // namespace evr::vkcore
