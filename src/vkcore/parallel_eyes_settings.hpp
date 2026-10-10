#pragma once

// Parallel Eye Rendering's switch and what it selects (docs/VR_STEREO.md "Parallel Eye Rendering"), read from
// the environment without the game, so it is unit-tested (tests/vkcore/parallel_eyes_settings_tests.cpp).
//
// ETERNALVR_PARALLEL_EYES=1 asks for it. It applies only in stereo (ETERNALVR_MODE=stereo), without a stereo
// experiment (ETERNALVR_STEREO_EXPERIMENT), with the UI layer on (its image hooks give each eye its view's
// picture) and on the game version the view slots know (checked by the layer). The anti-aliasing is TAA, DLSS
// with ETERNALVR_STEREO_DLSS=1 (each view's own feature, view_dlss.hpp; ETERNALVR_PE_DLSS=0 keeps the
// standard renderer for DLSS, which runs it per eye, as does ETERNALVR_STEREO_RUNTIME_CVARS=0 with DLSS), or
// none with ETERNALVR_STEREO_TAA=0 (the launcher's Off), held by the layer (antiAliasingCvars) unless
// ETERNALVR_STEREO_RUNTIME_CVARS=0. With it on, the layer selects everything it needs: view 1's own copies of
// the shared targets (the clones), each eye from its own view's screen pass (the eye copy), async compute off
// and its cvar set; the ETERNALVR_TEST_* knobs below change that only for experiments. Unset (or any other
// value), nothing of it runs.

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

// View 1's clones left out (ETERNALVR_TEST_VIEW_CLONE_SKIP=<item>,<item>...; view_clones.cpp): view 1 then
// uses the engine's object. Unset or empty: slot0. Set: exactly the items it lists, "none" for none.
enum CloneSkipGroup : std::uint32_t {
    kSkipSlot0 = 1u << 0,  // slot0: slot 0's TAA targets (device context +0x60..+0x78); view 1 has slot 1's
    kSkipDof = 1u << 1,    // dof: the depth of field targets, only while the layer holds r_dof 0
    kSkipGui = 1u << 2,    // gui: the GUI target's colour `_gui` (its depth stays view 1's)
    kSkipFlares = 1u << 3, // flares: `_cineLensflares`
    kSkipMotionBlur = 1u << 4, // mblur: `_velocityTileMax0/1`
    // refract: the refraction's images and targets (glass: its history pairs and the mask chain)
    kSkipRefraction = 1u << 5,
    // tblock: the transparency pass's own parameter block keeps the engine's scene mips and refraction mask
    // (view 1's glass then refracts view 0's picture)
    kSkipTransparencyBlock = 1u << 6,
};

struct CloneSkip {
    std::uint32_t groups = kSkipSlot0;
    std::vector<std::uint32_t> slots;  // an RVA (0x66E3180): one of the global image or target slots
    std::vector<std::uint32_t> fields; // dc+<offset> (dc+0x5E0): one of the device context fields
};

// ETERNALVR_TEST_INSTALL_FAIL (rig only): the install stops where a real failure would. `check`: after every
// check, before anything is changed (the standard renderer runs). `redirects`: in the redirects' hooks, after
// the block move and the raised counts, before the code bytes, as a failed redirect hook would; `hook`: after
// the code bytes are changed, as a failed clone hook would (both view 0 alone for the session).
enum class TestFail : std::uint8_t { None, Check, Redirects, Hook };

// Which auto-exposure image view 1 reads (ETERNALVR_PE_EXPOSURE; exposure_hooks.cpp). View 0 adapts the one
// exposure both eyes share and writes it into the image of its backend frame's parity; view 1 skips its own
// update. Same (the default): view 1 reads the image view 0 writes this frame. Prev: the one view 0 wrote the
// frame before, whatever order the views' post-process work runs in. Engine: the engine's own index, the
// parity of view 1's last own update (image 0 until it runs one; the eyes then differ while exposure
// changes), and view 1's update is skipped only where its eye pose was written (the rig's positive control).
enum class Exposure : std::uint8_t { Same, Prev, Engine };

