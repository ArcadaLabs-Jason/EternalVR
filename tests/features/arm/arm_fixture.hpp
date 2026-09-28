#pragma once

// The game's left arm in its bind pose, model space (x forward, y left, z up, metres): the joints of
// kArmJointNames as arms.md6skl places them (build 25216728), used as the "animated" pose in the tests.

#include "features/arm/arm_joints.hpp"
#include "support/approx.hpp"

namespace evr::test {

inline arm::ArmPoses bindArm() {
    using xr_math::IdViewAxis;
    using xr_math::ModelPose;
    const IdViewAxis hand{{0.281163f, -0.754156f, 0.593462f},
                          {0.959450f, 0.233858f, -0.157375f},
                          {-0.020100f, 0.613645f, 0.789327f}};
    return {
        ModelPose{{0.585000f, 0.000000f, 1.267500f}, {}},   // lefthandattach
        ModelPose{{0.378288f, 0.483837f, 1.263525f}, hand}, // LeftHand
        ModelPose{{0.318418f, 0.469244f, 1.273345f}, hand}, // leftforearmroll3
        ModelPose{{0.266032f, 0.456475f, 1.281938f}, hand}, // leftforearmroll2
        ModelPose{{0.213646f, 0.443707f, 1.290531f}, hand}, // leftforearmroll1
        ModelPose{{0.161260f, 0.430938f, 1.299123f}, hand}, // LeftForeArmRoll
        ModelPose{{0.108874f, 0.418170f, 1.307716f}, hand}, // LeftForeArm
        ModelPose{{-0.035312f, 0.224382f, 1.454700f},
                  {{0.859973f, -0.392064f, 0.326701f},
                   {0.509944f, 0.685366f, -0.519837f},
                   {-0.020100f, 0.613645f, 0.789327f}}}, // LeftArm
    };
}

inline bool approxAxis(const xr_math::IdViewAxis& a, const xr_math::IdViewAxis& b, float eps = 1e-4f) {
    return approxEqual(a.forward, b.forward, eps) && approxEqual(a.left, b.left, eps) &&
           approxEqual(a.up, b.up, eps);
}

inline bool approxPose(const xr_math::ModelPose& a, const xr_math::ModelPose& b, float eps = 1e-4f) {
    return approxEqual(a.position, b.position, eps) && approxAxis(a.axis, b.axis, eps);
}

} // namespace evr::test
