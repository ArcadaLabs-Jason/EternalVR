#pragma once

// The free off hand's arm (ETERNALVR_OFFHAND=free; docs/VR_HANDS_HUD.md, "The whole arm"): the left arm
// solved for the off-hand controller and mixed with the game's, for the off-hand hook (offhand_hook.cpp),
// which reads and rewrites the game's modifier of the left attach joint. The wrist goes to the controller's
// grip with its orientation (features/arm/hand_offset.hpp), the elbow by two-bone IK from a shoulder fixed to
// the head (features/arm/arm_solve.hpp), the forearm's roll joints share the wrist's twist; the forearm,
// elbow and shoulder get modifiers of the layer's own (game_arm.hpp). The arm solve's differences come from
// arm_solve_mode.hpp. Hook thread (the game thread running idHands::Update) only.

#include "features/input/controller_settings.hpp"
#include "vkcore/controllers_impl.hpp"
#include "xr_math/offhand_pose.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::vkcore::controllers::offhand_arm {

// No modifier reaches further than this from the animated pose (the probe's), nor the wrist in free mode.
constexpr float kMaxReachMetres = 1.5f;

// The arm solved for the controller and mixed with the game's by `weight`, from the attach joint's animated
// pose and the game's modifier of it this tick. Writes the layer's modifiers (forearm, elbow, shoulder) and
// returns the attach joint's; nullopt when the arm cannot be used this tick (nothing written). `rejected` is
// the hook's count of rejected ticks, for the trace.
std::optional<xr_math::JointMod> pose(const std::byte* hands,
                                      const xr_math::ModelPose& animatedAttach,
                                      const xr_math::JointMod& game,
                                      const WorldHand& world,
                                      const ModelPlacement& model,
                                      const input::ControllerSettings& cfg,
                                      float weight,
                                      std::uint64_t rejected);

// The left arm back to the game: the layer's modifiers to no change, its surfaces as the weapon's kit has
// them, the twist forgotten.
void giveBack(const std::byte* hands);

// The change SetJointMod is about to be handed for the left attach joint of `hands`, whoever made it, or
// nullopt when it could not be read: the next tick's read has it mixed in (a solve that writes past its
// targets, features/arm/mod_mix.hpp).
void noteAttachMod(const void* hands, const std::optional<xr_math::JointMod>& mod);

// Once a second with ETERNALVR_OFFHAND_TRACE: whether the off hand's pose line is due.
bool poseTraceDue(const input::ControllerSettings& cfg);

} // namespace evr::vkcore::controllers::offhand_arm
