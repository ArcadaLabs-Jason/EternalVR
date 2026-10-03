#pragma once

// What aims melee and the shoulder-mounted launchers under hand aim (issue #11, docs/VR_CONTROLLERS.md "Melee
// and equipment aim").
//
// Under hand aim the game's view angles follow the weapon hand: each game frame the camera hook moves them to
// the body plus the hand's ray (head aim's closed loop, aim_hooks.cpp). Two actions may aim with something
// else:
//
//   ETERNALVR_MELEE_AIM      head / offhand   melee, Blood Punch, glory kills and use (one button in the
//                                             game); a punch too
//   ETERNALVR_EQUIPMENT_AIM  head / offhand   the equipment launcher and the Flame Belch, which sit on the
//                                             Slayer's left shoulder; the off hand's throw gesture too
//
// Unset, the weapon hand aims them as before. Neither does anything under head or view aim.
//
// The equipment launcher and the Flame Belch aim from the shoulder launcher's muzzle joint, which follows the
// weapon hand with the arms model, not from the view angles (static RE, vkcore/equipment_launch_hook.cpp):
// the layer turns their launches and shots in its hooks and runs this policy for melee alone. The policy
// itself handles both actions.
//
// - The game takes a melee, glory kill or use target from its view angles when the press reaches it. So a
//   press that needs another target is held back until the camera hook has written that target at least
//   once (`targetWritten` reaches the target's generation), and at most kMaxHoldBackSeconds; the command
//   built after that write carries the press, and the game frame that processes it starts from the new
//   angles.
// - The target stays while a button of an action that aims with it is held, while the game forces the view
//   (a melee lunge, a glory kill), and for kKeepSeconds after both, so the punch and the lunge finish on
//   it. Then the weapon hand aims again at once. The view the player sees is the body plus the head
//   whatever the target, so neither change moves the picture: only the game's own angles turn.
// - A press of the other action takes the target over; a press of an action that keeps the weapon hand ends
//   the other's target first, held back the same way. A press whose target is chosen but not yet written
//   waits too; one still waiting when another press takes the target elsewhere goes out at once.
//
// Pure: no Windows, OpenXR or game memory. The mapper runs it on its command; the camera hook reads the
// target and reports the generation it wrote.

#include "game/eternal/game_action.hpp"

#include <cstdint>

namespace evr::input {

// What aims one action under hand aim.
enum class ActionAimSource : std::uint8_t {
    Same,    // the weapon hand, as every other action (default)
    Head,    // where the head looks
    OffHand, // the off hand's aim ray
};

const char* actionAimSourceName(ActionAimSource source);

// The actions that can aim with something else.
enum class AimedAction : std::uint8_t {
    None,
    Melee,     // GameAction::Melee
    Equipment, // GameAction::Equipment and GameAction::FlameBelch
};

const char* aimedActionName(AimedAction action);

struct ActionAimSettings {
    ActionAimSource melee = ActionAimSource::Same;
    ActionAimSource equipment = ActionAimSource::Same;

    [[nodiscard]] bool any() const {
        return melee != ActionAimSource::Same || equipment != ActionAimSource::Same;
    }
};

// The longest a press waits for its target (a frame or two is usual).
inline constexpr float kMaxHoldBackSeconds = 0.1f;
// How long the target stays after the action's button is let go and the game no longer forces the view.
inline constexpr float kKeepSeconds = 0.5f;

struct ActionAimInput {
    game::GameActionSet actions; // the mapper's actions this command
    // The camera hook writes the game's view angles now (hand aim on, the controllers attached, no forced
    // view, no menu): a press is held back only then.
    bool retargetable = false;
    bool forcedView = false;         // the game drives the view: the target stays
    std::uint64_t targetWritten = 0; // the latest target generation the camera hook wrote
    float dtSeconds = 0.0f;          // since the previous update (non-finite or negative counts as 0)
};

struct ActionAimOutput {
    game::GameActionSet actions;                    // to send: the input's, less a press being held back
    ActionAimSource target = ActionAimSource::Same; // what the view angles follow now
    std::uint64_t generation = 0;                   // bumped at every change of `target`
    AimedAction action = AimedAction::None;  // the action the target belongs to (None: the weapon hand)
    bool holdingBack = false;                // a press waits for its target
    bool released = false;                   // a held-back press goes out with this command
    AimedAction pressed = AimedAction::None; // ... the action it belongs to
    bool overtaken = false;                  // ... because another press took the target elsewhere
    bool timedOut = false;      // ... because kMaxHoldBackSeconds passed, not because the target was written
    float waitedSeconds = 0.0f; // how long it waited (with `released`)
    int waitedCommands = 0;     // commands it was held back from
    bool ended = false;         // the target went back to the weapon hand this update
};

class ActionAim {
public:
    explicit ActionAim(ActionAimSettings settings = {});

    ActionAimOutput update(const ActionAimInput& input);

    // Back to the weapon hand with nothing held (the controllers' input went stale).
    void reset();

    [[nodiscard]] const ActionAimSettings& settings() const { return settings_; }
    [[nodiscard]] std::uint64_t generation() const { return generation_; }

private:
    ActionAimSource sourceFor(AimedAction action) const;
    void setTarget(ActionAimSource target, AimedAction action);

    ActionAimSettings settings_;
    game::GameActionSet previous_;
    ActionAimSource target_ = ActionAimSource::Same;
    AimedAction action_ = AimedAction::None;
    std::uint64_t generation_ = 0;
    float keepSeconds_ = 0.0f;
    // A press waiting for its target: the actions held back (sent at least once when it ends, even if their
    // buttons were let go meanwhile), how long and over how many commands.
    game::GameActionSet heldBack_;
    AimedAction heldAction_ = AimedAction::None;
    float waitedSeconds_ = 0.0f;
    int waitedCommands_ = 0;
};

} // namespace evr::input
