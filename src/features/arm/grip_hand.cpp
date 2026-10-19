// The right glove on the gun's grip (grip_hand.hpp).

#include "features/arm/grip_hand.hpp"

#include "common/quat.hpp"
#include "features/arm/arm_frames.hpp"
#include "features/arm/mod_mix.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::arm {

namespace {

constexpr float kDegreesPerRadian = 180.0f / 3.14159265f;

xr_math::ModelPose pose(Vec3 position, Quat rotation) {
    return {position, fromQuat(rotation)};
}

float dot(Quat a, Quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// The angle between two orientations, degrees.
float turnDegrees(const xr_math::IdViewAxis& a, const xr_math::IdViewAxis& b) {
    const Quat q = conjugate(toQuat(a)) * toQuat(b);
    return 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
}

} // namespace

std::optional<HandJointIndices> findHandJoints(const Skeleton& skeleton,
                                               std::int16_t wrist,
                                               const std::array<std::int16_t, kHandJointCount>& handles,
                                               std::string& error) {
    const auto count = static_cast<std::int16_t>(skeleton.parents.size());
    if (wrist < 0 || wrist >= count || skeleton.nameHandles.size() != skeleton.parents.size()) {
        error = "the wrist " + std::to_string(wrist) + " is not in the skeleton";
        return std::nullopt;
    }
    HandJointIndices out{};
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const std::int16_t p = kHandJointParents[i];
        const std::int16_t parent = p < 0 ? wrist : out[static_cast<std::size_t>(p)];
        int found = 0;
        for (std::int16_t j = 0; j < count; ++j) {
            const auto at = static_cast<std::size_t>(j);
            if (skeleton.parents[at] == parent && skeleton.nameHandles[at] == handles[i] && ++found == 1) {
                out[i] = j;
            }
        }
        if (found != 1) {
            error = std::string("the wrist's joint ") + kRightHandJointNames[i] + " is " +
                    (found == 0 ? "not under " : "not unique under ") + "joint " + std::to_string(parent);
            return std::nullopt;
        }
    }
    return out;
}

const GripPose& heavyCannonGrip() {
    // Local poses (position, then the quaternion x y z w) of the Heavy Cannon idle's frame 0, which the idle
    // holds still: md6/player/human/base/motion/core/assaultrifles/heavy_cannon/idle.md6anim on
    // marine.md6skl [static, build 25216728].
    static const GripPose grip{
        pose({-0.170182f, -0.039651f, -0.073791f}, {-0.562867f, 0.366232f, 0.470160f, 0.572717f}),
        {
            pose({-0.024765f, -0.019050f, 0.011430f}, {-0.069854f, 0.884539f, -0.401622f, 0.226739f}),
            pose({-0.000001f, -0.051435f, 0.000001f}, {-0.238522f, 0.000022f, 0.000022f, 0.971137f}),
            pose({0.000000f, -0.022860f, -0.000001f}, {-0.282329f, 0.000022f, 0.000022f, 0.959318f}),
            pose({-0.013811f, -0.042864f, -0.000000f}, {0.000000f, 0.000000f, -0.043619f, 0.999048f}),
            pose({-0.000000f, -0.059616f, -0.000001f}, {-0.187766f, 0.021127f, 0.094153f, 0.977462f}),
            pose({0.000001f, -0.047245f, 0.000000f}, {-0.386732f, 0.000022f, 0.000022f, 0.922192f}),
            pose({-0.000002f, -0.028122f, 0.000000f}, {-0.640770f, 0.000022f, 0.000022f, 0.767733f}),
            pose({0.000001f, -0.042863f, -0.000002f}, {-0.000000f, 0.000000f, 0.026177f, 0.999657f}),
            pose({0.000001f, -0.060741f, 0.000001f}, {-0.543747f, -0.024882f, 0.067049f, 0.836196f}),
            pose({-0.000002f, -0.048370f, -0.000000f}, {-0.674305f, 0.000022f, 0.000022f, 0.738453f}),
            pose({0.000001f, -0.029242f, 0.000001f}, {-0.443056f, 0.000022f, 0.000022f, 0.896494f}),
            pose({0.009527f, -0.040005f, -0.000000f}, {0.000000f, -0.000000f, 0.121869f, 0.992546f}),
            pose({-0.000001f, -0.056242f, 0.000001f}, {-0.661401f, -0.003129f, -0.010509f, 0.749953f}),
            pose({0.000001f, -0.042744f, -0.000001f}, {-0.489366f, 0.000022f, 0.000022f, 0.872078f}),
            pose({-0.000001f, -0.028121f, -0.000000f}, {-0.435244f, 0.000022f, 0.000022f, 0.900313f}),
            pose({0.019050f, -0.035243f, -0.000002f}, {0.000000f, 0.000000f, 0.207912f, 0.978148f}),
            pose({-0.000000f, -0.050618f, 0.000000f}, {-0.605811f, 0.028723f, -0.037312f, 0.794214f}),
            pose({0.000001f, -0.033746f, 0.000000f}, {-0.474088f, 0.000022f, 0.000022f, 0.880478f}),
            pose({-0.000000f, -0.022497f, 0.000001f}, {-0.355096f, 0.000022f, 0.000022f, 0.934830f}),
        }};
    return grip;
}

