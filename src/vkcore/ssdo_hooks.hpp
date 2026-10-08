#pragma once

// SSDO's temporal history per eye under Route S (stereo_seq/ssdo_history.hpp; Steam build 25216728). On by
// default; ETERNALVR_STEREO_SSDO_TAA=0 turns it off.
//
// - Eye R's two accumulation targets are made right after the device context constructor makes the engine's
//   three (RVA 0x1C1CD80, hook after the unfiltered target is attached at RVA 0x1C1EC1D): two images through
//   the image manager with the engine's image description, each attached to a render target of its own
//   (constructor RVA 0x1C73CA0, attach RVA 0x1C740A0).
// - When the render size changes the device context resizes its three in place at half the size (RVA
//   0x1C21600, with the render-target resize RVA 0x1C743C0); right after them (hook at RVA 0x1C2199A) eye R's
//   two follow, and both eyes start their history over. Sizes that still differ fail closed.
// - At the entry of SSDO's parameter setup (RVA 0x1C71630, called by the render-view job before the frame's
//   SSDO pass runs), the array the engine picks the render's targets from (the job's context + 0x58 and the
//   SSDO pass context + 0x20, the device context + 0x550 before) is pointed at the eye's own, and when the
//   eye's history is stale the filter's last frame is moved back, so the engine resets the filter itself.
//
// Located by signature; anything missing leaves the game untouched and keeps r_SSDOTemporalAA held at 0
// (stereo_seq::stereoSsdoFilterCvar: per-eye TAA's set, or the layer's run-time set without it). Route S
// only: Parallel Eye Rendering keeps the launcher's r_SSDOTemporalAA 0.

namespace evr::vkcore {

// Locates and installs the three hooks once per process, before the renderer starts (the targets are made
// with the device context); later calls return the first result. From vkCreateInstance whenever Route S is
// requested: the history needs the eye tags only, not per-eye TAA.
bool installSsdoHooksEarly();

// The three hooks are installed (eye R's targets may not be made yet).
bool ssdoHooksInstalled();

// The per-eye history is in place (stereo_seq::ssdoNotReady: requested, installed, eye R's targets made and
// of the engine's size, not failed closed, rs_enable 0): SSDO's temporal filter can stay on in stereo.
bool ssdoPerEyeReady();

// Eye L's view of each stereo tick (presenter_seq.cpp): the first logs one `seq-ssdo:` line saying whether
// the per-eye history is ready, and why not (eye R's targets never made, for one).
void ssdoOnStereoTick();

} // namespace evr::vkcore
