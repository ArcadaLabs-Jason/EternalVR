#pragma once

// Lens flares under Parallel Eye Rendering, each eye's from its own view (docs/VR_STEREO.md, Parallel Eye
// Rendering, "Lens flares"; RVAs in Steam build 25216728, whose PE timestamp the install checked). On by
// default; ETERNALVR_PE_FLARES=0 keeps them off as before (r_skipFlares 1).
//
// A flare's update (RVA 0x1936650) projects its quads on the CPU, once a frame, from the world-views latch of
// the game's head-centred view, into one vertex block both views draw: the same screen spot in both eyes,
// right in neither. Here:
// - The prepare takes room for two halves of quads (RVA 0x1936415): view 0's first, view 1's after them.
// - After each view's latch (presenter_stereo.cpp) the update runs again with that view's render view
//   over its half (flare_core.hpp: the records of the flare job, the query slots it took).
// - View 1's binds of a flare's index range (the four index-buffer binds, RVAs 0x1C2DFA3, 0x1C2CA52,
//   0x1C2D1CB, 0x1C2D89B) get the offset of its half; view 0's stay.
// - The occlusion queries: view 1 issues its own, the slots' twins in the pool's upper half (its surface
//   setup's marks RVA 0x1C7CFC7, its occlusion pass RVAs 0x1C90B06 and 0x1C90B87, its vertices; the twins
//   are never stored in the render entity, flare_core.cpp), and each view's copy (RVA 0x1C32F79) scans only
//   its own half and waits for the results again (view_query_copy.cpp), as one view does.
// - A wait on a query nobody issued stalls the GPU (issue #18's class): view 0's latch clears the pool's
//   pending bits with atomic stores (counted and logged when it finds any set), and a copy waits only when
//   every bit it scans was marked by its own view's setup since then and since that view's last copy,
//   which uses them up; the queries each view's occlusion pass begins are checked against what its copies
//   waited on at the next latch, and one it did not begin stops the waits (pe_flare_guard.hpp).
// Every site's bytes are checked first; a miss, a hook that fails, a rebuild that faults, a view's command
// context that is no longer the one in the game's table, or view 1's flares not seen at the binds turn it
// off: flares off again (r_skipFlares 1), one line.

#include <cstddef>

namespace evr::vkcore {

// From Parallel Eye Rendering's install (view_install.cpp), after view 1's command contexts exist and before
// the first present.
void installParallelEyeFlares(const std::byte* base);

// Flares are drawn per view: r_skipFlares is not held (runtime_cvars.cpp). False before the install, with
// ETERNALVR_PE_FLARES=0, after a failed check or once turned off.
bool parallelEyeFlaresLive();

// After view `viewIndex`'s latch (its eye pose written or not). Render job thread.
void parallelEyeFlaresLatched(std::byte* renderView, int viewIndex, bool poseWritten);

// The occlusion copy in progress on this thread keeps VK_QUERY_RESULT_WAIT_BIT (view_query_copy.cpp).
bool parallelEyeFlaresCopyWaits();

} // namespace evr::vkcore
