#pragma once

#include "vkcore/code_ranges.hpp"

#include <cstddef>
#include <vector>

// Parallel Eye Rendering's second set of command contexts (docs/rig-findings/perf-multiview-slots.md section
// 10). The engine records every view into one table of command contexts (13 categories, up to 4 slots each)
// and resets them at the start of each view, so two views cannot record at once. The resource-state tracker
// is keyed by a context's category and slot (at most 4), so view 1's contexts live in the same table, in the
// free slot 1 of the per-view categories that have one context: we build them and raise the category counts,
// and the engine then makes their command buffers and begins, ends, chains and submits them with its own.

namespace evr::vkcore {

// From the game's vkCreateInstance (after the renderer built its table, before its command pools), in two
// steps so nothing changes until every step of the install has been checked:
// - prepareViewContexts checks the count tables and the table's slots and takes the contexts' memory; it
//   adds the count tables to `writes` (view_install.cpp makes them writable). False, with nothing of the
//   game changed, when one is not as known.
// - installViewContexts builds view 1's contexts into slot 1 and raises the counts (the tables writable); it
//   cannot fail.
bool prepareViewContexts(const std::byte* base, std::vector<CodeRange>& writes);
void installViewContexts();

} // namespace evr::vkcore
