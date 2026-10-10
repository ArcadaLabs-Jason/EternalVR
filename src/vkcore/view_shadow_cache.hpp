#pragma once

// Parallel Eye Rendering: view 1's shadow setup takes the shadow cache entries view 0 made and leaves the
// cache's frame and view 0's entries alone.
//
// Each view's shadow setup job (0x1CF0240, a node of the render-view job graph built by 0x1CFA640; rcx = the
// view's light block, render context + 0x5226F8) reads back the view's visible-light bits, picks each
// shadowed light's shadow map level and finds or makes its entry in the shadow atlas cache: the cache's begin
// (0x1D058B0, 0x1D05A30) and frame count (+0x30, incremented at 0x1CF02B9), lookup 0x1D05A40, allocation
// 0x1D05220, and release of the light's entries at the other levels 0x1D05BF0. The cache is one engine object
// ([0x66E2E88], the light block's +0x40 for both views). View 1 renders no shadow map tiles (it shades with
// view 0's atlas, view_one_passes.cpp), yet its setup released view 0's entries wherever the eyes picked
// other levels and counted every frame twice: both eyes lit darker (rig, e1m1_intro still recipe, mean
// luminance against Route S: eye L 0.86, eye R 0.82; 0.95 and 0.96 with this).
//
// While view 1's setup runs (a thread-local mark around the job), the cache's begin and frame count are left
// out, and so are the releases the setup and its record function 0x1CFE2B0 make of a light's entries; a
// lookup that misses tries the light's other levels, nearest first: view 1 takes the entry view 0 made for
// the light. Only a light view 0 has no entry for gets one of its own, and that allocation may release an
// older entry no light used this frame when the atlas is full, as the game's does. View 1's setup waits for
// view 0's (at most 2 ms) and the two run under one lock. Only in frames that dispatch view 0. Installed by
// view_shared_state.cpp.

#include <cstddef>

namespace evr::vkcore {

// Checks every hooked site's bytes; false (logged) when one is not as known, before anything changes.
bool prepareViewShadowCache(const std::byte* base);

// The hooks (inert until Parallel Eye Rendering is on); false if one fails.
bool installViewShadowCache(const std::byte* base);

// At the start of each two-view dispatch: view 0's setup has not run yet, and whether view 0 is dispatched.
void viewShadowCacheFrameStart(bool view0Dispatched);

// The counts for the dispatcher's periodic line.
void viewShadowCacheLogCounts();

} // namespace evr::vkcore
