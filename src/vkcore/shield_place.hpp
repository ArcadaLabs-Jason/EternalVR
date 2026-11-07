#pragma once

// The Chaingun's Energy Shield in front of the gun in the hand (xr_math/shield_pose.hpp, DECISIONS T-217).
//
// Static RE (Steam build 25216728): the shield's entity (idEnergyShell) thinks once a game tick (0xBC82A0,
// from its vslot 0x2A0). It puts itself at the player's eye with an axis from the view angles, runs what the
// shield does (the damage it takes and deals, measured from the player), and last sends its own origin and
// axis to its effect: a transform built at [rbp+0x80] (origin at +0x8, axis at +0x20) and passed in rdx to
// the entity's vslot 0x4E0 (0xB63430), which queues it for the effect manager. A mid hook on that call (RVA
// 0xBC9670: the shield in rsi, its owner in r13) moves the transform alone in front of the gun, along the
// ray the shots take, so the entity, and with it everything the shield does, stays the game's. That transform
// alone does not move the shield, though: its effect's actions track their entity (FX_ORG_TRACK_POS), and
// every frame the effect's owner (idFXEntityActionGroupOwner, vslot 0x18, 0xBD7370) asks the entity for its
// pose (vslot 0x3A0: idAnimatedEntity's, 0xB5B850, shared by many classes; with a physics object, its origin
// and axis, which the think put at the eye). A second mid hook at that path's tail (RVA 0xB5B8C2: the entity
// in rdi, the origin out at rsi, the axis out at rbp) moves the answer by the same move, for the placed
// shield alone (compared with the entity the think placed; any other entity passes untouched). Its scale goes
// through the entity's own effect scale (idEntity, vslot 0xB80, 0xBE0E80: the scale for the effect group
// at +0xB30), sent every think while the shield is placed, and the game's 1 once after.
//
// Only for the local player's shield, with the controllers' viewmodel on: under hand aim the ray is the
// weapon hand's and the gun the one the viewmodel hook placed this tick; under head aim the ray is the
// rendered head's along the view and the gun the game's in front of the face. Forced views (glory kills,
// cutscenes), a stale hand or a gun not placed this tick keep the game's shield. Fail closed: the three
// signatures unique, nothing installed otherwise, nothing written while the multiplayer guard refuses.

namespace evr::vkcore::controllers::shield_place {

// Finds and hooks the shield's effect transform (logged); false leaves the game's shield.
bool install();

} // namespace evr::vkcore::controllers::shield_place
