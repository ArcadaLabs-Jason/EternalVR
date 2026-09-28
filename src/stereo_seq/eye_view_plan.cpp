#include "stereo_seq/eye_view_plan.hpp"

namespace evr::stereo_seq {

EyeViewPlan planEyeView(Eye eye, const SeqViewSettings& settings) {
    EyeViewPlan plan;
    if (eye == Eye::Mono) {
        return plan;
    }
    plan.writePose = !settings.sameView;
    plan.forceFullResolution = settings.fullResolution;
    // Auto exposure adapts once per tick (in eye L's frame); eye R uses the same value.
    plan.skipAutoExposureUpdate = settings.exposureOnce && eye == Eye::Right;
    plan.discontinuousViewPosition = settings.discontinuous;
    // Only meaningful with the eye's own projection: the game's view keeps the game's weapon FOV.
    plan.inhibitModelFovScale = plan.writePose && settings.inhibitModelFov;
    return plan;
}

} // namespace evr::stereo_seq
