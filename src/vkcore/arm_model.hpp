#pragma once

// The arms model the hands are given (ETERNALVR_ARM_MODEL, rig tests only; docs/VR_HANDS_HUD.md, "Arms
// model"). Nothing is installed and nothing changes without the variable (the default is `game`).
//
// The first-person arms are one model owned by the hands (idHands, idPlayer+0xD2C8): the render model at
// idHands+0x370, its md6 decl at +0x4D0. The game sets it only through idHands::SetModel(hands, decl)
// [static, build 25216728: Steam RVA 0x1369450, Game Pass 0x136DA30], called from two places:
// idPlayer::InitHands (Steam 0x13EAB10, GP 0x13EF0F0; the player's hands decl, at spawn and on a map load,
// followed by the hands' kit) and the cosmetic skin's apply (Steam 0x1355EA0, GP 0x135A480; the skin's
// first-person md6 decl, followed by the skin's kit). Weapon switches never set it; they apply the weapon's
// kit to whatever model the hands have. A mid hook on each of the two calls hands SetModel the candidate
// decl instead of the game's, so the game's own code frees and builds the model, its animator, its joint
// modifiers (and the layer's room among them, offhand_mods.hpp) and the kits, at the moment it would anyway.
//
//   game   no change
//   base   the decl the game's inherits from whose name ends in fp_hands.md6 (the game's own,
//          md6def/player/human/base/praetor.md6, has md6def/player/human/base/fp_hands.md6 as its parent
//          on the rig): the base hands model, mesh md6/player/human/base/assets/mesh/arms.md6mesh (79
//          joints, no blade or forearm device), found by following the decl's parent (+0xB8) up to four
//          steps
//   skin   a Slayer skin's first-person decl,
//          md6def/customization/player/human/set<N>/base/praetor_set<N>.md6 (ETERNALVR_ARM_MODEL=skin:<N>,
//          set 3 by default, which gameresources.resources always holds),
//          found by name in the game's md6 decl list (the resource list for the class idDeclMD6, then its
//          find by name, as idDeclInfo::FindWithInheritance looks a name up before loading anything)
//
// A candidate is used only when it is a decl of the same class as the game's (the same vtable); otherwise
// the game's decl is kept and the reason logged once. Fail closed: the call sites, SetModel and the lookup
// functions are found by signature (each unique in the Steam and the Game Pass builds) and checked byte by
// byte at install; nothing is swapped while the multiplayer guard refuses. Game thread (the callers') only.

namespace evr::vkcore::controllers::arm_model {

// Finds and hooks the two calls of SetModel for the setting's model (logged); false leaves the game's.
bool install();

} // namespace evr::vkcore::controllers::arm_model
