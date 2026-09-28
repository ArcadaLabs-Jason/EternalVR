#include "features/roomscale/body_follow.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::roomscale {

namespace {

// Frames further apart than this (a hitch, a menu without game views) are not a displacement to follow.
constexpr double kMaxFrameSeconds = 0.25;
// The absorbed move per frame may exceed what the request asked for by this factor and margin (the game
// accelerates and stops a little past the request).
constexpr float kAbsorbFactor = 1.5f;
constexpr float kAbsorbMarginMetres = 0.005f;
// The body's speed is smoothed over about this long (single frames are a few milliseconds apart).
constexpr float kSpeedSmoothingSeconds = 0.015f;
// Following stops this share of the deadzone from the head.
constexpr float kStopShare = 0.4f;

Vec3 horizontal(Vec3 v) {
    return {v.x, 0.0f, v.z};
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float scaleOf(float unitsPerMetre) {
    return finiteInRangeOr(unitsPerMetre, 0.01f, 100.0f, 1.0f);
}

} // namespace

BodyFollowSettings sanitized(BodyFollowSettings settings) {
    const BodyFollowSettings defaults;
    settings.deadzoneMetres = finiteInRangeOr(settings.deadzoneMetres, 0.01f, 0.5f, defaults.deadzoneMetres);
    if (settings.walkCommand < 30 || settings.walkCommand > 127) {
        settings.walkCommand = defaults.walkCommand;
    }
    if (settings.creepCommand < 20 || settings.creepCommand > 127) {
        settings.creepCommand = defaults.creepCommand;
    }
    settings.coastSeconds = finiteInRangeOr(settings.coastSeconds, 0.0f, 0.5f, defaults.coastSeconds);
    settings.maxSpeed = finiteInRangeOr(settings.maxSpeed, 0.5f, 5.0f, defaults.maxSpeed);
    return settings;
}

FollowRequest followRequest(Vec3 gapRoom, float speedToward, bool wasEngaged, const BodyFollowSettings& raw) {
    const BodyFollowSettings s = sanitized(raw);
    FollowRequest out;
    const Vec3 flat = horizontal(gapRoom);
    if (!finite(flat) || !std::isfinite(speedToward)) {
        return out;
    }
    const float gap = length(flat);
    const float stopRadius = kStopShare * s.deadzoneMetres;
    out.engaged = wasEngaged ? gap > stopRadius : gap > s.deadzoneMetres;
    if (!out.engaged) {
        return out;
    }
    // Where the body stops if the command ends now (the coast is the same in room and game terms: a time).
    // Walk while it would still stop short of the stop radius, creep while it would stop short of the head.
    const float stop = std::max(speedToward, 0.0f) * s.coastSeconds;
    int command = 0;
    if (gap - stop > stopRadius) {
        out.tier = FollowTier::Walk;
        command = s.walkCommand;
    } else if (gap - stop > 0.0f) {
        out.tier = FollowTier::Creep;
        command = s.creepCommand;
    } else {
        out.tier = FollowTier::Coast;
    }
    const Vec3 direction = flat * (1.0f / gap);
    const float fraction = static_cast<float>(command) / 127.0f;
    out.move = {direction.x * fraction, -direction.z * fraction};
    return out;
}

Vec3 displacementInRoom(Vec3 worldUnits, Vec3 bodyForward, Vec3 bodyLeft, float unitsPerMetre) {
    // The inverse of headOffsetInWorld: id Tech (forward, left, up) in the body frame, then OpenXR axes.
    const Vec3 bodyUp = cross(bodyForward, bodyLeft);
    const float scale = scaleOf(unitsPerMetre);
    const float forward = dot(worldUnits, bodyForward) / scale;
    const float left = dot(worldUnits, bodyLeft) / scale;
    const float up = dot(worldUnits, bodyUp) / scale;
    return {-left, up, -forward};
}

const char* followTierName(FollowTier tier) {
    switch (tier) {
    case FollowTier::None:
        return "none";
    case FollowTier::Coast:
        return "coast";
    case FollowTier::Creep:
        return "creep";
    case FollowTier::Walk:
        return "walk";
    }
    return "?";
}

const char* followBlockName(FollowBlock block) {
    switch (block) {
    case FollowBlock::None:
        return "none";
    case FollowBlock::Off:
        return "off";
    case FollowBlock::NotAnchored:
        return "not anchored";
    case FollowBlock::Seated:
        return "seated";
    case FollowBlock::NoOrigin:
        return "no origin";
    case FollowBlock::Teleport:
        return "teleport";
    case FollowBlock::Menu:
        return "menu";
    case FollowBlock::Cutscene:
        return "cutscene or forced view";
    case FollowBlock::Stick:
        return "stick";
    case FollowBlock::JumpOrDash:
        return "jump or dash";
    case FollowBlock::Airborne:
        return "airborne";
    case FollowBlock::Fast:
        return "fast motion";
    case FollowBlock::Settling:
        return "settling";
    case FollowBlock::Count:
        break;
    }
    return "?";
}

BodyFollow::BodyFollow(const BodyFollowSettings& settings) : settings_(sanitized(settings)) {
    settings_.enabled = settings.enabled;
}

void BodyFollow::reset() {
    velocity_ = {};
    lastSeconds_ = -1.0;
    blockedUntil_ = -1.0;
    engaged_ = false;
}

FollowStep BodyFollow::update(const FollowTick& tick) {
    FollowStep step;
    const double dt = lastSeconds_ < 0.0 ? 0.0 : tick.seconds - lastSeconds_;
    lastSeconds_ = tick.seconds;
    const bool moved = tick.displacement && finite(*tick.displacement);
    const Vec3 d = moved ? *tick.displacement : Vec3{};
    const float along = length(horizontal(d));
    const float fast = std::max(2.0f * settings_.maxSpeed, 4.0f);

    FollowBlock cause = FollowBlock::None;
    if (!settings_.enabled || !tick.hookInstalled) {
        cause = FollowBlock::Off;
    } else if (!tick.anchored) {
        cause = FollowBlock::NotAnchored;
    } else if (tick.seated) {
        cause = FollowBlock::Seated;
    } else if (!moved || !(dt > 0.0) || dt > kMaxFrameSeconds || !finite(tick.gapRoom)) {
        cause = FollowBlock::NoOrigin;
    } else if (length(d) > kTeleportMetres) {
        cause = FollowBlock::Teleport;
    } else if (tick.menu) {
        cause = FollowBlock::Menu;
    } else if (tick.cutscene || tick.forcedView) {
        cause = FollowBlock::Cutscene;
    } else if (tick.stick) {
        cause = FollowBlock::Stick;
    } else if (tick.jumpOrDash) {
        cause = FollowBlock::JumpOrDash;
    } else if (std::fabs(d.y) > kAirborneSpeed * static_cast<float>(dt)) {
        cause = FollowBlock::Airborne;
    } else if (along > fast * static_cast<float>(dt)) {
        cause = FollowBlock::Fast;
    }
    step.cause = cause;
    if (cause != FollowBlock::None) {
        blockedUntil_ = tick.seconds + kResumeSeconds;
        engaged_ = false;
        velocity_ = {};
        step.block = cause;
        return step;
    }
    if (tick.seconds < blockedUntil_) {
        engaged_ = false;
        step.block = FollowBlock::Settling;
        return step;
    }

    // While follow is what moves the body, all of its horizontal move (toward the head, sliding along a
    // wall, coasting past it) is taken into the room, no more per frame than follow can move it; the rest of
    // the body's motion is the game's and the view rides with it.
    const float dtf = static_cast<float>(dt);
    const Vec3 move = horizontal(d);
    if (tick.commanded && along > 0.0f) {
        const float cap = kAbsorbFactor * settings_.maxSpeed * dtf + kAbsorbMarginMetres;
        step.absorbed = along > cap ? move * (cap / along) : move;
    }
    const float blend = dtf / (dtf + kSpeedSmoothingSeconds);
    velocity_ = velocity_ + (move * (1.0f / dtf) - velocity_) * blend;
    const Vec3 gap = horizontal(tick.gapRoom) - step.absorbed;
    const float gapLength = length(gap);
    const float toward = gapLength > 0.0f ? dot(velocity_, gap) / gapLength : 0.0f;
    step.gap = gapLength;
    step.speedToward = toward;
    step.request = followRequest(gap, toward, engaged_, settings_);
    engaged_ = step.request.engaged;
    return step;
}

} // namespace evr::roomscale
