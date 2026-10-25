#pragma once

// Stereoscopic cutscenes on the flat screen (ETERNALVR_CUTSCENE_3D, docs/VR_STEREO.md "Stereoscopic
// cutscenes"). A cutscene shown on the flat screen is normally one render of the game's own camera, the same
// image for both eyes. With the setting on (and Route S stereo running) the camera hook instead records the
// frame as a stereo pair of that camera: two eyes a comfortable separation apart along the camera's left
// axis, parallel, with off-axis frusta (xr_math/cinema_eyes.hpp). The per-eye hook then renders both from the
// record like any stereo frame; the XR frame loop shows the pair on two screen quads, one per eye
// (cinemaEyeLayers).

#include "vkcore/xr_presenter.hpp"

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>

namespace evr::vkcore {

// Camera hook, game thread, every game view, first (onGameView): the cutscene's and the glory kill's
// tracking, and their frames on the flat screen (as a stereo pair with cinema3dGameView). True: the frame is
// done (the flat screen's, or tracking is not ready yet); false: a head-tracked frame, which the caller
// writes.
bool flatScreenGameView(XrPresenter::Impl& p, std::byte* renderView, std::byte* player);

// Camera hook, game thread, every game view of a cutscene (or glory kill) shown on the flat screen, after the
// cinema FOV is written into the view. True: the frame was recorded as a stereo pair (the caller leaves the
// game's view as it is). False: not applicable (the setting is off, a glory kill, no Route S, alternate eyes,
// an unusable FOV); the frame goes to the flat screen as it always did.
bool cinema3dGameView(XrPresenter::Impl& p,
                      std::byte* renderView,
                      const std::byte* player,
                      bool cutsceneOnScreen);

// Camera hook, game thread, from prepareEyes for the head-tracked frames: ends a pair run once the cutscene
// is over, and true for the first few frames after a run ended (the shot changed back to the player: both
// eyes drop their temporal history; xr_math::CinemaRun).
bool cinema3dResetAfter(const XrPresenter::Impl& p, bool inCutscene);

// XR worker, the flat screen's quad for a frame of the ring (no menu): `quad` shows eye L's half (a mono
// frame fills both) at the screen's place and size.
void fillScreenQuad(const XrPresenter::Impl& p, XrCompositionLayerQuad& quad);

// XR worker, the screen without a menu over it: `left` (from fillScreenQuad) shows only a cutscene's band of
// the image, then cinemaEyeLayers. Returns the number of layers written into `layers` after `left`.
std::uint32_t screenLayers(XrPresenter::Impl& p,
                           XrCompositionLayerQuad& left,
                           XrCompositionLayerQuad& right,
                           XrCompositionLayerQuad& ui,
                           const XrCompositionLayerBaseHeader** layers,
                           bool& uiShown);

// XR worker: when the shown frame is a stereo cutscene pair (both eyes rendered), `left` becomes eye L's quad
// and `right` is made eye R's, each showing its half of the ring; eye R's quad, then the GUI's quad `ui` over
// the screen while the GUI image is fresh, go into `layers`, and `uiShown` says whether the game leaves the
// GUI out of the eye images (xr_math::cinemaLayerPlan). Returns how many layers it wrote: 0, and nothing
// changes, when it is not such a frame or the pair goes on the mono quad (no GUI image within the hold).
std::uint32_t cinemaEyeLayers(XrPresenter::Impl& p,
                              XrCompositionLayerQuad& left,
                              XrCompositionLayerQuad& right,
                              XrCompositionLayerQuad& ui,
                              const XrCompositionLayerBaseHeader** layers,
                              bool& uiShown);

// XR worker, every frame: whether the UI layer works (its GUI images exist and its swapchain did not fail).
// The camera hook records a cutscene as a pair only while it does: eye R's render has no GUI.
void cinema3dNoteUiLayer(bool working);

} // namespace evr::vkcore
