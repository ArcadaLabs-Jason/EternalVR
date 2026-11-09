#pragma once

// Two-handed aim in the layer (ETERNALVR_TWO_HAND; features/input/two_hand.hpp, docs/VR_HANDS_HUD.md,
// "Two-handed aim").
//
// - The camera hook runs the hold once a game frame (beginGameView), right after the weapon hand's aim is
//   smoothed (the held aim is built from the aim as tracked and smoothed by the hold with the same
//   setting), and turns that aim in `poses` and in the view's record together, so everything that reads the
//   one-handed aim reads the two-handed one in the same frame: the view angles under hand aim, the shots, the
//   gun's placement and its sights, the aim dot. The turn eases in and out (input::kTwoHandRampSeconds).
// - The off-hand hook notes each tick whether the gun is up (input::gunUp) and which item the arms hold, and
//   asks gloveOnSupport() to put the off glove on the gun (the game's own grip of its support point, moved
//   along the gun to where the off hand is: gloveSupportMetres) instead of on its controller.
// - The mapper asks supportsWeapon(): the off hand's grip on the gun holds it, so its binding is held back
//   (MapperContext::supportHandOnWeapon), also on the frame it is pressed.
//
// With the setting off nothing here runs: every function returns at once and the aim is untouched.

#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace evr::vkcore::controllers::two_hand {

// The off-hand hook, each tick of the player's hands: whether the gun is up, and the arms' item
// (ArmsTick::arms).
void noteHands(bool gunUp, const std::byte* armsItem);

// The camera hook, after the weapon hand's aim is smoothed: `poses` (room space) and `weapon` (LOCAL, the
// view's record) turned to the line between the hands while the off hand holds the support point. `player`
// is the camera hook's (its death closes the gate).
void aim(State& s, const std::byte* player, GameViewPoses& poses, std::optional<WeaponAim>& weapon);

// The camera hook, on a frame it returns from before aim() (the controllers detached, gamepad mode), and
// aim() itself on a level load: lets go at once and forgets the grip. `why` is logged when a hold ends.
void reset(const char* why);

// The mapper: the hand whose grip holds the gun now (held, or where a squeeze would take it), the off hand of
// the hand the gun is in; nullopt for none, also when the camera hook has not run aim() for a while (a load).
std::optional<input::Hand> supportsWeapon();

// The off-hand hook: the off glove goes on the support point (held, as recently as the camera hook ran; not
// under the planted fault).
bool gloveOnSupport();
// ... and, then, how far the support point is from the weapon hand's grip (metres): the glove sits where the
// off hand is along the gun up to there (input::gloveSlide); nullopt when the glove is not on the gun.
std::optional<float> gloveSupportMetres();

// The rig's dump: whether the hold is on, and the support row in use (metres, the weapon hand's aim frame,
// reflected for the left hand), or nullopt for none; nullopt altogether with the setting off.
struct DumpState {
    bool engaged = false;
    std::optional<std::array<float, 3>> row;
};
std::optional<DumpState> dumpState();

} // namespace evr::vkcore::controllers::two_hand
