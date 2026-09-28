#pragma once

// The previous frame of animated and moving objects per eye (stereo_seq/object_prev.hpp,
// docs/rig-findings/stereo-object-motion.md; Steam build 25216728). On by default;
// ETERNALVR_STEREO_OBJECT_PREV=0 turns it off.
//
// The render-view job's parameter setup (RVA 0x1C54650) binds prevJointOffsetsBuffer and
// prevModelMatricesBuffer from a ring of three buffers, each after `call 0x1CBB2D0` (the render counter)
// and `lea r8d, [rax + 2]` (the render before). A hook on each `lea` (RVA 0x1C54A8C and 0x1C54AD3) gives
// eye R's render the counter that picks its own render of the tick before instead.
//
// Located by signature and cross-checked (both parameters by name); anything missing leaves the game
// untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installObjectPrevHooks();

} // namespace evr::vkcore
