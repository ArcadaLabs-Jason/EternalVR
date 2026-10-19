#include "features/arm/grip_hand.hpp"

#include "common/quat.hpp"
#include "features/arm/arm_fixture.hpp"
#include "features/arm/arm_frames.hpp"
#include "features/arm/mod_mix.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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
using evr::arm::kFingerModCount;
using evr::arm::kFingerMods;
using evr::arm::kHandJointCount;
using evr::arm::kHandJointParents;
using evr::arm::mixPose;
using evr::arm::ModWeights;
using evr::arm::relative;
using evr::arm::WristTracker;
using evr::xr_math::ModelPose;

namespace {

constexpr float kWeight = 0.95f; // the hands' modifier node on the rig
constexpr float kRad = 3.14159265f / 180.0f;

ModelPose local(Vec3 p, Quat q) {
    return {p, evr::arm::fromQuat(q)};
}

float turnDegrees(const evr::xr_math::IdViewAxis& a, const evr::xr_math::IdViewAxis& b) {
    const Quat q = conjugate(evr::arm::toQuat(a)) * evr::arm::toQuat(b);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) / kRad;
}

// The chaingun's idle (md6/player/human/base/motion/core/heavy/chaingun/idle.md6anim, build 25216728,
// decoded): the right wrist on the gun's rear top handle, 0.84 from the weapon's root, in its frame, and the
// hand's joints in their parents' frames (kRightHandJointNames order).
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

// The right arm as read: the attach joint, the wrist at `wristLocal` under it, the forearm hanging from the
// wrist as in the bind pose.
ArmPoses readArm(const ModelPose& wristLocal) {
    const ArmPoses bind = evr::test::bindRightArm();
    ArmPoses out{};
    out[index(ArmJoint::Attach)] = kAttach;
    const ModelPose wrist = compose(kAttach, wristLocal);
    for (std::size_t i = index(ArmJoint::Hand); i < evr::arm::kArmJointCount; ++i) {
        out[i] = compose(wrist, relative(bind[index(ArmJoint::Hand)], bind[i]));
    }
    return out;
}

// The game's side, a model of the hands' blend (mod_mix.hpp): each joint's pose without modifiers mixed with
// its pose with the layer's overrides at the node's weight in its parent's frame; the wrist's parent (the
// attach joint) is not written. What the glove's joints land at, each in its parent's frame.
struct Landed {
    ModelPose wrist;                             // attach frame
    std::array<ModelPose, kHandJointCount> hand; // parents' frames
};
Landed land(const ModelPose& animatedWrist, const GripOverrides* written, float weight) {
    Landed out{animatedWrist, kHandleHand};
    if (!written) {
        return out;
    }
    out.wrist = mixPose(animatedWrist, relative(kAttach, written->wrist), weight);
    // The written model poses of the hand's joints: the fingers' overrides, the roots carried by the wrist's.
    std::array<ModelPose, kHandJointCount> model{};
    std::array<int, kHandJointCount> finger{};
    finger.fill(-1);
    for (std::size_t k = 0; k < kFingerModCount; ++k) {
        finger[kFingerMods[k]] = static_cast<int>(k);
    }
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const std::int16_t p = kHandJointParents[i];
        const ModelPose& parent = p < 0 ? written->wrist : model[static_cast<std::size_t>(p)];
        model[i] = finger[i] >= 0 ? written->fingers[static_cast<std::size_t>(finger[i])]
                                  : compose(parent, kHandleHand[i]);
        out.hand[i] = mixPose(kHandleHand[i], relative(parent, model[i]), weight);
    }
    return out;
}

float smooth(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    return c * c * (3.0f - 2.0f * c);
}

// The game's side of a run: which write a read shows, dropped frames, and the hands' modifier weights.
struct Game {
    std::size_t lag = 1;              // a read shows the landing of the write this many ticks before
    std::vector<std::size_t> dropped; // ticks whose read repeats the one before (a dropped frame)
    // The weights the owner holds at tick t, and the weight the blend mixes tick t's write at (default: the
    // current weight; in a fade the blend may run further along it).
    std::function<ModWeights(std::size_t)> weights = [](std::size_t) {
        return ModWeights{kWeight, kWeight};
    };
    std::function<float(std::size_t)> mixedAt;
};

