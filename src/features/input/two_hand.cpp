#include "features/input/two_hand.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace evr::input {

namespace {

// The shortest turn taking unit `from` onto unit `to`; nullopt when they are opposite (no single shortest
// turn).
std::optional<Quat> turnBetween(Vec3 from, Vec3 to) {
    const float c = dot(from, to);
    if (!(c > -0.9999f)) {
        return std::nullopt;
    }
    const Vec3 axis = cross(from, to);
    // The half-angle form: (axis, 1 + c) normalised is the turn by the angle between them.
    return normalize(Quat{axis.x, axis.y, axis.z, 1.0f + c});
}

float dotQ(Quat a, Quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// From `a` toward `b` by `t` along the shorter arc.
Quat slerp(Quat a, Quat b, float t) {
    float d = dotQ(a, b);
    if (d < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) {
        return normalize(
            Quat{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w)});
    }
    const float theta = std::acos(std::min(d, 1.0f));
    const float s = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / s;
    const float wb = std::sin(t * theta) / s;
    return normalize(
        Quat{wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w});
}

bool finiteQuat(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

bool finiteVec(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

} // namespace

SupportRow supportRowFor(SupportRow row, Hand weaponHand) {
    if (weaponHand == Hand::Left) {
        row.left = -row.left;
    }
    return row;
}

Vec3 supportInAim(SupportRow row) {
    // id Tech forward, left, up are OpenXR -Z, -X, +Y.
    return {-row.left, row.up, -row.forward};
}

Vec3 supportPoint(Vec3 grip, Quat aim, SupportRow row) {
    return grip + rotate(aim, supportInAim(row));
}

std::optional<OnGun> onGun(Vec3 grip, Quat aim, Vec3 offGrip, SupportRow row) {
    const Vec3 support = rotate(aim, supportInAim(row));
    const float reach = length(support);
    const Vec3 to = offGrip - grip;
    if (!(reach > 0.0f) || !finiteVec(to) || !finiteVec(support)) {
        return std::nullopt;
    }
    const Vec3 u = support * (1.0f / reach);
    const float along = dot(to, u);
    const float off = length(to - u * along);
    if (!std::isfinite(along) || !std::isfinite(off)) {
        return std::nullopt;
    }
    return OnGun{along, off};
}

bool withinCapture(OnGun at, SupportRow row) {
    const float reach = length(supportInAim(row));
    return at.along >= kTwoHandEngageLineMetres && at.along <= reach + kTwoHandCaptureMetres &&
           at.off <= kTwoHandCaptureMetres &&
           std::atan2(at.off, at.along) <= kTwoHandCaptureDegrees * kRadiansPerDegree;
}

Vec3 gloveSlide(Vec3 grip, Vec3 offGrip, float supportLength) {
    const Vec3 line = offGrip - grip;
    const float l = length(line);
    if (!(l > 0.0f) || !std::isfinite(l) || !(supportLength > 0.0f) || !std::isfinite(supportLength)) {
        return {};
    }
    return line * ((std::min(l, supportLength) - supportLength) / l);
}

std::optional<Quat> twoHandTurn(Vec3 grip, Quat aim, Vec3 offGrip, SupportRow row) {
    const Vec3 line = offGrip - grip;
    const Vec3 support = rotate(aim, supportInAim(row));
    if (!(length(line) >= kTwoHandMinLineMetres) || !(length(support) > 0.0f)) {
        return std::nullopt;
    }
    const std::optional<Quat> turn = turnBetween(normalize(support), normalize(line));
    if (!turn || !finiteQuat(*turn)) {
        return std::nullopt;
    }
    return turn;
}

std::optional<Quat> twoHandAim(Vec3 grip, Quat aim, Vec3 offGrip, SupportRow row) {
    const std::optional<Quat> turn = twoHandTurn(grip, aim, offGrip, row);
    if (!turn) {
        return std::nullopt;
    }
    return normalize(*turn * aim);
}

bool gunUp(ArmSignals signals) {
    signals.handsFlags &= ~hands_flag::kReloading;
    signals.offHandTracked = true;
    signals.modelPlaced = true;
    signals.forcedView = false;
    return decideArm(signals, OffhandMode::Free).controller;
}

const char* twoHandBlockName(TwoHandBlock block) {
    switch (block) {
    case TwoHandBlock::None:
        return "none";
    case TwoHandBlock::Off:
        return "off";
    case TwoHandBlock::NotHandAim:
        return "not hand aim";
    case TwoHandBlock::Dead:
        return "the player is dead";
    case TwoHandBlock::Demon:
        return "piloting a demon";
    case TwoHandBlock::ForcedView:
        return "forced view";
    case TwoHandBlock::WeaponWheel:
        return "weapon wheel";
    case TwoHandBlock::Menu:
        return "menu";
    case TwoHandBlock::Swimming:
        return "swimming";
    case TwoHandBlock::GunDown:
        return "gun down";
    case TwoHandBlock::NoSupport:
        return "no support grip";
    }
    return "?";
}

TwoHandBlock twoHandBlock(const TwoHandGates& g) {
    if (!g.enabled) {
        return TwoHandBlock::Off;
    }
    if (!g.handAim) {
        return TwoHandBlock::NotHandAim;
    }
    if (g.dead) {
        return TwoHandBlock::Dead;
    }
    if (g.demon) {
        return TwoHandBlock::Demon;
    }
    if (g.forcedView) {
        return TwoHandBlock::ForcedView;
    }
    if (g.weaponWheel) {
        return TwoHandBlock::WeaponWheel;
    }
    if (g.menu) {
        return TwoHandBlock::Menu;
    }
    if (g.swimming) {
        return TwoHandBlock::Swimming;
    }
    if (!g.gunUp) {
        return TwoHandBlock::GunDown;
    }
    if (!g.support) {
        return TwoHandBlock::NoSupport;
    }
    return TwoHandBlock::None;
}

const char* twoHandEndName(TwoHandEnd end) {
    switch (end) {
    case TwoHandEnd::None:
        return "none";
    case TwoHandEnd::GripLetGo:
        return "grip let go";
    case TwoHandEnd::TooFar:
        return "off hand past the release radius";
    case TwoHandEnd::TooClose:
        return "hands too close";
    case TwoHandEnd::Blocked:
        return "gate closed";
    case TwoHandEnd::Untracked:
        return "a hand lost";
    }
    return "?";
}

TwoHandHold::TwoHandHold(AnalogThresholds grip, std::optional<OneEuroParams> smoothing)
    : grip_(sanitizedThresholds(grip, kGripThresholds)) {
    if (smoothing) {
        filter_.emplace(*smoothing);
    }
}

void TwoHandHold::reset() {
    grip_ = AnalogButton(grip_.thresholds());
    engaged_ = false;
    fresh_ = false;
    applied_ = Quat{};
    from_ = Quat{};
    ease_ = 1.0f;
    if (filter_) {
        filter_->reset();
    }
}

TwoHandResult TwoHandHold::update(const TwoHandFrame& f) {
    TwoHandResult r;
    r.aim = f.aim;
    grip_.update(f.offGrip);
    const bool gripDown = grip_.isDown();
    const bool tracked = f.weaponTracked && f.offTracked;
    const bool open = f.block == TwoHandBlock::None;
    if (!gripDown) {
        fresh_ = true;
    }
    if (tracked && open) {
        if (const std::optional<OnGun> at = onGun(f.weaponGrip, f.aim, f.offGripPoint, f.support)) {
            r.distance = at->off;
            r.along = at->along;
            r.capturable = fresh_ && withinCapture(*at, f.support) &&
                           length(f.offGripPoint - f.weaponGrip) >= kTwoHandEngageLineMetres;
        }
    }
    // The held aim is built from the weapon hand's aim as tracked (the one-handed aim's own input) and then
    // smoothed, if at all, by the hold's filter.
    const Quat trackedAim = f.trackedAim.value_or(f.aim);
    const float reach = length(supportInAim(f.support));
    const float line = length(f.offGripPoint - f.weaponGrip);
    const bool was = engaged_;
    std::optional<Quat> turn;
    if (engaged_) {
        TwoHandEnd end = TwoHandEnd::None;
        if (!open) {
            end = TwoHandEnd::Blocked;
        } else if (!tracked) {
            end = TwoHandEnd::Untracked;
        } else if (!gripDown) {
            end = TwoHandEnd::GripLetGo;
        } else if (turn = twoHandTurn(f.weaponGrip, trackedAim, f.offGripPoint, f.support); !turn) {
            end = TwoHandEnd::TooClose;
        } else {
            // Held, the off hand is on the gun's support line: how far past the support point it has slid.
            r.slide = line - reach;
            if (!(r.slide <= kTwoHandReleaseMetres)) {
                end = TwoHandEnd::TooFar;
            }
        }
        if (end != TwoHandEnd::None) {
            engaged_ = false;
            r.ended = end;
            r.slide = 0.0f;
        }
    } else if (gripDown && r.capturable) {
        turn = twoHandTurn(f.weaponGrip, trackedAim, f.offGripPoint, f.support);
        if (turn) {
            engaged_ = true;
            r.slide = line - reach;
        }
    }
    if (gripDown) {
        fresh_ = false; // this squeeze has taken the gun, or it never will
    }
    r.engaged = engaged_;
    r.started = engaged_ && !was;
    const float dt = std::isfinite(f.dt) ? std::clamp(f.dt, 0.0f, 1.0f) : 0.0f;
    clock_ += dt;
    // Held, the aim is this frame's two-handed one (`turn` is set on every path that leaves the hold
    // engaged), smoothed from the grab on (the filter starts there, so the grab's first frame is the line's
    // own); the turn the ease goes to is from the one-handed aim to it. Else none.
    Quat goal;
    if (engaged_) {
        Quat held = normalize(turn.value_or(Quat{}) * trackedAim);
        if (filter_) {
            if (r.started) {
                filter_->reset();
            }
            const Quat smoothed = normalize(filter_->update(held, clock_));
            held = finiteQuat(smoothed) ? smoothed : held;
        }
        r.target = held;
        goal = normalize(held * conjugate(f.aim));
    }

    // The ease: from the turn applied last frame toward this frame's (the two-handed turn, or none).
    if (engaged_ != was) {
        from_ = applied_;
        ease_ = 0.0f;
    }
    ease_ = std::min(1.0f, ease_ + dt / kTwoHandRampSeconds);
    if (ease_ >= 1.0f) {
        applied_ = goal;
    } else {
        const float t = ease_ * ease_ * (3.0f - 2.0f * ease_); // smoothstep: no kick at either end
        applied_ = slerp(from_, goal, t);
        if (!finiteQuat(applied_)) {
            applied_ = goal;
            ease_ = 1.0f;
        }
    }
    r.easing = ease_ < 1.0f;
    if (engaged_ && !r.easing) {
        r.aim = r.target; // exactly the two-handed aim
    } else if (r.easing) {
        r.aim = normalize(applied_ * f.aim);
    } // else released and settled: exactly the weapon hand's own
    return r;
}

} // namespace evr::input
