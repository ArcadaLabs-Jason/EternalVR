#pragma once

// The auto-exposure index per eye under Route S (docs/rig-findings/stereo-temporal.md 3.3; Steam build
// 25216728). Eye R skips its exposure update (ETERNALVR_STEREO_EXPOSURE_ONCE, on by default), so eye L's
// backend frame parity is constant and with the engine's own index eye L would read a "previous" exposure
// nothing writes any more: exposure would stop adapting. A hook after the engine stored its index (RVA
// 0x1C98D46, in 0x1C988E0) gives eye L and mono frames an index alternating by their own frame count and
// eye R the one eye L wrote this tick (stereo_seq::ExposurePlanner).
//
// It needs the eye tags, not per-eye TAA: it is installed whenever Route S runs and holds with per-eye TAA
// off (ETERNALVR_STEREO_TAA=0, the launcher's anti-aliasing Off) or failed closed as well
// (stereo_seq::exposureIndexHeld). Where it cannot be installed, eye R updates its own exposure instead
// (stereo_seq::planEyeView), and the two eyes form one adapting chain.
//
// Parallel Eye Rendering uses the same hook site (docs/VR_STEREO.md "One auto exposure for both eyes"): view
// 1 skips its exposure update, and the engine's index for it reads view 1's own last-updated frame count,
// which nothing writes then, so view 1 would read image 0 every frame while view 0 alternates. The hook gives
// view 1 view 0's image (ETERNALVR_PE_EXPOSURE, parallel_eyes::Exposure) and sets its skip flag where the eye
// pose write returned early.

#include "vkcore/parallel_eyes_settings.hpp"

#include <cstdint>

namespace evr::vkcore {

// Route S start: locates and installs the hook once per process; later calls return the first result.
// `exposureOnce`: ETERNALVR_STEREO_EXPOSURE_ONCE (stereo_seq::ExposureGate).
bool installExposureHook(bool exposureOnce);
// Parallel Eye Rendering's install (view_install.cpp), after the view slots are active: the same hook, with
// view 1's index chosen by `mode` while parallelEyesTouch() allows it. Engine installs it too, to count.
bool installParallelEyesExposureHook(parallel_eyes::Exposure mode);
bool exposureHookInstalled();

// The hook gives each render its planner index now (Route S hooks active, multiplayer guard armed, and
// per-eye TAA on, or failed closed or not requested with eye R's exposure skip on).
bool exposureIndexHeld();

// Route S start, after its hooks: one line naming how exposure and the scattering and SSDO histories are kept
// per eye (`seq-exposure: ...`), so a run's log shows the mode.
void logStereoTemporalMode();

struct ExposureCounters {
    std::uint64_t held[2] = {};        // renders given the planner's index: eye L and mono, eye R
    std::uint64_t inFlightDiffers = 0; // renders whose tag in flight names another eye than their own
};
ExposureCounters exposureCounters();

} // namespace evr::vkcore
