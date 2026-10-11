#pragma once

// The refraction's temporal history per eye under Route S (vkcore/refraction_hooks.hpp; Steam build
// 25216728).
//
// Glass and other refractive surfaces sample a half-size copy of the scene behind them. The refraction pass
// (RVA 0x1C63DB0) blends each refraction update's copy (up to r_refractionMaxUpdates, 2) with its copy of the
// render before, from a pair of accumulation images per update (the device context's
// _refractionAccumulationBuffer<pair><update>), and with the refraction mask on
// (r_transparencyEnableRefractionMask 1, the default) it also reads the mask tiles of this render and of the
// last (_refractionMaskTiles32x0/1), which the mask pass (RVA 0x1C64360) writes. Every pair is picked by the
// backend counter's parity: a render writes [counter & 1] and reads [(counter & 1) ^ 1]. With two renders per
// tick eye L always gets one parity and eye R the other, so each eye blended its glass with the other eye's
// picture: the scene through the glass moved the wrong way. r_refractionTAA, which the stereo set held at 0,
// is read only without the mask.
//
// Here each eye has a set of its own (eye L and mono the engine's images, eye R as many more) and alternates
// within it by its own renders. Before a render's passes the plan names the slot of the eye's set the render
// writes; the other slot, its last written one, is read. The mask pass and each refraction update (job
// threads) ask for the same render: the first asking makes the plan, the others get it again. An eye starts
// its history over (the refraction pass then blends with no history):
// - its first render, its first after reset(), and its first after invalidate() (dynamic resolution);
// - its own last render is neither one nor two renders back (a mono stretch, a skipped eye R, glass out of
//   view for a while);
// - a mono render right after an eye L render (most likely eye R's, its tag not found: it must not take eye
//   L's history), and eye L's next render after that one (which would read it).
// Mono renders count as eye L.
//
// The engine starts every update over when the render's update count (transparency context + 0x137D0)
// differs from the last render's (global, RVA 0x6687038), whichever eye that was; with the history per eye
// eye R's update 1 after its renders with one update would read its slot of an older render, or of none.
// Each update therefore starts over, too, when
// - the eye's update count differs from its own last render's (the engine's rule, per eye), or
// - the eye's own last render did not run that update.
// Otherwise the update keeps its history, also when the engine would start it over only because the other
// eye's count differs.

#include "stereo_seq/eye_tags.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::stereo_seq {

// The updates the engine's images hold (r_refractionMaxUpdates 2).
inline constexpr int kRefractionUpdates = 2;

// The engine's index of an accumulation image (_refractionAccumulationBuffer<pair><update>: device context +
// 0x440 + 8 * index, its target + 0x5F0 + 8 * index). The pass writes [parity * 2 + update] and reads
// [(parity ^ 1) * 2 + update].
constexpr int refractionAccIndex(int pair, int update) {
    return pair * kRefractionUpdates + update;
}

enum class RefractionRestart : std::uint8_t {
    None,
    First,         // the eye's first render (or its first after reset())
    Gap,           // its own last render is not one or two renders back
    Untagged,      // a mono render right after an eye L render
    AfterUntagged, // eye L's next render after such a mono render
    Resumed,       // its first render after dynamic resolution held the history off (invalidate())
    // One update, the render going on:
    CountChanged, // the eye's update count differs from its own last render's
    NotInLast,    // the eye's own last render did not run the update
};
inline constexpr int kRefractionRestarts = 8;

const char* refractionRestartName(RefractionRestart restart);

struct RefractionPlan {
    int eye = 0;   // 0: eye L and mono, 1: eye R
    int write = 0; // the slot of the eye's set this render writes; the other one is read
    RefractionRestart restart = RefractionRestart::None; // the render's
    bool repeated = false;                               // the render asked before: the same plan again
    // For an update (None and false for the mask pass): why the update starts over while the render goes on
    // (CountChanged, NotInLast), and whether this update of the render asked before.
    RefractionRestart updateRestart = RefractionRestart::None;
    bool updateRepeated = false;

    // The update blends with no history.
    bool fresh() const {
        return restart != RefractionRestart::None || updateRestart != RefractionRestart::None;
    }
};

class RefractionHistory {
public:
    // Forgets every eye's renders: each eye starts over at its next render.
    void reset();
    // The images were written outside the history: each eye starts over at its next render, for `why`.
    void invalidate(RefractionRestart why);

    // The mask pass of a render of `eye` with the backend counter `counter` the engine picks its parity by.
    RefractionPlan plan(Eye eye, std::uint32_t counter);
    // Refraction update `update` of that render, which runs `updates` of them (the transparency context's
    // count). An update outside 0 .. kRefractionUpdates - 1 starts over (NotInLast) and is not kept.
    RefractionPlan plan(Eye eye, std::uint32_t counter, int update, int updates);

private:
    struct PerEye {
        int written = -1; // the slot its last render wrote; -1 before its first
        std::optional<std::uint32_t> lastRender;
        std::optional<std::uint32_t> previous; // its render before lastRender, the one lastRender reads
        Eye lastEye = Eye::Mono;
        bool afterUntagged = false; // eye L's chain: a mono render right after eye L wrote into it
        RefractionRestart pending = RefractionRestart::None; // the reason of the next first render
        RefractionPlan last{};
        int updates = 0;         // lastRender's update count (0 until an update asks)
        int previousUpdates = 0; // previous's
        // The render that last ran each update, and lastRender's decision for it.
        std::array<std::optional<std::uint32_t>, kRefractionUpdates> ran{};
        std::array<std::optional<RefractionRestart>, kRefractionUpdates> decided{};
    };
    PerEye& render(Eye eye, std::uint32_t counter, RefractionPlan& out);
    std::array<PerEye, 2> eyes_{};
};

// ETERNALVR_STEREO_REFRACTION_TAA: 1, on or true keeps the history per eye; hold holds it off in stereo
// (every stereo render blends with no history); unset, 0, off, false or anything else leaves the engine's
// (shared between the eyes), the default until the per-eye history has been tried in a headset.
enum class RefractionMode : std::uint8_t { PerEye, Hold, Off };
RefractionMode refractionMode(std::string_view value);
const char* refractionModeName(RefractionMode mode);

// What the per-eye history needs before it runs.
struct RefractionReadiness {
    RefractionMode mode = RefractionMode::PerEye;
    bool installed = false;         // every hook in place
    bool gameTouch = false;         // the multiplayer guard allows game writes
    bool failedClosed = false;      // a check after start-up failed (sticky)
    bool imagesMade = false;        // eye R's images were made with the device context, of the engine's size
    bool dynamicResolution = false; // rs_enable reads non-zero: the engine scales its own images by parity
};

// Why the history is not per eye, or nullptr when it is.
const char* refractionNotReady(const RefractionReadiness& readiness);

// A stereo render (eye L or eye R) whose history is held off: hold mode, or per eye requested but not ready
// (failed closed, eye R's images missing, dynamic resolution on) while the hooks are in place. Mono renders
// are left to the engine.
bool refractionHeldOff(const RefractionReadiness& readiness, Eye eye);

} // namespace evr::stereo_seq
