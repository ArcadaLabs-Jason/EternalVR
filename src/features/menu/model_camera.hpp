#pragma once

// The camera a menu's 3D model is placed from (docs/VR_MENUS.md, "The menu's 3D model").
//
// Some screens show a 3D model next to their GUI: the weapon in the weapon mod screen and in the Dossier's
// customize screen. The game places it in the world, every tick, a few centimetres in front of its camera
// along the ray through the GUI element it belongs to (r_znear deep, pushed back by the model's size), and
// draws it in the 3D view under the GUI. In VR the camera is the head, so the model stayed in front of the
// eyes while the GUI is on the world-locked menu panel. Here is a camera that looks at the panel instead:
// facing it, with the field of view the game uses on a flat screen across the GUI image as the panel shows
// it, so that a model placed from it lands on the panel where the flat menu puts it. The model, as the game
// placed it from that camera, is then magnified about the camera until it sits on the panel's plane: seen
// from the camera it does not change, and seen from anywhere else it is on the panel at the size it has
// on a flat screen relative to the menu.
//
// Plain math, no OpenXR or game memory. Conventions: the panel and the head in OpenXR axes (LOCAL, metres;
// a quad lies in its pose's XY plane, visible from +Z, image row 0 at its top); the game's world in id Tech
// axes and units (a view axis is forward, left, up).

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "features/menu/panel_pointer.hpp"

#include <cstdint>
#include <optional>

namespace evr::menu {

// The GUI image as the menu panel shows it: the panel (LOCAL) shows the rectangle `rect*` (pixels) of an
// image of `imageWidth` x `imageHeight` pixels, the size the game places its model by.
struct PanelImage {
    Panel panel;
    std::uint32_t imageWidth = 0;
    std::uint32_t imageHeight = 0;
    float rectX = 0.0f;
    float rectY = 0.0f;
    float rectWidth = 0.0f;
    float rectHeight = 0.0f;
};

// One rendered head in both spaces: its pose in LOCAL and the view the game drew it with (the camera hook's
// origin and axis), which together take LOCAL into the game's world.
struct WorldHead {
    Pose local;
    Vec3 origin;
    Vec3 forward{1.0f, 0.0f, 0.0f};
    Vec3 left{0.0f, 1.0f, 0.0f};
    Vec3 up{0.0f, 0.0f, 1.0f};
    float unitsPerMetre = 1.0f;
};

// A camera in the game's world.
struct ModelCamera {
    Vec3 origin;
    Vec3 forward{1.0f, 0.0f, 0.0f};
    Vec3 left{0.0f, 1.0f, 0.0f};
    Vec3 up{0.0f, 0.0f, 1.0f};
    float fovX = 0.0f; // full angles, degrees (renderView_t fov_x / fov_y)
    float fovY = 0.0f;
    float panelDistance = 0.0f; // from the camera to the panel's plane, game units
};

// A LOCAL point or direction in the game's world, by the head's two poses.
Vec3 worldPoint(const WorldHead& head, Vec3 local);
Vec3 worldDirection(const WorldHead& head, Vec3 local);

// The camera facing the panel's front from the point where the whole GUI image, centred on the panel's
// image centre, fills `fovXDegrees` across; its vertical field of view follows from the image's shape.
// nullopt for an empty panel, rectangle or image, a field of view outside 1..170 degrees, a world scale
// outside 0.01..100 or anything not finite.
std::optional<ModelCamera> panelCamera(const PanelImage& image, const WorldHead& head, float fovXDegrees);

// A model the game placed from `camera` at `position` with `scale`, magnified about the camera so that its
// origin is on the panel's plane.
struct ModelOnPanel {
    Vec3 position;
    Vec3 scale;
    float factor = 1.0f;
};
// Factors outside these are not a model in front of the camera (nothing is changed then).
inline constexpr float kMinModelFactor = 0.01f;
inline constexpr float kMaxModelFactor = 1000.0f;
std::optional<ModelOnPanel> modelOnPanel(const ModelCamera& camera, Vec3 position, Vec3 scale);

// A light of a menu model's light rig, as the engine's renderLight_t has it (game units): its type (0 point,
// 1 spot, 2 parallel, 3 area), radius and centre, intensity (colorScale), and the distances from the view it
// is culled at (maxVisibleRange), fades over (fadeVisibilityOver) and casts shadows to
// (maxShadowVisibleRange); 0 there is no limit.
struct RigLight {
    int type = 0;
    Vec3 radius;
    Vec3 center;
    float intensity = 1.0f;
    float visibleRange = 0.0f;
    float fadeOver = 0.0f;
    float shadowRange = 0.0f;
};
inline constexpr int kPointLight = 0;
inline constexpr int kSpotLight = 1;
// The engine gives a light of this type with a radius of all zeros its default radius (RVA 0xCAFBB9).
inline constexpr int kDefaultRadiusType = 4;
inline constexpr float kDefaultRadius = 6.0f;
// The light for its rig grown by `factor` (see rigFactor): every distance times `factor`, the intensity times
// factor squared (the light falls off with the square of the distance, and the rig's offsets grow by `factor`
// too) and `intensityTrim`. Point lights only (type 0, and the engine's type 4): nullopt for the others
// (their frustums are left as they are), anything not finite, a factor outside
// kMinModelFactor..kMaxModelFactor or a trim not above 0.
std::optional<RigLight> grownRigLight(const RigLight& light, float factor, float intensityTrim = 1.0f);

// How far the rig grows for a model magnified by `modelFactor`: as far as the model, but no light further
// from the model than kRigLightReach (measured on the rig 2026-10-07: rig lights 7.9 and 14.6 units from the
// magnified weapon lit nothing of it, one 4.6 units away did; the engine limit behind it is not known). Never
// below 1; 1 for a rig whose size is not known. `farthestOffset` is the rig's largest light offset from the
// model, game units.
inline constexpr float kRigLightReach = 5.0f;
float rigFactor(float modelFactor, float farthestOffset);
// A rig that grows less than its model (rigFactor capped it) has its lights closer to the model, for its
// size, than on the flat screen, so they light it brighter; they are dimmed by rigFactor / modelFactor, but
// no more than to kRigIntensityTrim (measured on the rig with the combat shotgun, rig x4.28 for model x12.46:
// mean brightness 98 against the flat screen's 84; 120 untrimmed, 60 with the rig not grown). 1 for a rig
// that grows with its model.
inline constexpr float kRigIntensityTrim = 0.5f;
float rigIntensityTrim(float modelFactor, float rigFactor);

} // namespace evr::menu
