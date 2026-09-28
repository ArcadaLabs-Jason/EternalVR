#pragma once

// Stereo (docs/VR_STEREO.md): the data one game frame carries for each eye, the stereo settings and the
// per-eye hook's statistics. Route S (synchronized sequential) is in presenter_seq.cpp with its engine
// hooks in seq_hooks.cpp; the per-eye hook and the engine two-view experiments are in presenter_stereo.cpp
// and stereo_hooks.cpp.

#include "stereo_seq/eye_view_plan.hpp"
#include "stereo_seq/seq_settings.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "xr_math/stereo_view.hpp"

#include <openxr/openxr.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::vkcore {

// One eye of a game frame: what the per-eye hook writes into that eye's render view and what the
// projection layer submits for it.
struct EyeRecord {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}}; // eye in LOCAL
    XrFovf fov{};                                               // the eye's own (asymmetric) FOV
    std::array<float, 3> offset{};                              // world offset from the head-centred origin
    std::array<float, 9> axis{};                                // the eye's viewaxis (forward, left, up rows)
};

struct StereoSettings {
    bool requested = false;      // ETERNALVR_MODE=stereo
    bool sequential = false;     // Route S: stereo without an experiment (seq_hooks.hpp)
    bool enabled = false;        // eye data prepared: Route S or an experiment (stereo_hooks.hpp)
    int views = 1;               // 2 for the two-views experiment
    bool eyePoses = true;        // ETERNALVR_STEREO_EYE_POSES: 0 renders both eyes from the head centre (E4)
    bool inhibitModelFov = true; // ETERNALVR_STEREO_INHIBIT_MODEL_FOV: renderView_t.inhibitModelFovScale
    bool copyJitter = true;      // ETERNALVR_STEREO_JITTER_COPY: the second view takes the first's TAA jitter
    float testWeaponFov = 0.0f;  // ETERNALVR_TEST_WEAPON_FOV: weaponFOVX/Y override in degrees (E3), 0 off
    // Route S (docs/VR_STEREO.md): ETERNALVR_STEREO_SAME_VIEW, _FULL_RES, _EXPOSURE_ONCE, _DISCONTINUOUS.
    stereo_seq::SeqViewSettings seqView;
    bool prevMatrices = true; // ETERNALVR_STEREO_PREV_MATRICES
    bool fixCentered = true;  // ETERNALVR_STEREO_FIX_CENTERED: centred matrix depth row after each eye latch
    std::optional<stereo_seq::CaptureSetting> capture; // ETERNALVR_CAPTURE_EYES
};

StereoSettings readStereoSettings();

// Per-eye hook statistics (render job threads).
struct StereoStats {
    std::atomic<std::uint64_t> eyeViews[2]{};
    std::atomic<std::uint64_t> unmatched{0};    // views whose axis matched no recorded game frame
    std::atomic<std::uint64_t> noProjection{0}; // no usable depth rows or FOV
    std::atomic<std::uint64_t> jitterCopies{0};
    std::atomic<std::uint64_t> jitterDiffered{0}; // the second view's own jitter differed from the first's
    std::atomic<std::uint64_t> latched[2]{};
    std::atomic<std::uint64_t> latchMismatch{0}; // latched projection differs from the one written
    std::atomic<int> loggedEyes{0};
    std::atomic<int> loggedLatches{0};
    std::atomic<unsigned long long> lastStatsTicks{0};
    std::atomic<std::uint32_t> lastRenderFrames{0};
    std::atomic<std::uint32_t> lastBackendFrames{0};
    std::atomic<std::uint64_t> lastLatchTotal{0};
    // The projection written into each view, for the post-latch check.
    std::mutex writtenMutex;
    std::array<xr_math::EngineMatrix, 2> written{};
    std::array<bool, 2> writtenValid{};
};

} // namespace evr::vkcore
