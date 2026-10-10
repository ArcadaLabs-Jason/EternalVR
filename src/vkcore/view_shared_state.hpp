#pragma once

// Parallel Eye Rendering: the engine state both views share, where view 1 takes what view 0 made this frame:
// its compute skinning output room from view 0's counter (view_skin_alloc.hpp), view 0's shadow cache
// entries (view_shadow_cache.hpp), and the occlusion queries' copy without the wait (view_query_copy.hpp).
// Installed by view_redirects.cpp. The water's start from view 0's (view_water.hpp) is installed by
// view_install.cpp and told of each dispatch here.

#include <cstddef>

namespace evr::vkcore {

// Checks every site's bytes; false (logged) when one is not as known, before anything changes.
bool prepareViewSharedState(const std::byte* base);

// The hooks (inert until Parallel Eye Rendering is on); false if one fails.
bool installViewSharedState(const std::byte* base);

// At the start of each two-view dispatch: whether view 0 is dispatched this frame (ETERNALVR_TEST_VIEW_ONLY=1
// renders view 1 alone; view 1 then takes nothing from view 0).
void viewSharedStateFrameStart(bool view0Dispatched);

// The counts for the dispatcher's periodic line.
void viewSharedStateLogCounts();

} // namespace evr::vkcore
