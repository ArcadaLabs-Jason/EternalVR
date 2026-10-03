#include "features/menu/map_pan_keys.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace evr::menu {

namespace {

constexpr float kDiagonal = 0.70710678f;
constexpr float kPi = 3.14159265f;
constexpr float kQuarterPi = 0.25f * kPi;
constexpr float kEdge = 0.92387953f; // cos(22.5 degrees): the octagon's edge at its middle
// What is owed is kept within this (seconds at full speed), so a stick that cannot be met (none can, past
// the keys' full speed) does not build up a debt that runs on after it moves.
constexpr float kMaxOwed = 1.5f * static_cast<float>(kKeyHoldSeconds);
// A step longer than this counts as this (a stall in the frames is not paid back as a run of key time).
constexpr double kMaxStepSeconds = 0.1;

struct Choice {
    MapKeys keys;
    input::Axis2 pan; // what the game makes of the keys (normalised)
};

constexpr std::array<Choice, 9> kChoices{{
    {{}, {0.0f, 0.0f}},
    {{.up = true}, {0.0f, 1.0f}},
    {{.down = true}, {0.0f, -1.0f}},
    {{.left = true}, {-1.0f, 0.0f}},
    {{.right = true}, {1.0f, 0.0f}},
    {{.up = true, .left = true}, {-kDiagonal, kDiagonal}},
    {{.up = true, .right = true}, {kDiagonal, kDiagonal}},
    {{.left = true, .down = true}, {-kDiagonal, -kDiagonal}},
    {{.down = true, .right = true}, {kDiagonal, -kDiagonal}},
}};

} // namespace

void MapPanKeys::reset() {
    owed_ = {};
    heldPan_ = {};
    held_ = {};
    heldFor_ = 0.0;
    holding_ = false;
}

MapKeys MapPanKeys::update(input::Axis2 pan, double dt) {
    if (!input::isFinite(pan) || (pan.x == 0.0f && pan.y == 0.0f)) {
        reset();
        return {};
    }
    // The keys reach full speed only in their eight directions; between two of them the most they can make
    // is the octagon's edge (0.92 at worst, half way). A stick beyond it is taken back to it in its own
    // direction, so the direction holds and nothing is owed that the keys cannot pay.
    const float m = input::magnitude(pan);
    const float sector = std::fmod(std::atan2(pan.y, pan.x) + 2.0f * kPi, kQuarterPi);
    const float reach = kEdge / std::cos(sector - 0.5f * kQuarterPi);
    if (m > reach) {
        pan = pan * (reach / m);
    }
    // The time since the last frame was spent with the pair held then, against the stick now.
    const float step = std::isfinite(dt) ? static_cast<float>(std::clamp(dt, 0.0, kMaxStepSeconds)) : 0.0f;
    if (holding_) {
        owed_ = {std::clamp(owed_.x + (pan.x - heldPan_.x) * step, -kMaxOwed, kMaxOwed),
                 std::clamp(owed_.y + (pan.y - heldPan_.y) * step, -kMaxOwed, kMaxOwed)};
        heldFor_ += step;
        if (heldFor_ < kKeyHoldSeconds) {
            return held_;
        }
    }
    // The next pair: the one whose shortest hold leaves the least owed.
    const auto hold = static_cast<float>(kKeyHoldSeconds);
    const Choice* best = &kChoices[0];
    float bestError = 0.0f;
    for (const Choice& c : kChoices) {
        const float error =
            std::hypot(owed_.x + (pan.x - c.pan.x) * hold, owed_.y + (pan.y - c.pan.y) * hold);
        if (&c == &kChoices[0] || error < bestError - 1e-6f * hold) {
            best = &c;
            bestError = error;
        }
    }
    held_ = best->keys;
    heldPan_ = best->pan;
    heldFor_ = 0.0;
    holding_ = true;
    return held_;
}

} // namespace evr::menu
