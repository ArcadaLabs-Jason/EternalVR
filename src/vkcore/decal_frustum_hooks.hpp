#pragma once

// Decals for the Route S eye pair from the frustum around both eyes
// (docs/rig-findings/stereo-umbra-decals.md; Steam build 25216728, the same decal gather on Game
// Pass 1.0.56.0 by static reading).
//
// The decal gather (RVA 0x1C77C90, a job) lists the decals Umbra's per-view query reported visible. A Route S
// eye's frustum is narrower than the flat game's on its inner side, so Umbra left out decals that still cover
// surfaces in view (the wet patches of puddled floors), and both eyes draw the list eye L's view gathers. A
// detour on the gather first adds every active decal whose bounds reach the frustum centred on the eye's axis
// with the larger of the eye's wider side and the flat game's half-angle (stereo_seq::pairDecalProjection),
// box against side planes. Geometry, lights and world areas keep Umbra's per-eye query; a mono view's list is
// the engine's.
//
// Located by signature, with the gather's Umbra-off block (the decal record layout) checked. On a game build
// where the pattern is not found nothing is hooked and the log says why (`seq-decals:` lines).

#include <cstddef>

namespace evr::vkcore {

// Locates and installs the detour once per process; later calls return the first result.
bool installDecalFrustumHook();

// The layer wrote an eye's pose and `projection` (16 floats, the explicit projection) into `renderView` (the
// idRenderView). Only a gather whose view is such an eye view, with that projection latched, gets the decals
// around the pair; every other view (menus, cinematics, zoom, probes) keeps the engine's list.
void noteEyeViewProjection(const std::byte* renderView, const float* projection);

} // namespace evr::vkcore