struct Settings {
    bool requested = false; // ETERNALVR_PARALLEL_EYES=1 and nothing below rules it out
    // Why not, when ETERNALVR_PARALLEL_EYES=1 is set but `requested` is false; empty otherwise.
    std::string why;
    bool clones = true; // ETERNALVR_TEST_VIEW_CLONES=0: off
    // ETERNALVR_TEST_VIEW_CLONE_LOG=0: no census of what view 1 does with its clones (view_clone_census.cpp;
    // one binary search per bind and write mark); the line per clone after each build stays.
    bool cloneCensus = true;
    CloneSkip cloneSkip; // ETERNALVR_TEST_VIEW_CLONE_SKIP
    // ETERNALVR_TEST_VIEW_CLONE_NAMES=build: each build names its clones anew (as before), so a rebuild makes
    // a whole new set and the old one stays until a map load; by default a clone keeps its name, a rebuild
    // keeps it and the engine's resize purges it for the next build (view_clone_make.hpp).
    bool cloneBuildNames = false;
    // ETERNALVR_TEST_VIEW_CLONE_REBUILD=<seconds> (1 to 3600): the clones are made again that long after each
    // build, at most kForcedRebuilds times (rig only: the video memory across rebuilds).
    int cloneRebuildSeconds = 0;
    EyeCopy eyeCopy = EyeCopy::Screen; // ETERNALVR_TEST_EYE_COPY=0 or 1
    // ETERNALVR_TEST_PE_POSE=latest: a present carries the newest view record, not that of the frame it
    // shows.
    bool latestPose = false;
    // ETERNALVR_TEST_PE_REPEATS=show: a present whose eye 1 would repeat its last copy (eye 0 shows a new
    // image: the eyes a frame apart) is shown; by default the headset keeps the last pair instead. With
    // ETERNALVR_TEST_PE_PAIRING=guess only: a pair never has one eye older than the other.
    bool showRepeats = false;
    // ETERNALVR_TEST_PE_PAIRING=guess: eye 0 is the presented image and eye 1 the copy of view 1's image a
    // present is guessed to go with (as before); by default each eye is a copy of its own view's image made
    // after its frame's submit, and a present shows a pair of one frame (view_snapshot.hpp).
    bool guessPairs = false;
    // ETERNALVR_TEST_PE_PAIR_SLOTS=2 to 4: the pairs kept (each two eye images, about 18 MB each at
    // 2056x2216).
    std::uint32_t pairSlots = 2;
    // ETERNALVR_TEST_PE_EYE1_LAG=1 (a rig control for the eye sync check; not with the guess): a new pair
    // shows eye 1 of the pair shown before it, so eye 1 is a frame late. At least 3 pairs are kept then: the
    // pair shown before stays besides the one being shown and the one being written.
    bool eye1Lag = false;
    // ETERNALVR_TEST_PE_DROP=<N> (2 to 60; a rig control for the judder check; not with the guess): every Nth
    // present with a new pair is not handed to the headset, which keeps the last pair as when no pair is new.
    // 0: off.
    std::uint32_t dropEvery = 0;
    // ETERNALVR_TEST_PE_UPDATE_UNION=0: view 1's update lists are not added to view 0's (view_updates.hpp),
    // so models only eye R sees are not prepared or updated, as before.
    bool updateUnion = true;
    // ETERNALVR_TEST_PE_SWAP_GUARD=0: view 0's screen pass is not left out when its swapchain image was
    // destroyed since the last acquire (view_swap_guard.hpp), so a swapchain recreate in a two-view frame
    // crashes as before.
    bool swapGuard = true;
    // ETERNALVR_TEST_PE_RT_UPSCALE_HOLD=0: r_raytracedReflectionsTemporalUpscaleQuality is left as the game
    // has it (its profile load writes 1 to 3 over the launcher's 0), not held at 0 (runtime_cvars_pe.cpp).
    bool rtUpscaleHold = true;
    // ETERNALVR_TEST_PE_WATER=0: view 1's water setup and job work on the world's water state as before
    // (view_water.hpp): the simulation steps twice a frame and each view takes the other's grid matrix.
    bool water = true;
    Exposure exposure = Exposure::Same; // ETERNALVR_PE_EXPOSURE=same, prev or engine
    std::uint32_t off = 0;              // ViewPart bits from ETERNALVR_TEST_VIEW_OFF
    int viewOnly = -1;                  // ETERNALVR_TEST_VIEW_ONLY=0 or 1: that view alone; -1 both
    TestFail testFail = TestFail::None; // ETERNALVR_TEST_INSTALL_FAIL=check, redirects or hook
    bool antiAliasingOff = false;       // ETERNALVR_STEREO_TAA=0 (as Route S reads it): no anti-aliasing
    // ETERNALVR_STEREO_DLSS=1 (as Route S reads it), not with antiAliasingOff: DLSS in both views, at the
    // r_dlssQuality of ETERNALVR_STEREO_DLSS_QUALITY as Route S reads it (taaDlssQuality, taa_hooks.hpp).
    bool dlss = false;
    // ETERNALVR_STEREO_RUNTIME_CVARS=0 (as Route S reads it): the anti-aliasing is left as the game has it.
    bool antiAliasingHeld = true;
    bool alternateEyes = false; // ETERNALVR_ALTERNATE_EYES asks for alternate eyes (on or auto): a Route S
                                // mode, ignored here (one line), and its auto does not turn frame pacing off
    std::vector<std::string> warnings; // a value that does not parse, ignored
};

inline constexpr int kForcedRebuilds = 5;

// Reads the settings. `uiLayer` says whether the UI layer is on (ETERNALVR_UI_LAYER and its default,
// ui_settings.hpp); asked only with ETERNALVR_PARALLEL_EYES=1.
Settings readSettings(const EnvLookup& env, const std::function<bool()>& uiLayer);

// The anti-aliasing cvars the layer holds with it (runtime_cvars_pe.cpp): with `off`, the stereo path's set
// for no temporal effects (stereo_seq::stereoRuntimeCvars: r_TAASafeMode 1, r_antialiasing 0); with `dlss`,
// DLSS (r_antialiasing 2, and r_dlssQuality at `dlssQuality` from 0 to 3); else TAA (r_antialiasing 1), so
// the player's own mode (DLSS from the game's menu, say) is not used.
std::vector<stereo_seq::CvarExpectation> antiAliasingCvars(bool off, bool dlss = false, int dlssQuality = -1);

// The parts `off` names, comma-separated, for the log ("none" for none).
std::string partsText(std::uint32_t off);

// ETERNALVR_TEST_VIEW_CLONE_SKIP's list: slot0, dof, gui, flares, mblur, refract, none, an RVA (hex, with or
// without 0x) of 0x1000 or more, dc+<hex offset> below 0x1000; both a multiple of 8. An item that is none of
// these is a warning; a list without a single usable item keeps the default.
CloneSkip readCloneSkip(std::wstring_view list, std::vector<std::string>& warnings);

// The items, comma-separated, for the log ("none" for none).
std::string cloneSkipText(const CloneSkip& skip);

} // namespace evr::vkcore::parallel_eyes
