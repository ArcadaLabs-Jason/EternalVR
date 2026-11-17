#pragma once

// Builders for two-handed hold frames used across the two-handed aim tests.

#include "features/input/two_hand.hpp"

namespace evr::test {

inline constexpr float kTwoHandDt = 1.0f / 90.0f;

// The combat shotgun's and the Rocket Launcher's rows (data/weapons/viewmodel_offsets.toml).
inline constexpr input::SupportRow kShotgunRow{0.296f, 0.006f, 0.057f};
inline constexpr input::SupportRow kRocketRow{0.163f, 0.003f, -0.004f};
// A neutral weapon hand: 0.2 m right, 0.3 m below and 0.4 m ahead of the head, pointing ahead (-Z).
inline constexpr Vec3 kWeaponGrip{0.2f, -0.3f, -0.4f};

inline input::TwoHandFrame twoHandFrame(Vec3 offGrip, float grip, input::SupportRow row = kShotgunRow) {
    input::TwoHandFrame f;
    f.block = input::TwoHandBlock::None;
    f.offGrip = grip;
    f.weaponTracked = true;
    f.offTracked = true;
    f.weaponGrip = kWeaponGrip;
    f.offGripPoint = offGrip;
    f.support = row;
    f.dt = kTwoHandDt;
    return f;
}

inline Vec3 supportAt(Quat aim = {}, input::SupportRow row = kShotgunRow) {
    return input::supportPoint(kWeaponGrip, aim, row);
}

// The off hand brought to `off` with its grip up, then squeezed there: the result of the press frame.
inline input::TwoHandResult grabAt(input::TwoHandHold& hold, Vec3 off, input::SupportRow row = kShotgunRow) {
    hold.update(twoHandFrame(off, 0.0f, row));
    return hold.update(twoHandFrame(off, 1.0f, row));
}

} // namespace evr::test
