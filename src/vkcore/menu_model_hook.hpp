#pragma once

// The 3D model of a menu on the world-locked menu panel (docs/VR_MENUS.md, "The menu's 3D model").
//
// The weapon in the weapon mod screen and in the Dossier's customize screen is an idSWFWidget_Model. Every
// tick its Think calls idMenuWidget_3D_Stand::UpdatePosition (RVA 0x15A6D30 in build 25216728), which asks
// the current world for its render view (RenderViewForIndex(0), the idRenderView whose renderView_t `g` is
// at +0) and places the model in the world in front of that camera: at vieworg + viewaxis * (the ray
// through its GUI element, from fov_x / fov_y, r_znear deep, plus the widget's offset). The layer makes
// that view the head (and each eye in turn), so the model stayed in front of the eyes while the GUI is on
// the panel.
//
// Two mid hooks in UpdatePosition, only while the panel shows the UI quad over a head-tracked frame and the
// multiplayer guard allows game touches:
// - after RenderViewForIndex returns (RVA 0x15A6D97: `mov rbx, rax; test rax, rax; je`): rax becomes this
//   thread's copy of that idRenderView (all 0x29950 bytes) with the panel camera's origin, axis and field
//   of view (features/menu/model_camera.hpp). The function reads nothing else from it.
// - after the model's position is written (RVA 0x15A7134, rbx still the copy): the position (r12) and
//   scale (rsi) are magnified about the panel camera so that the model sits on the panel's plane.
//
// ETERNALVR_MENU_MODEL_PANEL=0 leaves the model as the game places it; `near` keeps only the panel camera
// (the model stays r_znear in front of that camera, not on the panel).

#include "features/menu/model_camera.hpp"

#include <cstdint>

struct XrCompositionLayerQuad;

namespace evr::vkcore::menu_model {

// Locates and installs both hooks once per process (later calls return the first result); logs the RVAs,
// or why it is off.
bool install();

// XR worker, every rendered frame: whether the menu panel shows the UI quad over a head-tracked frame
// (`showing`), and then the quad as placed on the panel (LOCAL) and the whole GUI image's size in pixels.
void notePanel(bool showing,
               const XrCompositionLayerQuad& quad,
               std::uint32_t imageWidth,
               std::uint32_t imageHeight);

// Camera hook, every head-tracked game view: the head in LOCAL and as the game draws it, and the field of
// view the game asked for (its own fov_x, before the layer's).
void noteHead(const menu::WorldHead& head, float gameFovX);

} // namespace evr::vkcore::menu_model
