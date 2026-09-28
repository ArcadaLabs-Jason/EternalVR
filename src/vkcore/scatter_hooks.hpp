#pragma once

// The light scattering's temporal history per eye (stereo_seq/scatter_history.hpp,
// docs/rig-findings/stereo-scatter.md; Steam build 25216728). On by default; ETERNALVR_STEREO_SCATTER_TAA=0
// turns it off.
//
// - Eye R's four volume images are made with the engine's image manager right after the device context
//   constructor makes the engine's four (RVA 0x1C1CD80, hook after the last store at RVA 0x1C1F05C), with the
//   same image description.
// - When the render size changes the engine resizes its volumes in place (RVA 0x1CDD6D0); right after it
//   (hook at RVA 0x1CDDD7D) eye R's four follow, and so do whichever of the engine's four eye R's took the
//   place of, then both eyes clear their history.
// - At the start of the scattering setup (RVA 0x1C71F90, the first of the scattering passes in each render)
//   the device context's two pairs and the filter's state are set for the eye being rendered.
//
// Located by signature; anything missing leaves the game untouched and keeps r_lightScatteringTAA held off.

namespace evr::vkcore {

// Locates and installs the three hooks once per process, before the renderer starts (the images are made with
// the device context); later calls return the first result.
bool installScatterHooksEarly();

// The per-eye history is in place (requested, installed, eye R's images made): the scattering's temporal
// filter can stay on in stereo.
bool scatterPerEyeReady();

} // namespace evr::vkcore
