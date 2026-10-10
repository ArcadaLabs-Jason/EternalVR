#pragma once

// Parallel Eye Rendering: the copy of the occlusion queries' results does not wait for them.
//
// Each view's surface setup (0x1C7A920, through 0x1C7CF80) marks an occlusion-tested object's two queries
// (the indices at +0x108 and +0x110, taken from the global counter 0x66E386C by 0x1C343C0 and restarted at 0
// every frame) pending in one global bit array (0x66E3878) with plain read-modify-writes. The occlusion pass
// (0x1C8FA40, category 7) begins and ends them in the occlusion pool ([0x66E3870], 0x400 queries, reset once
// a frame by 0x1C34FE0 or 0x1C32860). Each view's emissive and blend job (0x1C62040, category 10) starts with
// 0x1C32F20, which copies every pending query into a buffer with VK_QUERY_RESULT_WAIT_BIT and clears the
// bits. With one view each bit is set, issued and copied in one frame. With two views the bit array and the
// object's indices are shared: a bit can outlive its frame (set after both views' copies, or rewritten by the
// other view's clear), and the next frame's first copy waits on queries reset at that frame's start and not
// issued again: the queue stalls at that copy until Windows resets the GPU (TDR, nvlddmkm 153). On the rig
// this happened in every walking run in e1m1_intro once an occlusion-tested object came into range. While
// Parallel Eye Rendering runs, both copies (0x1C32FAF, 0x1C33039) are made without the wait flag: queries the
// GPU has not finished are not copied, and the buffer keeps their previous results. As the indices restart
// every frame, such a slot may hold another object's result (0x1C32F20 clears the bits all the same, at
// 0x1C33007): an object culled or kept by a stale result for a frame. Installed by view_shared_state.cpp.

#include <cstddef>

namespace evr::vkcore {

// Checks both copies' flag stores; false (logged) when one is not as known, before anything changes.
bool prepareViewQueryCopy(const std::byte* base);

// The hooks (inert until Parallel Eye Rendering is on); false if one fails.
bool installViewQueryCopy(const std::byte* base);

// The count for the dispatcher's periodic line.
void viewQueryCopyLogCounts();

} // namespace evr::vkcore