// One run of the weapon arm's glove against the blend model, a tick at a time, as weapon_arm.cpp drives it:
// the read is an earlier tick's landing, the tracker estimates the wrist, the arm is carried to the grip, the
// arm blend's weight (`weights`, one per tick; 0 gives the arm back) mixes the wrist, and the glove is
// written at the weight gloveWeight gives, eased by GloveSettle. `animatedWrist(t)` is the game's own wrist
// that tick (attach frame). What the game shows each tick.
template <typename Animated>
std::vector<Landed> run(const std::vector<float>& weights, Animated animatedWrist, const Game& game = {}) {
    WristTracker tracker;
    evr::arm::GloveSettle settle;
    std::size_t sinceWrite = 3; // ticks since the last override: a read that none can show is clean
    std::vector<Landed> out;
    const GripPose& grip = heavyCannonGrip();
    Landed lastRead{};
    for (std::size_t t = 0; t < weights.size(); ++t) {
        const ModelPose animated = animatedWrist(t);
        const bool drop = std::find(game.dropped.begin(), game.dropped.end(), t) != game.dropped.end();
        const Landed read = drop && t > 0   ? lastRead
                            : t >= game.lag ? out[t - game.lag]
                                            : land(animated, nullptr, 0.0f);
        lastRead = read;
        const ModWeights w = game.weights(t);
        const evr::arm::GloveWeight gw = evr::arm::gloveWeight(w);
        const float settled = settle.update(gw.settled, 1.0f / 90.0f, 0.15f);
        const auto estimate =
            tracker.update(sinceWrite >= 3, kAttach, compose(kAttach, read.wrist), gw.settled);
        REQUIRE(estimate);
        const float weight = weights[t];
        const float mixAt = game.mixedAt ? game.mixedAt(t) : w.current;
        if (weight <= 0.0f || settled <= 0.0f) {
            tracker.idle();
            ++sinceWrite;
            out.push_back(land(animated, nullptr, mixAt));
            continue;
        }
        const evr::arm::GloveArm arms = evr::arm::gloveArm(readArm(read.wrist), *estimate, grip, settled);
        CHECK(arms.from[index(ArmJoint::Attach)].position == kAttach.position); // the gun is not moved
        CHECK(arms.animated[index(ArmJoint::Attach)].position == kAttach.position);
        const ModelPose wanted = evr::arm::blendPose(arms.animated[index(ArmJoint::Hand)],
                                                     arms.from[index(ArmJoint::Hand)], weight);
        const GripOverrides written = evr::arm::gripOverrides(kAttach, *estimate, relative(kAttach, wanted),
                                                              kHandleHand, grip, weight * settled, gw.write);
        tracker.wrote(kAttach, written.wrist, gw.write);
        sinceWrite = 0;
        out.push_back(land(animated, &written, mixAt));
    }
    return out;
}

// The largest distance of the landed wrist from the grip's over ticks [from, to).
float worstMiss(const std::vector<Landed>& landed, std::size_t from, std::size_t to) {
    float worst = 0.0f;
    for (std::size_t t = from; t < to && t < landed.size(); ++t) {
        worst = std::max(worst, length(landed[t].wrist.position - heavyCannonGrip().wrist.position));
    }
    return worst;
}

// The largest move of the landed wrist in one tick, and how far it ever went past the grip along the way
// from the handle hand (both game units).
struct Path {
    float step = 0.0f;
    float past = 0.0f;
};
Path pathOf(const std::vector<Landed>& landed) {
    const Vec3 from = kHandleWrist.position;
    const Vec3 way = heavyCannonGrip().wrist.position - from;
    const float whole = length(way);
    Path out;
    Vec3 before = landed.empty() ? from : landed.front().wrist.position;
    for (const Landed& l : landed) {
        out.step = std::max(out.step, length(l.wrist.position - before));
        out.past = std::max(out.past, dot(l.wrist.position - from, way) / whole - whole);
        before = l.wrist.position;
    }
    return out;
}

