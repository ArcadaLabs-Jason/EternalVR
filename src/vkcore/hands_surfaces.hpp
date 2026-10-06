#pragma once

// The arms the layer poses, made visible on the hands model (docs/VR_HANDS_HUD.md, "The weapon's mesh
// kit"; features/arm/arm_surfaces.hpp).
//
// Each weapon decl picks a mesh kit for the first-person hands model, and most pick one that hides the
// right arm (the combat shotgun's "ArmLeft"), a few one that hides both ("HideAll": ballista, BFG,
// unmaykr). So the weapon arm's joints were posed on a surface that is not drawn. While the layer poses an
// arm (its weight above zero and the pose written this tick), this shows that arm's surfaces with the
// game's own Show; when it stops (weight zero: untracked, hands hidden, forced view, glory kill, melee,
// weapon switch; a rejected tick; release; a multiplayer guard trip) it hides with the game's Hide exactly
// the surfaces it showed, so the game-posed arm never pokes into view.
//
// The game's code [static, build 25216728, checked byte by byte at install]:
// - idHands' show/hide apply (0x137F690) takes the model at idHands+0x370 (renderModel) and jumps to the
//   model's apply (0x19C8590), which calls SetMeshKit (0x19CF4B0) with kit group 5 ("Body"), then for the
//   decl's per-surface lists FindSurfaces (0x19CEE30), Hide (0x19CF620) and Show (0x19D0340). Install
//   follows those calls, so the model, Show and Hide are the ones the game's kits use.
// - Show / Hide (model, surface index): set / clear bit `index` of the dwords at model+0x518 (128 bits)
//   and OR the byte at model+0x57C with 0x10 (the model's surfaces are rebuilt). Nothing else.
// - The surfaces by name (FindSurfaces): model+0x4D8 -> +0x78 an array of surface pointers, count at
//   +0x80; each surface's name (char*) at +0x8, compared up to a '$', ignoring case.
// - The kits (SetMeshKit): model+0x4D0 -> group g's kits at +0x3B8 + g*0x18 (0x48 bytes each), count at
//   +0x3C0 + g*0x18; a kit's name (char*) at +0x8, its surface indices (int*) at +0x30, their count at
//   +0x38.
// The arm's surfaces are found on each new model by name (arm_low_rt_base; the left arm's three), else as
// the surfaces of the kit holding only that arm ("ArmRight", "ArmLeft"); never by a fixed index.
//
// Fail closed: nothing is shown unless every check passed and the surfaces were found; a faulting call
// turns this off for the session (logged). Writes only while the multiplayer guard allows them, except the
// one give-back after a trip. Off-hand hook thread (the game thread running idHands::Update) only.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_surfaces.hpp"
#include "vkcore/game_text.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::hands_surfaces {

// Finds and checks the game's code (logged). False leaves every surface to the game's kits. `armsHidden`
// (ETERNALVR_ARMS=hidden): both arms' surfaces are hidden every tick instead, posed or not; after a
// multiplayer guard trip hiding stops (no give-back: arm_surfaces.hpp, planHiddenSurface).
bool install(const GameImage& image, bool armsHidden);

// Every tick of the arm on `side`: `posed` when the layer posed that arm this tick. Shows its hidden
// surfaces on the hands' model while posed, hides again the ones it showed otherwise.
void update(arm::ArmSide side, const std::byte* hands, bool posed);

// After a multiplayer guard trip, once: hides again what the layer showed on that arm. Waits for the hands
// it was shown on (another hands' tick passes). True once done (or nothing to give back).
bool releaseAfterTrip(arm::ArmSide side, const std::byte* hands);

// The arm's surfaces after the last update, for the trace.
arm::SurfaceState state(arm::ArmSide side);

} // namespace evr::vkcore::controllers::hands_surfaces
