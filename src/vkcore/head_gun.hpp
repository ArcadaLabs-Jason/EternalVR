#pragma once

// Head aim's gun (input::GunPlacement::Head, DECISIONS T-143): the game places the arms model from its view,
// which under head aim is the head's, and the viewmodel hook moves that placement by the rendered head's
// offset from the game's eye, so the gun sits in front of the face the player sees (a lean or a crouch in
// the room included) and still points where the shots go. Its orientation, the game's weapon bob, recoil
// and animations stay the game's. Nothing here needs the weapon hand: a hand out of tracking does not move
// the gun. The offset is used only while it is current (input::headGunOrigin): when the views stop, the gun
// goes back to the game's placement as the view goes back to the game's eye.

#include "common/vector.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::head_gun {

// Camera hook (endGameView), every head-tracked view: the head's offset from the game's eye in game units
// (zero with head position off). It is the offset shots and traces may start from, clear of the walls
// (roomscale::ClearanceStep::validOffset: up to the first contact), not the offset the view was moved by:
// with the head in a wall the view goes the whole way (and fades), the gun only to the contact, so it is
// not drawn inside the wall.
void noteView(Vec3 headOffset);

// Viewmodel hook, under head aim, at a tick of the player's hands `hands`: the game's model origin at
// `originAt` (the render model's final origin, three floats) moved by the noted head offset. `eye` is the
// player's first-person origin, for the log. False, and nothing written, when the note is missing or not
// current, is from before these hands' first tick (a level load), or the origin cannot be read.
bool moveToHead(const std::byte* hands, std::byte* originAt, Vec3 eye);

// On detach: the note is dropped.
void forget();

} // namespace evr::vkcore::controllers::head_gun
