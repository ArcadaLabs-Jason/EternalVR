#pragma once

// Parallel Eye Rendering rig knobs on top of the command buffer check (cb_check.hpp,
// ETERNALVR_TEST_CB_CHECK=1): they found which of view 1's passes darkened the eyes (docs/VR_STEREO.md
// "Parallel Eye Rendering").
//
// - ETERNALVR_TEST_VIEW1_GPU_SKIP=all, or a list of categories (e.g. 8,9), drops the draws and dispatches
//   recorded into view 1's command contexts (slot 1, or slots 2-3 in the split categories 7, 9 and 10), while
//   its CPU work, barriers and submits stay. ETERNALVR_TEST_VIEW0_GPU_SKIP=<categories> does the same to view
//   0's contexts (slot 0, or slots 0-1 in the split categories; "all" is not offered: slot 0 also serves the
//   frame).
// - ETERNALVR_TEST_CALLERS=<category> logs the distinct game call stacks (first 24) of the draws and
//   dispatches recorded into view 1's context of that category.

#include <vulkan/vulkan.h>

namespace evr::vkcore::cb_check {

// True when the draw or dispatch on `cb` is to be dropped (a GPU skip knob names its view and category).
bool skipDraw(VkCommandBuffer cb);

// Logs the call's game stack when ETERNALVR_TEST_CALLERS names the category of view 1's context `cb` belongs
// to.
void noteCaller(VkCommandBuffer cb, const char* call);

} // namespace evr::vkcore::cb_check
