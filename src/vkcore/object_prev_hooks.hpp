#pragma once

// The object-transform ring handed out per render (stereo_seq/object_prev.hpp,
// docs/rig-findings/stereo-object-motion.md; Steam build 25216728). On by default;
// ETERNALVR_STEREO_OBJECT_PREV=0 turns it off (eye R's moving objects then have no motion).
//
// The ring of three buffers is indexed by the render counter (`call 0x1CBB2D0`) in five places: its upload
// (`mov r8d, eax` at RVA 0x1C00C2E, in 0x1C00B40) and the render-view job's parameter setup (RVA 0x1C54650),
// which binds jointOffsetsBuffer and modelMatricesBuffer (`mov r8d, eax` at 0x1C54A00 and 0x1C54A46) and
// their prev* twins (`lea r8d, [rax + 2]` at 0x1C54A8C and 0x1C54AD3). A hook on each hands the engine the
// counter ObjectRing picks for the render, found by its own tag (eye and tick; the one that presents
// with backend frame counter + 1). ETERNALVR_STEREO_RING_TRACE=1
// logs the picks (ring_trace.hpp).
//
// Located by signature and cross-checked (the counter call and every parameter by name); anything missing
// leaves the game untouched and logs why.

namespace evr::vkcore {

// Locates and installs the hooks once per process; later calls return the first result.
bool installObjectPrevHooks();

} // namespace evr::vkcore
