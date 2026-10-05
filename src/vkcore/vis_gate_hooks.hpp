#pragma once

// The first-visible gate widened to either eye's last render under Route S
// (docs/rig-findings/stereo-visibility-counter.md; RVAs in Steam build 25216728). On by default;
// ETERNALVR_STEREO_VIS_GATE=0 turns it off.
//
// The renderer adds a non-static model to a view only after it has been counted in more consecutive renders
// of that view slot than its firstVisibleFrameCount (default 2). Both Route S eyes render view slot 0, so
// each render's previous one is the other eye's, and a model in one eye's frustum only (a pickup in eye R's
// outer strip) restarts its count on every render and is never drawn. The three gates (main gather 0x1C76C80
// at RVA 0x1C775E4, second gather 0x1C78D50 at 0x1C79131 and 0x1C7943C) load the view's render counter and
// compare the model's lastVisible stamp with it; a mid hook on each load does that compare itself with
// stereo_seq::visGateContinues over the last two renders and resumes on the engine's own continue or restart
// path, so the engine's stores and the counter are unchanged.
//
// Only models are widened, never effects (model type 0-3: particles, flares, beams, ribbons). A flare one eye
// drew left the other eye's occlusion query copy waiting on queries its render never issued (the queue
// stalled for good); 0.1.22 copied without the wait instead, and a flare could then read another flare's
// stale count and flash at full brightness (issue #18). Effects one eye sees stay unseen there, as before
// 0.1.22.
//
// The three sites are located by signature (Steam and Store builds); a miss leaves the game untouched and
// logs why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installVisGateHooks();

} // namespace evr::vkcore
