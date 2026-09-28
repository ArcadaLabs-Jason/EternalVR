#pragma once

// Engine side of synchronized sequential stereo, Route S (docs/VR_STEREO.md; static findings and every
// address in docs/rig-findings/stereo-routes.md section 2, Steam build 25216728).
//
// The engine renders a frame through a chain of jobs; the last one, the frame-end job (RVA 0x1CBA1C0),
// hands the frame to the render thread and clears the render-frame guard. The chain reaches it through a
// function pointer in .data (RVA 0x39A9600, read only at RVA 0x1CBA17B). Route S swaps that pointer for a
// wrapper: the wrapper calls the original (eye L, the engine's own chain), then, on a stereo tick, calls
// the render system's "render one frame synchronously" (RVA 0x1CBEA30, vtable slot 0x50; the loading
// screens render through it without a game tick) with the same arguments, which runs the whole chain
// again for eye R. The per-eye hook of stereo_hooks.cpp sees which eye's chain it runs in
// (seqChainEye) and writes that eye's view; a hook after the previous-matrix store (RVA 0x1C75D81) keeps
// each eye's previous-frame matrices apart; the wrapper tags every frame with its eye for the present
// hook (seqTakePresent).
//
// Everything is located by signature and cross-checked (the pointer's target, its section, the render
// system's vtable, the job the synchronous render queues, the store the hook follows). Any mismatch
// leaves the game untouched and stereo off (mono), with the reason in the log.

#include "stereo_seq/eye_tags.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

struct SeqHookSettings {
    bool prevMatrices = true; // ETERNALVR_STEREO_PREV_MATRICES
};

// Locates, checks and installs the Route S hooks once per process; later calls return the first result.
bool installSeqHooks(const SeqHookSettings& settings);

bool seqHooksActive();

// Stereo ticks allowed (true by default). While false every frame renders mono through the wrapper.
void seqSetStereoAllowed(bool allowed);

// Asked by the per-eye hook in the engine's own chain before it writes eye L's view. Ready: the tick can
// be a stereo one (allowed, the eye tags in step); a tick whose eye L view was written is rendered as a
// pair. NeedsBase: the eye tags need a new base first; the hook leaves the game's view (shown mono) and
// marks the tick wanted (seqMarkWanted), and the frame-end job takes the base on that mono frame, so the
// next tick can pair. Off: mono.
enum class SeqReadiness { Off, NeedsBase, Ready };
SeqReadiness seqStereoReadiness();
void seqMarkWanted();

// The eye whose chain is running: Right inside the eye R render the wrapper started, Left otherwise (the
// engine's own chain, which is eye L on a stereo tick and the only view on a mono one).
stereo_seq::Eye seqChainEye();

// The per-eye hook wrote (or, with the same-view test, would have written) `eye`'s view for game frame
// `tick` in the current chain. Without this for eye L the tick renders mono.
void seqMarkEyeView(stereo_seq::Eye eye, std::uint64_t tick);

// The game frame eye L's view was written for, inside the eye R render of that tick.
std::uint64_t seqRightTick();

// Called at hook points inside eye R's chain: records how deep below the wrapper's call it runs on the
// wrapper's own stack (docs/VR_STEREO.md, "Stack").
void seqNoteNestedStack();

// Present hook (render thread): the tag of the frame this present shows.
stereo_seq::PresentMatch seqTakePresent();

// Inside a backend frame's render jobs (before its present): that frame's tag, found by the backend
// counter it will present with (the counter read now, plus one). nullopt when it has none.
std::optional<stereo_seq::RenderTag> seqTagInFlight();

struct SeqCounters {
    std::uint64_t frameEnds = 0;       // wrapper calls in the engine's own chain
    std::uint64_t stereoTicks = 0;     // eye R renders started
    std::uint64_t rightFrameEnds = 0;  // wrapper calls inside eye R's chain
    std::uint64_t guardBusy = 0;       // eye R skipped: the render-frame guard was still set
    std::uint64_t stackSkips = 0;      // eye R skipped: too little stack left for its chain
    std::uint64_t guardTrips = 0;      // eye R skipped: the multiplayer guard tripped after eye L
    std::uint64_t noBackendFrames = 0; // frames the engine ended without a backend frame
    std::uint64_t drains = 0;          // waits for an idle render thread (tag base)
    std::uint64_t drainFailures = 0;
    std::uint64_t unverifiedBases = 0; // bases taken from a quiet period, not from the frame counts
    std::uint64_t prevRewrites = 0;
    std::uint64_t prevKept = 0;
    std::uint32_t renderFrames = 0;  // renderSystem + 0x10
    std::uint32_t backendFrames = 0; // renderBackend + 0xB0
    std::int32_t swapInterval = -1;  // r_swapInterval as the render thread reads it (-1: unknown)
    std::size_t deepestNested = 0;   // bytes: deepest eye R chain point seen below the wrapper's call
    std::size_t leastHeadroom = 0;   // bytes: least stack left at an eye R call (0: none or unknown)
    stereo_seq::EyeTagQueue::Stats tags;
    bool tagsSynced = false;
};
SeqCounters seqCounters();

} // namespace evr::vkcore
