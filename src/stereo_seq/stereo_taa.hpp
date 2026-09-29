#pragma once

// Per-eye temporal history for Route S (ETERNALVR_STEREO_TAA=1; docs/rig-findings/stereo-temporal.md):
// the decisions that need no engine, Vulkan or NGX, so they are tested on every platform.
//
// The engine keeps a render view's TAA accumulation as two images and picks them by the parity of the
// backend frame counter b: the TAA pass writes image (b + 1) & 1 and reads image b & 1 as the previous
// frame (selectors at RVA 0x1CBB5A0 and 0x1CBB6C0). Route S renders two backend frames per game tick, so
// b & 1 is constant per eye and each eye would read the other eye's output. With a second pair of images
// for eye R, each eye alternates within its own pair by the count of its own frames (the eye tag's
// eyeSeq), so it always reads what it wrote one tick earlier.

#include "stereo_seq/eye_tags.hpp"
#include "stereo_seq/seq_settings.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace evr::stereo_seq {

enum class AccumRole : std::uint8_t {
    Output,  // the image the TAA pass (or DLSS) writes this frame
    History, // the image it reads as the previous frame
};

struct AccumPick {
    bool engine = true; // keep the engine's own choice (parity of the backend frame counter)
    int pair = 0;       // 0: the engine's images, 1: the second pair (eye R)
    int index = 0;      // 0 or 1 within the pair
};

// `tag`: the eye tag of the backend frame being recorded, nullptr when it has none (tags out of step, a
// frame from before the base). `secondPair`: eye R's images exist. Untagged frames and frames without
// eye R's images keep the engine's choice.
AccumPick pickAccumulation(AccumRole role, const RenderTag* tag, bool secondPair);

// The TAA sub-sample (jitter phase) index both eyes of game frame `gameFrame` get: one phase per tick,
// consecutive ticks consecutive phases (the engine derives it from the render frame counter, which
// advances twice per stereo tick, so each eye would see every other phase only). `numSubSamples` is
// r_TAANumSubSamples, clamped to 1..255.
std::uint8_t taaSubSample(std::uint64_t gameFrame, int numSubSamples);

// The auto-exposure image a frame writes and reads (the engine's pair at device context + 0x5D0, index at
// post-process context + 0x140, set at RVA 0x1C98D40). The engine picks it by the backend frame's parity
// unless the view skips its update: with eye R skipping (Route S's exposure-once setting), eye L's parity
// is constant and it would read a stale previous exposure every tick. Eye L and mono frames alternate by
// their own frame count, so each reads the exposure it wrote one frame earlier; eye R reads the one its eye
// L wrote this tick.
class ExposurePlanner {
public:
    int indexFor(const RenderTag& tag);

private:
    int lastLeft_ = 0;
};

// When the TAA history of both eyes is reset (renderView_t.disableTssaaNextFewFrames on both views of a
// tick; the engine then treats the view's history as invalid for its next three renders). Eye R's history
// is valid only if eye R rendered the previous game frame; the engine's reset counter is shared by the two
// renders of a tick, so both eyes are reset together (a one-eye reset is a one-eye blur flash).
class TaaResetPlanner {
public:
    // Eye L's per-eye hook for stereo game frame `gameFrame`: true when this tick's views get the flag.
    bool onLeft(std::uint64_t gameFrame);
    // Eye R's per-eye hook: its eye L's answer (true for a frame eye L did not decide).
    bool onRight(std::uint64_t gameFrame);

private:
    std::uint64_t lastRight_ = 0; // the last game frame eye R rendered (0: none yet)
    std::uint64_t leftFrame_ = 0; // the game frame eye L decided last
    bool leftDecision_ = true;
};

