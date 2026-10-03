#pragma once

// Piloting a demon: the Cultist Base Revenant (e1m3_cult), the campaign's one section where the player
// controls another body. The Slayer (idPlayer) stays alive, controls a first-person idDemonPlayer that points
// back at it, and the game builds the demon's camera through the player's view. There the movement stick must
// follow the demon's own facing (the camera the game builds), not the Slayer's aim, and body follow must not
// push the demon toward the head. The offsets were read on Steam build 25216728; they are used on the known
// builds (checked through the idPlayer vtable, PlayerAim), the Game Pass one being the same code relinked.

#include "game/eternal/game_action.hpp"
#include "vkcore/player_aim.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers {

// The game view belongs to a first-person demon the local player controls. The game builds that view through
// the idPlayer, so for an idPlayer this follows its controlled entity (idPlayer +0x88B0); for any other
// entity it checks the entity itself. Either way the demon must be locally controlled, viewed in first
// person, and point back at that idPlayer (idDemonPlayer::player). Reads through SEH.
[[nodiscard]] bool isPilotedDemon(const std::byte* entity, const PlayerAim& player);

// The game view hook reports each view: whether it was a piloted demon. Logs the start and end of a
// piloting stretch.
void notePilotedDemon(bool piloting);

// A piloted demon's view was seen within the last half second, and the handling is on
// (ETERNALVR_DEMON_VIEW=0 turns it off, to compare with the old behaviour).
[[nodiscard]] bool pilotingDemon();

// While piloting, the actions the demon has its own binding for press that binding instead of the Slayer's
// bits: fire, the weapon mod (the Revenant's rocket barrage), dash and jump (its jetpack). Otherwise, or
// when the demon's table cannot be read, the buttons are returned unchanged.
[[nodiscard]] std::uint64_t pilotedDemonButtons(const game::GameActionSet& actions, std::uint64_t buttons);

// While piloting, the action whose demon binding presses any of `bits`: the game's prompts for the demon's
// abilities name the demon's buttons ("_quick2", the Revenant's rocket barrage, which the weapon mod
// presses). Nullopt when not piloting, when no demon binding has those bits, or when the table cannot be
// read.
[[nodiscard]] std::optional<game::GameAction> pilotedDemonAction(std::uint64_t bits);

// The piloted demon last seen (0 before any).
[[nodiscard]] std::uintptr_t pilotedDemon();

// Demon aim (demon_aim.cpp): the demon aims where the head looks, or under the demon's hand aim
// (input::demonAimSource) where the weapon hand points. The camera hook publishes those angles (id Tech
// degrees, the yaw relative to the body) for every view while piloting; the demon's update publishes the body
// yaw it aimed from, which the view is built on, and the yaw it aimed at. Nullopt when not piloting or the
// hook has not aimed within a quarter second.
struct PilotAim {
    float bodyYaw = 0.0f;
    float aimYaw = 0.0f;
};
void notePilotAim(float pitch, float yaw);
[[nodiscard]] std::optional<PilotAim> pilotAim();

// Installs the detour on the demon's update; false (logged) on a build without the known functions.
bool installDemonAimHook();

} // namespace evr::vkcore::controllers
