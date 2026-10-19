#pragma once

// The right glove on the gun's grip (docs/VR_HANDS_HUD.md, "The weapon arm", "The glove on the grip"). The
// chaingun's and the ballista's idle animations hold the gun by its rear top handle
// (chaingun_base_handle_rear, gauss_base_handle), a bar along the gun beside the camera, with the right hand
// turned 78 and 73 degrees from the grip every other gun's idle holds (all within 7 degrees of the Heavy
// Cannon's), 84 and 59 cm from the weapon's root. Placed at a controller, that glove sits far from the real
// hand and turned across it. For such a weapon the weapon arm puts the glove's wrist (RightHand) on a grip
// that matches the controller, with the fingers closed around it, in the game's own grip of a gun the right
// hand holds that way.
//
// The gun hangs from the right attach joint (`righthandattach`, the weapon's root), and the wrist is its
// child; the fingers hang from the wrist. So a modifier on the wrist moves the glove and leaves the gun where
// the game put it. The glove's pose comes from the game's animation: the right hand of the Heavy Cannon's
// idle, which the chaingun's own zoom and reload use too (the same joint poses to 3e-6) [static: the idle
// animations of build 25216728, md6/player/human/base/motion/core/, decoded; rig: the decoded local poses
// match the hands rig's read of the combat shotgun's idle to 1e-4].
//
// The hands' modifier node mixes every modifier into the pose at its weight (features/arm/mod_mix.hpp), so
// the layer writes past its targets from the joints' animated poses. Those are hidden by the layer's own
// overrides once they show, so they are recovered from the read: the fingers' from the last read the
// overrides had not reached (the parked hand's fingers do not move in these idles), the wrist's by taking the
// mix back out of the read with the override the layer wrote (WristTracker).
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_joints.hpp"
#include "features/arm/arm_skeleton.hpp"
#include "xr_math/offhand_pose.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::arm {

// The right hand's joints under the wrist (RightHand) the glove's fingers hang from, parent before child: the
// thumb's three, then for each finger its root and three.
inline constexpr std::size_t kHandJointCount = 19;
inline constexpr std::array<const char*, kHandJointCount> kRightHandJointNames{
    "RightHandThumb1",  "RightHandThumb2",  "RightHandThumb3",  "righthandindexroot",
    "RightHandIndex1",  "RightHandIndex2",  "RightHandIndex3",  "righthandmiddleroot",
    "RightHandMiddle1", "RightHandMiddle2", "RightHandMiddle3", "righthandringroot",
    "RightHandRing1",   "RightHandRing2",   "RightHandRing3",   "righthandpinkyroot",
    "RightHandPinky1",  "RightHandPinky2",  "RightHandPinky3"};
// The left hand's, the same joints in the same order (the bind pose has them reflected to 0.5 mm) [static:
// marine.md6skl].
inline constexpr std::array<const char*, kHandJointCount> kLeftHandJointNames{
    "LeftHandThumb1",    "LeftHandThumb2",   "LeftHandThumb3",     "lefthandindexroot", "LeftHandIndex1",
    "LeftHandIndex2",    "LeftHandIndex3",   "lefthandmiddleroot", "LeftHandMiddle1",   "LeftHandMiddle2",
    "LeftHandMiddle3",   "lefthandringroot", "LeftHandRing1",      "LeftHandRing2",     "LeftHandRing3",
    "lefthandpinkyroot", "LeftHandPinky1",   "LeftHandPinky2",     "LeftHandPinky3"};
constexpr const std::array<const char*, kHandJointCount>& handJointNames(ArmSide side) {
    return side == ArmSide::Right ? kRightHandJointNames : kLeftHandJointNames;
}
// Each one's parent: an index into the list, or -1 for the wrist.
inline constexpr std::array<std::int16_t, kHandJointCount> kHandJointParents{
    -1, 0, 1, -1, 3, 4, 5, -1, 7, 8, 9, -1, 11, 12, 13, -1, 15, 16, 17};
// The joints the layer writes: the fifteen finger bones. The finger roots are never turned by the game's
// animations (the same pose in every idle decoded), so they are carried by the wrist as animated.
inline constexpr std::size_t kFingerModCount = 15;
inline constexpr std::array<std::uint8_t, kFingerModCount> kFingerMods{0,  1,  2,  4,  5,  6,  8, 9,
                                                                       10, 12, 13, 14, 16, 17, 18};

