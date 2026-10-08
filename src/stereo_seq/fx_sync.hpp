#pragma once

// CPU particles and effects in both eyes of a Route S tick (docs/rig-findings/stereo-fx-lag.md).
//
// CPU particles and effects (sprites, smoke, sparks, blood) go through a ring of three vertex slots. Each
// render binds the vertices the render before it generated (the previous slot), then steps the simulation and
// generates new vertices into the current slot for the render after it. In mono that is a one-frame lag
// nobody sees. Under Route S eye L's render of a tick drew what eye R's render of the tick before generated,
// and eye R's drew eye L's of the same tick: eye L was one tick behind on every particle. Eye R now leaves
// the ring, the particle light pool and the generation of what eye L generated to eye L: it draws the slot
// eye L drew, with eye L's binding and lights, so both eyes show the tick before, as mono does.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::stereo_seq {

enum class FxSyncMode {
    Off,   // nothing is patched: eye L is a tick behind eye R on CPU particles and effects
    Count, // the hooks only count what On would do
    On,    // eye R reuses eye L's particle ring, light pool and generation
};

// ETERNALVR_STEREO_FX_SYNC: "1"/"on"/"true"/"yes" On, "count" Count, anything else (or unset) Off. Off by
// default since 0.1.35: in 0.1.34 eye R drew snow and some effects as tiles of their whole sprite sheet in
// e1m3 (a player's headset captures), which the storm deck A/B never showed.
FxSyncMode fxSyncMode(std::string_view value);

enum class FxAction {
    Run,        // the engine's code
    RunCounted, // the engine's code, counted as one On would change (Count)
    UseEyeL,    // eye R: the ring stays where eye L left it (its previous slot reopened), no generation
};

// The render's answer, the same at every site (the ring's advance, the light pool's reset, the particle and
// the effect generation). Only eye R of a Route S tick (the render the wrapper nests after eye L's) changes,
// and only while the multiplayer guard lets the layer touch the game.
FxAction fxActionFor(FxSyncMode mode, Eye eye, bool gameTouchAllowed);

// The particle light pool's reset (its count to 0) in the world's prepare. The pool is filled only by the
// bind, which eye R skips for the models eye L generated, and the show pass after the updates hides every
// pooled light past the count: the reset follows the ring's advance in the same prepare, so a kept ring keeps
// eye L's count and eye R shows eye L's lights. nullopt: no ring hook acted in this prepare (a world without
// a ring, or the hooks not live yet): the engine's reset.
FxAction fxPoolResetFor(std::optional<FxAction> ringAdvance);

// One particle system's or effect's generation in a render whose answer is `render`, from the model's stamp
// (the ring frame of its last generation) and the ring's frame now. Eye R uses eye L's work only for a model
// eye L generated in this tick: with the ring kept that is a stamp equal to the ring's frame; counting, eye
// R's render advanced the ring, so one below it. Any other model (one eye L did not have in its list) runs
// the engine's code: eye R binds it when it was generated the render before (adding its lights after eye L's)
// and generates it, and eye L's next render binds that.
FxAction fxGenerationFor(FxAction render, std::uint32_t stamp, std::uint32_t ringFrame);

// The ring's previous slot from its current index, as the engine computes it (0x18E7928, 0x1A0F580):
// (index + 2) % 3.
std::int32_t fxPreviousSlot(std::int32_t index);

} // namespace evr::stereo_seq
