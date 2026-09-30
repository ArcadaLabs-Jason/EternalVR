#pragma once

// How glory kills are shown in the headset (ETERNALVR_GLORY_KILLS, docs/VR_HEAD_TRACKED.md). A glory kill
// (the game's sync kill, idPlayer::syncMaster set) moves and turns the camera on its own, which is the
// motion some players feel. The options:
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

// "follow", "steady", "fade" or "screen", any case, surrounding spaces ignored; nullopt otherwise.
std::optional<GloryView> parseGloryView(std::string_view text);
const char* gloryViewName(GloryView view);

struct GloryTiming {
    // After the sync flag clears, the episode lasts while the game still forces the view (its camera eases
    // back to the player's), at most this long.
    double settleSeconds = 0.5;
};

class GloryEpisode {
public:
    explicit GloryEpisode(GloryTiming timing = {});

    struct Step {
        bool active = false;  // a glory kill is being shown
        bool started = false; // this frame is its first
        bool ended = false;   // the previous frame was its last
    };

    // One game frame: `sync` is idPlayer::syncMaster set, `forcedView` the forced-view gate, `seconds` a
    // monotonic clock. A non-finite clock changes nothing.
    Step update(bool sync, bool forcedView, double seconds);

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] unsigned long long episodes() const { return episodes_; }
    void reset();

private:
    GloryTiming timing_;
    bool active_ = false;
    bool syncSeen_ = false;
    double syncEndedAt_ = -1.0;
    unsigned long long episodes_ = 0;
};

} // namespace evr::comfort
