#include "vkcore/runtime_cvars_pe.hpp"

#include "stereo_seq/seq_settings.hpp"
#include "vkcore/log.hpp"
#include "vkcore/parallel_eyes_settings.hpp"
#include "vkcore/runtime_cvars_impl.hpp"
#include "vkcore/taa_hooks.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace evr::vkcore::runtime_cvars {

namespace {

// Set by setParallelEyes, before the first apply; under heldMutex().
bool g_parallelEyes = false;
ParallelEyesHolds g_parallelHolds;

} // namespace

// Parallel Eye Rendering's set, held from its first present (presenter_copy.cpp), on every frame:
// - r_useNewDepthDownscale 0. With the new depth downsample (0x1C73530) view 1's light binning got wrong tile
//   depth bounds in e1m3: its light lists drew tile-shaped black holes over the near floor (rig runs cum2,
//   cnd1); the old downsample (0x1C73780) leaves both views right. Registered with flags 0x1, as
//   r_skipFlares: the game never saves either (no 0x10000 / 0x20000 flag), so the launcher has nothing to
//   restore.
// - Route S's window and present set, so a load path that applies the player's video mode cannot take the
//   eyes to the display's size (as in Route S).
// - Route S's comfort set (stereo_seq::stereoComfortCvars): without it the game's low-health damage view
//   effect left both eyes red after a death and checkpoint reload (rig runs pe1 and crt1), which Route
//   S never shows because it holds view_skipDamageEffect and the rest.
// - The launcher's anti-aliasing (parallel_eyes::antiAliasingCvars): TAA, DLSS (TAA while it has fallen back,
//   view_dlss.hpp), or with Off the stereo path's r_TAASafeMode 1 and r_antialiasing 0 (the game applies the
//   player's own mode after the command line).
//   ETERNALVR_STEREO_RUNTIME_CVARS=0 leaves it as the game has it, as Route S's stereo set (rig experiments).
// - r_SSR 0 with the launcher's Screen-space reflections Off (ETERNALVR_STEREO_SSR=off); otherwise the game's
//   own setting stays (Route S's per-eye TAA holds it in taa_hooks.cpp).
// - r_raytracedReflectionsTemporalUpscaleQuality 0 (not with ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0): the
//   reflections read no history, as under Route S (runtime_cvars_pe.hpp).
// An ETERNALVR_DEBUG_CVARS entry for one of these cvars wins; a CPU Saver item for one is left out, as in
// Route S.
void addParallelEyeSet(std::vector<Held>& held) {
    if (!g_parallelEyes) {
        return;
    }
    held.push_back(Held{"r_useNewDepthDownscale", "0", true, false});
    held.back().parallel = true;
    // Flare models are projected once a frame, with the game's own centred camera, into one vertex block both
    // views draw: the same screen spot in both eyes, right in neither (and the same occlusion queries issued
    // twice in a frame). Off until each eye projects its own.
    held.push_back(Held{"r_skipFlares", "1", true, false});
    held.back().parallel = true;
    if (g_parallelHolds.antiAliasingHeld) {
        for (const auto& c : parallel_eyes::antiAliasingCvars(g_parallelHolds.antiAliasingOff,
                                                              g_parallelHolds.dlss, taaDlssQuality())) {
            held.push_back(Held{std::string(c.name), std::string(c.value), true, false});
            held.back().parallel = true;
            held.back().viewDlss = g_parallelHolds.dlss;
        }
    } else {
        EVR_LOG("%s: Parallel Eye Rendering's anti-aliasing is left as the game has it "
                "(ETERNALVR_STEREO_RUNTIME_CVARS=0)",
                kTag);
    }
    addWindowSet(held, true);
    for (const auto& c : stereo_seq::stereoComfortCvars()) {
        held.push_back(comfortHeld(c));
        held.back().parallel = true;
    }
    const std::string ssr = narrowEnv(L"ETERNALVR_STEREO_SSR");
    if (stereo_seq::stereoSsrCvar(ssr) && !stereo_seq::stereoSsrFollowsGame(ssr)) {
        held.push_back(Held{"r_SSR", "0", true, false});
        held.back().parallel = true;
    }
    if (g_parallelHolds.rtUpscaleHeld) {
        held.push_back(Held{"r_raytracedReflectionsTemporalUpscaleQuality", "0", true, false});
        held.back().parallel = true;
    }
}

void setParallelEyes(const ParallelEyesHolds& holds) {
    std::lock_guard lock(heldMutex());
    if (applied()) {
        EVR_LOG("%s: Parallel Eye set asked for after the first apply; not held", kTag);
        return;
    }
    g_parallelEyes = true;
    g_parallelHolds = holds;
}

} // namespace evr::vkcore::runtime_cvars
