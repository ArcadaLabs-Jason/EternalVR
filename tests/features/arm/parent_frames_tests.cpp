#include "features/arm/parent_frames.hpp"

#include "common/quat.hpp"
#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/mod_mix.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using evr::Quat;
using evr::Vec3;
using evr::arm::ArmJoint;
using evr::arm::ArmPoses;
using evr::arm::compose;
using evr::arm::GripOverrides;
using evr::arm::GripPose;
using evr::arm::heavyCannonGrip;
using evr::arm::index;
using evr::arm::kArmJointCount;
using evr::arm::kFingerModCount;
using evr::arm::kFingerMods;
using evr::arm::kGloveWrites;
using evr::arm::kHandJointCount;
using evr::arm::kHandJointParents;
using evr::arm::kLayerModJoints;
using evr::arm::mixPose;
using evr::arm::ModWrite;
using evr::arm::relative;
using evr::arm::WristTracker;
using evr::xr_math::ModelPose;

namespace {

constexpr float kWeight = 0.95f; // the hands' modifier node on the rig
constexpr float kRad = 3.14159265f / 180.0f;

ModelPose local(Vec3 p, Quat q) {
    return {p, evr::arm::fromQuat(q)};
}

// The chaingun's idle (md6/player/human/base/motion/core/heavy/chaingun/idle.md6anim, build 25216728,
// decoded): the right wrist on the gun's rear top handle in the weapon root's frame, and the hand's joints in
// their parents' frames (kRightHandJointNames order).
const ModelPose kHandleWrist =
    local({-0.791511f, -0.040667f, 0.284859f}, {-0.207102f, 0.638828f, 0.725742f, 0.149354f});
const std::array<ModelPose, kHandJointCount> kHandleHand{
    local({-0.024765f, -0.019050f, 0.011430f}, {-0.138780f, 0.486388f, -0.523764f, 0.685447f}),
    local({-0.000001f, -0.051435f, 0.000001f}, {-0.378273f, 0.000022f, 0.000022f, 0.925694f}),
    local({0.000000f, -0.022860f, -0.000001f}, {-0.554667f, 0.000022f, 0.000022f, 0.832073f}),
    local({-0.013811f, -0.042864f, -0.000000f}, {0.000000f, 0.000000f, -0.043619f, 0.999048f}),
    local({-0.000000f, -0.059616f, -0.000001f}, {-0.654625f, -0.172962f, 0.038477f, 0.734895f}),
    local({0.000001f, -0.047245f, 0.000000f}, {-0.545344f, 0.000108f, 0.002611f, 0.838208f}),
    local({-0.000002f, -0.028122f, 0.000000f}, {-0.390487f, 0.000022f, 0.000022f, 0.920608f}),
    local({0.000001f, -0.042863f, -0.000002f}, {-0.000000f, 0.000000f, 0.026177f, 0.999657f}),
    local({0.000001f, -0.060741f, 0.000001f}, {-0.698389f, -0.089578f, -0.003259f, 0.710084f}),
    local({-0.000002f, -0.048370f, -0.000000f}, {-0.618716f, 0.000022f, 0.000022f, 0.785615f}),
    local({0.000001f, -0.029242f, 0.000001f}, {-0.327992f, 0.000022f, 0.000022f, 0.944680f}),
    local({0.009527f, -0.040005f, -0.000000f}, {0.000000f, -0.000000f, 0.121869f, 0.992546f}),
    local({-0.000001f, -0.056242f, 0.000001f}, {-0.651344f, -0.001705f, -0.030924f, 0.758150f}),
    local({0.000001f, -0.042744f, -0.000001f}, {-0.545387f, 0.000022f, 0.000022f, 0.838184f}),
    local({-0.000001f, -0.028121f, -0.000000f}, {-0.354363f, 0.000022f, 0.000022f, 0.935108f}),
    local({0.019050f, -0.035243f, -0.000002f}, {0.000000f, 0.000000f, 0.207912f, 0.978148f}),
    local({-0.000000f, -0.050618f, 0.000000f}, {-0.605638f, 0.086168f, 0.001575f, 0.791059f}),
    local({0.000001f, -0.033746f, 0.000000f}, {-0.364419f, 0.000022f, 0.000022f, 0.931235f}),
    local({-0.000000f, -0.022497f, 0.000001f}, {-0.571370f, 0.001662f, -0.044606f, 0.819478f}),
};

// The chaingun's right attach joint (the gun's root) as the hands rig read it in a held pose (model space).
const ModelPose kAttach{
    {0.70526f, -0.15155f, -0.25466f},
    {{1.00000f, 0.00120f, -0.00230f}, {-0.00149f, 0.99098f, -0.13397f}, {0.00212f, 0.13397f, 0.99098f}}};

// The forearm's joints in their parents' frames, from the right arm's bind pose (ArmJoint order; the
// attach joint's and the wrist's unused).
ArmPoses forearmLocals() {
    const ArmPoses bind = evr::test::bindRightArm();
    ArmPoses out{};
    for (std::size_t i = index(ArmJoint::Roll3); i < kArmJointCount; ++i) {
        out[i] = relative(bind[i - 1], bind[i]);
    }
    return out;
}

// A shot's kick of the gun's root within its idle, `s` from 0 (still) to 1 (the kick's peak): 16 cm back,
// 4 cm up and 10 degrees up about the root's own x, about what a frame of the Ballista's shot moved the
// glove on the rig (ga-mod1: 15 cm).
ModelPose kick(float s) {
    const float half = 0.5f * 10.0f * kRad * s;
    return local({-0.16f * s, 0.0f, 0.04f * s}, {std::sin(half), 0.0f, 0.0f, std::cos(half)});
}

// The kick over a run: a shot every 12 ticks, the root thrown to the peak in one tick and back over ten.
float kickAt(std::size_t t) {
    static constexpr std::array<float, 12> kShot{0.0f, 1.0f,  0.8f,  0.6f,  0.45f, 0.3f,
                                                 0.2f, 0.12f, 0.06f, 0.02f, 0.0f,  0.0f};
    return t < 6 ? 0.0f : kShot[(t - 6) % kShot.size()];
}

// What the game draws in one frame for the layer's writes, a model of the modifier pass and the blend
// (parent_frames.hpp, mod_mix.hpp): each override becomes its joint's pose in its parent's frame (in model
// space, against the parent as modified so far, the translation from the skeleton's root, here at the
// origin; in the parent's frame, as written), and each joint is mixed from its animated pose in its parent's
// frame at the node's weight. No writes: the animated pose.
struct Drawn {
    ModelPose attach;
    ArmPoses arm{};                              // model space; the attach joint's unused
    std::array<ModelPose, kHandJointCount> hand; // model space
};
Drawn draw(const ModelPose& attach,
           const ModelPose& animatedWrist,
           const std::array<ModelPose, kGloveWrites>* modelPoses, // the writes' poses as model poses
           const std::array<ModelPose, kGloveWrites>* parentPoses) {
    const ArmPoses forearm = forearmLocals();
    Drawn out{attach, {}, {}};
    ModelPose modified = attach; // the parent as modified so far
    ModelPose drawn = attach;    // the parent as drawn
    const auto apply = [&](std::size_t k, const ModelPose& animatedLocal, const ModelPose& parentModified,
                           const ModelPose& parentDrawn, ModelPose& modifiedOut) {
        ModelPose modLocal = animatedLocal;
        if (modelPoses) {
            modLocal = relative(parentModified, (*modelPoses)[k]);
        } else if (parentPoses) {
            modLocal = (*parentPoses)[k];
        }
        modifiedOut = compose(parentModified, modLocal);
        return compose(parentDrawn, mixPose(animatedLocal, modLocal, kWeight));
    };
    ModelPose wristModified;
    out.arm[index(ArmJoint::Hand)] = apply(0, animatedWrist, modified, drawn, wristModified);
    modified = wristModified;
    drawn = out.arm[index(ArmJoint::Hand)];
    for (std::size_t m = 0; m < kLayerModJoints.size(); ++m) {
        const std::size_t j = index(kLayerModJoints[m]);
        ModelPose next;
        out.arm[j] = apply(1 + m, forearm[j], modified, drawn, next);
        modified = next;
        drawn = out.arm[j];
    }
    std::array<int, kHandJointCount> finger{};
    finger.fill(-1);
    for (std::size_t k = 0; k < kFingerModCount; ++k) {
        finger[kFingerMods[k]] = static_cast<int>(k);
    }
    std::array<ModelPose, kHandJointCount> handModified{};
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const std::int16_t p = kHandJointParents[i];
        const ModelPose& pm = p < 0 ? wristModified : handModified[static_cast<std::size_t>(p)];
        const ModelPose& pd = p < 0 ? out.arm[index(ArmJoint::Hand)] : out.hand[static_cast<std::size_t>(p)];
        if (finger[i] < 0) { // a finger root: no modifier
            handModified[i] = compose(pm, kHandleHand[i]);
            out.hand[i] = compose(pd, kHandleHand[i]);
            continue;
        }
        out.hand[i] = apply(1 + kLayerModJoints.size() + static_cast<std::size_t>(finger[i]), kHandleHand[i],
                            pm, pd, handModified[i]);
    }
    return out;
}

