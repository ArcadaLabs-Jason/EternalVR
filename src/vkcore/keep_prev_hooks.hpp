#pragma once

// Eye R keeps the previous model matrix eye L's commit set (stereo_seq/keep_prev.hpp,
// docs/rig-findings/stereo-moved-flag.md; Steam build 25216728). On by default; ETERNALVR_STEREO_KEEP_PREV=0
// turns it off, =count only counts. Only with per-eye TAA or DLSS (anti-aliasing on).
//
// The commit's model-matrix step (0x1C8ADE0) copies the current model matrix over the previous one (four
// movups from [rsi] to [rdi + r14], the first at RVA 0x1C8AE60), then calls 0x399EB0 for the new current one
// (the call at RVA 0x1C8AEA7). A hook on the first copy stamps the entity (rbx) in eye L and, in eye R, saves
// the previous matrix of an entity eye L committed in the same tick; a hook after the call (RVA 0x1C8AEAC)
// puts it back. The copy that follows when the entity does not interpolate (0x1C8AEB3) is left alone.
//
// With alternate eyes (ETERNALVR_ALTERNATE_EYES=1 or auto) the same two hooks instead leave every commit's
// previous matrix at the entity's current matrix of two renders back, each eye's own last render
// (stereo_seq/alternate_prev.hpp; docs/rig-findings/alternate-eye.md section 9.1).
//
// Located by signature and cross-checked; anything missing leaves the game untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installKeepPrevHooks();

} // namespace evr::vkcore
