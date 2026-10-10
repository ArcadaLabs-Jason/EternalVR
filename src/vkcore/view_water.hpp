#pragma once

// Parallel Eye Rendering: the water's simulation steps once a frame, both views bind the same ripples and
// caustics, and each view keeps its own grid matrix (docs/VR_STEREO.md "Parallel Eye Rendering"; RVAs in
// Steam build 25216728). Route S's per-eye water handles its two renders per tick; this is for the two
// views of one frame.
//
// Each view's render-view job 0x1C575F0 calls the water setup (RVA 0x1CE3A90, from RVA 0x1C5792A) with its
// water context (render context + 0x705B78), into which it stored the world's water state (+0xE0, render
// world + 0xB4550, one per world) and the backend frame counter (+0xF0). The setup decides from the state
// which parts of the simulation the frame steps (the waves' FFT, +0x1125; the ripples, +0x1126; the
// caustics, +0x1127), moves the state's indices on (+0xC30 displacement, +0xC34 caustics ring, +0xC38
// ripples) and binds the grid matrix of the render before (+0xC48), which it then overwrites with its own;
// the view's water job (RVA 0x1CE2A30) reads the state through the context again (RVA 0x1CE2B1A) and steps
// what the setup asked for (the job's FFT reads the displacement index again then). With two views every
// frame moved the indices on twice: both views stepped the ripples into the same two images, so they ran at
// twice their speed with the eyes one step apart; the caustics ring moved two slots; the displacement index
// flipped back, so a view could bind a displacement image its own step had not written; and each view's grid
// took the other view's matrix as its previous one. The two views' setups and jobs also ran at the same time
// on that one state.
//
// Here view 1's setup and job work on a copy of the state (view_water_start.hpp: the world's, with the
// fields the setup decides from taken from the state view 0's setup started from, and view 1's own grid
// matrix), set into view 1's context at the setup's entry; the engine stores the world's again before the
// next setup. So view 1 takes view 0's decisions and binds the images view 0 does, its matrix stays its own,
// and the world's state is changed by view 0 alone. At the setup's end view 1's ripple and caustics steps are
// cleared (their images are shared: view 0 steps them); its waves step stays, since the displacement and the
// FFT images are view 1's clones (view_clones.cpp), which only view 1's own step writes. When view 0's setup
// ended with no water in view while view 1's sees water, view 1's steps stay instead, the fields its setup
// wrote go back into the world's state and its job runs on the world's (view_water_start.hpp,
// view1StepsWorld; checked at view 1's setup end and at its job's state read). At the job's state
// read view 1's copy takes the world's grid mesh (+0xC18), which view 0's setup remakes after a grid
// resolution change. View 1 never remakes the world's grid mesh: its call of the make (RVA 0x1CE3430) on its
// copy waits for view 0's setup and takes the world's mesh instead, so view 0 cannot free a mesh view 1 holds
// (the make frees the old mesh at once). The setups wait for nothing else, except view 1's entry for view 0's
// setup when the grid mesh or the wave spectrum is to be remade (r_waterGridResolution or r_waterQualityFFT
// changed: a map's first water frame, a water setting changed), at most 30 ms; the spectrum flag one view's
// setup took (it clears the cvar's "modified") is given to the other's job too. When view 0 is not rendered
// (ETERNALVR_TEST_VIEW_ONLY=1) or view 0's setup of a later frame already began, view 1's render is left to
// the engine, counted.
//
// On with Parallel Eye Rendering; ETERNALVR_TEST_PE_WATER=0 leaves it out. Every 10 s one `view-water:` line
// counts view 1's renders by where they started from, those left to the engine and why, the steps cleared
// and whether view 1 decided as view 0 did.

#include <cstddef>

namespace evr::vkcore {

// From view_install.cpp's checks, before anything of the game is changed (the redirects hook RVA 0x1C57880,
// one of the sites checked, later): checks the code it relies on byte by byte; a miss logs the bytes found
// and those expected.
void prepareViewWater(const std::byte* base);

// From view_install.cpp after the engine changes: installs the hooks (inert until Parallel Eye Rendering is
// active) when every check passed. A miss logs and leaves it out; Parallel Eye Rendering runs without it.
void installViewWater(const std::byte* base);

// At the start of each two-view dispatch (view_shared_state.cpp): whether view 0 is rendered this frame.
void viewWaterFrameStart(bool view0Dispatched);

} // namespace evr::vkcore