using HandJointIndices = std::array<std::int16_t, kHandJointCount>;

// The hand's joints in `skeleton` under the wrist `wrist`: each the joint whose name handle is `handles[i]`
// (the game's handle of kRightHandJointNames[i]) and whose parent is kHandJointParents[i]'s; nullopt with
// `error` set when one is missing, not unique, or hangs elsewhere.
std::optional<HandJointIndices> findHandJoints(const Skeleton& skeleton,
                                               std::int16_t wrist,
                                               const std::array<std::int16_t, kHandJointCount>& handles,
                                               std::string& error);

// The hand's joints in their parents' frames (kRightHandJointNames order) from their model poses `model`:
// the wrist first, then the hand's joints in that order.
std::array<xr_math::ModelPose, kHandJointCount>
handLocal(const std::array<xr_math::ModelPose, 1 + kHandJointCount>& model);

// A glove's grip on a gun: the wrist in the weapon's root joint's frame (the right attach joint) and each
// hand joint in its parent's frame (kRightHandJointNames order), game units.
struct GripPose {
    xr_math::ModelPose wrist;
    std::array<xr_math::ModelPose, kHandJointCount> hand;
};

// The right hand of the Heavy Cannon's idle (the chaingun's zoom and reload hold it the same).
const GripPose& heavyCannonGrip();

// The pose `a` whose mix with `written` at `weight` (mixPose(a, written, weight), mod_mix.hpp) is `landed`:
// the animated pose taken back out of a read. `weight` must be below 1 (at 1 nothing of `a` shows) and
// usable; nullopt when no pose mixes to `landed` (it is turned further from `written` than the mix leaves
// it).
std::optional<xr_math::ModelPose>
animatedUnder(const xr_math::ModelPose& landed, const xr_math::ModelPose& written, float weight);

// The wrist's animated pose in the attach joint's frame while the layer's override hides it.
//
// A read the override had not reached gives it as read. After that each read is the last frame's mix of the
// animated pose and the override written for it, held in the attach joint's frame (parent_frames.hpp), so the
// animated pose is taken back out (animatedUnder) with the override written for it as it was written,
// whatever the attach joint did since, but only once each of the last kSteadyWrites overrides was within
// kSteadyUnits (game units) and kSteadyDegrees of the one before: the read is a frame or two behind, so while
// the override moves (the blend in or out) the read may belong to an older one (taken back out with the wrong
// override it is off by weight / (1 - weight) times their difference, 19 times at 0.95, and the glove lands
// off by about that difference), and the last good estimate is kept. The read is taken to show the override
// written the tick before, as every read the rig could tell apart did (readLag). With that guard a read two
// frames behind, or a dropped frame, still lands the glove within 3 mm while the game's wrist moves under it
// (unit-tested). An estimate that is not a plausible pose, or further than kMaxUnits from the attach joint,
// is not taken either.
class WristTracker {
public:
    static constexpr int kSteadyWrites = 3;
    static constexpr float kSteadyUnits = 0.003f;
    static constexpr float kSteadyDegrees = 0.5f;
    static constexpr float kMaxUnits = 1.5f;

    // This tick's read of the attach joint and the wrist (model space); `clean` when no override of the
    // layer's could show in it. With `settled` (the hands' modifier weight settled, gloveWeight) the read is
    // taken back out at the weight the last override was written past by; otherwise only clean reads count.
    // The estimate (attach frame), or nullopt before the first clean read.
    std::optional<xr_math::ModelPose>
    update(bool clean, const xr_math::ModelPose& attach, const xr_math::ModelPose& wrist, bool settled);
    // The override written this tick (model space, as placed under the attach joint `attach`; it is written
    // in that joint's frame) and the weight it was written past by.
    void wrote(const xr_math::ModelPose& attach, const xr_math::ModelPose& written, float weight);
    // No override written this tick (the arm the game's).
    void idle();
    // Forget everything (other hands, a new skeleton).
    void reset();

    [[nodiscard]] std::optional<xr_math::ModelPose> estimate() const { return animated_; }

