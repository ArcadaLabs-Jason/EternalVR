#pragma once

// Lens flares at each eye's own position under Route S (docs/VR_STEREO.md, "Lens flares"; RVAs in Steam build
// 25216728). On by default; ETERNALVR_FLARES_PER_EYE=0 turns it off.
//
// A flare (idRenderModelFlare) builds its quads on the CPU in clip space: its UpdateInView (RVA 0x1936650)
// projects the flare's origin with the render view's view and projection matrices and writes 4 vertices per
// element into the model's block of the transparency-quad ring, once per render. The only caller is the flare
// job of the world update (RVA 0x18E1670), which runs before the screen-views pass, so the matrices are the
// world-views latch of the game's head-centred view and both Route S eyes drew every flare at the head's
// position (a floor flare 28 px apart between the eyes where the scenery next to it is 251 px apart: beyond
// infinity).
//
// So the flare job's calls are recorded (the model, its vertex block, its quad count and the intensity the
// prepare left), and after each eye's latch (the post-latch hook, presenter_seq.cpp) UpdateInView runs again
// for each of them with the eye's latched render view: the same block, the intensity put back first, and the
// two occlusion query slots it would allocate replaced by the ones the engine's call took this render, so the
// queries the render issues still match the vertices. Nothing is allocated; the engine's call stays as it is.
//
// Route S only: with Parallel Eye Rendering active nothing is recorded or rebuilt. Every hook asks the
// multiplayer guard first. The sites are located and checked byte by byte; a miss leaves the game untouched
// and logs why.

#include <cstddef>

namespace evr::vkcore {

// Locates and installs the hooks once per process (Route S, with its hooks); later calls return the first
// result.
bool installFlareViewHooks();

// After a Route S eye's latch, with that eye's pose written into `renderView`: the flares the engine updated
// for this render are built again from the render view's latched matrices. Render job thread.
void rebuildFlaresForEye(std::byte* renderView);

} // namespace evr::vkcore