// The arm blend's weight: in over `in` ticks, held, out over `out` ticks, then the game's.
std::vector<float> ramp(int in, int hold, int out, int after) {
    std::vector<float> w;
    for (int i = 1; i <= in; ++i) {
        w.push_back(smooth(static_cast<float>(i) / static_cast<float>(in)));
    }
    w.insert(w.end(), static_cast<std::size_t>(hold), 1.0f);
    for (int i = out - 1; i >= 0; --i) {
        w.push_back(smooth(static_cast<float>(i) / static_cast<float>(out)));
    }
    w.insert(w.end(), static_cast<std::size_t>(after), 0.0f);
    return w;
}

// The marine skeleton's parent table (marine.md6skl, build 25216728, 92 joints): RightHand 3, its hand's
// joints 4 to 22.
const std::vector<std::int16_t> kMarineParents{
    -1, 0,  0,  2,  3,  4,  5,  3,  7,  8,  9,  3,  11, 12, 13, 3,  15, 16, 17, 3,  19, 20, 21,
    3,  3,  24, 25, 26, 27, 28, 0,  30, 31, 32, 33, 31, 35, 36, 37, 31, 39, 40, 41, 31, 43, 44,
    45, 31, 47, 48, 49, 31, 31, 52, 53, 54, 55, 56, 54, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
    0,  0,  0,  0,  0,  0,  74, 75, 76, 77, 74, 79, 80, 81, 74, 83, 84, 85, 86, 85, 88, 85, 90};

evr::arm::Skeleton marine() {
    evr::arm::Skeleton s;
    s.parents = kMarineParents;
    for (std::size_t i = 0; i < s.parents.size(); ++i) {
        s.nameHandles.push_back(static_cast<std::int16_t>(1000 + i));
    }
    return s;
}

std::array<std::int16_t, kHandJointCount> handlesOf4To22() {
    std::array<std::int16_t, kHandJointCount> h{};
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        h[i] = static_cast<std::int16_t>(1004 + i);
    }
    return h;
}

} // namespace

TEST_CASE("grip hand: the glove's joints are found under the wrist by name handle and parent") {
    std::string error;
    const auto hand = evr::arm::findHandJoints(marine(), 3, handlesOf4To22(), error);
    REQUIRE(hand);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        CHECK((*hand)[i] == static_cast<std::int16_t>(4 + i));
    }
    // Not under this wrist (the left one is 31).
    CHECK_FALSE(evr::arm::findHandJoints(marine(), 31, handlesOf4To22(), error));
    // A finger hanging from the wrist instead of its root.
    evr::arm::Skeleton moved = marine();
    moved.parents[8] = 3;
    CHECK_FALSE(evr::arm::findHandJoints(moved, 3, handlesOf4To22(), error));
    CHECK(error.find("RightHandIndex1") != std::string::npos);
    // A name the skeleton does not have.
    auto handles = handlesOf4To22();
    handles[18] = 999;
    CHECK_FALSE(evr::arm::findHandJoints(marine(), 3, handles, error));
}

TEST_CASE("grip hand: the animated pose comes back out of the blend's mix") {
    const ModelPose a = local({0.3f, -0.2f, 0.1f}, normalize(Quat{0.2f, -0.4f, 0.1f, 0.9f}));
    for (const float weight : {0.95f, 0.8f, 0.6f}) {
        for (const float turn : {0.0f, 10.0f, 90.0f, 170.0f}) {
            const ModelPose written{
                a.position + Vec3{0.5f, -0.3f, 0.7f},
                evr::arm::rotateAbout(a.axis, normalize(Vec3{0.3f, 0.8f, 0.2f}), turn * kRad)};
            const auto back = evr::arm::animatedUnder(mixPose(a, written, weight), written, weight);
            REQUIRE(back);
            CHECK(length(back->position - a.position) < 1e-4f);
            CHECK(turnDegrees(back->axis, a.axis) < 0.05f);
        }
    }
    CHECK_FALSE(evr::arm::animatedUnder(a, a, 1.0f)); // nothing of the animated pose shows at 1
    CHECK_FALSE(evr::arm::animatedUnder(a, a, 0.4f)); // not a usable weight
}

