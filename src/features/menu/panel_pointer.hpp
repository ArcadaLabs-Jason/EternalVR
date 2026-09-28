#pragma once

// The laser pointer's geometry (docs/VR_MENUS.md): a controller's ray against the menu panel, the hit
// point as the game's cursor position, and the two small quads that draw the pointer (the dot on the
// panel and the beam from the hand). Plain math, no OpenXR: every rule is tested on any machine.
//
// Conventions are OpenXR's: +Y up, -Z forward; a quad layer lies in its pose's XY plane, centred on the
// pose, visible from its +Z side; image row 0 is the quad's top (+Y) edge.

#include "common/pose.hpp"
#include "common/vector.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::menu {

// A flat panel: a quad layer's pose and size (metres).
struct Panel {
    Pose pose;
    float width = 0.0f;
    float height = 0.0f;
};

// Where a ray meets a panel.
struct PanelHit {
    float u = 0.0f;        // 0 at the left edge, 1 at the right
    float v = 0.0f;        // 0 at the top edge, 1 at the bottom (image rows)
    float distance = 0.0f; // along the ray, metres
    Vec3 point;            // in the panel's space (the space its pose is given in)
};

// The first point where the ray from `origin` along `direction` meets the front (+Z) side of `panel`, if
// that point lies on the panel (edges included). A ray from behind the panel, one parallel to it, one that
// points away, a zero direction or an empty panel give nothing.
std::optional<PanelHit> intersectPanel(const Panel& panel, Vec3 origin, Vec3 direction);

// The ray of an OpenXR aim pose: from its position along its -Z axis.
std::optional<PanelHit> intersectPanel(const Panel& panel, const Pose& aim);

// The controllers are located in room space (LOCAL under the recenter transform, room_anchor.hpp) while the
// panel is placed and drawn in LOCAL, where it stays put in the real room across every re-anchor. A hand's
// room-space pose back in LOCAL, given the room transform it was located with (roomFromLocal).
Pose localFromRoom(const Pose& roomFromLocal, const Pose& room);

// A pixel position in the game's cursor range.
struct CursorPixel {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend constexpr bool operator==(CursorPixel a, CursorPixel b) = default;
};

// The pixel under (u, v) on an image of `width` x `height` pixels (the game's GUI size), clamped to the
// image: the game's cursor range is 0..width-1 by 0..height-1 here.
CursorPixel cursorPixel(float u, float v, std::uint32_t width, std::uint32_t height);

// The pose of a dot `offset` metres in front of the panel at (u, v), turned like the panel.
Pose dotPose(const Panel& panel, float u, float v, float offset);

// A thin quad from `from` to `to` (the beam), turned about its length to face `eye`: the pose (its +Y runs
// from `from` to `to`) and the size (`thickness` wide, the distance long). nullopt when the two points meet.
struct BeamQuad {
    Pose pose;
    float width = 0.0f;
    float length = 0.0f;
};
std::optional<BeamQuad> beamQuad(Vec3 from, Vec3 to, Vec3 eye, float thickness);

// The beam's image: `width` x `height` RGBA8, premultiplied alpha, a light line with soft sides that fades
// toward its far end (row 0).
std::vector<std::uint8_t> beamImage(std::uint32_t width, std::uint32_t height);

// A rotation whose columns are the three given orthonormal axes (right-handed).
Quat quatFromAxes(Vec3 xAxis, Vec3 yAxis, Vec3 zAxis);

} // namespace evr::menu
