#pragma once

// Parallel Eye Rendering's switch and what it selects (docs/VR_STEREO.md "Parallel Eye Rendering"), read from
// the environment without the game, so it is unit-tested (tests/vkcore/parallel_eyes_settings_tests.cpp).
//
// ETERNALVR_PARALLEL_EYES=1 asks for it. It applies only in stereo (ETERNALVR_MODE=stereo), without a stereo
// experiment (ETERNALVR_STEREO_EXPERIMENT), without DLSS (ETERNALVR_STEREO_DLSS: Route S runs it per eye),
// with the UI layer on (its image hooks give each eye its view's picture) and on the game version the view
// slots know (checked by the layer). The anti-aliasing is TAA, or none with ETERNALVR_STEREO_TAA=0 (the
// launcher's Off), held by the layer (antiAliasingCvars) unless ETERNALVR_STEREO_RUNTIME_CVARS=0. With it on,
// the layer selects everything it needs: view 1's own copies of the shared targets (the clones), each eye
// from its own view's screen pass (the eye copy), async compute off and its cvar set; the ETERNALVR_TEST_*
// knobs below change that only for experiments. Unset (or any other value), nothing of it runs.

#include "stereo_seq/seq_settings.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore::parallel_eyes {

// Looks up one environment variable: its text, or nullopt when unset.
using EnvLookup = std::function<std::optional<std::wstring>(std::wstring_view name)>;

// Where each eye's picture comes from. Screen (the default): each view's own screen pass (tone-mapped), eye 0
// the presented image and eye 1 view 1's. Final (ETERNALVR_TEST_EYE_COPY=1): each view's image before the
// screen pass, gamma-darker; for experiments. Off (ETERNALVR_TEST_EYE_COPY=0): both eyes the presented image.
enum class EyeCopy : std::uint8_t { Off, Screen, Final };

// Parts that ETERNALVR_TEST_VIEW_OFF=<part>,<part>... leaves out, one fix at a time, to tell which one a
// picture or stability change comes from (experiments only).
enum ViewPart : std::uint32_t {
    kEdges = 1u << 0,   // edges: view 1's light binning waits for view 0's (view_binning.cpp)
    kDcCopy = 1u << 1,  // dc: view 1's own device context copy (depth pyramid, history targets)
    kBinds = 1u << 2,   // binds: view 1's passes bind the clones
    kPool = 1u << 3,    // pool: view 1's own light and decal tile list pool
    kShadows = 1u << 4, // shadows: view 1 skips its shadow atlas work and shades with view 0's
    kEnv = 1u << 5,     // env: view 1's environment (sky lighting, fog) filled
    kVolumes = 1u << 6, // volumes: view 1's light scattering waits for its volume count
    kScreen = 1u << 7,  // screen: view 1's screen pass writes its own image
};

// ETERNALVR_TEST_INSTALL_FAIL (rig only): the install stops where a real failure would. `check`: after every
// check, before anything is changed (the standard renderer runs). `redirects`: in the redirects' hooks, after
// the block move and the raised counts, before the code bytes, as a failed redirect hook would; `hook`: after
// the code bytes are changed, as a failed clone hook would (both view 0 alone for the session).
enum class TestFail : std::uint8_t { None, Check, Redirects, Hook };

struct Settings {
    bool requested = false; // ETERNALVR_PARALLEL_EYES=1 and nothing below rules it out
    // Why not, when ETERNALVR_PARALLEL_EYES=1 is set but `requested` is false; empty otherwise.
    std::string why;
    bool clones = true;                 // ETERNALVR_TEST_VIEW_CLONES=0: off
    EyeCopy eyeCopy = EyeCopy::Screen;  // ETERNALVR_TEST_EYE_COPY=0 or 1
    std::uint32_t off = 0;              // ViewPart bits from ETERNALVR_TEST_VIEW_OFF
    int viewOnly = -1;                  // ETERNALVR_TEST_VIEW_ONLY=0 or 1: that view alone; -1 both
    TestFail testFail = TestFail::None; // ETERNALVR_TEST_INSTALL_FAIL=check, redirects or hook
    bool antiAliasingOff = false;       // ETERNALVR_STEREO_TAA=0 (as Route S reads it): no anti-aliasing
    // ETERNALVR_STEREO_RUNTIME_CVARS=0 (as Route S reads it): the anti-aliasing is left as the game has it.
    bool antiAliasingHeld = true;
    bool alternateEyes = false; // ETERNALVR_ALTERNATE_EYES asks for alternate eyes (on or auto): a Route S
                                // mode, ignored here (one line), and its auto does not turn frame pacing off
    std::vector<std::string> warnings; // a value that does not parse, ignored
};

// Reads the settings. `uiLayer` says whether the UI layer is on (ETERNALVR_UI_LAYER and its default,
// ui_settings.hpp); asked only with ETERNALVR_PARALLEL_EYES=1.
Settings readSettings(const EnvLookup& env, const std::function<bool()>& uiLayer);

// The anti-aliasing cvars the layer holds with it (runtime_cvars.cpp): with `off`, the stereo path's set for
// no temporal effects (stereo_seq::stereoRuntimeCvars: r_TAASafeMode 1, r_antialiasing 0); else TAA
// (r_antialiasing 1), so the player's own mode (DLSS from the game's menu, say) is not used.
std::vector<stereo_seq::CvarExpectation> antiAliasingCvars(bool off);

// The parts `off` names, comma-separated, for the log ("none" for none).
std::string partsText(std::uint32_t off);

} // namespace evr::vkcore::parallel_eyes
