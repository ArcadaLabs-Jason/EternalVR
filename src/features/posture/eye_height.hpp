#pragma once

// Maps the player's real head to the character's eye height (ARCHITECTURE section 6).
//
// The LOCAL reference space is re-anchored at session start and on recenter. Whatever height the
// player's head is at that moment becomes the character's eye height, so seated and standing players
// both see the world from the character's eyes with no setup. Movement after the anchor (leaning,
// crouching) is kept one-to-one. The character's eye height itself is game data
// (game/eternal/player_dimensions.hpp).

namespace evr::posture {

// Vertical offset to add to head poses in the anchor space so that the head height captured at
// anchor time lands exactly at `targetEyeHeight`.
//
// `anchorHeadHeight` is the head's Y in the anchor space when the anchor was taken. For a freshly
// re-anchored LOCAL space it is close to zero, but it is passed explicitly because recenter can
// happen some frames after the head sample used for it.
float eyeHeightOffset(float anchorHeadHeight, float targetEyeHeight);

} // namespace evr::posture