    // Rig diagnostics: which of the last overrides (1: the last tick's, 2: the one before, ...) this read of
    // the wrist shows, from where each would land on the estimate at `weight`, and how far the read is from
    // it (game units); nullopt when the overrides are too close together to tell (under kLagApartUnits).
    struct Lag {
        int ticks = 0;
        float miss = 0.0f;
    };
    static constexpr float kLagApartUnits = 0.01f;
    [[nodiscard]] std::optional<Lag>
    readLag(const xr_math::ModelPose& attach, const xr_math::ModelPose& wrist, float weight) const;

private:
    std::optional<xr_math::ModelPose> animated_;
    std::array<xr_math::ModelPose, kSteadyWrites> written_{}; // attach frame, the last first
    int writes_ = 0;                                          // overrides written in a row, capped
    float weight_ = 1.0f;                                     // the last override's write weight
};

// The hands' modifier weight as the owner holds it: the current weight the blend mixes at and the target it
// fades toward (vkcore/offhand_mods.hpp).
struct ModWeights {
    float current = 0.0f;
    float target = 0.0f;
};

// How the glove is written for this tick's weights. The blend mixes at a weight between the current and the
// target that the layer cannot know (the game may step the fade before it mixes), so the glove is always
// written past by the larger of the two (not past at all when that is not usable): it lands short of its
// target when the blend mixes at less, never beyond. Settled (a usable current weight within kSettledWeight
// of its target) the two differ by under 0.01 and a read may be taken back out at the weight written; while
// not, the glove eases off the grip (GloveSettle) until the weight settles.
struct GloveWeight {
    float write = 1.0f;
    bool settled = false;
};
inline constexpr float kSettledWeight = 0.01f;
GloveWeight gloveWeight(ModWeights weights);

// The glove's settle weight (0 to 1, eased): in over `blendSeconds` while the hands' weight is settled, out
// over the same while it is not, so a weight that fades or drops eases the glove off the grip. What the layer
// cannot ease is the game's own change of weight from one tick to the next: the glove written for one weight
// and mixed at another lands short of its target by that ratio at once (1 - current / target of the way where
// a rising fade starts; the whole of the drop where the weight falls below 0.5).
class GloveSettle {
public:
    float update(bool settled, float dtSeconds, float blendSeconds);
    void reset() { ramp_ = 0.0f; }

private:
    float ramp_ = 0.0f;
};

// The weapon arm for a weapon with a grip (features/arm/arm_solve.hpp, solveArmToWrist): `animated` is the
// arm as read (readArm's, the wrist's pose stale while the glove's override shows), carried rigidly to the
// wrist's animated pose `wristAnimated` (attach frame, WristTracker); `from` is the arm the solve starts
// from, carried rigidly to the wrist `gripWeight` (0 to 1) of the way from there to `grip`'s wrist. The
// attach joint (the gun) is left as read in both.
struct GloveArm {
    ArmPoses animated;
    ArmPoses from;
};
GloveArm gloveArm(const ArmPoses& read,
                  const xr_math::ModelPose& wristAnimated,
                  const GripPose& grip,
                  float gripWeight);

// What the layer writes for the glove (game units): the wrist's override and the fifteen fingers'
// (kFingerMods order), in model space under the attach joint they were placed under, and each in its
// parent's frame, which is how they are written (parent_frames.hpp): the wrist in the attach joint's frame,
// each finger in its parent's.
struct GripOverrides {
    xr_math::ModelPose attach;
    xr_math::ModelPose wrist;
    std::array<xr_math::ModelPose, kFingerModCount> fingers;
    xr_math::ModelPose wristLocal;
    std::array<xr_math::ModelPose, kFingerModCount> fingersLocal;
    // Written in model space instead (parent_frames.hpp): a glove that follows a controller, not the gun (the
    // right glove as the off hand with the weapon in the left hand).
    bool modelSpace = false;
};

// The overrides that land the wrist at `wantedWrist` and close the fingers `curl` of the way (0 to 1) from
// their animated pose `animatedHand` (each in its parent's frame) to `grip`'s. `attach` is the attach joint
// as read, `animatedWrist` the wrist's animated pose in its frame (WristTracker), `wantedWrist` in the same
// frame. With `weight` (the hands' modifier weight, usable) every joint is written past its target so it
// lands there; without, as wanted.
GripOverrides gripOverrides(const xr_math::ModelPose& attach,
                            const xr_math::ModelPose& animatedWrist,
                            const xr_math::ModelPose& wantedWrist,
                            const std::array<xr_math::ModelPose, kHandJointCount>& animatedHand,
                            const GripPose& grip,
                            float curl,
                            std::optional<float> weight);

} // namespace evr::arm
