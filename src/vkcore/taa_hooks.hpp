#pragma once

// Per-eye temporal history for Route S, ETERNALVR_STEREO_TAA=1 (docs/rig-findings/stereo-temporal.md;
// Steam build 25216728).
//
// Route S renders each game tick twice through the same render view, and the engine picks its temporal
// images by the parity of the backend frame counter, which advances once per render: with two renders per
// tick each eye would read the other eye's history. Per-eye TAA keeps the eyes apart:
//
// - A second pair of accumulation images for eye R, built by the engine's own slot builder (RVA
//   0x1C20150) right after it built the real slot, while the renderer starts (a hook in the device
//   context constructor, installed from vkCreateInstance).
// - The two accumulation selectors (RVA 0x1CBB5A0 output, 0x1CBB6C0 history), called by the render-view
//   job of each backend frame, are detoured: the frame's eye tag (found by the backend counter) picks the
//   pair and the eye's own frame count the image, so each eye reads what it wrote one tick earlier.
// - The per-eye hook gives both eyes of a tick the same jitter phase (the tick's, not the render
//   counter's) and resets both eyes' history when eye R missed a tick (presenter_seq.cpp).
// - DLSS: eye R evaluates a twin NGX feature (taa_ngx.cpp).
// - The temporal effects whose history is still shared (anti-ghosting mask, SSDO, light scattering, depth
//   of field, water, refraction, ray-traced reflection upscale) are switched off through the engine's cvar
//   setter.
//
// Fail closed: when any piece is missing, the first stereo tick writes the v1 set instead (r_antialiasing
// 0, r_TAASafeMode 1: no temporal accumulation at all). Every game write and every redirect asks the
// multiplayer guard first; after a trip the hooks only forward to the engine.

#include <cstdint>

namespace evr::vkcore {

// ETERNALVR_STEREO_TAA=1 with ETERNALVR_MODE=stereo and no stereo experiment.
bool taaRequested();
// ETERNALVR_STEREO_DLSS=1: DLSS per eye instead of TAA.
bool taaDlssRequested();
// ETERNALVR_STEREO_DLSS_QUALITY: the r_dlssQuality value held while DLSS runs (stereo_seq::dlssQualityValue),
// -1 for the game's own.
int taaDlssQuality();

// From vkCreateInstance, after the multiplayer guard: hooks the device context's slot loop so that eye R's
// images are built with the renderer. Does nothing unless requested and the guard allows game writes.
void installTaaEarly();

// Route S start (after its own hooks): the selectors, the cvars and the NGX twins. False (logged) when a
// piece is missing; the first stereo tick then fails closed.
bool installTaaHooks();

// Eye L's per-eye hook on each stereo tick. The first call switches per-eye TAA on (the forced cvars) or
// fails closed (the v1 cvars); later calls apply the DLSS fallback if eye R's NGX feature failed.
void taaOnStereoTick();

// Per-eye history is in use: the per-eye hook writes the jitter phase and the resets.
bool taaPerEyeActive();

// r_TAANumSubSamples (32 when unknown).
int taaNumSubSamples();

struct TaaCounters {
    std::uint64_t picks[2] = {};       // selector answers per eye (eye L with mono, eye R)
    std::uint64_t enginePicks = 0;     // left to the engine (untagged, another view, off)
    std::uint64_t opaquePicks = 0;     // opaque accumulation answered with eye R's image
    std::uint64_t distortionBinds = 0; // distortionLastFrameMap bound to the eye's own history
    // Output picks by tag eye (L, R) and latched projection side (left, centred, right).
    std::uint64_t tagVsView[2][3] = {};
    std::uint64_t secondPairBuilds = 0;
    std::uint64_t twinCreates = 0; // eye R's DLSS features created
    std::uint64_t twinFailures = 0;
    std::uint64_t evaluates[2] = {};   // DLSS evaluations per eye
    std::uint64_t evaluatesNoTwin = 0; // eye R evaluations left on the game's feature
    std::uint64_t twinResets = 0;
    std::uint64_t releases = 0;
    // Cvar values read back now (-1: not located): r_jitter is the engine's own verdict, set every frame
    // to 1 exactly when temporal AA or DLSS runs (r_TAASafeMode 0 and an AA mode above 0).
    int antialiasing = -1;
    int safeMode = -1;
    int jitter = -1;
    int antiGhosting = -1;
    bool perEye = false;
    int sizeA[2] = {}; // the engine's accumulation render target (width, height)
    int sizeB[2] = {}; // eye R's
};
TaaCounters taaCounters();

} // namespace evr::vkcore
