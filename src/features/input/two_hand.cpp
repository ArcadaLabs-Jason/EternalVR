#include "features/input/two_hand.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

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

// Whether a squeeze made in this gate may have the right to take the gun: an interruption that ends the
// hold (a switch, a throw or a melee; a forced view) can pass with the grip still down, the others (a menu,
// the wheel, death, ...) are not the gun's.
bool mayKeepRight(TwoHandBlock block) {
    return block == TwoHandBlock::None || block == TwoHandBlock::GunDown || block == TwoHandBlock::ForcedView;
}

// The off hand is near enough the capture zone to be arriving at it: within kTwoHandArriveMetres of the
// support line, from where a hand can still reach the capture's own minimum along the line (the engage line
// less kTwoHandArriveMarginMetres) to that far past the support point (no cone: the hand is on its way). A
// hand nearer the weapon hand than that, or behind it, is not arriving: it could never take the gun.
bool arriving(OnGun at, SupportRow row) {
    const float reach = length(supportInAim(row));
    return at.along >= kTwoHandEngageLineMetres - kTwoHandArriveMarginMetres &&
           at.along <= reach + kTwoHandArriveMetres && at.off <= kTwoHandArriveMetres;
}

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
    wasDown_ = true;
    right_ = false;
    landed_ = false;
    arriveLeft_ = 0.0f;
    squeezed_ = false;
    tookThisSqueeze_ = false;
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
    const float dt = std::isfinite(f.dt) ? std::clamp(f.dt, 0.0f, 1.0f) : 0.0f;
    // Where the off hand is on the gun, with the gate open or not (a squeeze made on the gun in an
    // interruption keeps its right to take it).
    std::optional<OnGun> at;
    bool inZone = false;
    if (tracked) {
        at = onGun(f.weaponGrip, f.aim, f.offGripPoint, f.support);
        if (at) {
            r.distance = at->off;
            r.along = at->along;
            inZone = withinCapture(*at, f.support) &&
                     length(f.offGripPoint - f.weaponGrip) >= kTwoHandEngageLineMetres;
        }
    }

    // The squeeze's right to take the gun: given when it starts on the gun (or with the hand arriving, which
    // then has kTwoHandArriveSeconds to land in the zone), kept while the grip stays down.
    const bool pressed = gripDown && !wasDown_;
    std::optional<TwoHandMiss> missed;
    if (!gripDown) {
        if (squeezed_ && !tookThisSqueeze_) {
            squeeze_.endBlock = f.block;
            squeeze_.kept = right_;
            missed = squeeze_;
        }
        squeezed_ = false;
        tookThisSqueeze_ = false;
        right_ = false;
        landed_ = false;
        arriveLeft_ = 0.0f;
    } else if (pressed) {
        squeezed_ = true;
        tookThisSqueeze_ = false;
        squeeze_ = TwoHandMiss{};
        squeeze_.weapon = std::string(f.weapon);
        squeeze_.pressBlock = f.block;
        if (at) {
            squeeze_.pressKnown = true;
            squeeze_.along = at->along;
            squeeze_.off = at->off;
        }
        landed_ = inZone && mayKeepRight(f.block);
        right_ = landed_ || (at && mayKeepRight(f.block) && arriving(*at, f.support));
        arriveLeft_ = landed_ ? 0.0f : kTwoHandArriveSeconds;
    } else if (right_ && !mayKeepRight(f.block) && f.block != TwoHandBlock::NoSupport) {
        // The gate became one that is not the gun's (a menu, the wheel, death, a demon, a swim): the
        // squeeze's right to take it is over for this squeeze. (NoSupport is not in it: a melee shows an item
        // with no support grip for a third of a second, and the gun is taken again after it.)
        right_ = false;
        landed_ = false;
        arriveLeft_ = 0.0f;
    } else if (right_ && !landed_) {
        if (inZone) {
            landed_ = mayKeepRight(f.block);
            right_ = landed_;
        } else {
            arriveLeft_ -= dt;
            right_ = arriveLeft_ > 0.0f;
        }
    }
    squeeze_.peak = gripDown ? std::max(squeeze_.peak, f.offGrip) : squeeze_.peak;
    wasDown_ = gripDown;
    r.missed = std::move(missed);
    // Not published while the held item takes no support grip: nothing to hold the binding back for there.
    r.keeps = gripDown && right_ && f.block != TwoHandBlock::NoSupport;
    r.arrivable = !gripDown && tracked && at && mayKeepRight(f.block) && arriving(*at, f.support);
    r.capturable = open && inZone && (!gripDown || right_);
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
    r.engaged = engaged_;
    r.started = engaged_ && !was;
    tookThisSqueeze_ = tookThisSqueeze_ || engaged_;
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
