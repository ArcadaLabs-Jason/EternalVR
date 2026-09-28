#pragma once

// The game's own window under Route S (docs/VR_STEREO.md, Desktop window): how many swapchain images the
// layer asks for, and what the window shows (the desktop mirror). Plain logic, tested on every platform.

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::stereo_seq {

// Route S presents twice per tick. On a display the compositor flips to (a real monitor), a presented image
// stays with the compositor until the next refresh, so with the game's two images only one present per
// refresh gets through and the tick rate is half the refresh (the owner's TV at 120 Hz: 60 ticks). With
// more images the game never waits for the display. Returns the image count to create the swapchain with:
// the game's own count raised to `wanted` (0 keeps the game's), within what the surface allows
// (`surfaceMax` 0: no limit).
std::uint32_t stereoSwapchainImages(std::uint32_t gameMin,
                                    std::uint32_t wanted,
                                    std::uint32_t surfaceMin,
                                    std::uint32_t surfaceMax);

// ETERNALVR_STEREO_SWAP_IMAGES: 0 (keep the game's count) to 8; nullopt for anything else.
std::optional<std::uint32_t> parseSwapImages(std::wstring_view text);

// ETERNALVR_MIRROR: what the game's window shows during stereo pairs. Left (the default): eye L in both
// presents of a tick, so the window shows one eye steadily instead of the eyes in turn; Right: eye R (one
// tick late); Off: black.
enum class Mirror : std::uint8_t { Left, Right, Off };
std::optional<Mirror> parseMirror(std::wstring_view text);
const char* toString(Mirror mirror);

// What a present of one eye does to the image the window receives.
enum class MirrorStep : std::uint8_t {
    None,  // the image goes out as rendered (and is not kept)
    Store, // keep a copy for the other eye's present; the image goes out as rendered
    Load,  // the image is replaced by the kept copy
    Clear, // the image is cleared to black
};
// `eye` 0 is eye L, 1 is eye R.
MirrorStep mirrorStep(Mirror mirror, std::uint32_t eye);

// Which of the game's presents reach its window when the layer may hand an image back without presenting it
// (VK_KHR_swapchain_maintenance1). The window is only a mirror: the runtime paces the game, so no present
// should ever wait for the desktop display. Of a stereo pair only the mirrored eye is a candidate (eye L
// for Left and Off, eye R for Right), and a candidate reaches the window only when the last one that did
// is at least two refreshes of the window's display old: a display that takes one image per refresh never
// makes a present wait.
enum class PresentKind : std::uint8_t {
    Mono, // a frame shown in both eyes (menus, loading, a mono tick)
    EyeL, // the first present of a stereo pair
    EyeR, // the second
};
class WindowPresentGate {
public:
    // Decides one present at `seconds` (a monotonic clock) for a display refreshing at `refreshHz`
    // (0 or less: unknown, taken as 60 Hz). True: present it; false: hand the image back.
    bool present(double seconds, double refreshHz, PresentKind kind, Mirror mirror);

    struct Counters {
        std::uint64_t presented = 0;
        std::uint64_t otherEye = 0; // the eye the window does not show
        std::uint64_t tooSoon = 0;  // within two refreshes of the last present
    };
    [[nodiscard]] const Counters& counters() const { return counters_; }

private:
    double last_ = -1.0e9;
    Counters counters_;
};

// While a menu is up over a head-tracked frame (the pause menu, the in-game screens, a popup) the headset
// shows the GUI on the menu panel, and the engine's composite leaves it out of the eye images (the UI layer,
// docs/VR_MENUS.md): the eye image the window would get is the world alone, black behind the pause menu. The
// window shows the panel's image instead: a present that carries the GUI (mono or eye L) keeps it in the
// mirror's private image, and a present that reaches the window loads that image (cut to its band).
struct PanelMirror {
    bool keep = false; // keep this present's GUI image (only a mono or eye L present has one)
    MirrorStep step = MirrorStep::None; // Load: the window's image is the kept panel image
};
// `gated`: only the presents the window shows reach it (WindowPresentGate); ungated, every present does.
// Nothing is kept or loaded on a present the window does not show, except eye L's GUI when eye R is the
// present the window gets (gated with Right). `off` stays black.
PanelMirror panelMirror(Mirror mirror, bool gated, PresentKind kind, bool toWindow);

} // namespace evr::stereo_seq
