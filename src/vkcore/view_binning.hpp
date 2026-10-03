#pragma once

// Parallel Eye Rendering: view 1's light and decal binning waits for view 0's. Both views' binning (job graph
// nodes queued by 0x1CFA640) works in the renderer's one scratch, so view 1's binning roots get job graph
// edges from view 0's three sink nodes. Installed by view_redirects.cpp.
//
// - View 0 marks the frame once it is past its sink sites; view 1 waits for that mark at most 2 ms before
//   it adds its edges (were it first, it would find no sinks and both views would bin into the scratch at
//   once). 16 waits in a row that run out stop the waiting for the session, with one log line.
// - A watchdog thread: when no frame at all has been dispatched for 3 s right after a run of two-view frames
//   (play, not a load), one line says whether the last frame's binning finished (view 0's sinks, view 1's
//   roots) or is still waiting, read from the scheduler's nodes. Sinks finished with a root still waiting
//   would be a lost wakeup of the edges: they are then turned off for the session (one line).
// - ETERNALVR_TEST_VIEW_OFF containing "edges": no edges. ETERNALVR_TEST_BINNING_DELAY=<ms> (1..100, test
//   only): view 0's binning sleeps that long before its sink sites, so view 1 reaches its roots first.

#include <cstddef>

namespace evr::vkcore {

// The sink, roots and watchdog hooks; false when one fails.
bool installViewBinning(const std::byte* base);

// At the start of each two-view dispatch: the last frame's sink handles and view 0's mark are stale.
void viewBinningFrameStart();

// Any other frame the dispatcher sees (one view, no world, after a guard trip), for the watchdog.
void viewBinningOneViewFrame();

// The counts for the dispatcher's periodic line.
void viewBinningLogCounts();

} // namespace evr::vkcore
