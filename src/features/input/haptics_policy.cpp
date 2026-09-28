#include "features/input/haptics_policy.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

namespace {

struct PulseShape {
    float amplitude;
    float seconds;
};

// Before the strength is applied. Short enough to read as clicks, not buzzing.
constexpr PulseShape kFireStart{0.6f, 0.04f};
constexpr PulseShape kFireRepeat{0.35f, 0.03f};
constexpr PulseShape kPunch{1.0f, 0.08f};
constexpr PulseShape kMenuEnter{0.2f, 0.015f};
constexpr PulseShape kMenuClick{0.35f, 0.02f};
constexpr PulseShape kCapture{0.8f, 0.12f};
// A game rumble pulse still playing is renewed when it has less than this left or its level moved by more
// than kRumbleStep; weaker than kMinRumble (after the strength) counts as none.
constexpr double kRumbleRenewSeconds = 0.04;
constexpr float kRumbleStep = 0.05f;
constexpr float kMinRumble = 0.01f;

std::size_t index(Hand hand) {
    return static_cast<std::size_t>(hand);
}

float motor(float value) {
    return finiteInRangeOr(value, 0.0f, 1.0f, value > 1.0f ? 1.0f : 0.0f);
}

} // namespace

const char* hapticSourceName(HapticSource source) {
    switch (source) {
    case HapticSource::Fire:
        return "fire";
    case HapticSource::Punch:
        return "punch";
    case HapticSource::Menu:
        return "menu";
    case HapticSource::Game:
        return "game";
    case HapticSource::Capture:
        return "capture";
    case HapticSource::Count:
        break;
    }
    return "?";
}

HapticsPolicy::HapticsPolicy(float strength)
    : strength_(finiteInRangeOr(strength, 0.0f, 1.0f, kDefaultHapticStrength)) {}

void HapticsPolicy::reset() {
    hands_ = {};
    fireWasHeld_ = false;
    nextFire_ = 0.0;
}

void HapticsPolicy::offer(std::optional<HapticCommand>& best,
                          HapticSource source,
                          float amplitude,
                          float seconds) const {
    const float scaled = std::min(amplitude * strength_, 1.0f);
    if (!(scaled > 0.0f) || !(seconds > 0.0f)) {
        return;
    }
    if (!best || scaled > best->amplitude) {
        best = HapticCommand{HapticCommandKind::Pulse, source, scaled, seconds};
    }
}

bool HapticsPolicy::offerRumble(std::optional<HapticCommand>& best,
                                const HandHaptics& hand,
                                float level,
                                double now) const {
    const float scaled = std::min(level * strength_, 1.0f);
    if (!(scaled >= kMinRumble)) {
        return true;
    }
    const bool renew = !hand.playing || hand.playing->source != HapticSource::Game ||
                       hand.playing->until - now < kRumbleRenewSeconds ||
                       std::fabs(hand.playing->amplitude - scaled) > kRumbleStep;
    if (renew) {
        offer(best, HapticSource::Game, level, kRumbleHoldSeconds);
    }
    return false;
}

HapticCommand
HapticsPolicy::decide(HandHaptics& hand, std::optional<HapticCommand> best, bool rumbleEnded, double now) {
    const bool playing = hand.playing && hand.playing->until > now;
    if (best) {
        const bool stronger =
            playing && hand.playing->source != best->source && hand.playing->amplitude > best->amplitude;
        if (!stronger) {
            hand.playing = Playing{best->source, best->amplitude, now + best->seconds};
            return *best;
        }
    }
    if (rumbleEnded && playing && hand.playing->source == HapticSource::Game) {
        hand.playing.reset();
        return HapticCommand{HapticCommandKind::Stop, HapticSource::Game, 0.0f, 0.0f};
    }
    return {};
}

std::array<HapticCommand, 2> HapticsPolicy::update(const HapticsFrame& frame) {
    const double now = frame.seconds;
    std::array<std::optional<HapticCommand>, 2> best;
    auto& weapon = best[index(frame.weaponHand)];

    if (frame.fireHeld && !fireWasHeld_) {
        offer(weapon, HapticSource::Fire, kFireStart.amplitude, kFireStart.seconds);
        nextFire_ = now + kFireRepeatSeconds;
    } else if (frame.fireHeld && now >= nextFire_) {
        offer(weapon, HapticSource::Fire, kFireRepeat.amplitude, kFireRepeat.seconds);
        // Steady from the first pulse: a late frame does not push every later pulse back.
        nextFire_ = std::max(nextFire_ + kFireRepeatSeconds, now);
    }
    fireWasHeld_ = frame.fireHeld;

    const float low = motor(frame.rumble.low);
    const float high = motor(frame.rumble.high);
    std::array<bool, 2> rumbleEnded{};
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        auto& b = best[index(hand)];
        HandHaptics& state = hands_[index(hand)];
        const float level = hand == frame.weaponHand ? std::max(low, high) : low;
        rumbleEnded[index(hand)] = offerRumble(b, state, level, now);
        if (frame.punch[index(hand)]) {
            offer(b, HapticSource::Punch, kPunch.amplitude, kPunch.seconds);
        }
        if (frame.capture) {
            offer(b, HapticSource::Capture, kCapture.amplitude, kCapture.seconds);
        }
        const MenuTick tick = frame.menu[index(hand)];
        if (tick != MenuTick::None && strength_ > 0.0f && now - state.lastMenu >= kMenuGapSeconds) {
            const PulseShape& shape = tick == MenuTick::Click ? kMenuClick : kMenuEnter;
            state.lastMenu = now;
            offer(b, HapticSource::Menu, shape.amplitude, shape.seconds);
        }
    }

    std::array<HapticCommand, 2> out;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        out[index(hand)] = decide(hands_[index(hand)], best[index(hand)], rumbleEnded[index(hand)], now);
    }
    return out;
}

} // namespace evr::input
