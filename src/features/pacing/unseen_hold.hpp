#pragma once

// Holding the game back while nobody can see its frames (T-228; docs/VR_STEREO.md "Frame pacing", Behind a
// runtime's menu and while nobody looks). Keep-active keeps the game running whenever the XR session runs, so
// with the headset off, asleep or showing something else it would render as fast as it can for the whole
// break: heat and power on a laptop, with nobody looking (public issue #29: a 3070 laptop's frame rate
// falling step by step over hours with breaks). The XR worker tells this watch the session's state and, where
// the runtime senses it, the user's presence; the present hook holds the game to what the watch says:
//
// - Low (kLowImagesPerSecond): the frames reach nobody. The session SYNCHRONIZED after it had focus (the
//   runtime shows none of the game's frames: a headset asleep or off, the runtime's home or another app), or
//   the runtime reporting the user not present (XR_EXT_user_presence: the headset taken off) after it had
//   reported the user present in this session.
// - DisplayRate (UnfocusedCap): the session VISIBLE after it had focus: a runtime's menu over the game, which
//   still shows behind it (public issue #19's hold).
// - None: focused and present; before the session's first focus (the game's start and first load are never
//   held); and while the session does not run at all (the game's own window is then all there is to see).
//
// A presence of false never reported true before in the session is not trusted: a sensor that always reads
// "not present" would otherwise hold a player wearing the headset to a few images a second. Nor is any
// presence from a system that says it does not sense the user (XrSystemUserPresencePropertiesEXT).
//
// No OpenXR or Windows here.

#include <cstdint>

namespace evr::pacing {

// The images a second of the low hold. Below the launcher's PlayThreshold (10 new pairs a second: a window
// under it is taken as a menu or a pause, never as the game's own rate) with room for the window around the
// hold's start and end, and high enough that the game's audio and simulation keep running as on a slow PC.
inline constexpr double kLowImagesPerSecond = 8.0;
inline constexpr double kLowIntervalSeconds = 1.0 / kLowImagesPerSecond;

// The XR session as the hold reads it. NotRunning: IDLE, READY, STOPPING, LOSS_PENDING, EXITING, lost or no
// session (no frames are made for the headset).
enum class SessionView : std::uint8_t { NotRunning, Synchronized, Visible, Focused };

enum class HoldLevel : std::uint8_t { None, DisplayRate, Low };

enum class HoldReason : std::uint8_t {
    None,
    Menu,       // VISIBLE after focus: a runtime's menu over the game
    Hidden,     // SYNCHRONIZED after focus: the runtime shows none of the game's frames
    Away,       // the runtime reports the user not present
    RigVisible, // VISIBLE taken as nobody looking (ETERNALVR_TEST_UNSEEN_VISIBLE=1, a rig test)
};

struct UnseenHold {
    HoldLevel level = HoldLevel::None;
    HoldReason reason = HoldReason::None;
};

// The reason as the log names it, e.g. "the session is SYNCHRONIZED: ...".
const char* holdReasonText(HoldReason reason);

class ViewerWatch {
public:
    // The system senses the user (the extension enabled and XrSystemUserPresencePropertiesEXT says so); set
    // per system, kept by ready(). Without it presence() is ignored.
    void sensesPresence(bool senses) { senses_ = senses; }
    [[nodiscard]] bool sensesPresence() const { return senses_; }
    // READY (or a new session): its focus and the user's presence are not known yet, and it does not run.
    void ready();
    // Every session state change, READY's after ready().
    void state(SessionView view);
    // XR_EXT_user_presence's event (the runtime queues one when the session begins, then one per change).
    void presence(bool present);

    [[nodiscard]] UnseenHold hold() const;

private:
    enum class Presence : std::uint8_t { Unknown, Present, Absent };

    bool senses_ = false;
    SessionView view_ = SessionView::NotRunning;
    bool hadFocus_ = false;    // FOCUSED since READY
    bool seenPresent_ = false; // the user reported present since READY
    Presence presence_ = Presence::Unknown;
};

} // namespace evr::pacing
