#pragma once

// A planted fault for the hands rig (ETERNALVR_TEST_ARM_FAULT=twist, docs/rig-findings/hands-rig.md):
// development use only, the rig checker's positive control.
//
// solveArm (arm_solve.hpp) shares the wrist's twist about the forearm among the forearm's roll joints by
// their distance from the elbow, so a turned palm twists the sleeve. The fault takes every roll joint's
// share back out, leaving the whole twist on one joint, the wrist (LeftHand, RightHand): the skin between
// the last roll joint and the hand then wrings like a candy wrapper. A rig sweep with it must fail the
// checker's twist and collapse checks; one without it must not fail them for the same reason.
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_solve.hpp"

namespace evr::arm {

// Every roll joint of `solution` turned back about the solved forearm by its share of the forearm's twist
// (`solution.split.forearm`), so the wrist alone carries the forearm's twist: measured from the bind pose
// each takes the elbow joint's orientation; measured from the animated hand each is turned back by its
// share, taken from `animated` (the pose the solution was made from) as solveArm takes them. The wrist,
// elbow, upper arm and attach joint are not changed.
void plantTwistFault(ArmSolution& solution, const ArmPoses& animated);

} // namespace evr::arm
