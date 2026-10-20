#include "vkcore/seq_eye_fixes.hpp"

#include "vkcore/bin_tile_hooks.hpp"
#include "vkcore/decal_frustum_hooks.hpp"
#include "vkcore/fx_sync_hooks.hpp"
#include "vkcore/geomcache_prev_hooks.hpp"
#include "vkcore/keep_prev_hooks.hpp"
#include "vkcore/moved_flag_hooks.hpp"
#include "vkcore/object_prev_hooks.hpp"
#include "vkcore/ssr_noise_hooks.hpp"
#include "vkcore/vis_gate_hooks.hpp"
#include "vkcore/world_gui_hooks.hpp"

namespace evr::vkcore {

void installSeqEyeFixes() {
    installBinTileHook();     // lights and decals binned in each eye's own frustum; a missing piece only logs
    installObjectPrevHooks(); // the object ring, a slot per render: eye R's objects keep their motion
    installVisGateHooks();    // models one eye sees are drawn (the first-visible gate)
    installWorldGuiHook();    // world GUIs (holograms, screens) in eye R too
    installMovedFlagHooks();  // moving objects keep their motion vectors in eye R
    installKeepPrevHooks();   // and their previous model matrix from eye L
    installGeomCachePrevHooks(); // and geometry caches (banners, hanging bodies) their previous frame
    installFxSyncHooks();        // CPU particles and effects alike in both eyes
    installSsrNoiseHooks();      // the screen-space reflections' noise index
    installDecalFrustumHook();   // decals from the frustum around the eye pair
}

} // namespace evr::vkcore
