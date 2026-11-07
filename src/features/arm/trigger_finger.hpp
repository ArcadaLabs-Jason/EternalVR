#pragma once

// The weapon hand's index finger on the trigger (docs/ROADMAP.md feature step 4; docs/VR_HANDS_HUD.md, "The
// index finger on the trigger"): while the layer poses the arm that holds the gun, its index finger follows
// the weapon-hand controller's trigger. Off the trigger (not touched) it lifts; touched, it rests on the
// trigger; pulled, it closes onto the trigger.
//
// Each pose is the game's own grip of the held gun with the index finger's three bones opened some share of
// the way toward straight: each bone's turn in its parent's frame scaled down about its own axis, the finger
// root and the other fingers left as the game holds them. Pulled is the game's grip itself (share 0), the
// pose the game's animators put the finger on the trigger in; resting is a little open (kRestOpen), the
// finger's pad just off the trigger's face; lifted is the held gun's own share ([trigger_finger] row,
// game/eternal/ weapon_offsets.hpp). So the finger only ever moves between the game's grip and a straighter
// finger along the same bend: never further into the gun than the game puts it, and the scale works on either
// hand (the left hand's bones are the right's reflected) with no axis assumed.
//
// A controller without a touch sensor on its trigger (a Vive wand, a Windows Mixed Reality controller) never
// reports a touch, so until the trigger has been touched since the controller was last tracked the finger
// rests on the trigger instead of lifting off it.
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_joints.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/hand_pose.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::arm {

// Whether the finger follows the trigger: on the New arms path only, as the free off hand's fingers (T-160):
// the free off hand (ETERNALVR_OFFHAND=free) and the bind solve (ETERNALVR_ARM_SOLVE=bind), which the
// launcher sends whenever the weapon hand aims with motion controllers. Elsewhere the finger is the game's
// (DECISIONS T-221).
constexpr bool followsTrigger(bool freeOffHand, bool bindSolve) {
    return freeOffHand && bindSolve;
}

// The weapon-hand controller's trigger.
struct TriggerSensors {
    float trigger = 0.0f; // 0..1
    bool touch = false;   // a finger on the trigger
};

// The share the index finger opens from the game's grip toward straight while it rests on the trigger: on the
// Heavy Cannon's grip its tip 7 mm forward of where the game holds it on the trigger.
inline constexpr float kRestOpen = 0.1f;
// A trigger value up to this counts as let go (an analog trigger at rest reads a little above 0).
inline constexpr float kTriggerFree = 0.02f;
// The largest lift a [trigger_finger] row may give: straight.
inline constexpr float kMaxLift = 1.0f;

// The share for these sensors: `lift` while the finger is off the trigger (not touched, not pulled, and the
// trigger's touch sensed since the controller was tracked: `touchSensed`), else kRestOpen falling to 0 as the
// trigger is pulled. Values out of range or not a number are clamped (a trigger to 0..1, `lift` to
// 0..kMaxLift).
float triggerOpen(const TriggerSensors& sensors, bool touchSensed, float lift);

// The share eased toward triggerOpen's (an exponential approach, `seconds` to close two thirds of the way, as
// the off hand's fingers, FingerEase), and whether the trigger's touch has been sensed: forgotten while the
// controller is not tracked (another controller may come back). The first update after a reset takes the
// share as it is.
class TriggerFinger {
public:
    static constexpr float kSeconds = 0.06f;
    float update(
        const TriggerSensors& sensors, bool tracked, float lift, float dtSeconds, float seconds = kSeconds);
    // The arm given back: the share starts again from the sensors (the touch sensed is kept).
    void reset() { started_ = false; }
    [[nodiscard]] bool touchSensed() const { return touchSeen_; }

private:
    float now_ = 0.0f;
    bool started_ = false;
    bool touchSeen_ = false;
};

// The index finger's three bones in the hand's joints (kRightHandJointNames order: Index1, Index2, Index3),
// and their modifiers among the fifteen finger modifiers (kFingerMods order, bit k for modifier k).
inline constexpr std::array<std::size_t, 3> kIndexJoints{4, 5, 6};
inline constexpr std::uint16_t kIndexModMask = 0x38;

// The finger modifiers (bit k for kFingerMods[k]) on the joints of kIndexJoints: what kIndexModMask must be.
constexpr std::uint16_t indexModMask() {
    std::uint16_t mask = 0;
    for (std::size_t k = 0; k < kFingerModCount; ++k) {
        for (const std::size_t joint : kIndexJoints) {
            mask = kFingerMods[k] == joint ? static_cast<std::uint16_t>(mask | (1u << k)) : mask;
        }
    }
    return mask;
}
static_assert(kIndexModMask == indexModMask(),
              "kIndexModMask names the index finger's modifiers in kFingerMods");
static_assert(std::string_view(kRightHandJointNames[kIndexJoints[0]]) == "RightHandIndex1" &&
                  std::string_view(kRightHandJointNames[kIndexJoints[1]]) == "RightHandIndex2" &&
                  std::string_view(kRightHandJointNames[kIndexJoints[2]]) == "RightHandIndex3",
              "kIndexJoints are the index finger's bones");

// Whether the arm ticked holds the gun: the weapon arm's own tick (the model's right arm), or with the weapon
// in the left hand (both gloves held, vkcore/weapon_arm.cpp holding()) the left arm; the right arm there is
// the off hand's glove.
constexpr bool holdsGun(bool weaponArmTick, ArmSide side) {
    return weaponArmTick || side == ArmSide::Left;
}

// What the weapon arm writes for the index finger this tick.
struct IndexChoice {
    enum class From : std::uint8_t {
        Game,  // nothing: the game's finger
        Glove, // the glove's grip opened by `open` (all fifteen fingers are the glove's writes anyway)
        Grip,  // the game's own grip of the held item as last read clean, opened by `open`: only `mask`
               // written
    };
    From from = From::Game;
    float open = 0.0f;
    std::uint16_t mask = 0;
};

// The choice from this tick's share (`open`, nullopt for the game's finger), whether the glove is on a grip
// (`glove`), whether the game's grip was read clean for the item held now (`gripRead`: a read taken for
// another item, as after a switch the arm was not given back for long enough to read, does not count), and
// the arm's weight (0 to 1, which the share goes with on the game's grip; the glove's own curl carries it
// there).
IndexChoice indexChoice(std::optional<float> open, bool glove, bool gripRead, float armWeight);

// `hand` (each joint in its parent's frame) with each of the index finger's bones turned `open` of the way
// from its turn toward none: the turn's angle scaled by 1 - open about the same axis. 1 is straight; below 0
// bends the finger past the grip (the rig's planted fault). Positions and every other joint as they are.
HandLocal openIndex(const HandLocal& hand, float open);

} // namespace evr::arm