// The rule of the code before this one, for the comparison: every override in model space as the layer placed
// it (the wrist and the fingers as gripOverrides placed them, the forearm as written).
std::array<ModelPose, kGloveWrites> modelSpace(const ArmPoses& arm, const GripOverrides& glove) {
    std::array<ModelPose, kGloveWrites> out{};
    out[0] = glove.wrist;
    for (std::size_t m = 0; m < kLayerModJoints.size(); ++m) {
        out[1 + m] = arm[index(kLayerModJoints[m])];
    }
    for (std::size_t k = 0; k < kFingerModCount; ++k) {
        out[1 + kLayerModJoints.size() + k] = glove.fingers[k];
    }
    return out;
}

// The model-space pose a write in the parent's frame stands for, and the write as the game reads it: model
// space (flag 1, kept as model poses) or the parent's frame.
struct Writes {
    std::array<ModelPose, kGloveWrites> model{};
    std::array<ModelPose, kGloveWrites> parent{};
    bool modelSpace = false;
};
Writes asWritten(const std::array<ModWrite, kGloveWrites>& writes) {
    Writes out;
    for (std::size_t k = 0; k < kGloveWrites; ++k) {
        out.modelSpace = out.modelSpace || (writes[k].flags & evr::arm::kModModelSpace) != 0;
        out.model[k] = writes[k].pose;
        out.parent[k] = writes[k].pose;
    }
    return out;
}

