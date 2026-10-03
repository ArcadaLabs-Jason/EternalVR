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
// What is owed is kept within this, so a stick that cannot be met (none can, past the keys' full speed) does
// not build up a debt that runs on after it moves.
constexpr float kMaxOwed = 1.5f;

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
}

MapKeys MapPanKeys::update(input::Axis2 pan) {
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
    const input::Axis2 want{owed_.x + pan.x, owed_.y + pan.y};
    const Choice* best = &kChoices[0];
    float bestError = 0.0f;
    for (const Choice& c : kChoices) {
        const float error = std::hypot(want.x - c.pan.x, want.y - c.pan.y);
        if (&c == &kChoices[0] || error < bestError - 1e-6f) {
            best = &c;
            bestError = error;
        }
    }
    owed_ = {std::clamp(want.x - best->pan.x, -kMaxOwed, kMaxOwed),
             std::clamp(want.y - best->pan.y, -kMaxOwed, kMaxOwed)};
    return best->keys;
}

} // namespace evr::menu
