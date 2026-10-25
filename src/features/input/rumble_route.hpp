#pragma once

// Which hand feels the game's rumble (docs/VR_CONTROLLERS.md, "Vibration"). On by default.
//
// The game mixes all its rumbles into two motors (docs/rig-findings/haptics.md) and says nothing about what
// made them, so the cause is guessed from what the player had just done, as the mapper saw it:
//
// - Weapon: the fire action was down, or went up within `kCauseWindowSeconds`: the recoil is the weapon
//   hand's.
// - Melee: a melee press (a punch or the button) within `kMeleeWindowSeconds` before: the hand that punched,
//   or the weapon hand for the button. A hit lands a little after the press, so its window is longer.
// - Shoulder: the equipment launcher or the Flame Belch went down within `kCauseWindowSeconds`: it sits on
//   the Slayer's left shoulder, so it is the off hand's.
// - Body: none of those (damage, an explosion, a landing, a shake): both hands, as without routing.
//
// The cause is chosen when a rumble starts (its level was under `kQuietLevel` before) or rises by a good step
// while it plays (another rumble stacked on it), from the most recent of those events, and kept until the
// rumble ends. The hands that feel it get the louder of the two motors, the other hand a quarter of the low
// motor (a supporting hand feels the weapon a little); a body cause is the old split: the low motor on both
// hands, the high one on the weapon hand. Pure: the vibration policy feeds it once a frame.

#include "features/input/controller_state.hpp"
#include "features/input/game_input.hpp"

#include <array>
#include <cstdint>

namespace evr::input {

enum class RumbleCause : std::uint8_t {
    Body,
    Weapon,
    Melee,
    Shoulder,
};

const char* rumbleCauseName(RumbleCause cause);

inline constexpr double kCauseWindowSeconds = 0.15;
inline constexpr double kMeleeWindowSeconds = 0.45;
// A rumble under this level (either motor, 0..1) counts as none.
inline constexpr float kQuietLevel = 0.02f;
// A rise of this much while a rumble plays is another rumble starting.
inline constexpr float kStackStep = 0.3f;
// What the hand that does not feel a rumble gets of its low motor.
inline constexpr float kSupportShare = 0.25f;

struct RumbleEvents {
    bool fireHeld = false;
    std::array<bool, 2>
        melee{};           // per hand (indexed by Hand): a melee press this frame, and which hand made it
    bool shoulder = false; // the equipment launcher or the Flame Belch went down this frame
};

// What one mapper frame says about the cause of the next rumble, added to `melee` and `shoulder` (the
// vibration collects them between its own frames): a melee press and the hand it belongs to (the hand that
// punched, else the weapon hand), and a press of the shoulder launcher.
void addRumbleCauses(const GameInput& input, Hand weaponHand, std::array<bool, 2>& melee, bool& shoulder);

struct RumbleLevels {
    std::array<float, 2> level{}; // per hand: 0..1, before the strength
    RumbleCause cause = RumbleCause::Body;
    bool changed = false; // the cause was chosen again this frame (for the log)
};

class RumbleRouter {
public:
    // `low` and `high` are the game's motors, 0..1.
    RumbleLevels route(double seconds, Hand weaponHand, const RumbleEvents& events, float low, float high);

    void reset();

private:
    bool fireDown_ = false;
    double firePressedAt_ = -1.0e9;  // when the fire action last went down
    double fireReleasedAt_ = -1.0e9; // the last time it was down
    std::array<double, 2> meleeAt_{-1.0e9, -1.0e9};
    double shoulderAt_ = -1.0e9;
    float previous_ = 0.0f;
    RumbleCause cause_ = RumbleCause::Body;
    Hand meleeHand_ = Hand::Right;
};

} // namespace evr::input
