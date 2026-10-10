#pragma once

// Parallel Eye Rendering: view 1's own copies (clones) of the screen-sized targets and images the engine
// keeps one of for the frame (the post-process chain, the scene target, depth and its downsample chain,
// history targets, the screen target), and view 1's own passes where the engine has one
// (view_one_passes.cpp). The per-view setups' stores of those targets into view 1's render context store view
// 1's clones, and view 1's passes bind them (view_clone_binds.cpp). On with Parallel Eye Rendering;
// ETERNALVR_TEST_VIEW_CLONES=0 leaves view 1 on the engine's targets (an experiment: both views then draw
// into the same images).
//
// The clone set is made on the first two-view frame and again when the engine's targets change (a resize, or
// a map load that frees unreferenced scratch images: up to 64 remakes). A rebuild keeps each image clone by
// its name; the engine's resize purges them all after its device idle and the next build allocates them
// again under their names (view_clone_make.hpp). The 32nd change of the targets themselves (the first set
// counts as one of 32 builds; 8 with ETERNALVR_TEST_VIEW_CLONE_NAMES=build) turns the clones off for the
// process (logged), and view 1 is then not rendered. Each build logs every clone (view_clone_census.cpp).

#include <cstddef>

namespace evr::vkcore {

// From view_install.cpp's install, before anything of the game is changed: every site the hooks need is
// checked and the clones' memory is taken. False when one is not as known (nothing changed).
bool prepareViewClones(const std::byte* base);

// Then, after the code bytes are changed: the hooks (inert until Parallel Eye Rendering is on). False if one
// fails (the hooks installed before it stay, inert).
bool installViewClones();

// On the render thread before the views of a two-view frame are dispatched: makes or remakes the clones and
// refreshes view 1's device context copy. True when it made a new set: the images view 1 renders into (and
// the eye copy reads) are then new ones the frames sent before did not write (view_frames.hpp).
bool viewClonesPrepare();

// The clones turned off for the process (the engine's targets changed too often): the dispatcher then sends
// view 0 alone. False with ETERNALVR_TEST_VIEW_CLONES=0, which leaves view 1 on the engine's targets.
bool viewClonesStopped();

// View 1's clone of the engine's post-process final image (RVA 0x66E3208's colour), or null: the eye copy's
// eye 1 with ETERNALVR_TEST_EYE_COPY=1 (presenter_eyes.hpp).
void* viewClonesFinalImage();

// View 1's screen pass output (its clone of the screen target dc+0x508's colour), or null: the eye copy's
// eye 1 by default.
void* viewClonesScreenImage();

} // namespace evr::vkcore
