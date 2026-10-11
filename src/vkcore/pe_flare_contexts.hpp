#pragma once

// Lens flares per view under Parallel Eye Rendering (pe_flares.hpp): which view a command context belongs to.
// The game's command context table (pe_flare_sites::kContextTable) holds 13 categories of 4 slots; the
// per-view categories with one context have view 1's in slot 1, the three with four have view 1's in slots 2
// and 3, as view_redirects.cpp splits them. The cells are read once at the install and compared again at
// every view 0 latch.

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::pe_flare_contexts {

inline constexpr int kTableSlots = 4;

// Reads each view's cells from the table at `base` (the game's image). False when a view's cell is empty.
bool capture(const std::byte* base);

// The view (0 or 1) whose command context `context` is, or -1: any per-view category (the binds), the
// occlusion pass's category 7, the copy's category 10.
int bindView(std::uintptr_t context);
int occlusionView(std::uintptr_t context);
int copyView(std::uintptr_t context);

// The first cell (category * kTableSlots + slot) that no longer holds the context captured at the install,
// or -1 when all still do.
int changedCell(std::uintptr_t base);

} // namespace evr::vkcore::pe_flare_contexts
