#pragma once

// The wrist HUD (docs/VR_HANDS_HUD.md): the HUD's corner blocks on small quads on the inside of the off
// hand's wrist, shown while it faces the head, faded in and out.
//
// Frames: the hand pose is the OpenXR grip pose of the off hand (-Z where the straightened index finger
// points, +X out of the palm of the left hand and into the palm of the right one, +Y out of the thumb
// side). The panel lies along the inside of the forearm, behind the grip (+Z toward the elbow): its normal
// is the palm's (+X on the left hand, -X on the right), the image's right runs to the player's right when
// the forearm lies across the chest and the image's up is the thumb side (+Y). Turning the palm up with the
// forearm across the chest (the look at a watch worn on the inside of the wrist) shows it to the eyes,
// upright. All poses are in one space (the layer's room space, or the grip space itself): hand, head and
// the result.

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "ui_layer/hud_regions.hpp"
#include "ui_layer/ui_settings.hpp"

#include <cstdint>
#include <vector>

namespace evr::ui_layer {

// The rotation whose matrix has the columns `right`, `up`, `normal` (orthonormal, right-handed).
Quat rotationFromBasis(Vec3 right, Vec3 up, Vec3 normal);

// The panel's orientation in the grip frame: image right, image up, normal (right x up = normal).
// `leftHand` is the hand the wrist HUD is on (the off hand: left unless the handedness is left).
Quat wristPanelRotation(bool leftHand);

// The panel centre's offset in the hand frame for the configured offset (x mirrored for the right hand).
Vec3 wristPanelOffset(const WristSettings& s, bool leftHand);

// Facing: the angle in degrees between the panel's normal and the direction from the panel to the head.
// 0 = the panel looks straight at the eyes; 180 = it faces away.
float wristFacingDegrees(const Pose& hand, const Pose& head, const WristSettings& s, bool leftHand);

// Gaze: the angle in degrees between the head's forward (-Z) and the direction from the head to the
// panel. 0 = looking straight at it. A hand held out to the side can face the head while the player looks
// elsewhere; the gaze keeps the HUD off then.
float wristGazeDegrees(const Pose& hand, const Pose& head, const WristSettings& s, bool leftHand);

// Show / hide with hysteresis: shown once both angles drop to their show limits (`showDegrees`,
// `gazeShowDegrees`), hidden again only when one goes above its hide limit.
class WristFacing {
public:
    bool update(float facingDegrees, float gazeDegrees, const WristSettings& s);
    void reset() { shown_ = false; }
    [[nodiscard]] bool shown() const { return shown_; }

private:
    bool shown_ = false;
};

// The opacity ramp: 0..1, rising over `fadeInSeconds` while the HUD should show and falling over
// `fadeOutSeconds` otherwise. A zero duration switches at once.
class WristFade {
public:
    float update(bool target, float dtSeconds, const WristSettings& s);
    void reset() { alpha_ = 0.0f; }
    [[nodiscard]] float alpha() const { return alpha_; }

private:
    float alpha_ = 0.0f;
};

// One quad of the wrist HUD: which part of the GUI target it shows, where (in the same space as the hand)
// and how big.
struct WristQuad {
    WristBlock block = WristBlock::Vitals;
    PixelRect rect;
    Pose pose;
    float width = 0.0f;
    float height = 0.0f;
};

// The wrist HUD's quads for a `width` x `height` GUI target and the hand's grip pose: the vitals and weapon
// blocks side by side along the forearm (vitals toward the image's left, as on screen), their row
// `s.widthMetres` wide at one common scale, and the abilities block (when enabled) centred above them.
// Empty for an empty target.
std::vector<WristQuad> layoutWristQuads(
    const Pose& hand, std::uint32_t width, std::uint32_t height, const WristSettings& s, bool leftHand);

// The rectangles of the target the head-locked quad keeps in wrist or weapon mode: `shown` (the UI quad's
// image rectangle: the 16:9 band, or the whole target with ETERNALVR_UI_CROP=0) minus the cut rectangles
// (hud_regions.hpp) of the blocks in `moved` (both corners for the wrist), as disjoint pieces.
std::vector<PixelRect> headLockedPieces(const PixelRect& shown,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        const std::vector<WristBlock>& moved);

} // namespace evr::ui_layer
