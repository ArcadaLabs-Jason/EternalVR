#pragma once

// Where the viewmodel hook puts the arms model and the gun on a tick of the player's hands, with the motion
// controllers on (docs/VR_CONTROLLERS.md "Head aim and the gun", DECISIONS T-143).
//
// - Hand aim: at the weapon hand's controller (T-054), except the fists of a melee or a Blood Punch while
//   melee aims with the head or the off hand: the punch's hit follows the fists on the hands model, so they
//   stay where the game's view puts them (issue #15).
// - Head aim: where the game's view puts it, carried by the rendered head's offset from the game's eye, the
//   way gamepad mode shows the gun (the game's own placement) but in front of the face the player sees. The
//   game's view is the head's under head aim, so the gun points where the shots go and the crosshair is;
//   with it at the weapon hand a low hand held the gun at the floor while the shots went where the head
//   looked. The arms model is then not at the hand, so both arms are the game's (offhand_policy.hpp,
//   `modelPlaced`), as in gamepad mode.
// - View aim: at the hand, as before (the game's own aim; nothing else is written).
//
// Pure: no Windows, OpenXR or game memory.

#include "features/input/action_aim.hpp"
#include "features/input/controller_settings.hpp"
#include "features/input/view_pose_clock.hpp"

#include "common/vector.hpp"

#include <cstdint>
#include <optional>

namespace evr::input {

enum class GunPlacement : std::uint8_t {
    Hand,     // at the weapon hand's controller, with the held item's row (viewmodel_offsets.toml)
    GameView, // where the game's view puts it, nothing written
    Head,     // where the game's view puts it, moved by the rendered head's offset from the game's eye
};

// `aim`: ETERNALVR_AIM; `fistsHeld`: the held item is the fists of a melee or a Blood Punch; `meleeAim`:
// ETERNALVR_MELEE_AIM.
GunPlacement gunPlacement(AimSource aim, bool fistsHeld, ActionAimSource meleeAim);

// GunPlacement::Head: the rendered head's offset from the game's eye as the camera hook noted it for a view.
struct HeadOffsetNote {
    Vec3 offset;              // game units
    std::int64_t written = 0; // on the views' clock (ViewPoseClock)
    bool valid = false;
};

// The model's origin `gameOrigin` (the game's placement) carried by the note's offset, or nullopt: the game's
// placement stays. Nullopt with no note, a note written before `notBefore` (the first tick of the player's
// hands: new hands are a level load, whose views noted the old level's lean), a note no longer current on
// the views' clock (the camera hook stopped noting, so the view went back to the game's eye: head tracking
// lost, a load, a freeze), or anything non-finite.
std::optional<Vec3> headGunOrigin(Vec3 gameOrigin,
                                  const HeadOffsetNote& note,
                                  std::int64_t notBefore,
                                  const ViewClockRead& views);

} // namespace evr::input
