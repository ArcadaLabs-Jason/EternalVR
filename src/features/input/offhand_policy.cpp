#include "features/input/offhand_policy.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

namespace {

// A pending action that animates the left arm (or hides the hands).
bool leftArmAction(std::int32_t action) {
    const auto a = static_cast<HandsAction>(action);
    switch (a) {
    case HandsAction::Melee:
    case HandsAction::MeleeRight: // the other hand's punch still swings the body and both arms
    case HandsAction::MeleeLeft:
    case HandsAction::BringDown:
    case HandsAction::BringUp:
    case HandsAction::ThrowAttach:
    case HandsAction::ThrowItem:
    case HandsAction::CustomAnim:
    case HandsAction::HammerThrow:
    case HandsAction::HammerSlam:
        return true;
    default:
        break;
    }
    // Generic hides (22..24) and the chainsaw (26..31).
    return (action >= static_cast<std::int32_t>(HandsAction::GenericHideInstant) &&
            action < static_cast<std::int32_t>(HandsAction::GenericUnhide)) ||
           (action >= static_cast<std::int32_t>(HandsAction::ChainsawFailedGk) &&
            action <= static_cast<std::int32_t>(HandsAction::ChainsawStabFail));
}

float smoothstep(float x) {
    const float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

const char* offhandModeName(OffhandMode mode) {
    switch (mode) {
    case OffhandMode::Game:
        return "game";
    case OffhandMode::Free:
        return "free";
    case OffhandMode::Probe:
        return "probe";
    }
    return "game";
}

const char* weaponArmModeName(WeaponArmMode mode) {
    return mode == WeaponArmMode::Ik ? "ik" : "game";
}

const char* armReasonName(ArmReason reason) {
    switch (reason) {
    case ArmReason::Controller:
        return "controller";
    case ArmReason::ModeGame:
        return "mode game";
    case ArmReason::Untracked:
        return "untracked";
    case ArmReason::ForcedView:
        return "forced view";
    case ArmReason::Sync:
        return "sync";
    case ArmReason::HandsHidden:
        return "hands hidden";
    case ArmReason::Action:
        return "left-arm action";
    case ArmReason::BusyFlags:
        return "left-arm animation";
    case ArmReason::HandsState:
        return "hands state";
    }
    return "?";
}

ArmDecision decideArm(const ArmSignals& s, OffhandMode mode) {
    const auto game = [](ArmReason r) {
        return ArmDecision{false, r};
    };
    if (mode == OffhandMode::Game) {
        return game(ArmReason::ModeGame);
    }
    if (s.forcedView) {
        return game(ArmReason::ForcedView);
    }
    if (s.syncActive) {
        return game(ArmReason::Sync);
    }
    if (s.fpHandsDisabled != 0 || s.hiddenReasons != 0 || s.destHandsState == kHandsStateHidden) {
        return game(ArmReason::HandsHidden);
    }
    if (leftArmAction(s.pendingAction)) {
        return game(ArmReason::Action);
    }
    if ((s.handsFlags & hands_flag::kLeftArmBusy) != 0) {
        return game(ArmReason::BusyFlags);
    }
    if (s.destHandsState == kHandsStateChainsawRev || s.destHandsState == kHandsStateChainsawStab ||
        s.destHandsState == kHandsStateTransitioning) {
        return game(ArmReason::HandsState);
    }
    if (!s.offHandTracked || !s.modelPlaced) {
        return game(ArmReason::Untracked);
    }
    return {true, ArmReason::Controller};
}

ArmDecision decideWeaponArm(const ArmSignals& signals, WeaponArmMode mode) {
    return decideArm(signals, mode == WeaponArmMode::Ik ? OffhandMode::Free : OffhandMode::Game);
}

float ArmBlend::update(bool controller, float dtSeconds, float blendSeconds, float holdSeconds) {
    const float dt = std::isfinite(dtSeconds) ? std::max(0.0f, dtSeconds) : 0.0f;
    if (!controller) {
        sinceGame_ = 0.0f;
        const float out = blendSeconds / 3.0f;
        ramp_ = out > 0.0f ? std::max(0.0f, ramp_ - dt / out) : 0.0f;
        return weight();
    }
    sinceGame_ += dt;
    if (sinceGame_ < holdSeconds) {
        return weight();
    }
    ramp_ = blendSeconds > 0.0f ? std::min(1.0f, ramp_ + dt / blendSeconds) : 1.0f;
    return weight();
}

float ArmBlend::weight() const {
    return smoothstep(ramp_);
}

void ArmBlend::reset() {
    ramp_ = 0.0f;
    sinceGame_ = 1e9f;
}

} // namespace evr::input
