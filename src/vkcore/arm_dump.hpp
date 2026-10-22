#pragma once

// The hands rig's dump (ETERNALVR_TEST_ARM_DUMP=<file>, docs/rig-findings/hands-rig.md): rig use only.
//
// Every 0.25 s the off-hand hook appends one JSON line (features/arm/arm_dump_line.hpp) with what the
// layer read and wrote for both arms at that tick: the scripted input as read, both arms' final poses (the
// layer's modifiers as written), their read-back and animated poses, the wrist target, the posing state
// and weight, the arms model's placement, the held item, its active mods and the sights row in force, the
// weapon's root joint and the ray shots take.
// Everything comes from values the off-hand hook, the weapon arm and the viewmodel hook already read; no
// game function or field is added. Without the variable nothing is collected or written.
//
// Off-hand hook thread (the game thread running idHands::Update) only.

#include "features/arm/arm_dump_line.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm.hpp"

#include <cstddef>

namespace evr::vkcore::controllers::arm_dump {

// The variable names a file.
bool on();

// This tick's view: the hands and the arms model's placement the arms were posed from.
void noteView(const WorldHand& world, const ModelPlacement& model);

// This tick's arm on `side`: the game's, with the layer's share `weight` (no poses), or the layer's, with
// the solve's twist (radians), the read it started from, the poses written and the wrist target (none for
// the weapon arm).
void noteGameArm(arm::ArmSide side, float weight);
void noteLayerArm(arm::ArmSide side,
                  float weight,
                  float twist,
                  const game_arm::ArmRead& read,
                  const arm::ArmPoses& written,
                  const xr_math::ModelPose* target);

// After the hook ran for the tick's hands (`arms`, with the tick's arms' item): writes a line when 0.25 s
// have passed since the last and a view was noted this tick, then forgets the tick's notes.
void tick(const ArmsTick& arms);

} // namespace evr::vkcore::controllers::arm_dump
