#pragma once

// Parallel Eye Rendering: models only eye R sees are prepared and updated (docs/VR_STEREO.md "Parallel Eye
// Rendering"; RVAs in Steam build 25216728).
//
// The world update (RVA 0x18E5070, world vtable +0x120) runs once a frame, for world view 0 only. Its jobs
// prepare (0x18E78D0) and update (UpdateInView: particles 0x1955150, flares 0x1936650, beams 0x18F21E0,
// ribbons 0x1970BD0) the models on view 0's four lists: render view +0x970 + list * 0x8000, int32 indices
// into the world's models (+0x270E0, count +0x270E8), 0x2000 entries each, the counts at +0x20970. Each
// view's gather fills its own lists (the render-view job setup 0x1C5C2F0 points its block at them), and
// nothing read view 1's: a model only view 1 sees (the strip about 40 to 54 degrees right of centre) was
// never simulated, and eye R drew it with stale geometry or none.
//
// A hook in the world update, after `test r15, r15` and before the job counts are read from the lists (RVA
// 0x18E51DC: rbx the world, r15 view 0's render view), adds view 1's entries that are not on view 0's lists
// to them, within each list's 0x2000 entries. The lists only feed the prepare and the updates: what each view
// draws stays its own gather's. The lists read are those of the last backend frame's gathers: both views'
// gather blocks (render context +0x4D9F50) must have been set up for this world's views at its last world
// frame, and both gathers must have ended: a second hook at the end of the gather's last job (0x1C79FC0, the
// gather's end node or the inline setup's call; it appends to the lists itself) records the view and world
// frame each render context's gather finished for. That is waited for at most 0.5 ms (16 waits in a row that
// run out stop the waiting for the session; such frames are left out).
//
// On with Parallel Eye Rendering; ETERNALVR_TEST_PE_UPDATE_UNION=0 leaves it out. Every 10 s one line counts
// the entries added per list, those already on view 0's, those over the cap and the frames left out.

#include <cstddef>

namespace evr::vkcore {

// From view_install.cpp after the engine changes: checks the code it relies on byte by byte and installs the
// hook (inert until Parallel Eye Rendering is active). A miss logs and leaves it out; Parallel Eye Rendering
// runs without it.
void installViewUpdates(const std::byte* base);

} // namespace evr::vkcore
