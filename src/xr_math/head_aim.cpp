#include "xr_math/head_aim.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::xr_math {

namespace {

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

} // namespace

float normalize180(float degrees) {
    float d = std::fmod(degrees + 180.0f, 360.0f);
    if (d < 0.0f) {
        d += 360.0f;
    }
    return d - 180.0f;
}

bool plausible(const IdAngles& angles) {
    const auto ok = [](float degrees) {
        return std::isfinite(degrees) && std::fabs(degrees) <= 1e6f;
    };
    return ok(angles.pitch) && ok(angles.yaw) && ok(angles.roll);
}

IdViewAxis axisFromAngles(const IdAngles& angles) {
    const float sy = std::sin(angles.yaw * kRadiansPerDegree);
    const float cy = std::cos(angles.yaw * kRadiansPerDegree);
    const float sp = std::sin(angles.pitch * kRadiansPerDegree);
    const float cp = std::cos(angles.pitch * kRadiansPerDegree);
    const float sr = std::sin(angles.roll * kRadiansPerDegree);
    const float cr = std::cos(angles.roll * kRadiansPerDegree);
    IdViewAxis axis;
    axis.forward = {cp * cy, cp * sy, -sp};
    axis.left = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp};
    axis.up = {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    return axis;
}

IdAngles anglesFromAxis(const IdViewAxis& axis) {
    IdAngles a;
    const Vec3 f = axis.forward;
    const float horizontal = std::sqrt(f.x * f.x + f.y * f.y);
    a.pitch = -std::atan2(f.z, horizontal) * kDegreesPerRadian;
    a.yaw = horizontal > 1e-6f ? std::atan2(f.y, f.x) * kDegreesPerRadian : 0.0f;
    // Roll: the angle of the up vector about forward, measured from the rolled-back frame's up.
    const IdViewAxis flat = axisFromAngles({a.pitch, a.yaw, 0.0f});
    a.roll = std::atan2(-dot(axis.up, flat.left), dot(axis.up, flat.up)) * kDegreesPerRadian;
    return a;
}

IdAngles headAngles(Quat headInIdTech) {
    return anglesFromAxis(viewAxisFromQuat(headInIdTech));
}

namespace {

bool sameDelta(float a, float b) {
    return std::fabs(normalize180(a - b)) < 1e-3f;
}

// The most recent value head aim wrote that equals `deltaYaw`, if it is still in the ring.
const HeadAimState::Written* findWritten(const HeadAimState& state, float deltaYaw) {
    const std::size_t n = std::min(state.writtenCount, state.written.size());
    for (std::size_t i = 0; i < n; ++i) { // newest first
        const auto& w = state.written[(state.writtenCount - 1 - i) % state.written.size()];
        if (sameDelta(w.deltaYaw, deltaYaw)) {
            return &w;
        }
    }
    return nullptr;
}

} // namespace

float headYawHeld(const HeadAimState& state, float deltaYaw, bool gameRewrote) {
    const float last = state.injected ? state.injectedYaw : 0.0f;
    if (!gameRewrote) {
        return last;
    }
    if (state.hasRestored && sameDelta(state.restored.deltaYaw, deltaYaw)) {
        return state.restored.injectedYaw;
    }
    if (const HeadAimState::Written* w = findWritten(state, deltaYaw)) {
        return w->injectedYaw;
    }
    return last;
}

float drivenBodyYaw(const HeadAimState& state, float gameYaw, float deltaYaw, bool gameRewrote) {
    return normalize180(gameYaw - headYawHeld(state, deltaYaw, gameRewrote));
}

HeadAimStep headAimStep(HeadAimState& state,
                        const IdAngles& game,
                        float deltaYaw,
                        const IdAngles& head,
                        bool gameRewrote,
                        float pitchLimit) {
    HeadAimStep step;
    // The head yaw the delta holds now.
    float inDelta = state.injected ? state.injectedYaw : 0.0f;
    if (!gameRewrote) {
        state.hasRestored = false; // a hold of the delta lasts only while the game keeps rewriting it
    } else {
        state.hasRestored = state.hasRestored && sameDelta(state.restored.deltaYaw, deltaYaw);
        if (!state.hasRestored) {
            if (const HeadAimState::Written* w = findWritten(state, deltaYaw)) {
                state.restored = *w;
                state.restoredIsOurs = true;
            } else {
                // The game's own value: it re-aimed the view that held the last injected head yaw. Kept, so
                // a delta the game holds there frame after frame keeps the body still.
                state.restored = {deltaYaw, inDelta};
                state.restoredIsOurs = false;
            }
            state.hasRestored = true;
        }
        inDelta = state.restored.injectedYaw;
        step.restoredWrite = state.restoredIsOurs;
    }
    step.bodyYaw = normalize180(game.yaw - inDelta);
    step.deltaYaw = normalize180(head.yaw - inDelta);
    const float targetPitch = std::clamp(head.pitch, -pitchLimit, pitchLimit);
    step.deltaPitch = targetPitch - game.pitch;
    state.injected = true;
    state.injectedYaw = head.yaw;
    return step;
}

void noteWritten(HeadAimState& state, float writtenDeltaYaw) {
    state.written[state.writtenCount % state.written.size()] = {writtenDeltaYaw, state.injectedYaw};
    ++state.writtenCount;
}

Quat headSway(float yawDegrees, float pitchDegrees, float periodSeconds, double seconds) {
    if (!(periodSeconds > 0.0f)) {
        return Quat::identity();
    }
    const double phase = std::fmod(seconds, static_cast<double>(periodSeconds)) / periodSeconds;
    const float s = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
    return headTurn(yawDegrees * s, pitchDegrees * s);
}

Quat headTurn(float yawDegrees, float pitchDegrees) {
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawDegrees * kRadiansPerDegree);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchDegrees * kRadiansPerDegree);
    return yaw * pitch;
}

} // namespace evr::xr_math
