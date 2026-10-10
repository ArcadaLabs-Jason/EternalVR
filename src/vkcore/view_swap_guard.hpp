#pragma once

// Parallel Eye Rendering: view 0's screen pass is left out of a frame whose swapchain was recreated before it
// ran (docs/VR_STEREO.md "Parallel Eye Rendering"; RVAs in Steam build 25216728).
//
// The screen pass 0x1CDF6E0 writes the colour image of the screen target [dc + 0x508] (dc = [0x66E3B88]),
// which the acquire 0x1D09280 sets to the swapchain image it acquired ([idSwapChain + 0x38 + index * 8],
// 0x1D0A6F4) or to the fallback image [0x66E31D0] without a swapchain. The last view of a frame runs it in
// the finish job 0x1CD8380 right after the acquire (call at 0x1CDF444). Every view before it (with Parallel
// Eye Rendering view 0: the dispatcher sends it with "more views follow") runs it earlier, as the per-view
// job 0x1C58030 (record 0x39A1E00, a tail jump to the pass) or inline at 0x1C57572, with the colour image the
// last acquire left. A swapchain recreate inside that frame (the frame begin job 0x1CD6F80 -> 0x1CDD3C0 ->
// 0x1D090E0: a window resize, an out-of-date swapchain, r_hdrDisplay changed) destroys the old swapchain and
// deletes its images (0x1D09540) before view 0's pass runs, so the pass bound a deleted image and the game
// crashed in the parameter resolve (0x1C334B9, reading null + 0x94).
//
// A hook at the pass's first instruction leaves view 0's pass out (resuming at a plain `ret`, 0x1CBB5E7) when
// it does not return into the finish job (0x1CDF449) and the screen target's colour image is neither one of
// the swapchain's images (dc + 0xB8 + 0x38, count at dc + 0xB8 + 0x80) nor the fallback: pointers are
// compared, the stale image is never read (swap_images.hpp). The pass writes only that image (the context it
// records into, command table cell [11, 0], holds the frame's post-process passes as well, and with one view
// never holds a screen pass), so the frame goes on without view 0's picture: view 1's copy of the same frame
// is not made either (view_one_passes.cpp, view_snapshot.hpp), and a present that would pair with it keeps
// the last pair as for a repeat. The finish job's pass is never left out.
//
// The pass is left out as well while the frame begin's recreate runs (an inference keeps the begin job before
// the views' jobs; no lock is taken: the recreate waits on the window thread, and a lock held across it and
// the pass could not be shown free of a deadlock). The watch (view_swap_watch.hpp) logs each destroy with the
// begin that made it and holds the eye copy's presents of new images no view 0 pass drew yet; the first view
// 0 pass checked after a destroy logs what its colour image was. ETERNALVR_TEST_PE_SWAP_GUARD=0 leaves the
// pass in (the watch still logs).

#include <cstddef>

namespace evr::vkcore {

// From view_install.cpp's checks, before anything of the game is changed (a view redirect hooks the inline
// call's `lea` at 0x1C5755D later): checks the code the guard and the watch rely on byte by byte. A miss
// logs; it never stops Parallel Eye Rendering.
void prepareViewSwapGuard(const std::byte* base);

// From view_install.cpp after the engine changes: the watch's hooks when their sites were as known, and on
// its own the screen pass hook when the guard's were (inert until Parallel Eye Rendering is active). A miss
// logs and leaves that part out; Parallel Eye Rendering runs without it.
void installViewSwapGuard(const std::byte* base);

// View 1's screen pass (view_one_passes.cpp): true once when view 0's pass of the same frame was left out, so
// view 1's image of that frame is not copied for eye 1.
bool viewSwapGuardTakeView0LeftOut();

// The counts for the dispatcher's periodic line.
void viewSwapGuardLogCounts();

} // namespace evr::vkcore