std::optional<xr_math::ModelPose>
animatedUnder(const xr_math::ModelPose& landed, const xr_math::ModelPose& written, float weight) {
    if (!usableModWeight(weight) || weight >= 1.0f) {
        return std::nullopt;
    }
    const float rest = 1.0f - weight;
    const Vec3 position = (landed.position - written.position * weight) * (1.0f / rest);
    // The mix is (rest qa + weight qw) / s with qw taken the short way round from qa, so qa = (s qf - weight
    // qw) / rest, s fixed by |qa| = 1: s^2 - 2 s weight c + weight^2 - rest^2 = 0 with c = qf.qw. The larger
    // root is the mix's (the smaller gives -qw's side).
    const Quat qf = toQuat(landed.axis);
    Quat qw = toQuat(written.axis);
    float c = dot(qf, qw);
    if (c < 0.0f) {
        qw = {-qw.x, -qw.y, -qw.z, -qw.w};
        c = -c;
    }
    const float disc = weight * weight * (c * c - 1.0f) + rest * rest;
    if (disc < -1e-6f) {
        return std::nullopt;
    }
    const float s = weight * c + std::sqrt(std::max(0.0f, disc));
    const Quat qa{(s * qf.x - weight * qw.x) / rest, (s * qf.y - weight * qw.y) / rest,
                  (s * qf.z - weight * qw.z) / rest, (s * qf.w - weight * qw.w) / rest};
    if (dot(qa, qw) < 0.0f) {
        return std::nullopt;
    }
    return xr_math::ModelPose{position, fromQuat(normalize(qa))};
}

std::optional<xr_math::ModelPose> WristTracker::update(bool clean,
                                                       const xr_math::ModelPose& attach,
                                                       const xr_math::ModelPose& wrist,
                                                       bool settled) {
    const xr_math::ModelPose landed = relative(attach, wrist);
    if (clean) {
        animated_ = landed;
        return animated_;
    }
    if (!animated_ || !settled || writes_ < kSteadyWrites) {
        return animated_;
    }
    for (std::size_t i = 1; i < written_.size(); ++i) {
        if (length(written_[i].position - written_[i - 1].position) > kSteadyUnits ||
            turnDegrees(written_[i].axis, written_[i - 1].axis) > kSteadyDegrees) {
            return animated_;
        }
    }
    // The override written last, as the game placed it: under the attach joint this read gives.
    const auto a = animatedUnder(landed, relative(attach, model_[0]), weight_);
    if (a && plausiblePose(*a) && length(a->position) <= kMaxUnits) {
        animated_ = a;
    }
    return animated_;
}

std::optional<WristTracker::Lag>
WristTracker::readLag(const xr_math::ModelPose& attach, const xr_math::ModelPose& wrist, float weight) const {
    if (!animated_ || writes_ < kSteadyWrites) {
        return std::nullopt;
    }
    for (std::size_t i = 1; i < model_.size(); ++i) {
        if (length(model_[i].position - model_[i - 1].position) < kLagApartUnits) {
            return std::nullopt;
        }
    }
    const xr_math::ModelPose landed = relative(attach, wrist);
    std::optional<Lag> out;
    for (std::size_t i = 0; i < model_.size(); ++i) {
        const xr_math::ModelPose at = mixPose(*animated_, relative(attach, model_[i]), weight);
        const float miss = length(at.position - landed.position);
        if (!out || miss < out->miss) {
            out = Lag{static_cast<int>(i) + 1, miss};
        }
    }
    return out;
}

