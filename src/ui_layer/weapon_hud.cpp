#include "ui_layer/weapon_hud.hpp"

#include <algorithm>
#include <cmath>

namespace evr::ui_layer {

namespace {

constexpr float kDegreesPerRadian = 57.29577951f;

} // namespace

Pose weaponFrame(const Pose& aim, const Pose& grip, bool gripValid) {
    return {aim.orientation, gripValid ? grip.position : aim.position};
}

Quat weaponPanelRotation(float tiltDegrees) {
    const float t = tiltDegrees / kDegreesPerRadian;
    const Vec3 right{1.0f, 0.0f, 0.0f};
    const Vec3 normal{0.0f, std::sin(t), std::cos(t)};
    // right x up = normal: up = normal x right, leaning toward the muzzle as the face turns up.
    return rotationFromBasis(right, cross(normal, right), normal);
}

Vec3 weaponPanelOffset(const WeaponHudSettings& s, bool leftHand) {
    return {leftHand ? -s.offset.x : s.offset.x, s.offset.y, s.offset.z};
}

float weaponFacingDegrees(const Pose& weapon, const Pose& head, const WeaponHudSettings& s, bool leftHand) {
    const Vec3 centre = transformPoint(weapon, weaponPanelOffset(s, leftHand));
    const Vec3 normal =
        rotate(weapon.orientation, rotate(weaponPanelRotation(s.tiltDegrees), Vec3{0.0f, 0.0f, 1.0f}));
    const Vec3 toHead = normalize(head.position - centre);
    if (toHead == Vec3{}) {
        return 180.0f;
    }
    const float c = std::clamp(dot(normalize(normal), toHead), -1.0f, 1.0f);
    return std::acos(c) * kDegreesPerRadian;
}

WristSettings weaponShowSettings(const WeaponHudSettings& weapon, const WristSettings& wrist) {
    WristSettings s;
    s.always = false;
    s.showDegrees = weapon.showDegrees;
    s.hideDegrees = weapon.hideDegrees;
    s.gazeShowDegrees = 180.0f;
    s.gazeHideDegrees = 180.0f;
    s.fadeInSeconds = wrist.fadeInSeconds;
    s.fadeOutSeconds = wrist.fadeOutSeconds;
    return s;
}

std::vector<WristBlock> weaponMovedBlocks(const WeaponHudSettings& s) {
    if (s.vitals) {
        return {WristBlock::Vitals, WristBlock::Weapon};
    }
    return {WristBlock::Weapon};
}

std::vector<WristQuad> layoutWeaponQuads(const Pose& weapon,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         const WeaponHudSettings& s,
                                         bool leftHand) {
    const PixelRect ammo = toPixels(wristBlockRect(WristBlock::Weapon), width, height);
    if (ammo.width == 0 || ammo.height == 0) {
        return {};
    }
    PixelRect vitals;
    if (s.vitals) {
        vitals = toPixels(wristBlockRect(WristBlock::Vitals), width, height);
    }
    const float metresPerPixel = s.widthMetres / static_cast<float>(ammo.width);
    const Pose panel =
        compose(weapon, Pose{weaponPanelRotation(s.tiltDegrees), weaponPanelOffset(s, leftHand)});
    const auto quadAt = [&](WristBlock block, const PixelRect& rect, float x) {
        WristQuad q;
        q.block = block;
        q.rect = rect;
        q.width = static_cast<float>(rect.width) * metresPerPixel;
        q.height = static_cast<float>(rect.height) * metresPerPixel;
        q.pose = compose(panel, Pose{Quat::identity(), {x, 0.0f, 0.0f}});
        return q;
    };
    std::vector<WristQuad> quads;
    if (vitals.width == 0 || vitals.height == 0) {
        quads.push_back(quadAt(WristBlock::Weapon, ammo, 0.0f));
        return quads;
    }
    const float vitalsWidth = static_cast<float>(vitals.width) * metresPerPixel;
    const float rowWidth = vitalsWidth + s.widthMetres;
    quads.push_back(quadAt(WristBlock::Vitals, vitals, -rowWidth / 2.0f + vitalsWidth / 2.0f));
    quads.push_back(quadAt(WristBlock::Weapon, ammo, rowWidth / 2.0f - s.widthMetres / 2.0f));
    return quads;
}

} // namespace evr::ui_layer