// Per-eye DLSS: the game creates one NGX feature (a handle); eye R evaluates a twin, created on eye R's
// first evaluation of it with the same parameters. Handles are opaque values here.
class NgxTwins {
public:
    // A twin was made (or tried) for the game's feature `primary`: `twin` is eye R's (0 when its creation
    // failed; not tried again until the game releases `primary`).
    void created(std::uintptr_t primary, std::uintptr_t twin);
    // A twin was made or tried for `primary`.
    bool known(std::uintptr_t primary) const;
    // Eye R's feature for the game's `primary`, 0 when there is none.
    std::uintptr_t twinOf(std::uintptr_t primary) const;
    // The game released `primary`: returns its twin to release (0: none) and forgets both.
    std::uintptr_t released(std::uintptr_t primary);
    // Eye R evaluates the twin of `primary` for game frame `gameFrame`: true when the twin's history must be
    // reset (its first evaluation, or eye R's last evaluation was not `step` game frames earlier: 1 under
    // Route S, 2 with alternate eyes, alternate_eyes.hpp).
    bool resetTwin(std::uintptr_t primary, std::uint64_t gameFrame, std::uint64_t step = 1) {
        return resetTwin(primary, gameFrame, step, step);
    }
    // The same, with eye R's last evaluation anywhere from `minStep` to `maxStep` game frames earlier
    // (ETERNALVR_ALTERNATE_EYES=auto: 1 or 2).
    bool
    resetTwin(std::uintptr_t primary, std::uint64_t gameFrame, std::uint64_t minStep, std::uint64_t maxStep);
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::uintptr_t primary = 0;
        std::uintptr_t twin = 0;
        std::uint64_t lastFrame = 0; // the last game frame eye R evaluated the twin for (0: never)
    };
    std::vector<Entry> entries_;
};

// Cvars with per-eye TAA (docs/rig-findings/stereo-temporal.md): the temporal effects whose history is
// not per eye yet, set to these values in memory by the layer once per-eye history is in place.
const std::vector<CvarExpectation>& stereoTaaForcedCvars();

// The v1 set the layer writes when ETERNALVR_STEREO_TAA=1 was asked for but per-eye history is not in
// place (fail closed): no TAA, DLSS or other temporal accumulation.
const std::vector<CvarExpectation>& stereoTaaFailClosedCvars();

// What the command line should carry for per-eye TAA (logged at start-up): temporal accumulation on,
// dynamic resolution and vsync off.
const std::vector<CvarExpectation>& stereoTaaCommandLineCvars();

// A switch variable's value: "0", "false" and "off" (any case) turn it off, "1", "true" and "on" turn it
// on, anything else (unset or empty included) keeps `fallback`. ETERNALVR_STEREO_TAA defaults to on
// under Route S, ETERNALVR_STEREO_DLSS to off.
bool switchValue(std::string_view value, bool fallback);

// The anti-aliasing mode per-eye history holds (r_antialiasing): the player's mode when it is TAA (1) or
// DLSS (2), TAA when it is off; DLSS when the DLSS option is on and eye R can have its own DLSS feature;
// TAA whenever DLSS is asked for but eye R cannot have one.
int heldAntialiasing(int current, bool dlssOption, bool dlssPerEye);

// The r_dlssQuality value (0 ultra performance, 1 performance, 2 balanced, 3 quality) for a quality name or
// number ("quality", "balanced", "performance", "ultra_performance", or "0" to "3", any case); -1 for
// anything else, unset and empty included (the game's own setting stays).
int dlssQualityValue(std::string_view value);

// The pieces per-eye TAA needs; the first one missing, or nullptr when all are there.
struct TaaReadiness {
    bool selectors = false;  // both accumulation selectors hooked
    bool secondPair = false; // eye R's images were built with the device context
    bool subSamples = false; // r_TAANumSubSamples located (the per-eye jitter phase)
    bool exposure = false;   // the auto-exposure index hooked
    bool cvarSetter = false; // the engine's cvar setter and every forced cvar located
};
const char* taaMissingPiece(const TaaReadiness& readiness);

} // namespace evr::stereo_seq