TEST_CASE("grip hand: the glove lands on the gun's grip, fingers closed, and the gun is not moved") {
    const std::vector<Landed> landed = run(ramp(14, 40, 5, 0), [](std::size_t) { return kHandleWrist; });
    const GripPose& grip = heavyCannonGrip();
    const Landed& held = landed[14 + 39];
    // The handle glove is 0.84 from the gun's root; on the grip it is 0.19 from it, in the grip's pose.
    CHECK(length(held.wrist.position - grip.wrist.position) < 0.001f);
    CHECK(turnDegrees(held.wrist.axis, grip.wrist.axis) < 0.2f);
    for (const std::uint8_t i : kFingerMods) {
        CAPTURE(evr::arm::kRightHandJointNames[i]);
        CHECK(turnDegrees(held.hand[i].axis, grip.hand[i].axis) < 0.2f);
    }
}

TEST_CASE("grip hand: the glove blends onto the grip and back to the game's, no jump") {
    // In over 14 ticks (0.15 s at 90 Hz), held, handed back over 5 (a third of it, the arm policy's quick
    // hand-back), then the game's.
    const std::vector<Landed> landed = run(ramp(14, 30, 5, 3), [](std::size_t) { return kHandleWrist; });
    const float whole = length(heavyCannonGrip().wrist.position - kHandleWrist.position);
    float largest = 0.0f;
    ModelPose before = kHandleWrist;
    for (const Landed& l : landed) {
        largest = std::max(largest, length(l.wrist.position - before.position));
        before = l.wrist;
    }
    // The steepest tick of an eased 5-tick blend moves 0.3 of the way (smoothstep's 1.5 / 5); a snap moves
    // all.
    CHECK(largest < 0.35f * whole);
    CHECK(length(landed.back().wrist.position - kHandleWrist.position) < 1e-5f);
}

TEST_CASE("grip hand: the glove stays on the grip while the game's own wrist moves under it") {
    // The chaingun's zoom: the game's animation takes its handle wrist onto the grip over 30 ticks while the
    // layer holds the glove there. The tracker follows the hidden animated wrist out of the read, so the
    // glove does not drift (written past a stale animated pose it would land 5 % of the 0.67 move off).
    const GripPose& grip = heavyCannonGrip();
    const auto animated = [&](std::size_t t) {
        const float s = smooth((static_cast<float>(t) - 30.0f) / 30.0f);
        return mixPose(kHandleWrist, grip.wrist, s);
    };
    const std::vector<Landed> landed = run(ramp(14, 90, 1, 0), animated);
    float worst = 0.0f;
    for (std::size_t t = 20; t < 14 + 89; ++t) {
        worst = std::max(worst, length(landed[t].wrist.position - grip.wrist.position));
    }
    CHECK(worst < 0.003f);
}

TEST_CASE("grip hand: the glove settles on the grip with the read two frames behind") {
    Game game;
    game.lag = 2;
    const std::vector<Landed> landed =
        run(ramp(14, 90, 1, 0), [](std::size_t) { return kHandleWrist; }, game);
    CHECK(worstMiss(landed, 30, 104) < 0.003f);
}

TEST_CASE("grip hand: the read two frames behind while the game's own wrist moves under the glove") {
    const GripPose& grip = heavyCannonGrip();
    const auto animated = [&](std::size_t t) {
        return mixPose(kHandleWrist, grip.wrist, smooth((static_cast<float>(t) - 30.0f) / 30.0f));
    };
    Game game;
    game.lag = 2;
    CHECK(worstMiss(run(ramp(14, 90, 1, 0), animated, game), 20, 104) < 0.003f);
}

TEST_CASE("grip hand: dropped frames while the game's own wrist moves under the glove") {
    const GripPose& grip = heavyCannonGrip();
    const auto animated = [&](std::size_t t) {
        return mixPose(kHandleWrist, grip.wrist, smooth((static_cast<float>(t) - 30.0f) / 30.0f));
    };
    Game game;
    game.dropped = {32, 33, 41, 47, 48, 49, 70};
    CHECK(worstMiss(run(ramp(14, 90, 1, 0), animated, game), 20, 104) < 0.003f);
}

