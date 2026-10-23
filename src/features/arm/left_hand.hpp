#pragma once

// The arms with the weapon in the left hand (docs/VR_HANDS_HUD.md, "The weapon in the left hand").
//
// The arms model is never mirrored: the gun hangs from the right attach joint (`righthandattach`, the
// weapon's root), and the game's animations hold it with the right glove. The left attach joint
// (`lefthandattach`, the left arm's) is posed with it: in the hands rig's dumps both read the same pose to
// 0.0 mm and 0.0000 in their axes, the game's weapon-lag modifier included, on the combat shotgun, the Heavy
// Cannon, the chaingun, the ballista, the BFG and the Crucible [rig: the arm dumps of the hands rig's runs,
// every tick the game had the left arm; the chaingun's mod held parts them by 13 to 25 cm, which
// vkcore/left_hand_arms.hpp allows for]. So with the weapon in the left hand each arm takes the other's role
// in the gun's root frame, reflected across that frame's x-z plane (the gun's middle: the root's y axis is
// the model's left to 0.991 or better on every gun dumped): the left glove holds the gun as the right glove
// does in the game's animation, and the right glove is the off hand. The gun itself is not mirrored.
//
// The reflection keeps each joint's frame right-handed the way the skeleton's bind pose does
// (arm_joints.hpp: the right arm is the left reflected with each joint's axes negated): a joint held in the
// root's frame takes the position reflected and each axis reflected and negated; a joint held in its own
// parent's frame on the other hand keeps its turn, and its offset points the other way.
//
// Pure: no game memory, tested on its own.

#include "features/arm/grip_hand.hpp"
#include "features/arm/hand_offset.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/weapon_pose.hpp"

#include <cstdint>
#include <optional>

namespace evr::arm {

// A joint's pose in the gun's root frame (the attach joints' frame), reflected across the frame's x-z plane:
// the pose of the same joint of the other arm.
xr_math::ModelPose mirroredInRoot(const xr_math::ModelPose& local);

// A hand joint's pose in its parent's frame, for the same joint of the other hand.
xr_math::ModelPose mirroredLocal(const xr_math::ModelPose& local);

// A glove's grip for the other glove: the wrist mirroredInRoot, each hand joint mirroredLocal.
GripPose mirroredGrip(const GripPose& grip);

// The right wrist (model space) for the right controller's grip `grip` (model space, its own orientation):
// the left hand's wristFromGrip with `offset` (given for the left hand), reflected across the model's x-z
// plane on both sides, so the right glove sits in the right controller as the left one does in the left.
xr_math::ModelPose rightWristFromGrip(const xr_math::ModelPose& grip, const HandOffset& offset);

// A wrist read under the left attach joint `root` (`wrist` in its frame), placed in the frame of the gun's
// root `gunRoot` as read with the right glove's grip (all model space): nullopt when the two roots were not
// one pose (further apart than `maxApart`, game units, or turned more than kSameRootDegrees), as when one of
// them was read while the game held the attach joints apart (the chaingun's mod: 13 to 25 cm).
inline constexpr float kSameRootDegrees = 2.0f;
// Whether two reads of a root are one pose: within `maxApart` (game units) and kSameRootDegrees.
bool sameRootPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float maxApart);
std::optional<xr_math::ModelPose> onGunRoot(const xr_math::ModelPose& root,
                                            const xr_math::ModelPose& wrist,
                                            const xr_math::ModelPose& gunRoot,
                                            float maxApart);

// Whether a pose read once a tick has held still: each of the last kSteadyReads reads within `maxMove` (game
// units) and kSteadyDegrees of the one before, numbered by `serial` without a gap (a skipped read starts the
// count again; the same serial again is no new read). The game's grips are taken from such reads only: read
// while the gun is still coming up after a switch or a melee, the gun's root sits elsewhere in the model
// (rig: 8 cm after a melee), and the gun would be placed that far off the hand.
class SteadyPose {
public:
    static constexpr int kSteadyReads = 3;
    static constexpr float kSteadyDegrees = 0.2f;

    bool update(const xr_math::ModelPose& pose, std::uint32_t serial, float maxMove);
    void reset();

private:
    std::optional<xr_math::ModelPose> last_;
    std::uint32_t serial_ = 0;
    int count_ = 0;
};

// The viewmodel rows are measured for the right glove. For the left glove the model takes the row's turn
// reflected (the yaw and roll change sign: only the Crucible's row turns the model; a gun's sight line is the
// gun's own, so a sight row applies unchanged) and, once placed, is moved, not turned, until the left glove's
// wrist sits on the left hand's wrist point. `weight` (0 to 1) blends from the right glove's placement (0,
// before the left glove's wrist on the held item is known) to the left glove's (1).
xr_math::IdAngles leftHandTurn(const xr_math::IdAngles& rowTurn, float weight);

// The turn and translation the viewmodel places the model by for a weapon's row: the row's turn reflected
// toward the left glove by `weight` (leftHandTurn), and the row's translation moved so that, when the
// [sights] row in force (`sight`, a mod's) is not the weapon's own (`ownSight`, the one the offset rows are
// measured with), the gun turns about the wrist point `wrist` (metres, the aim's frame) under the turn
// actually applied (xr_math::translationKeepingWrist). `keepWrist` false (ETERNALVR_VIEWMODEL_OFFSET in place
// of the table's row) leaves the translation as given.
struct RowPlacement {
    Vec3 translation;
    xr_math::IdAngles rotation;
};
RowPlacement rowPlacement(Vec3 translation,
                          const xr_math::IdAngles& rowTurn,
                          float weight,
                          const std::optional<xr_math::IdAngles>& ownSight,
                          const std::optional<xr_math::IdAngles>& sight,
                          Vec3 wrist,
                          bool keepWrist);

// How far to move the model `placed` (relative to the eye, game units) so that its point `wristInModel` (the
// left glove's wrist on the gun, model space, game units) lands at `wristInGrip` (metres, the grip's forward,
// left and up) in the controller's grip: its position `grip` and its own orientation `gripAxis`.
Vec3 leftGloveShift(const xr_math::EyeRelativePose& placed,
                    Vec3 wristInModel,
                    Vec3 grip,
                    const IdViewAxis& gripAxis,
                    Vec3 wristInGrip,
                    float unitsPerMetre);

// The left glove's wrist on the held item's gun (model space, game units) that the model is placed by
// (leftGloveShift), for one held item at a time.
class LeftWristOnGun {
public:
    // Whether the wrist on `item` has been taken from the game's grip (its row is not needed).
    [[nodiscard]] bool fromGrip(const void* item) const { return item == item_ && fromGrip_.has_value(); }

    // The wrist on the held item `item`: where the left glove's grip `leftGrip` (in the gun's root frame)
    // puts it on the gun's root `root` as the game's grip was read, once both are known; after that, while
    // either lapses (a kept grip whose root no longer holds), the last one so taken for the same item; until
    // one is taken, the item's [left_hand_wrist] row `rowMetres` (the same point as measured at idle, metres)
    // at `unitsPerMetre`; nullopt with none (the model placed for the right glove). A new item starts over.
    std::optional<Vec3> update(const void* item,
                               const std::optional<xr_math::ModelPose>& root,
                               const std::optional<GripPose>& leftGrip,
                               const std::optional<Vec3>& rowMetres,
                               float unitsPerMetre);

    // Forgets the wrist taken from a grip (new hands: an item's pointer may be another item's).
    void forget() {
        item_ = nullptr;
        fromGrip_.reset();
    }

private:
    const void* item_ = nullptr;
    std::optional<Vec3> fromGrip_;
};

} // namespace evr::arm
