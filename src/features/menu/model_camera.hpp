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

} // namespace evr::menu
