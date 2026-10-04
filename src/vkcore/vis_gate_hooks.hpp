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
// Two companions go in with the gates, or nothing does. Particles (UpdateInView, 0x195523D) simulate only
// when updated in the previous render, which is always the other eye's: the check is widened to the last two
// renders too, so an effect one eye sees is simulated. And the copies of the occlusion queries' results
// (0x1C32FAF, 0x1C33039) are made without VK_QUERY_RESULT_WAIT_BIT: once a one-eye model is drawn, a copy
// waits on queries its render never issued and the game's queue stalls for good (as under Parallel Eye
// Rendering); without the wait, unfinished queries keep their previous results.
//
// All six sites are located by signature (Steam and Store builds); a miss leaves the game untouched and logs
// why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installVisGateHooks();

} // namespace evr::vkcore
