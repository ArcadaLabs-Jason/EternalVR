#pragma once

// The head in geometry (room-scale v1, T-062; R10 section 3.2 step 4).
//
// Each game frame a sphere is swept from the game's eye (on the body, always clear) to the rendered head.
// Where it first touches geometry, the rest of the way is the penetration depth. The camera is never
// pushed back (that is the uncomfortable option); instead the view fades to black by depth, 0 at the
// first touch and fully black `fullDepthMetres` further in, and anything the game starts from the view
// (the hand's shot origin) uses the head where the sweep first touched: the last clear point on the way.
//
// HeadClearance turns a sweep result into the penetration and the valid offset; HeadFade turns the
// penetration into the fade shown, rate-limited so a sudden touch reaches full black within
// `riseSeconds` and a step back clears within `fallSeconds`.

#include "common/vector.hpp"

#include <optional>

namespace evr::roomscale {

struct ClearanceStep {
    Vec3 validOffset; // the offset shots and traces may start from (game units): the first contact
    float penetrationMetres = 0.0f;
    bool blocked = false; // the sweep hit this frame
};

class HeadClearance {
public:
    // `desiredOffset`: the rendered head's offset from the game's eye (game units). `hitFraction`: where
    // along eye -> eye + desiredOffset the sphere first touches geometry, in [0, 1]; nullopt when the way
    // is clear or there is no collision query. A fraction outside [0, 1] or non-finite counts as clear.
    ClearanceStep update(Vec3 desiredOffset, std::optional<float> hitFraction, float unitsPerMetre);

    [[nodiscard]] Vec3 lastClear() const { return lastClear_; }
    void reset() { lastClear_ = {}; }

private:
    Vec3 lastClear_{};
};

struct FadeTiming {
    float fullDepthMetres = 0.10f;
    float riseSeconds = 0.10f; // 0 to fully black at most this long
    float fallSeconds = 0.25f; // fully black to clear at most this long
};

// The fade the penetration asks for: 0 at the surface, 1 at `fullDepthMetres` and beyond.
float fadeTarget(float penetrationMetres, const FadeTiming& timing);

// The depth the shown fade follows (metres, as for fadeTarget). `hold` (the blink over a re-anchor, a glory
// kill shown as a fade) is fully black whatever the head does and whatever `headFade` says; otherwise the
// head's own depth (in geometry, past the lean cap) counts only with the head fade on (ETERNALVR_HEAD_FADE).
float shownFadeDepth(bool headFade, bool hold, float headDepthMetres);

class HeadFade {
public:
    explicit HeadFade(FadeTiming timing = {});

    // Moves the fade toward the target of `penetrationMetres` over `dtSeconds` and returns it (0 clear,
    // 1 black). A non-finite or negative step changes nothing.
    float update(float penetrationMetres, double dtSeconds);

    [[nodiscard]] float value() const { return value_; }
    void reset() { value_ = 0.0f; }

private:
    FadeTiming timing_;
    float value_ = 0.0f;
};

} // namespace evr::roomscale
