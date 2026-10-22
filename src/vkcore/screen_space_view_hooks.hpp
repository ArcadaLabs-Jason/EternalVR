#pragma once

// The screen-space reflections' view rotation per eye (stereo_seq/screen_space_view.hpp,
// docs/rig-findings/stereo-screen-space.md; Steam build 25216728).
//
// The render-view job sets the view's matrices as shader parameters (RVA 0x1C54BFB onwards: viewMatrixX..W,
// then inverseViewMatrixX..W, each with the engine's vec4 setter, RVA 0x1C53720, from the idRenderView in
// r13, the parameter context in r15). A hook on the instruction after inverseViewMatrixZ's set (RVA
// 0x1C54C95) sets inverseViewMatrixX/Y/Z again, sheared for the view's latched projection
// (stereo_seq::screenSpaceShear), so the SSR march in the opaque pass runs along the reflected ray's true
// screen line in each eye. A symmetric projection (mono, the flat game) gives no shear and is left alone.
//
// Located by signature and checked by the three parameters' names (the declaration 0x10 before the handle
// each `mov rdx` reads) and the setter being a function; anything missing leaves the game untouched and logs
// why, once.

namespace evr::vkcore {

// Locates and installs the hook once per process; later calls return the first result.
bool installScreenSpaceViewHook();

} // namespace evr::vkcore
