#include "ui_layer/wrist_hud.hpp"

#include <algorithm>
#include <cmath>

namespace evr::ui_layer {

namespace {

constexpr float kDegreesPerRadian = 57.29577951f;
// The gap between the vitals + weapon row and the abilities block above it, metres.
constexpr float kAbilitiesGap = 0.008f;

} // namespace

Quat rotationFromBasis(Vec3 r, Vec3 u, Vec3 n) {
    const float m00 = r.x, m01 = u.x, m02 = n.x;
    const float m10 = r.y, m11 = u.y, m12 = n.y;
    const float m20 = r.z, m21 = u.z, m22 = n.z;
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return normalize(q);
}

Quat wristPanelRotation(bool leftHand) {
    // Palm up, forearm across the body: the fingers (-Z) point to the other side and the thumb (+Y) away
    // from the body. The image's right runs toward the fingers on the left hand and toward the elbow on the
    // right one (both to the player's right), its up is the thumb side and its normal the palm's.
    if (leftHand) {
        return rotationFromBasis({0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    }
    return rotationFromBasis({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
}

Vec3 wristPanelOffset(const WristSettings& s, bool leftHand) {
    return {leftHand ? s.offset.x : -s.offset.x, s.offset.y, s.offset.z};
}

float wristFacingDegrees(const Pose& hand, const Pose& head, const WristSettings& s, bool leftHand) {
    const Vec3 centre = transformPoint(hand, wristPanelOffset(s, leftHand));
    const Vec3 normal =
        rotate(hand.orientation, rotate(wristPanelRotation(leftHand), Vec3{0.0f, 0.0f, 1.0f}));
    const Vec3 toHead = normalize(head.position - centre);
    if (toHead == Vec3{}) {
        return 180.0f;
    }
    const float c = std::clamp(dot(normalize(normal), toHead), -1.0f, 1.0f);
    return std::acos(c) * kDegreesPerRadian;
}

float wristGazeDegrees(const Pose& hand, const Pose& head, const WristSettings& s, bool leftHand) {
    const Vec3 centre = transformPoint(hand, wristPanelOffset(s, leftHand));
    const Vec3 toPanel = normalize(centre - head.position);
    if (toPanel == Vec3{}) {
        return 180.0f;
    }
    const Vec3 forward = normalize(rotate(head.orientation, Vec3{0.0f, 0.0f, -1.0f}));
    const float c = std::clamp(dot(forward, toPanel), -1.0f, 1.0f);
    return std::acos(c) * kDegreesPerRadian;
}

bool WristFacing::update(float facingDegrees, float gazeDegrees, const WristSettings& s) {
    if (s.always) {
        shown_ = true;
    } else if (!(facingDegrees <= s.hideDegrees) || !(gazeDegrees <= s.gazeHideDegrees)) {
        shown_ = false; // also for NaN
    } else if (facingDegrees <= s.showDegrees && gazeDegrees <= s.gazeShowDegrees) {
        shown_ = true;
    }
    return shown_;
}

float WristFade::update(bool target, float dtSeconds, const WristSettings& s) {
    const float dt = std::isfinite(dtSeconds) ? std::max(0.0f, dtSeconds) : 0.0f;
    const float duration = target ? s.fadeInSeconds : s.fadeOutSeconds;
    const float goal = target ? 1.0f : 0.0f;
    if (!(duration > 0.0f)) {
        alpha_ = goal;
    } else if (target) {
        alpha_ = std::min(1.0f, alpha_ + dt / duration);
    } else {
        alpha_ = std::max(0.0f, alpha_ - dt / duration);
    }
    return alpha_;
}

std::vector<WristQuad> layoutWristQuads(
    const Pose& hand, std::uint32_t width, std::uint32_t height, const WristSettings& s, bool leftHand) {
    const PixelRect vitals = toPixels(wristBlockRect(WristBlock::Vitals), width, height);
    const PixelRect weapon = toPixels(wristBlockRect(WristBlock::Weapon), width, height);
    const std::uint32_t rowPixels = vitals.width + weapon.width;
    if (rowPixels == 0 || vitals.height == 0 || weapon.height == 0) {
        return {};
    }
    const float metresPerPixel = s.widthMetres / static_cast<float>(rowPixels);
    const Pose panel = compose(hand, Pose{wristPanelRotation(leftHand), wristPanelOffset(s, leftHand)});
    const auto quadAt = [&](WristBlock block, const PixelRect& rect, float x, float y) {
        WristQuad q;
        q.block = block;
        q.rect = rect;
        q.width = static_cast<float>(rect.width) * metresPerPixel;
        q.height = static_cast<float>(rect.height) * metresPerPixel;
        q.pose = compose(panel, Pose{Quat::identity(), {x, y, 0.0f}});
        return q;
    };
    const float rowWidth = s.widthMetres;
    std::vector<WristQuad> quads;
    const float vitalsWidth = static_cast<float>(vitals.width) * metresPerPixel;
    const float weaponWidth = static_cast<float>(weapon.width) * metresPerPixel;
    quads.push_back(quadAt(WristBlock::Vitals, vitals, -rowWidth / 2.0f + vitalsWidth / 2.0f, 0.0f));
    quads.push_back(quadAt(WristBlock::Weapon, weapon, rowWidth / 2.0f - weaponWidth / 2.0f, 0.0f));
    if (s.abilities) {
        const PixelRect abilities = toPixels(wristBlockRect(WristBlock::Abilities), width, height);
        if (abilities.width != 0 && abilities.height != 0) {
            const float rowHeight =
                static_cast<float>(std::max(vitals.height, weapon.height)) * metresPerPixel;
            const float abilitiesHeight = static_cast<float>(abilities.height) * metresPerPixel;
            quads.push_back(quadAt(WristBlock::Abilities, abilities, 0.0f,
                                   rowHeight / 2.0f + kAbilitiesGap + abilitiesHeight / 2.0f));
        }
    }
    return quads;
}

std::vector<PixelRect> headLockedPieces(const PixelRect& shown, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    return subtractRects(
        shown, {cutRect(WristBlock::Vitals, width, height), cutRect(WristBlock::Weapon, width, height)});
}

} // namespace evr::ui_layer
