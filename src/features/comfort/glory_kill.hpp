#pragma once

// How glory kills are shown in the headset (ETERNALVR_GLORY_KILLS, docs/VR_HEAD_TRACKED.md). A glory kill
// (the game's sync kill: idPlayer::savedSyncEntity set to a `syncmelee/...` entity) moves and turns the
// camera on its own, which is the motion some players feel. The options:
// - Follow: the view follows the kill's camera and turns with the head from there (the first builds'
//   behaviour, and the default).
// - Steady: the view stays on the kill's animated eye but keeps the heading it had when the kill started;
//   only the head turns it. When the kill ends, the game's aim is turned back to that heading, so the
//   view never rotates on its own.
// - Fade: the view fades to black while the kill runs and back in when it ends.
// - Screen: the kill plays on the flat screen in front of the player, like a cutscene.
//
// GloryEpisode tells when a kill starts and ends from the sync flag and the forced-view gate, one update per
// game frame. No OpenXR or Windows here.

#include <optional>
#include <string_view>

namespace evr::comfort {

enum class GloryView { Follow, Steady, Fade, Screen };

// Whether a sync entity's entityDef name is a kill's (`syncmelee/<demon>`, the chainsaw's [inferred]) rather
// than a pickup's animation (`interact/...`: a Sentinel Crystal, a Praetor token, a rune, a mod bot). An
// unreadable (empty) name counts as a kill.
bool isKillSync(std::string_view entityDefName);

// "follow", "steady", "fade" or "screen", any case, surrounding spaces ignored; nullopt otherwise.
std::optional<GloryView> parseGloryView(std::string_view text);
const char* gloryViewName(GloryView view);

struct GloryTiming {
    // After the sync flag clears, the episode lasts while the game still forces the view (its camera eases
    // back to the player's), at most this long.
    double settleSeconds = 0.5;
    // A kill lasts a few seconds; one that runs this long has lost its end (a sync flag left set) and ends
    // here, so a fade or the flat screen never stays. The next starts once the flag has cleared. Time paused
    // (a menu up) does not count, and a gap between frames counts at most maxFrameSeconds.
    double maxSeconds = 10.0;
    double maxFrameSeconds = 0.25;
};

class GloryEpisode {
public:
    explicit GloryEpisode(GloryTiming timing = {});

    struct Step {
        bool active = false;   // a glory kill is being shown
        bool started = false;  // this frame is its first
        bool ended = false;    // the previous frame was its last
        bool timedOut = false; // ended at GloryTiming::maxSeconds with the sync flag still set
    };

    // One game frame: `sync` is a kill's sync entity set (isKillSync), `forcedView` the forced-view gate,
    // `seconds` a monotonic clock, `paused` a menu up (its time is not the kill's). A non-finite clock
    // changes nothing.
    Step update(bool sync, bool forcedView, double seconds, bool paused = false);

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] unsigned long long episodes() const { return episodes_; }
    [[nodiscard]] const GloryTiming& timing() const { return timing_; }
    void reset();

private:
    GloryTiming timing_;
    bool active_ = false;
    bool syncSeen_ = false;
    double syncEndedAt_ = -1.0;
    double lastSeconds_ = -1.0; // the last frame's clock
    double runSeconds_ = 0.0;   // the running kill's time, pauses and gaps left out
    bool timedOut_ = false;     // the last kill hit maxSeconds; none starts until the flag clears
    unsigned long long episodes_ = 0;
};

} // namespace evr::comfort
