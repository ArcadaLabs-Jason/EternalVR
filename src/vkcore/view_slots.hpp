#pragma once

// Parallel Eye Rendering: both eyes as two render views of one engine render, each with its own per-view
// storage (docs/VR_STEREO.md "Parallel Eye Rendering", docs/rig-findings/perf-multiview-slots.md). Steam
// build 25216728 only (checked by the PE timestamp): the sites are RVAs, not signatures.
//
// The build keeps per-view storage for one view in several places. Here the second view gets its own:
// - r_maxRenderViews is 2 from vkCreateInstance, and the renderer's static per-view block is moved to room
//   for two entries (view_block.cpp), so the engine initialises both.
// - The device context's view slot (inline, one entry): slot 1 lives in memory of ours; every site that
//   indexes the slot by view index gets the offset to it added when the index is 1.
// - The device context's occlusion-query state (one pointer, read by view index): view 1 reads a second one.
// - The render thread's per-view render context (one pointer): view 1 gets a second context, and the
//   per-view dispatcher runs once per view (its job descriptor and packet array hold one view).
//
// Multiplayer guard (docs/ARCHITECTURE.md section 4a): nothing is installed unless the guard is armed at
// vkCreateInstance. The dispatcher asks the guard for every frame and renders view 0 alone once it has
// tripped; the hooks that change view 1's work ask parallelEyesTouch(). The hooks that keep the engine's
// one-view storage safe for view index 1 (r_maxRenderViews stays 2), those that keep it consistent with the
// code bytes changed at install (view_redirects.cpp), and the locks and the one-view clamp that keep two-view
// frames' jobs apart (no-ops with one view) keep running after a trip, as they must.

#include "vkcore/parallel_eyes_settings.hpp"

#include <cstddef>

namespace evr::vkcore {

// The settings (read once; parallel_eyes_settings.hpp). On the first call, with ETERNALVR_PARALLEL_EYES=1,
// logs why it is off or that it is requested, and any warning.
const parallel_eyes::Settings& parallelEyesSettings();

// ETERNALVR_PARALLEL_EYES=1 and its conditions, on the build these sites know. On another build it logs once
// and stays false: the standard renderer (Route S), since two views without view slots crash the game. The
// request only: what was installed is viewSlotsActive and parallelEyesChangedEngine.
bool parallelEyesRequested();

// From the game's vkCreateInstance, before its present policy is decided (stereo_present.hpp), with the
// multiplayer guard installed first (view_install.cpp): every check of every step and the memory they take,
// then the storage hooks (inert), then the changes to the engine with the hooks that follow them (the block
// move, view 1's command contexts, the redirects and their code bytes, the clones), r_maxRenderViews last.
// Nothing changes unless requested, the build is the known one and the guard allows game writes. A failed
// check or storage hook leaves the game working as it was: the standard renderer (Route S) runs. A hook that
// fails after the first change leaves the changes made before it: Route S does not run on that engine; it
// renders view 0 alone for the session, as after a guard trip, shown as mono (logged "FAILED").
void installViewSlotsEarly();

// Fully installed: the two-view renderer runs (stereo_hooks.hpp: the TwoViews experiment without the
// environment variable). The second view keeps view index 1 (the per-eye hooks then leave its index and
// visibility alone).
bool viewSlotsActive();

// The install changed the engine: fully installed, or a hook failed after the first change. Route S and its
// present policy stay off then (taa_hooks.hpp routeSRequested, stereo_present.cpp, readStereoSettings).
bool parallelEyesChangedEngine();

// The question every hook that changes view 1's work asks: installed, and the multiplayer guard allows game
// touches; after a trip only until the frames the game built with two views before it have been rendered
// (each must finish the way it started, the dispatcher sends view 0 alone), false from the first one-view
// frame on.
bool parallelEyesTouch();

// The frame being presented rendered view 1 (view_frames.hpp: the last frames sent all did), so view 1's
// image is its own: the eye copy gives eye 1 the presented image otherwise.
bool viewSlotsView1Rendered();

// View 1's render context once built (null before): the per-view redirects tell view 1's work by it.
std::byte* viewSlotsContext1();

// View 0's render context (the engine's own), as the render thread last handed it to the dispatcher (null
// before the first two-view frame).
std::byte* viewSlotsContext0();

} // namespace evr::vkcore
