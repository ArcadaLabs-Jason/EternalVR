#include "features/input/rumble_route.hpp"

#include <algorithm>
#include <cstddef>

namespace evr::input {

namespace {

constexpr std::size_t index(Hand hand) {
    return static_cast<std::size_t>(hand);
}

} // namespace

const char* rumbleCauseName(RumbleCause cause) {
    switch (cause) {
    case RumbleCause::Body:
        return "the body";
    case RumbleCause::Weapon:
        return "the weapon";
    case RumbleCause::Melee:
        return "a melee";
    case RumbleCause::Shoulder:
        return "the shoulder launcher";
    }
    return "the body";
}

void addRumbleCauses(const GameInput& input, Hand weaponHand, std::array<bool, 2>& melee, bool& shoulder) {
    if (game::contains(input.pressed, game::GameAction::Melee)) {
        const Hand hand = input.punch[index(Hand::Left)]    ? Hand::Left
                          : input.punch[index(Hand::Right)] ? Hand::Right
                                                            : weaponHand;
        melee[index(hand)] = true;
    }
    const bool launcher = game::contains(input.pressed, game::GameAction::Equipment) ||
                          game::contains(input.pressed, game::GameAction::FlameBelch);
    shoulder = shoulder || launcher;
}

void RumbleRouter::reset() {
    *this = RumbleRouter{};
}

RumbleLevels
RumbleRouter::route(double seconds, Hand weaponHand, const RumbleEvents& events, float low, float high) {
    if (events.fireHeld) {
        if (!fireDown_) {
            firePressedAt_ = seconds;
        }
        fireReleasedAt_ = seconds;
    }
    fireDown_ = events.fireHeld;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        if (events.melee[index(hand)]) {
            meleeAt_[index(hand)] = seconds;
        }
    }
    if (events.shoulder) {
        shoulderAt_ = seconds;
    }

    const float level = std::max(low, high);
    RumbleLevels out;
    const bool started = previous_ < kQuietLevel && level >= kQuietLevel;
    const bool stacked = previous_ >= kQuietLevel && level - previous_ >= kStackStep;
    if (started || stacked) {
        // The most recent event within its window is the cause.
        double latest = -1.0e18;
        RumbleCause cause = RumbleCause::Body;
        if ((events.fireHeld || seconds - fireReleasedAt_ <= kCauseWindowSeconds) &&
            firePressedAt_ > latest) {
            latest = firePressedAt_;
            cause = RumbleCause::Weapon;
        }
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            const double at = meleeAt_[index(hand)];
            if (seconds - at <= kMeleeWindowSeconds && at > latest) {
                latest = at;
                cause = RumbleCause::Melee;
                meleeHand_ = hand;
            }
        }
        if (seconds - shoulderAt_ <= kCauseWindowSeconds && shoulderAt_ > latest) {
            cause = RumbleCause::Shoulder;
        }
        cause_ = cause;
        out.changed = true;
    }
    previous_ = level;
    out.cause = cause_;

    const Hand offHand = otherHand(weaponHand);
    const float loud = level;
    const float support = low * kSupportShare;
    switch (cause_) {
    case RumbleCause::Body:
        out.level[index(weaponHand)] = loud;
        out.level[index(offHand)] = low;
        break;
    case RumbleCause::Weapon:
        out.level[index(weaponHand)] = loud;
        out.level[index(offHand)] = support;
        break;
    case RumbleCause::Melee:
        out.level[index(meleeHand_)] = loud;
        out.level[index(otherHand(meleeHand_))] = support;
        break;
    case RumbleCause::Shoulder:
        out.level[index(offHand)] = loud;
        out.level[index(weaponHand)] = support;
        break;
    }
    return out;
}

} // namespace evr::input
