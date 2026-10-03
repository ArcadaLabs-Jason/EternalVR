#pragma once

#include "vkcore/code_ranges.hpp"

#include <cstddef>
#include <vector>

// Parallel Eye Rendering's per-view redirects (docs/rig-findings/perf-multiview-slots.md section 10), so the
// two views' job chains can record at the same time:
// - Command contexts: view 1 records into slot 1 of the per-view categories with one context
//   (view_contexts.cpp); the three categories with four contexts (depth and occlusion, opaque, emissive and
//   blend) are split, slots 0-1 for view 0 and 2-3 for view 1, each view's work spread over its two (code
//   bytes changed: the jobs' fan-out). The engine's loads of the table and its per-view Begin Frame resets
//   are redirected by the view they work for.
// - Jobs that work in the renderer's one scratch take turns, or run for view 0 only (skinning, ray tracing
//   structures); view 1's light binning waits for view 0's (view_binning.cpp).
// Build 25216728 only; installed from the game's vkCreateInstance by view_install.cpp.

namespace evr::vkcore {

// Every hook, inert until view_slots.cpp is active and the code bytes are changed. False if a hook failed
// (the hooks installed before it stay, inert).
bool installViewRedirects(const std::byte* base);

// The code bytes (the split categories' fan-out), in two steps so nothing changes until every step of the
// install has been checked: prepareViewRedirectPatches checks each byte and adds its range to `writes`
// (view_install.cpp makes them writable), false with nothing changed when one is not the expected byte;
// applyViewRedirectPatches changes them (the ranges writable) and cannot fail. From then on the
// split-category hooks keep view 0 on its two slots.
bool prepareViewRedirectPatches(const std::byte* base, std::vector<CodeRange>& writes);
void applyViewRedirectPatches(const std::byte* base);

// At the start of each two-view dispatch (the job graph's node handles of the last one are stale).
void viewRedirectsDispatchStart();

// The counts for the dispatcher's periodic line.
void viewRedirectsLogCounts();

} // namespace evr::vkcore
