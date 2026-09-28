#pragma once

// Settings of the UI layer (docs/rig-findings/ui-layer.md): the game's 2D target (HUD, menus, subtitles)
// captured every frame and shown on its own OpenXR quad instead of inside the eye images.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::ui_layer {

struct UiSettings {
    // ETERNALVR_UI_LAYER (default: on in Route S stereo).
    bool enabled = false;
    // ETERNALVR_UI_SKIP_COMPOSITE: no GUI composite in the eyes while the quad shows.
    bool skipComposite = true;
    // ETERNALVR_UI_DISTANCE: the quad's distance ahead of the head.
    float distanceMetres = 1.5f;
    // ETERNALVR_UI_WIDTH: the quad's width (its height follows the target's aspect).
    float widthMetres = 2.0f;
    // ETERNALVR_UI_OFFSET_Y: the quad's height offset (negative: lower).
    float offsetYMetres = 0.0f;
    // Under hand aim, a dot on the weapon hand's ray (the game's own crosshair marks the head's ray, so the
    // launcher hides it with +g_reticleMode 2): ETERNALVR_UI_RETICLE, its distance along the ray
    // (ETERNALVR_UI_RETICLE_DISTANCE) and angular diameter (ETERNALVR_UI_RETICLE_SIZE, degrees).
    bool reticle = true;
    float reticleDistanceMetres = 10.0f;
    float reticleDegrees = 1.0f;
    // Menus (docs/VR_MENUS.md): while the game shows its menu cursor, the menu is on a world-locked panel
    // placed in front of the head, and the controllers point at it with a laser (ETERNALVR_MENU_POINTER).
    // The panel's distance and width (ETERNALVR_MENU_DISTANCE, ETERNALVR_MENU_WIDTH) default to the UI
    // quad's, so the pause menu keeps its size when it turns world-locked; ETERNALVR_MENU_BEAM=0 leaves out
    // the beam and keeps only the dot.
    bool menuPointer = true;
    float menuDistanceMetres = 1.5f;
    float menuWidthMetres = 2.0f;
    bool menuBeam = true;
    // ETERNALVR_UI_CROP: the UI quad and the menu panel show only the 16:9 band of the GUI target the game's
    // screens are laid out in (wideContentRect), not the empty rows above and below it on a near-square eye
    // image (T-031).
    bool wideCrop = true;
    // ETERNALVR_UI_WASH: the full-screen additive wash (the red low-health vignette) is taken out of the
    // GUI image before it goes on the HUD quad (additive_wash.hpp); 0 shows the image as the game drew it.
    bool removeWash = true;
};

// Looks up one environment variable: its text, or nullopt when unset.
using EnvLookup = std::function<std::optional<std::wstring>(std::wstring_view name)>;

// Reads the settings. A value that does not parse or lies outside its range keeps the default and adds a
// line to `warnings`. On by default in Route S stereo (ETERNALVR_MODE=stereo, no
// ETERNALVR_STEREO_EXPERIMENT), off otherwise; ETERNALVR_UI_LAYER=1 or 0 overrides.
UiSettings readUiSettings(const EnvLookup& env, std::vector<std::string>& warnings);

// The quad's size in metres for a target of `width` x `height` pixels: `widthMetres` wide, the height from
// the target's aspect. nullopt for an empty target.
struct PanelSize {
    float width = 0.0f;
    float height = 0.0f;
};
std::optional<PanelSize> panelSize(float widthMetres, std::uint32_t width, std::uint32_t height);

// The reticle image: `size` x `size` RGBA8, premultiplied alpha: a white dot with a dark ring (visible on
// light and dark scenes), transparent around it, the ring's outer edge touching the image's edge.
std::vector<std::uint8_t> reticleImage(std::uint32_t size);

// A rectangle of the GUI target, in pixels.
struct PixelRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The rectangles to copy from a `width` x `height` GUI target so that a centred square of side
// `maskFraction` x `height` is left out (the game's crosshair, which marks the head's ray, under hand aim):
// the bands above and below it and the pieces left and right of it. One rectangle, the whole target, for a
// fraction of 0 or less.
std::vector<PixelRect>
copyRegionsWithoutCentre(std::uint32_t width, std::uint32_t height, float maskFraction);

// idSWF lays the game's screens out in a centred 16:9 frame fitted to the target's width: the menus, the
// HUD's corners and the subtitles sit inside it (docs/rig-findings/render-size.md, section 5; the UI captures
// at 2064x2100 and 2560x2100). The part of a `width` x `height` target that holds them: that band when the
// target is taller than 16:9 (its height rounded to whole pixels, centred), the whole target otherwise.
PixelRect wideContentRect(std::uint32_t width, std::uint32_t height);

// A point at (u, v) on a panel that shows `rect` of a `width` x `height` image, as (u, v) on the whole image
// (both 0..1, row 0 at the top).
struct ImageUv {
    float u = 0.0f;
    float v = 0.0f;
};
ImageUv contentToImage(float u, float v, const PixelRect& rect, std::uint32_t width, std::uint32_t height);

// The reticle quad's side in metres for an angular diameter of `degrees` at `distance` metres.
float reticleSideMetres(float distance, float degrees);

} // namespace evr::ui_layer
