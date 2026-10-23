#pragma once

// What the arm solve in use (ETERNALVR_ARM_SOLVE, docs/VR_HANDS_HUD.md, "The whole arm") changes, in one
// place: every branch between the bind pose's solve and the animated one (the layer's default until the bind
// pose's has been seen in a headset) asks this instead of the setting, so the two differ only here.
//
//   twistFrom    what the wrist's twist is measured from (arm_solve.hpp)
//   limitSwing   the off hand's wrist swing limited (arm_twist.hpp); the weapon arm's wrist is the game's
//   writesPast   every joint of both arms written past its target by the blend's mix (mod_mix.hpp), from the
//                hands' modifier weight, and the attach joint's change of the last tick noted for the next
//                read; the animated solve writes the joints as wanted (only the weapon arm's glove on a
//                grip is written past, in either solve: grip_hand.hpp)
//   wristChange  the off hand's seventh modifier, a change on its wrist that takes the rest of a turn the
//                attach joint cannot make (vkcore/offhand_mods.hpp)
//   modelShoulder  with a glove on a grip, whose modifiers are held in their joints' parents' frames, the
//                shoulder end (the arm's last joint) written in model space instead (parent_frames.hpp): a
//                gun's kick within a frame then moves the glove and the forearm with the gun but not the
//                shoulder, which the animated solve lets ride the kick (up to 17 cm for a frame of a
//                Ballista shot)
//
// Pure: no game memory, tested on its own.

#include "features/arm/arm_solve.hpp"

namespace evr::arm {

struct SolveMode {
    TwistFrom twistFrom = TwistFrom::Animated;
    bool limitSwing = false;
    bool writesPast = false;
    bool wristChange = false;
    bool modelShoulder = false;
};

constexpr SolveMode solveMode(TwistFrom twistFrom) {
    const bool bind = twistFrom == TwistFrom::Bind;
    return {twistFrom, bind, bind, bind, bind};
}

} // namespace evr::arm