GloveWeight gloveWeight(ModWeights weights) {
    const float larger = std::max(weights.current, weights.target);
    return {usableModWeight(larger) ? larger : 1.0f,
            usableModWeight(weights.current) && std::fabs(weights.current - weights.target) < kSettledWeight};
}

float GloveSettle::update(bool settled, float dtSeconds, float blendSeconds) {
    const float in = blendSeconds > 0.0f ? dtSeconds / blendSeconds : 1.0f;
    ramp_ = std::clamp(ramp_ + (settled ? in : -in), 0.0f, 1.0f);
    return ramp_ * ramp_ * (3.0f - 2.0f * ramp_);
}

void WristTracker::wrote(const xr_math::ModelPose& attach, const xr_math::ModelPose& written, float weight) {
    weight_ = weight;
    for (std::size_t i = written_.size() - 1; i > 0; --i) {
        written_[i] = written_[i - 1];
    }
    written_[0] = relative(attach, written);
    for (std::size_t i = model_.size() - 1; i > 0; --i) {
        model_[i] = model_[i - 1];
    }
    model_[0] = written;
    writes_ = std::min(writes_ + 1, kSteadyWrites);
}

void WristTracker::idle() {
    writes_ = 0;
}

void WristTracker::reset() {
    animated_.reset();
    writes_ = 0;
}

GloveArm gloveArm(const ArmPoses& read,
                  const xr_math::ModelPose& wristAnimated,
                  const GripPose& grip,
                  float gripWeight) {
    const std::size_t hand = index(ArmJoint::Hand);
    const xr_math::ModelPose& attach = read[index(ArmJoint::Attach)];
    const xr_math::ModelPose wrist = compose(attach, wristAnimated);
    const xr_math::ModelPose onGrip =
        compose(attach, mixPose(wristAnimated, grip.wrist, std::clamp(gripWeight, 0.0f, 1.0f)));
    GloveArm out{read, read};
    for (std::size_t i = hand; i < kArmJointCount; ++i) {
        const xr_math::ModelPose local = relative(read[hand], read[i]);
        out.animated[i] = compose(wrist, local);
        out.from[i] = compose(onGrip, local);
    }
    return out;
}

GripOverrides gripOverrides(const xr_math::ModelPose& attach,
                            const xr_math::ModelPose& animatedWrist,
                            const xr_math::ModelPose& wantedWrist,
                            const std::array<xr_math::ModelPose, kHandJointCount>& animatedHand,
                            const GripPose& grip,
                            float curl,
                            std::optional<float> weight) {
    const auto past = [&](const xr_math::ModelPose& from, const xr_math::ModelPose& to) {
        return weight ? pastPose(from, to, *weight) : to;
    };
    GripOverrides out;
    out.wrist = compose(attach, past(animatedWrist, wantedWrist));
    // Each joint placed under its parent's written pose: the modifiers are mixed in each joint's parent's
    // frame.
    std::array<xr_math::ModelPose, kHandJointCount> written{};
    std::array<bool, kHandJointCount> finger{};
    for (const std::uint8_t i : kFingerMods) {
        finger[i] = true;
    }
    const float t = std::clamp(curl, 0.0f, 1.0f);
    for (std::size_t i = 0; i < kHandJointCount; ++i) {
        const std::int16_t p = kHandJointParents[i];
        const xr_math::ModelPose& parent = p < 0 ? out.wrist : written[static_cast<std::size_t>(p)];
        const xr_math::ModelPose local =
            finger[i] ? past(animatedHand[i], mixPose(animatedHand[i], grip.hand[i], t)) : animatedHand[i];
        written[i] = compose(parent, local);
    }
    for (std::size_t k = 0; k < kFingerModCount; ++k) {
        out.fingers[k] = written[kFingerMods[k]];
    }
    return out;
}

} // namespace evr::arm
