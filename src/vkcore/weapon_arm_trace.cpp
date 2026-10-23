// The weapon arm's trace (weapon_arm_state.hpp).

#include "vkcore/weapon_arm_state.hpp"

#include "features/arm/arm_frames.hpp"
#include "vkcore/log.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace evr::vkcore::controllers::weapon_arm::detail {

namespace {

constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
constexpr std::uint64_t kMaxTickLines = 6000;

} // namespace

void trace(ArmState& s, const input::ArmDecision& d, float weight) {
    if ((s.traced && d.controller == s.lastDecision.controller && d.reason == s.lastDecision.reason) ||
        s.traceLines >= 400) {
        return;
    }
    s.traced = true;
    s.lastDecision = d;
    ++s.traceLines;
    EVR_LOG("%s: %s (%s), weight %.2f", s.tag, d.controller ? "ik" : "game", input::armReasonName(d.reason),
            weight);
}

bool poseTraceDue(ArmState& s, const input::ControllerSettings& cfg) {
    if (!cfg.offhandTrace) {
        return false;
    }
    const ULONGLONG ticks = GetTickCount64();
    if (ticks - s.lastPoseTrace < 1000) {
        return false;
    }
    s.lastPoseTrace = ticks;
    return true;
}

void traceTick(ArmState& s, const xr_math::ModelPose& attach, float miss, float sleeve, Vec3 shoulderRead) {
    const float shoulder = length(shoulderRead - s.lastShoulder);
    const std::optional<float> shoulderStep =
        s.lastShoulderRead ? std::optional(length(shoulderRead - *s.lastShoulderRead)) : std::nullopt;
    s.lastShoulderRead = shoulderRead;
    if (!s.lastAttach || !s.lastWanted || s.tickLines >= kMaxTickLines) {
        return;
    }
    ++s.tickLines;
    const Vec3 then = arm::compose(*s.lastAttach, *s.lastWanted).position;
    const Vec3 now = arm::compose(attach, *s.lastWanted).position;
    const Quat q = conjugate(arm::toQuat(s.lastAttach->axis)) * arm::toQuat(attach.axis);
    const float turn = 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
    EVR_LOG(
        "%s: glove tick: wrist %.1f mm off in the gun's frame, the sleeve's end %.1f mm off the glove, the "
        "shoulder end %.1f mm off; "
        "the gun's root moved %.1f mm at the grip (%.1f mm at the root, turned %.2f deg) from the read the "
        "write was placed under; the shoulder end moved %.1f mm from the read before",
        s.tag, static_cast<double>(miss) * 1000.0, static_cast<double>(sleeve) * 1000.0,
        static_cast<double>(shoulder) * 1000.0, static_cast<double>(length(now - then)) * 1000.0,
        static_cast<double>(length(attach.position - s.lastAttach->position)) * 1000.0,
        static_cast<double>(turn), static_cast<double>(shoulderStep.value_or(-0.001f)) * 1000.0);
}

} // namespace evr::vkcore::controllers::weapon_arm::detail