// What a tick of the weapon arm draws: the read is the frame before (`read`), the tracker takes the wrist's
// animated pose out of it, the glove goes on the grip at the full arm and grip weight, the forearm hangs from
// the wrist as in the bind pose, and gloveWrites makes the writes; the game draws them under the gun's root
// of this frame (`attach`).
struct Tick {
    Drawn drawn;
    ModelPose wanted; // the glove's wrist where it is meant to land, in the gun's root frame
};
Tick tick(WristTracker& tracker,
          const Drawn& read,
          bool clean,
          const ModelPose& attach,
          const ModelPose& animatedWrist,
          bool useModelSpace) {
    const GripPose& grip = heavyCannonGrip();
    const auto estimate =
        tracker.update(clean, read.attach, read.arm[index(ArmJoint::Hand)], /*settled=*/true);
    REQUIRE(estimate);
    const ModelPose wanted = grip.wrist;
    const GripOverrides glove =
        evr::arm::gripOverrides(read.attach, *estimate, wanted, kHandleHand, grip, 1.0f, kWeight);
    // The arm as weapon_arm.cpp writes it: the wrist where it is wanted, the forearm under the glove's
    // wrist as written.
    const ArmPoses forearm = forearmLocals();
    ArmPoses arm{};
    arm[index(ArmJoint::Attach)] = read.attach;
    arm[index(ArmJoint::Hand)] = compose(read.attach, wanted);
    ModelPose parent = glove.wrist;
    for (const ArmJoint j : kLayerModJoints) {
        arm[index(j)] = compose(parent, forearm[index(j)]);
        parent = arm[index(j)];
    }
    const Writes w = asWritten(evr::arm::gloveWrites(arm, glove, {1.0f, 1.0f, 1.0f}, {}));
    tracker.wrote(read.attach, glove.wrist, kWeight);
    if (useModelSpace) {
        const std::array<ModelPose, kGloveWrites> model = modelSpace(arm, glove);
        return {draw(attach, animatedWrist, &model, nullptr), wanted};
    }
    return {w.modelSpace ? draw(attach, animatedWrist, &w.model, nullptr)
                         : draw(attach, animatedWrist, nullptr, &w.parent),
            wanted};
}

