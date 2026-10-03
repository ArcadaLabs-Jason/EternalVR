#pragma once

// What the per-eye hook writes into each eye's render view under Route S (docs/VR_STEREO.md,
// docs/rig-findings/stereo-routes.md section 2.5). The hook runs after the engine copied the game's
// head-centred renderView_t into the render view and before the latch, once per eye chain.

#include "stereo_seq/eye_tags.hpp"

namespace evr::stereo_seq {

struct SeqViewSettings {
    bool sameView = false;       // ETERNALVR_STEREO_SAME_VIEW: both eyes keep the game's view (S1)
    bool fullResolution = true;  // renderView_t.forceFullResolution (+0x11) on both eyes
    bool exposureOnce = true;    // renderView_t.skipAutoExposureUpdate (+0x74C) on eye R
    bool discontinuous = false;  // ETERNALVR_STEREO_DISCONTINUOUS: +0xC on both eyes (S5)
    bool inhibitModelFov = true; // ETERNALVR_STEREO_INHIBIT_MODEL_FOV: +0x13 with the eye's projection
};

// Fields left false are not written (the engine's own value from the game's view stays).
struct EyeViewPlan {
    bool writePose = false;                 // vieworg + eye offset, viewaxis, explicit projection
    bool forceFullResolution = false;       // set to 1
    bool skipAutoExposureUpdate = false;    // set to 1
    bool discontinuousViewPosition = false; // set to 1
    bool inhibitModelFovScale = false;      // set to 1
};

// `exposureHeld`: the auto-exposure index hook gives eye R eye L's exposure this render
// (stereo_seq::exposureIndexHeld). Eye R skips its exposure update only then: without the hook eye L's
// backend frame parity is constant and its exposure would stop adapting, so both eyes update and form one
// chain (docs/rig-findings/stereo-temporal.md 3.3).
EyeViewPlan planEyeView(Eye eye, const SeqViewSettings& settings, bool exposureHeld);

} // namespace evr::stereo_seq
