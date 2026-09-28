#pragma once

// Settings of synchronized sequential stereo that are plain text (docs/VR_STEREO.md): the per-eye capture
// setting and the check of the v1 cvar set on the game's command line.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::stereo_seq {

// ETERNALVR_CAPTURE_EYES=<dir>[,<every N pairs>]: every Nth complete eye pair is written as two PNG files
// into <dir>. N defaults to 60. nullopt for an empty folder or an N that is not a positive number.
struct CaptureSetting {
    std::wstring directory;
    std::uint32_t everyPairs = 60;
};
std::optional<CaptureSetting> parseCaptureSetting(std::wstring_view text);

// A cvar value Route S v1 expects (docs/rig-findings/stereo-routes.md section 2.6).
struct CvarExpectation {
    std::string_view name;
    std::string_view value;
};

// r_TAASafeMode 1, r_antialiasing 0, r_jitter 0, rs_enable 0, r_swapInterval 0.
const std::vector<CvarExpectation>& sequentialCvars();

// How the eyes' temporal effects (TAA and the other history passes) are handled under Route S.
enum class StereoTemporal : std::uint8_t {
    Off,    // v1: off, since each eye would read the other eye's history
    PerEye, // each eye has its own history (the per-eye TAA module owns the TAA cvars then)
};

// The cvars the layer holds at run time under Route S (the game applies the player's settings after the
// command line, so the command line alone does not hold them). The single policy for them: Off gives the
// part of the v1 set stereo correctness needs, r_TAASafeMode 1 and r_antialiasing 0 (r_jitter follows TAA,
// the engine sets it; the present interval is the layer's, stereo_present); PerEye gives none, since the
// per-eye TAA module writes its own set.
const std::vector<CvarExpectation>& stereoRuntimeCvars(StereoTemporal temporal = StereoTemporal::Off);

// The comfort and correctness cvars the launcher sets on the command line, held at run time under Route S
// as well, so a player who turns one back on in the game's own settings during a session gets it off again
// at once: HDR output (the copy to the headset expects SDR), motion blur, depth of field, chromatic
// aberration, vignette, view bob and the view kicks and shakes (camera motion the head did not make), the
// weapon's FOV scale and the Meathook's single view turn (hand aim). Kept in step with the launcher's
// forced-cvars.txt, which restores the player's own values after the session.
const std::vector<CvarExpectation>& stereoComfortCvars();

// A cvar held at a value known only at run time (stereoWindowCvars).
struct CvarHold {
    std::string name;
    std::string value;
};

// The window and present cvars the layer holds under Route S, whatever the temporal mode: r_fullscreen 0,
// r_swapInterval 0 and r_windowWidth / r_windowHeight at the size the game was started with: the command
// line's when it sets both to a positive number (the launcher: the eye size, or the render size with the
// render size on; the rig: ETERNALVR_WINDOW's size), else ETERNALVR_WINDOW's width and height
// (`windowSetting`, "x,y,width,height"). The game applies the player's video mode on some load paths: in
// headset session 2 a new campaign took the window to 3840x2160 and each eye rendered 4x the pixels
// (docs/VR_STEREO.md, Cvars).
std::vector<CvarHold> stereoWindowCvars(std::string_view commandLine, std::string_view windowSetting = {});

struct SwapchainSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Watches the game's swapchain size against the size it should have: `expected` when the layer sets it
// (the render size answers the game's client area), else the launch size, the first swapchain's
// (ETERNALVR_WINDOW). Another size means the game changed its video mode. Returns the pixel ratio
// new / reference for such a swapchain, nullopt for the first one and for the reference size.
class LaunchSizeWatch {
public:
    std::optional<double> onSwapchain(SwapchainSize size,
                                      std::optional<SwapchainSize> expected = std::nullopt);
    // The size the last swapchain was compared with (the launch size until an expected one is given).
    SwapchainSize reference() const { return reference_; }

private:
    SwapchainSize launch_;
    SwapchainSize reference_;
};

struct CvarOnCommandLine {
    std::string name;
    std::string expected;
    std::optional<std::string> actual; // the last value the command line sets, if any
};

// What the command line sets for each expected cvar: `+name value`, `+set name value` and
// `+seta name value` count; a later setting wins; names compare case-insensitively.
std::vector<CvarOnCommandLine> cvarsOnCommandLine(std::string_view commandLine,
                                                  const std::vector<CvarExpectation>& expected);

} // namespace evr::stereo_seq
