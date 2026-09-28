#pragma once

// The backdrop test for the crosshair mask (docs/rig-findings/ui-layer.md section 5): under hand aim a
// centred square of the GUI target is left out of the copy. Over the game that square holds only the
// crosshair, but menus (the pause and settings backdrop) and cinematics (their fades) cover the whole
// target with an opaque layer, and the square would show as a see-through hole in it. A few pixels just
// outside the square are read back; while most of them are opaque, the target holds such a backdrop and the
// copy is not masked.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::ui_layer {

// A pixel of the GUI target (row 0 at the top).
struct PixelPoint {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

inline constexpr std::size_t kBackdropProbePoints = 8;

// The points read for the test on a `width` x `height` target whose centred square of side `maskFraction`
// x `height` is masked (as copyRegionsWithoutCentre cuts it): the corners and the edge midpoints of a
// square `margin` pixels outside it. Nullopt when there is no square or that ring does not fit the target.
std::optional<std::array<PixelPoint, kBackdropProbePoints>>
backdropProbePoints(std::uint32_t width, std::uint32_t height, float maskFraction, std::uint32_t margin);

// Whether one reading (kBackdropProbePoints RGBA8 pixels, premultiplied, 4 bytes each, alpha last) shows a
// backdrop: at least kBackdropMinPoints of them with alpha of kBackdropMinAlpha or more. Low on purpose: a
// fade is partly transparent for seconds on its way in and out, and a hole shows in any visible layer; a
// false positive only lets the game's own crosshair show.
inline constexpr std::size_t kBackdropMinPoints = 6;
inline constexpr std::uint8_t kBackdropMinAlpha = 32;
bool readingShowsBackdrop(const std::array<std::uint8_t, kBackdropProbePoints * 4>& rgba);

// The readings in order, with hysteresis: a backdrop is taken as up after kOnReadings readings in a row
// that show one, and as gone after kOffReadings in a row that do not. Starts with none.
class BackdropDetector {
public:
    static constexpr int kOnReadings = 2;
    static constexpr int kOffReadings = 8;

    // Takes one reading; true when the state changed with it.
    bool update(bool showsBackdrop);
    [[nodiscard]] bool backdrop() const { return backdrop_; }
    void reset();

private:
    bool backdrop_ = false;
    int run_ = 0; // readings in a row that disagree with the state
};

} // namespace evr::ui_layer
