#pragma once

// The free off hand's fingers from its controller (docs/ROADMAP.md feature step 4, "Hand poses";
// docs/VR_HANDS_HUD.md, "Hand poses"): with the bind solve and the free off hand only, the glove opens when
// the controller is let go and closes as it is squeezed.
//
// Each finger has a curl from 0 (open) to 1 (closed) read from the controller's sensors: the middle, ring and
// little fingers follow the grip's squeeze, the index finger the trigger (resting a little bent while it only
// touches the trigger), and the thumb lies down while it touches a button, the stick or the thumb rest. The
// curls are eased so a touch sensor that flips does not snap a finger (FingerEase).
//
// The closed hand is the game's own grip of a gun's handle (the right hand of the Heavy Cannon's idle,
// grip_hand.hpp, reflected for the left hand): the fingers close round a handle as they do round every gun,
// so no finger goes through the palm. The open hand is the same hand with every finger bone straightened in
// its parent's frame (the bone's turn taken out; its offset, the bone's length, kept), the finger roots and
// the thumb's base joint as the grip has them: a flat hand, the thumb out to the side. In the rig's skeleton
// each finger bone runs along its parent's -y and bends about its x, so a straight finger has no turn
// [static: the Heavy Cannon idle's second and third bones of every finger turn about x alone, to 2e-5; the
// first bones also turn up to about 11 degrees sideways, which the open hand straightens too].
//
// Pure: no game memory, tested on its own.

#include "features/arm/grip_hand.hpp"

#include <array>
#include <cstddef>

namespace evr::arm {

using HandLocal = std::array<xr_math::ModelPose, kHandJointCount>;

// The controller's sensors the fingers follow.
struct FingerSensors {
    float grip = 0.0f;    // 0..1
    float trigger = 0.0f; // 0..1
    bool triggerTouch = false;
    bool thumbTouch = false; // a thumb on a face button, the stick, the trackpad or the thumb rest
};

// Each finger's curl, 0 open to 1 closed.
struct FingerCurls {
    float thumb = 0.0f;
    float index = 0.0f;
    float middle = 0.0f;
    float ring = 0.0f;
    float pinky = 0.0f;
};

// The index finger's curl while it rests on the trigger without pulling it.
inline constexpr float kRestingIndexCurl = 0.3f;

FingerCurls fingerCurls(const FingerSensors& sensors);

// The curls eased toward the sensors' (an exponential approach, `seconds` to close two thirds of the way);
// the first update takes them as they are.
class FingerEase {
public:
    static constexpr float kSeconds = 0.06f;
    FingerCurls update(const FingerCurls& wanted, float dtSeconds, float seconds = kSeconds);
    void reset() { started_ = false; }

private:
    FingerCurls now_;
    bool started_ = false;
};

// How much of the posed fingers shows over the game's, 0 to 1, eased (as FingerEase, `seconds` to close two
// thirds of the way) toward 1 while the controller is tracked and toward 0 once it is lost or inactive, so
// the fingers go back to the game's instead of holding the last curls (or the zeros of a controller that is
// off). The first update after a reset starts at the wanted value.
class FingerHold {
public:
    static constexpr float kSeconds = 0.1f;
    float update(bool tracked, float dtSeconds, float seconds = kSeconds);
    void reset() { started_ = false; }

private:
    float now_ = 0.0f;
    bool started_ = false;
};

// The finger (0 thumb, 1 index, 2 middle, 3 ring, 4 little) of each hand joint (kRightHandJointNames order).
int fingerOf(std::size_t joint);

// The open hand for the closed hand `grip` (each joint in its parent's frame): every finger bone straight,
// the finger roots and the thumb's base as `grip` has them.
HandLocal openHand(const HandLocal& grip);

// The left hand's closed grip: the Heavy Cannon idle's right hand (grip_hand.hpp) reflected (left_hand.hpp).
const HandLocal& leftGripHand();

// The hand posed: each joint between `open` and `closed` by its finger's curl, with its offset `offsets`
// (each joint's position in its parent's frame, the model's own bone lengths, from a read of the game's
// hand).
HandLocal
posedHand(const HandLocal& open, const HandLocal& closed, const FingerCurls& curls, const HandLocal& offsets);

// Each joint of `game` (the game's own hand) turned `weight` of the way (0 to 1) to the joint of `posed`: the
// fingers leaving with the arm's weight and the controller's hold.
HandLocal blendHand(const HandLocal& game, const HandLocal& posed, float weight);

// The fifteen finger bones the layer writes (kFingerMods), from a posed hand.
std::array<xr_math::ModelPose, kFingerModCount> fingerWrites(const HandLocal& hand);

} // namespace evr::arm