TEST_CASE("grip hand: a fading modifier weight never takes the glove past the grip, nor snaps it") {
    // The weight fades from 0.95 to 0.6 and back, 0.02 a tick, and the blend mixes each write a tick further
    // along the fade than the weight read for it.
    const auto current = [](std::size_t t) {
        const float tick = static_cast<float>(t);
        if (t < 40) {
            return 0.95f;
        }
        if (t < 80) {
            return std::max(0.6f, 0.95f - 0.02f * (tick - 40.0f));
        }
        return std::min(0.95f, 0.6f + 0.02f * (tick - 80.0f));
    };
    Game game;
    game.weights = [&](std::size_t t) {
        return ModWeights{current(t), t < 40 ? 0.95f : t < 80 ? 0.6f : 0.95f};
    };
    game.mixedAt = [&](std::size_t t) {
        return current(t + 1);
    };
    const std::vector<Landed> landed =
        run(ramp(14, 150, 5, 0), [](std::size_t) { return kHandleWrist; }, game);
    const float whole = length(heavyCannonGrip().wrist.position - kHandleWrist.position);
    CHECK(pathOf(landed).past < 0.002f);
    // Every tick eases, but tick 80: the target jumps to 0.95 there, and the glove written past by it while
    // the blend still mixes at 0.62 lands that much short of where it was (1 - 0.62 / 0.95 of the way), the
    // price of never landing past it.
    const std::vector<Landed> down(landed.begin(), landed.begin() + 80);
    const std::vector<Landed> up(landed.begin() + 80, landed.end());
    CHECK(pathOf(down).step < 0.35f * whole);
    CHECK(pathOf(up).step < 0.35f * whole);
    CHECK(length(landed[80].wrist.position - landed[79].wrist.position) <
          (1.0f - 0.62f / 0.95f + 0.05f) * whole);
    CHECK(worstMiss(landed, 140, 164) < 0.003f);
}

TEST_CASE("grip hand: a modifier weight below the usable range eases the glove off the grip, then back") {
    Game game;
    game.weights = [](std::size_t t) {
        const float w = t >= 50 && t < 70 ? 0.4f : kWeight;
        return ModWeights{w, w};
    };
    const std::vector<Landed> landed =
        run(ramp(14, 120, 5, 0), [](std::size_t) { return kHandleWrist; }, game);
    const float whole = length(heavyCannonGrip().wrist.position - kHandleWrist.position);
    // At tick 50 the game mixes the glove written for 0.95 at 0.4: its own drop, which the layer cannot ease,
    // so the steps are checked from tick 51, where the glove eases.
    const std::vector<Landed> after(landed.begin() + 51, landed.end());
    const Path path = pathOf(after);
    CHECK(path.step < 0.35f * whole);
    CHECK(pathOf(landed).past < 0.002f);
    CHECK(worstMiss(landed, 110, 134) < 0.003f);
}

TEST_CASE("grip hand: a rise from a settled low weight never takes the glove past the grip") {
    // Settled at 0.6 with the glove on the grip; at tick 60 the target moves to 0.609 (still inside the
    // settled band) and the blend mixes at it; at tick 80 the target jumps to 0.95 and the current rises 0.02
    // a tick, mixed a tick further along than read.
    const auto current = [](std::size_t t) {
        return t < 80 ? 0.6f : std::min(0.95f, 0.6f + 0.02f * (static_cast<float>(t) - 79.0f));
    };
    const auto target = [](std::size_t t) {
        return t < 60 ? 0.6f : t < 80 ? 0.609f : 0.95f;
    };
    Game game;
    game.weights = [&](std::size_t t) {
        return ModWeights{current(t), target(t)};
    };
    game.mixedAt = [&](std::size_t t) {
        return t < 60 ? 0.6f : t < 80 ? 0.609f : current(t + 1);
    };
    const std::vector<Landed> landed =
        run(ramp(14, 150, 5, 0), [](std::size_t) { return kHandleWrist; }, game);
    CHECK(pathOf(landed).past < 0.002f);
    CHECK(worstMiss(landed, 40, 60) < 0.003f);
    CHECK(worstMiss(landed, 140, 164) < 0.003f);
}
