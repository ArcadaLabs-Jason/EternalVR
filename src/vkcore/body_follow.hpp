#pragma once

// Room-scale body follow in the game (features/roomscale/body_follow.hpp, docs/VR_ROOMSCALE.md "Body
// follow"): what the camera hook and the user-command hook hand each other.
//
// - The camera hook (room_scale.cpp) reads the player's origin, works out the move toward the head and
//   publishes it here in the room frame.
// - The user-command hook (usercmd_hook.cpp) adds it to the next command, turned into the game's view
//   frame, unless the stick or keys already move the player or jump or dash is pressed; it notes those,
//   and when a follow move went out, for the camera hook's next frame.
//
// Threads: the camera hook and the game's command build; everything here is atomic or under one lock.

#include "features/input/usercmd_injection.hpp"
#include "features/input/usercmd_motion.hpp"
#include "features/roomscale/body_follow.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::body_follow {

// Camera hook: the player's origin (the feet; world, game units) from its physics object
// (docs/rig-findings/collision-query.md section 2), checked against the game's eye `eye`. When the
// physics read is not plausible the eye itself stands in (logged once): its height moves with crouching
// and the step-up spring, its horizontal position with the body. nullopt when `player` is not the idPlayer.
std::optional<Vec3> playerOrigin(const std::byte* player, Vec3 eye, float unitsPerMetre);

// Camera hook: the height of the player's feet (game units) from its physics object alone, for bHaptics'
// landings; nullopt when `player` is not the idPlayer or the read fails.
std::optional<float> feetHeight(const std::byte* player);

// Camera hook: the move body follow asks for this frame (room frame; zero for none).
void publish(roomscale::FollowMove move);

// User-command hook, once per command of local user 0. `moving`: the command already moves the player
// (the stick or the keyboard); `jumpOrDash`: it jumps or dashes; `gameplay`: the game takes the command
// (no menu, no suppressed buttons); `viewYawRoom`: the game view's yaw in room space (radians,
// locomotion_direction.hpp convention). Returns the move to add to the command's move axes.
input::MoveAxes commandMove(bool moving, bool jumpOrDash, bool gameplay, float viewYawRoom);

// Camera hook, the command test (ETERNALVR_TEST_MOVE): the move value (negative: back or left) every
// command sends from now on along forward or right, instead of body follow's (0: none).
void setTestCommand(int value, bool sideways);

// User-command hook install: whether the command hook is in place (body follow needs it).
void setCommandHook(bool installed);

struct CommandState {
    bool hookInstalled = false;
    bool stick = false;      // a command since the last call moved the player by the stick or keys
    bool jumpOrDash = false; // a command since the last call jumped or dashed
    bool commanded = false;  // a follow move went into a command within the resume delay
};

// Camera hook, once per frame: the commands since the last call.
CommandState takeCommandState();

// The commands that carried a follow move, and the largest move axis sent since the last call.
std::uint64_t followCommands();
int takePeakAxis();

} // namespace evr::vkcore::body_follow
