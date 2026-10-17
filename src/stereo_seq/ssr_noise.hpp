#pragma once

// The screen-space reflections' noise index under Route S (docs/rig-findings/stereo-temporal.md section 3.2a;
// vkcore/ssr_noise_hooks.hpp): the decisions that need no engine, so they are tested on every platform.
//
// The engine sets SSRParms.w to bitreverse4(counter & 15), the counter being the backend frame counter the
// render-view job read for its render, which advances once per render. In flat play that walks all 16 values,
// one per tick. Route S renders each tick twice (eye L, then eye R), so one eye only ever gets the even
// counters and the other only the odd ones (which eye gets which depends on the counter's parity at the eye
// tags' base): each eye accumulates half of the sequence, and the halves never meet. Both eyes of a tick take
// the tick's own counter instead, as the jitter phase does (stereo_taa.hpp taaSubSample), so the walk over
// ticks is the flat one, in both eyes.

#include "stereo_seq/eye_tags.hpp"

#include <cstdint>
#include <optional>

namespace evr::stereo_seq {

// The engine's index for counter `counter`: its low four bits reversed (0, 8, 4, 12, 2, ... 15).
std::uint8_t ssrNoiseIndex(std::uint32_t counter);

// The counter (0 to 15) for game frame `tick`: the tick's, or with `alternate` (one eye per tick,
// alternate_eyes.hpp) half the tick's, as alternateSubSample does for the jitter. Consecutive renders of one
// eye get consecutive values, so each eye walks all 16 over 16 of its renders; with alternate eyes an eye R
// render shares its eye L's value when eye L rendered the even tick before it, and takes the next one when a
// mono render restarted the alternation on an odd tick.
std::uint8_t ssrTickCounter(std::uint64_t tick, bool alternate);

// The counter a render's SSR noise index is made from. `tag`: the render's eye tag (looked up by the counter
// the render-view job read for it), nullopt when it has none; `alternateEyes`: ETERNALVR_ALTERNATE_EYES is on
// or auto (seqAlternateEyes); `perEyeHistory`: per-eye TAA or DLSS runs (taaPerEyeActive), as for the jitter
// phase; `engineCounter`: the counter the engine read. An eye's own view (drawsEyeView, tick known) with
// per-eye history takes ssrTickCounter of its tick (alternate when the tag's tick renders one eye only);
// everything else (mono, untagged, no view applied, no per-eye history) keeps `engineCounter`.
std::uint32_t ssrCounterFor(const std::optional<RenderTag>& tag,
                            bool alternateEyes,
                            bool perEyeHistory,
                            std::uint32_t engineCounter);

} // namespace evr::stereo_seq
