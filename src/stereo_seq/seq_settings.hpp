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

// ETERNALVR_CAPTURE_BURST=<n>: an in-headset capture saves n consecutive frames instead of one, Route S pairs
// or Parallel Eye Rendering frames (both eyes each) or mono frames. 1 to kMaxCaptureBurst: each frame holds a
// host copy of each of its images (about 18 MB per image at 2056x2216, so 36 MB per pair) until the burst is
// written, and the background writer compresses them one after another (about 0.35 s per image). nullopt for
// anything else.
inline constexpr std::uint32_t kMaxCaptureBurst = 16;
std::optional<std::uint32_t> parseCaptureBurst(std::wstring_view text);

// A cvar value Route S v1 expects (docs/rig-findings/stereo-routes.md section 2.6).
struct CvarExpectation {
    std::string_view name;
    std::string_view value;
    bool fraction = false; // a float cvar: a hold compares it as a float (0.5 is not taken for 0)
    bool yields = false;   // a change in the game's menu ends the hold (mp_policy::YieldingHold)
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

// The Off set the layer holds from the start of a session until the first stereo tick decides per-eye TAA.
// With DLSS per eye asked for (`dlssPerEye`) it holds DLSS (r_antialiasing 2) and the asked r_dlssQuality
// (`dlssQuality`, 0 to 3; -1 leaves the game's own) instead of r_antialiasing 0: the game then sizes its
// render for DLSS in the menus, and the first stereo tick switches nothing. Switched there, the game made its
// first DLSS feature at the output size (its render targets not yet at DLSS's input size) and a second one a
// frame later, about 160 ms of the first stereo ticks on the test PC (docs/rig-findings/dlss-dll.md section
// 10). Without DLSS: the Off set itself.
std::vector<CvarExpectation> stereoCvarsBeforePlay(bool dlssPerEye, int dlssQuality);

// r_lightScatteringTAA under Route S, the light scattering's temporal filter: on (1) while the scattering
// history is per eye (vkcore/scatter_hooks.hpp), off (0) otherwise, since each eye would filter its fog with
// the other eye's volume. The per-eye history needs only the eye tags, so this holds in either temporal
// mode: with per-eye TAA off or failed closed the layer holds it beside the Off set, and per-eye TAA writes
// the same value in its own set.
CvarExpectation stereoScatterFilterCvar(bool perEyeHistory);

// r_SSDOTemporalAA under Route S, SSDO's temporal filter, by the same rule: on (1) while SSDO's history is
// per eye (vkcore/ssdo_hooks.hpp), off (0) otherwise, since each eye would filter its occlusion with the
// other eye's. Held beside the Off set with per-eye TAA off or failed closed; per-eye TAA writes it in its
// own set.
CvarExpectation stereoSsdoFilterCvar(bool perEyeHistory);

// The comfort and correctness cvars held at run time under Route S, so a player who turns one back on in the
// game's own settings during a session gets it off again at once: HDR output (the copy to the headset
// expects SDR), motion blur, depth of field, chromatic aberration, vignette, view bob and the view kicks and
// shakes (camera motion the head did not make), the damage tint and blur, the view effects' overlays, the
// underwater screen warp, the dash's radial blur, the weapon's FOV scale, the Meathook's single view turn
// (hand aim) and the game's Photo mode (g_allowPhotoMode 0). In stereo the launcher puts only r_hdrDisplay on
// the command line (the swapchain's format is picked at start-up): this hold sets the rest, so a multiplayer
// guard trip gives the player's values back (cvar_book.hpp). Head-tracked mono holds the same set but the
// underwater warp (monoVrHeldCvars); mono launches also keep it on the command line, where most of it never
// took (docs/rig-findings/profile-leak.md, "Mono"). Kept in step with the launcher's forced-cvars.txt, which
// restores the player's own values after the session, except r_waterPostProcess: in mono both eyes see one
// picture, so it is held in stereo only, and g_allowPhotoMode, which each map load sets (nothing saves it).
// session-keys.txt restores r_waterPostProcess and r_blurRadialScale (the game saves both; it writes
// r_blurRadialScale itself after r_motionblur 0).
const std::vector<CvarExpectation>& stereoComfortCvars();

// swf_anchorProfile 2, the game's HUD Scale at Large (Settings, HUD; 0 Small, 1 Medium, 2 Large), held at run
// time under Route S and Parallel Eye Rendering beside the comfort set. On the HUD quad at the default size
// the game's smaller HUD texts measure under half a degree at Medium (docs/test-protocols/ui-legibility.md);
// at Large each gains two or three pixels. The game keeps the setting in the player's profile (profile.bin),
// not in a text config, so the profile hooks put the player's own value into every profile save
// (mp_policy/profile_cvars.hpp); a player already at Large sees no change. A player who changes it in the
// game's menu during VR keeps that choice: the hold yields (mp_policy::YieldingHold). Mono launches leave it
// alone.
CvarExpectation stereoHudScaleCvar();

// What Route S and Parallel Eye Rendering hold for comfort and legibility (runtime_cvars.cpp,
// runtime_cvars_pe.cpp): the comfort set and the HUD Scale.
std::vector<CvarExpectation> stereoVrHeldCvars();

// What head-tracked mono holds at run time beside its window set (vkcore/runtime_cvars.hpp): the comfort set
// but r_waterPostProcess (one picture for both eyes, so the underwater warp stays as the game has it), at the
// same values. The launcher's command line has the same values (forced-cvars.txt, `| mono`), but on the rig
// (2026-11-21, Steam build 25216728) most never took: pm_noBob, the kicks and shakes, the damage tint and
// blur, the view effects' overlays, the weapon's FOV scale, the Meathook's view turn and the vignette read
// the game's own values from the first present to the end of the session, view_skipShakes the profile's
// (its load comes after the command line), r_chromaticAberration was not registered yet when the command line
// ran, and the game writes r_blurRadialScale 1 itself after r_motionblur 0; photo mode needs the hold too
// (each map load sets g_allowPhotoMode). The profile hooks keep the player's own view_skipShakes in
// profile.bin (vkcore/profile_hooks.hpp, installed in head-tracked mono too). The cinema screen holds none of
// it.
std::vector<CvarExpectation> monoVrHeldCvars();

// r_SSDO under Route S (ETERNALVR_STEREO_SSDO). The game turns SSDO off itself after r_TAASafeMode 1
// (0x1C6FCC0), which Route S holds at start-up, so without a hold every Route S session ran without it.
// SSDO's own temporal filter runs only while its history is per eye (stereoSsdoFilterCvar), so it reads no
// other eye's history. Unset or "1": held at 1, the game's default; "0": held at 0 (the launcher passes the
// player's own 0 from their config). Both follow the game's Directional Occlusion setting once it runs
// (directionalOcclusionSsdoCvar, vkcore/ssdo_menu_hook.hpp). "off": held at 0 whatever the game's setting
// (rig use; the launcher passes only 0 or 1). Nothing for any other value.
std::optional<CvarExpectation> stereoSsdoCvar(std::string_view setting);

// Whether the r_SSDO hold follows the game's Directional Occlusion setting: every setting but "off".
bool stereoSsdoFollowsGame(std::string_view setting);

// The r_SSDO the game's Directional Occlusion setting writes for a profile level (0x1420F20): 0 at level 0,
// 1 at levels 1 to 6 (Low to Ultra Nightmare; r_SSDOQuality 0, 1, 2 from Low). Nothing for any other level.
std::optional<CvarExpectation> directionalOcclusionSsdoCvar(int level);

// r_SSR while per-eye TAA (or per-eye DLSS) runs (ETERNALVR_STEREO_SSR; taa_hooks.cpp holds it). The same
// knock-on as SSDO: the game writes r_SSR 0 on every render while r_TAASafeMode is not 0 (0x1C6FCC0,
// 0x1C71630). SSR keeps no history of its own: it reads the last frame's colour through the TAA history
// selector (0x1CBB6C0), which per-eye TAA gives each eye's own, so it is held only then. Unset or "1": held
// at 1, the game's default; "0": held at 0 (the launcher passes the player's own 0 from their config,
// Reflections at Low); both follow the game's Reflections setting once it runs (reflectionsSsrCvar). "off":
// held at 0 whatever the game's setting (the launcher's Screen-space reflections Off). Nothing for any other
// value.
std::optional<CvarExpectation> stereoSsrCvar(std::string_view setting);

// Whether the r_SSR hold follows the game's Reflections setting: every setting but "off".
bool stereoSsrFollowsGame(std::string_view setting);

// The r_SSR the game's Reflections setting writes (0x1421DC0), told by the
// r_raytracedReflectionsTemporalUpscaleQuality it writes with it: 3 at Low (r_SSR 0), 2 at Medium and 1 from
// High (r_SSR 1). The setting runs at every profile load and every apply of the video menu, never from the
// TAA safe mode knock-on, and per-eye TAA holds that cvar at 0 (stereoTaaForcedCvars): a value from 1 to 3
// read before the hold writes it means the player's Reflections setting ran since (SsrHold,
// setting_follow.hpp). That needs the cvar at 0 from the start: the command line's
// +r_raytracedReflectionsTemporalUpscaleQuality 0 (its default 1 would read as Medium or higher until the
// profile's load). Nothing for any other value.
std::optional<CvarExpectation> reflectionsSsrCvar(int upscaleQuality);

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

// The window set head-tracked mono holds with the render size on (vkcore/runtime_cvars.hpp):
// only r_fullscreen 0 and the size of stereoWindowCvars, named one by one: r_swapInterval stays the player's
// (one present a frame, the flat game's vsync), and a cvar later added to the stereo set is not held in mono
// unless it is added here too. r_fullscreen 0 and the size keep the one image at the render size when a load
// path applies the player's video mode.
std::vector<CvarHold> monoWindowCvars(std::string_view commandLine, std::string_view windowSetting = {});

// Whether a held cvar is the window's size (r_windowWidth / r_windowHeight, compared case-insensitively). The
// layer stops holding these once the render size is off (vkcore/virtual_client.hpp): the game then renders
// at its window's size, and a size that differs from the window's makes the game resize its window, which
// on an AMD driver puts the swapchain out of date (VK_ERROR_OUT_OF_DATE_KHR).
bool isWindowSizeCvar(std::string_view name);

// A "name=value;name=value" list of cvars to hold (ETERNALVR_DEBUG_CVARS, ETERNALVR_CPU_SAVER). Spaces around
// names and values are dropped; an item without a name before '=' is skipped; a later item for the same name
// (compared case-insensitively) replaces the earlier one.
std::vector<CvarHold> parseCvarList(std::string_view text);

// A held value "<=N" is a cap: the layer writes N only while the cvar's value is above it, and never raises
// it (the CPU Saver's cap on a value the game's menu sets per quality level, where a fixed value would
// raise the cost for a player on a lower level). The cap N; nullopt for any other value or a bad number.
std::optional<float> parseCvarCap(std::string_view value);

// Whether two names are the same cvar (compared case-insensitively, as the game does).
bool sameCvarName(std::string_view a, std::string_view b);

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
