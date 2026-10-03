#pragma once

// view_slots.cpp's part of Parallel Eye Rendering's install, which view_install.cpp runs (view_slots.hpp):
// the per-view storage hooks and the switch that turns the two-view renderer on.

#include <cstddef>

namespace evr::vkcore::view_slots {

// Checks r_maxRenderViews' load and every view slot site and takes the storage hooks' memory, nothing of the
// game changed. False when one is not as known.
bool prepareStorage(const std::byte* base);

// The view slot, occlusion and render thread hooks (the dispatcher among them), inert until activate(): a
// failure leaves the hooks installed so far inert and the game working as it was.
bool installStorageHooks();

// Before the install's first change to the engine (parallelEyesChangedEngine from then on).
void markChanged();

// Last: r_maxRenderViews 2, and the two-view renderer on (viewSlotsActive).
void activate();

// The site counts, for the install's log line.
std::size_t slotSites();
std::size_t occlusionSites();

} // namespace evr::vkcore::view_slots