// The worst of each measure over a run, game units: the glove's wrist from where it is meant to land in the
// gun's root frame, the sleeve's end (the forearm's first joint) from where it hangs off the glove, and the
// fingers from the grip's in the gun's root frame.
struct Worst {
    float wrist = 0.0f;
    float sleeve = 0.0f;
    float fingers = 0.0f;
};

// A run of `ticks` with the gun's root kicked as `kickAt` scaled by `gunKick` (1: the full kick, 0: still),
// the game's own wrist moving under the glove by `wristKick` (in the root's frame, at the kick's peak),
// measured from tick 12 on.
Worst run(std::size_t ticks, float gunKick, Vec3 wristKick, bool useModelSpace = false) {
    WristTracker tracker;
    const ArmPoses forearm = forearmLocals();
    const GripPose& grip = heavyCannonGrip();
    const auto animatedWrist = [&](std::size_t t) {
        const float s = kickAt(t);
        return ModelPose{kHandleWrist.position + wristKick * s, kHandleWrist.axis};
    };
    const auto attachAt = [&](std::size_t t) {
        return compose(kAttach, kick(gunKick * kickAt(t)));
    };
    Drawn read = draw(attachAt(0), animatedWrist(0), nullptr, nullptr);
    Worst worst;
    for (std::size_t t = 1; t < ticks; ++t) {
        const Tick drawn = tick(tracker, read, t == 1, attachAt(t), animatedWrist(t), useModelSpace);
        const Drawn& d = drawn.drawn;
        if (t >= 12) {
            const ModelPose wrist = relative(d.attach, d.arm[index(ArmJoint::Hand)]);
            worst.wrist = std::max(worst.wrist, length(wrist.position - drawn.wanted.position));
            const ModelPose sleeve = relative(d.arm[index(ArmJoint::Hand)], d.arm[index(ArmJoint::Roll3)]);
            worst.sleeve =
                std::max(worst.sleeve, length(sleeve.position - forearm[index(ArmJoint::Roll3)].position));
            // The fingers where the grip closes them, under the wrist where it is meant to be.
            std::array<ModelPose, kHandJointCount> target{};
            for (std::size_t i = 0; i < kHandJointCount; ++i) {
                const std::int16_t p = kHandJointParents[i];
                const ModelPose& parent =
                    p < 0 ? compose(d.attach, drawn.wanted) : target[static_cast<std::size_t>(p)];
                target[i] = compose(parent, grip.hand[i]);
            }
            for (const std::uint8_t i : kFingerMods) {
                worst.fingers = std::max(worst.fingers, length(d.hand[i].position - target[i].position));
            }
        }
        read = d;
    }
    return worst;
}

} // namespace

TEST_CASE("parent frames: every glove write is an override in its parent's frame") {
    const GripPose& grip = heavyCannonGrip();
    const GripOverrides glove =
        evr::arm::gripOverrides(kAttach, kHandleWrist, grip.wrist, kHandleHand, grip, 1.0f, kWeight);
    const ArmPoses arm = evr::test::bindRightArm();
    const auto writes =
        evr::arm::gloveWrites(arm, glove, {1.0f, 1.0f, 1.0f}, {}, 1u << index(ArmJoint::Roll2));
    for (std::size_t k = 0; k < kGloveWrites; ++k) {
        CAPTURE(k);
        CHECK((writes[k].flags & evr::arm::kModModelSpace) == 0);
        // An override, so the trip's read-back counts it and clearLayerMods sets it back.
        CHECK((writes[k].flags & evr::arm::kModOverride) != 0);
        CHECK((writes[k].flags & evr::arm::kModScale) == (k == 2 ? evr::arm::kModScale : 0));
    }
    // The wrist in the attach joint's frame, the first forearm joint under the glove's wrist as written.
    CHECK(evr::test::approxPose(writes[0].pose, glove.wristLocal));
    CHECK(evr::test::approxPose(writes[1].pose, relative(glove.wrist, arm[index(ArmJoint::Roll3)]), 1e-4f));
    CHECK(evr::test::approxPose(writes[7].pose, glove.fingersLocal[0]));
    // Offsets in the model's internal units.
    const auto scaled = evr::arm::gloveWrites(arm, glove, {2.0f, 2.0f, 2.0f}, {});
    CHECK(std::fabs(scaled[0].pose.position.x * 2.0f - writes[0].pose.position.x) < 1e-6f);
}

