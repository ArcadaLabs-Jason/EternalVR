#include "features/input/virtual_gamepad.hpp"

#include <algorithm>
#include <cmath>

namespace evr::input {

namespace {

using game::GameAction;

struct PadBind {
    GameAction action;
    std::uint16_t button;
};

constexpr PadBind kPadBinds[] = {
    {GameAction::Jump, pad_button::kA},
    {GameAction::Dash, pad_button::kB},
    {GameAction::Chainsaw, pad_button::kX},
    {GameAction::FlameBelch, pad_button::kY},
    {GameAction::Equipment, pad_button::kLeftShoulder},
    {GameAction::QuickSwitch, pad_button::kRightShoulder},
    {GameAction::WeaponWheel, pad_button::kRightShoulder},
    {GameAction::Melee, pad_button::kRightThumb},
    {GameAction::Pause, pad_button::kStart},
    {GameAction::Dossier, pad_button::kBack},
    {GameAction::Automap, pad_button::kBack},
    {GameAction::SwitchWeaponMod, pad_button::kDpadUp},
    {GameAction::MissionInfo, pad_button::kDpadDown},
    {GameAction::SwitchEquipment, pad_button::kDpadLeft},
    {GameAction::Crucible, pad_button::kDpadRight},
};

constexpr int kAxisMax = 32767;

std::int16_t axis(float value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    const long scaled = std::lround(std::clamp(value, -1.0f, 1.0f) * static_cast<float>(kAxisMax));
    return static_cast<std::int16_t>(scaled);
}

// A stick value limited to the unit circle, so a diagonal never reads as the corner of the square.
Axis2 unitDisc(Axis2 v) {
    if (!isFinite(v)) {
        return {};
    }
    const float length = magnitude(v);
    return length > 1.0f ? v * (1.0f / length) : v;
}

std::int16_t sumAxis(std::int16_t a, std::int16_t b) {
    const int sum = static_cast<int>(a) + static_cast<int>(b);
    return static_cast<std::int16_t>(std::clamp(sum, -kAxisMax - 1, kAxisMax));
}

} // namespace

PadState padStateFor(const game::GameActionSet& down, Axis2 move, Axis2 look) {
    PadState pad;
    for (const PadBind& bind : kPadBinds) {
        if (game::contains(down, bind.action)) {
            pad.buttons |= bind.button;
        }
    }
    pad.rightTrigger = game::contains(down, GameAction::Fire) ? 255 : 0;
    pad.leftTrigger = game::contains(down, GameAction::WeaponMod) ? 255 : 0;
    const Axis2 m = unitDisc(move);
    const Axis2 l = unitDisc(look);
    pad.thumbLX = axis(m.x);
    pad.thumbLY = axis(m.y);
    pad.thumbRX = axis(l.x);
    pad.thumbRY = axis(l.y);
    return pad;
}

PadState mergePads(const PadState& real, const PadState& ours) {
    PadState out;
    out.buttons = static_cast<std::uint16_t>(real.buttons | ours.buttons);
    out.leftTrigger = std::max(real.leftTrigger, ours.leftTrigger);
    out.rightTrigger = std::max(real.rightTrigger, ours.rightTrigger);
    out.thumbLX = sumAxis(real.thumbLX, ours.thumbLX);
    out.thumbLY = sumAxis(real.thumbLY, ours.thumbLY);
    out.thumbRX = sumAxis(real.thumbRX, ours.thumbRX);
    out.thumbRY = sumAxis(real.thumbRY, ours.thumbRY);
    return out;
}

} // namespace evr::input
