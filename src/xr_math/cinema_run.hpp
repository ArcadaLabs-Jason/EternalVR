#pragma once

// Stereoscopic cutscenes (docs/VR_STEREO.md, "Stereoscopic cutscenes"), the run of pair frames: which frames
// start a new shot (both eyes drop their temporal history), when a run ends, and what the screen shows of a
// pair frame. No engine or OpenXR code here, so it is tested on every platform.

#include <cstdint>

namespace evr::xr_math {

// Frames, from the first of a new shot, that both eyes start with no temporal history. The per-eye hook's
// reset lasts a few frames on its own; the extra ones cover a record that a frame in flight finds by the view
// (a still shot repeats it) in place of the newest.
inline constexpr int kCinemaResetFrames = 3;
// A pair frame this long after the last one starts a new run: a cutscene that follows a skipped one or a
// level load without a head-tracked frame between them starts afresh (history, the separation limit).
inline constexpr double kCinemaRunGapSeconds = 0.5;
// The GUI stays out of the eye images this long after its newest image (the quad shows it while it is
// fresh): a short gap in the GUI capture does not put it back into eye L's image for a frame. Past it (or
// with no GUI image at all) a pair frame goes on the mono quad, where eye L's composited GUI is seen.
inline constexpr double kCinemaGuiHoldSeconds = 1.0;

struct CinemaPairStep {
    bool started = false;      // the first frame of a run
    bool afterGap = false;     // started because the last pair frame was kCinemaRunGapSeconds ago
    bool cut = false;          // the game's camera cut (never on a run's first frame)
    bool historyReset = false; // both eyes drop their temporal history on this frame
};

// Camera hook thread only.
class CinemaRun {
public:
    // A pair frame at `nowSeconds`, `cutByte` the game's cameraCut.
    CinemaPairStep pair(bool cutByte, double nowSeconds);
    // A frame of the flat screen that is not a pair (the setting off, a glory kill, no Route S): true when a
    // run ended with it.
    bool end();
    // A head-tracked frame: ends a run once the cutscene is over (`ended` set when this did), and true for
    // the first kCinemaResetFrames such frames after a run ended (the shot changed back to the player).
    bool headTracked(bool inCutscene, bool& ended);
    [[nodiscard]] bool active() const { return active_; }

private:
    bool active_ = false;
    int resetLeft_ = 0;
    double last_ = 0.0;
};

// A trace hit nearer than kCinemaConfirmNearMetres lowers the separation limit at once, so a ray that
// flickers through a thin object for one frame would pop the depth. The hit counts once it is seen on two
// frames in a row (a cut or a run's first frame takes it at once: there is no frame before it).
inline constexpr float kCinemaConfirmNearMetres = 0.4f;
class CinemaNearConfirm {
public:
    // The nearest trace distance of this frame (0: none) -> the one to use (0: none).
    float step(float nearestMetres, bool immediate);
    void reset() { previousNear_ = false; }

private:
    bool previousNear_ = false;
};

// What the screen shows of the frame the XR worker has: `pair` a stereo cutscene pair with both eyes and no
// menu over it, `guiFresh` the GUI image fresh, `secondsSinceGui` since the newest GUI image (negative: none
// yet).
struct CinemaLayerPlan {
    bool eyes = false;          // eye L and eye R on their own quads (else the one mono quad)
    bool gui = false;           // the GUI on a quad over them
    bool skipComposite = false; // the game leaves its GUI out of the eye images
};
CinemaLayerPlan cinemaLayerPlan(bool pair, bool guiFresh, double secondsSinceGui);

// The GUI quad sits nearer than the screen (cinemaGuiDistanceScale) at the screen's angular size, so each eye
// sees it shifted against the screen by half its crossed disparity: a GUI that covers the whole screen (the
// black of a cinematic's fade, the opening's Slayer sigil) leaves a strip of the scene behind it uncovered at
// one edge in each eye, the left edge in the left eye and the right edge in the right. While the GUI holds
// such a full-screen backdrop it goes to the screen instead, where it covers the pair exactly in both eyes;
// it moves between the two places, so subtitles over a fade glide instead of jumping: to the screen over
// kCinemaGuiToScreenSeconds (quickly: the backdrop test reports a backdrop a few frames after it appears, and
// until the quad arrives the strip shows), back in front of it over kCinemaGuiMoveSeconds.
inline constexpr double kCinemaGuiToScreenSeconds = 0.1;
inline constexpr double kCinemaGuiMoveSeconds = 0.3;

// XR worker only.
class CinemaGuiDepth {
public:
    // The GUI quad's distance as a fraction of the screen's for a frame `seconds` after the last one, with
    // `nearScale` the place in front of the screen and `backdrop` the full-screen backdrop up. The first
    // frame, and one more than kCinemaRunGapSeconds after the last (a new pair run), takes its place at once.
    float step(float nearScale, bool backdrop, double seconds);
    // Whether the last step had the GUI at the screen or on its way there.
    [[nodiscard]] bool towardScreen() const { return towardScreen_; }

private:
    float atScreen_ = -1.0f; // 0: in front of the screen, 1: at it; negative: no frame yet
    bool towardScreen_ = false;
};

} // namespace evr::xr_math