TEST_CASE("parent frames: with the gun still, the glove lands where the model-space writes put it") {
    // Nothing moves within a frame: held in the parent's frame or in model space, every joint lands the same.
    const Worst parent = run(40, 0.0f, {});
    const Worst model = run(40, 0.0f, {}, true);
    CHECK(parent.wrist < 1e-4f);
    CHECK(model.wrist < 1e-4f);
    CHECK(parent.fingers < 1e-4f);
    CHECK(model.fingers < 1e-4f);
    CHECK(parent.sleeve < 1e-4f);
    CHECK(model.sleeve < 1e-4f);
}

TEST_CASE("parent frames: the glove stays on the grip while the gun kicks within a frame") {
    // A shot's kick a frame long (16 cm and 10 degrees at the gun's root), the game's own wrist kicked 3 cm
    // under the glove: the glove, its fingers and the forearm's end ride the gun's root of the frame drawn.
    const Worst worst = run(90, 1.0f, {0.03f, 0.0f, 0.01f});
    CHECK(worst.wrist < 0.005f);
    CHECK(worst.fingers < 0.005f);
    CHECK(worst.sleeve < 0.001f);
    // The same writes in model space, under the root read a frame before: the glove lands where the gun was.
    const Worst model = run(90, 1.0f, {0.03f, 0.0f, 0.01f}, true);
    CHECK(model.wrist > 0.1f);
}

TEST_CASE("parent frames: the overrides land where the attach joint of the frame drawn carries them") {
    const ArmPoses bind = evr::test::bindRightArm();
    const ModelPose now = compose(bind[0], kick(1.0f));
    const ArmPoses carried = evr::arm::carriedBy(bind[0], now, bind);
    for (std::size_t i = 0; i < kArmJointCount; ++i) {
        CHECK(evr::test::approxPose(relative(now, carried[i]), relative(bind[0], bind[i]), 1e-4f));
    }
}

TEST_CASE("parent frames: a model scale that is not uniform writes in model space, as before") {
    // The game's side: the model scale (game units per internal unit, along the model's axes) applied to
    // the internal model positions; a model-space write is the internal pose from the root (here at the
    // origin), a write in the parent's frame is placed under the parent's internal pose.
    const auto lands = [](const ModWrite& w, const ModelPose& parentInternal, Vec3 scale) {
        const ModelPose internal =
            (w.flags & evr::arm::kModModelSpace) != 0 ? w.pose : compose(parentInternal, w.pose);
        const Vec3 p = internal.position;
        return Vec3{p.x * scale.x, p.y * scale.y, p.z * scale.z};
    };
    // A gun's root turned 40 degrees about z and 30 about x, so its axes are not the model's.
    const float a = 0.5f * 40.0f * kRad;
    const float b = 0.5f * 30.0f * kRad;
    const ModelPose turned = compose(compose(kAttach, local({}, {0.0f, 0.0f, std::sin(a), std::cos(a)})),
                                     local({}, {std::sin(b), 0.0f, 0.0f, std::cos(b)}));
    const GripPose& grip = heavyCannonGrip();
    for (const Vec3 scale : {Vec3{1.0f, 1.2f, 1.0f}, Vec3{1.2f, 1.2f, 1.2f}, Vec3{1.0f, 1.0f, 1.0f}}) {
        CAPTURE(scale.y);
        const ModelPose attach{
            {turned.position.x * scale.x, turned.position.y * scale.y, turned.position.z * scale.z},
            turned.axis}; // as GetJointTransforms reads it
        const GripOverrides glove =
            evr::arm::gripOverrides(attach, kHandleWrist, grip.wrist, kHandleHand, grip, 1.0f, std::nullopt);
        const auto writes = evr::arm::gloveWrites(evr::test::bindRightArm(), glove, scale, {});
        const bool uniform = scale.x == scale.y;
        CHECK(((writes[0].flags & evr::arm::kModModelSpace) == 0) == uniform);
        // The glove's wrist lands where it was meant to (game units), either way.
        CHECK(length(lands(writes[0], turned, scale) - glove.wrist.position) < 1e-4f);
    }
}
