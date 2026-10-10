#pragma once

// Parallel Eye Rendering: both views take their compute skinning output room from one counter.
//
// Each view's surface setup 0x1C00F70 (called per skinned surface by 0x1C7A920 and 0x1C7B2D0) takes the
// surface's room in the world geometry manager's compute skinning output (csSkinBuffer, [0x5BF13B8] + 0x760)
// with `lock xadd` on a counter in its own render context (+0x4D9F50 + 0x270, or + 0x274 for the geo decal
// part). The view's csSkinning dispatches (category 2, MVP_CULLING: 0x1C00DB0) write the skinned positions
// there and the view's draws read them back by that offset. 0x1C5C2F0 sets both counters to the part's start
// for every view, so with two views both packed their surfaces from the same start: view 1's dispatches,
// recorded after view 0's in category 2, overwrote view 0's positions with other surfaces' wherever the two
// views' lists of skinned surfaces differ (eye L's arms, weapon and demons drawn as spikes and shards). View
// 1's allocations are taken from view 0's counter instead, so the two views' outputs are disjoint, and the
// output is made with twice the room (r_worldGeometryManagerCSSkinBufferSize's read in the world geometry
// manager's setup, within the 20 bits of offset a draw surface keeps), as two views take about twice one
// view's. Only in frames that dispatch view 0 (with ETERNALVR_TEST_VIEW_ONLY=1 nothing starts view 0's
// counter, and view 1 keeps its own). Installed by view_shared_state.cpp.

#include <cstddef>

namespace evr::vkcore {

// Checks the allocation sites' and the size read's bytes; false (logged) when one is not as known, before
// anything changes.
bool prepareViewSkinAlloc(const std::byte* base);

// The hooks (inert until Parallel Eye Rendering is on; the size read, which runs once in the renderer's
// setup, only with the multiplayer guard armed); false if one fails.
bool installViewSkinAlloc(const std::byte* base);

// At the start of each two-view dispatch: whether view 0 is dispatched this frame.
void viewSkinAllocFrameStart(bool view0Dispatched);

// The counts for the dispatcher's periodic line.
void viewSkinAllocLogCounts();

} // namespace evr::vkcore
