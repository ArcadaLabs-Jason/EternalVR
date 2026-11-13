#pragma once

// How a pickup animation's camera is shown in the headset (ETERNALVR_PICKUP_CAMERA, docs/VR_HEAD_TRACKED.md,
// docs/rig-findings/camera-animations.md section 6). A pickup (a Praetor token, a Sentinel Crystal, a rune,
// the automap station: a sync animation whose entityDef is `interact/...`, comfort::isKillSync false) takes
// the view for a few seconds and turns the game's camera on its own. The options:
// - Still (the default): the head replaces the camera's rotation, as in play; its yaw turns the view only
//   once it is more than 15 degrees off the player's view (xr_math::drivenCamera), as for any camera the
//   game drives. The eye goes where the game's camera goes in both.
// - Play: the camera's turn away from the player's view angles (pitch, yaw and roll) goes on top of the head
//   from the pickup's start to the end of its forced view, so the view turns as the flat game's camera turns
//   while the head keeps turning it from there.
// No OpenXR or Windows here.

#include "common/vector.hpp"
#include "features/comfort/glory_kill.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::comfort {

enum class PickupCamera { Still, Play };

// "still" or "play", any case, surrounding spaces ignored; nullopt otherwise.
std::optional<PickupCamera> parsePickupCamera(std::string_view text);
const char* pickupCameraName(PickupCamera camera);

// Play: the heading the view keeps across a pickup whose camera is played on top of the head. The camera's
// cut to the animation's start is left out (xr_math::CameraTurn), but as the pickup ends the game sets the
// player's view angles to where its camera ended, cut included: taken as the body's new heading, that turned
// the view in one frame (the automap station's 45 degree turn, docs/rig-findings/camera-animations.md section
// 7). So the body keeps the heading it had before the pickup while it runs, and for a short window after it
// head aim turns the game's aim back to that heading, as after a glory kill shown steady. On the rig the
// game sets the angles 47 to 63 ms before the forced view ends and the episode ends one or two game frames
// after that, so the set always falls in the pickup and the window starts after it.
//
// Not after a pickup that moved the player away (further than kMovedMetres from where it started), across a
// map load, or one the player died in: the kept heading belongs to another place then, and the view takes the
// game's. No pickup measured on the rig moved the player that far (the Slayer gate's nest teleports into its
// arena 2.5 s after its pickup, as a forced view of its own): the distance is a safety net for one that
// would. Time with a menu up does not count toward the window. A stick turn in the window (head aim with the
// controllers, or gamepad mode) is overridden by the kept heading.
class PickupHeading {
public:
    // After the pickup, how long head aim keeps turning the game's aim back to the held heading.
    static constexpr double kRestoreSeconds = kHeadingRestoreSeconds;
    // A pickup moves the player up to 5.4 m on the rig (a gore nest's sync from the nest's middle): a margin
    // over that. Not reached by any pickup measured; with no world scale (0 or less) it is not checked.
    static constexpr float kMovedMetres = 10.0f;

    // One game view, before the body is built.
    struct Frame {
        bool started = false; // the pickup's episode starts this view
        bool ended = false;   // the previous view was its last
        double seconds = 0.0; // a monotonic clock
        Vec3 eye{};           // the game's eye (world units)
        float unitsPerMetre = 1.0f;
        std::uint32_t mapLoads = 0; // the map loads seen so far
        bool dead = false;          // the player is dead
        bool menu = false;          // a menu is up (the game paused)
    };
    // Why the last pickup's heading was not restored.
    enum class Dropped { None, Moved, MapLoad, Died };

    void step(const Frame& frame);
    // Each game view, after the body is built: its yaw and whether it is the player's own heading (head aim
    // gave it, not a camera the game drives or a menu's hold). Kept as the heading to hold only outside a
    // pickup and its restore window. Under a menu (step's Frame::menu) a body that is not the player's own
    // leaves the heading from before the menu as it was: the Sentinel Crystal's sync starts as its upgrade
    // menu closes.
    void noteBody(float yaw, bool own);

    // While the pickup runs: the heading the body keeps; nullopt without one.
    [[nodiscard]] std::optional<float> held() const { return running_ ? held_ : std::nullopt; }
    // For kRestoreSeconds after the pickup: the heading head aim turns the game's aim back to.
    [[nodiscard]] std::optional<float> restore(double seconds) const;
    // Set when a pickup ended: why its heading was dropped (None too when none was held), and how far the
    // player moved (metres; 0 with no world scale).
    [[nodiscard]] Dropped dropped() const { return dropped_; }
    [[nodiscard]] float movedMetres() const { return moved_; }

private:
    bool running_ = false;
    std::optional<float> last_; // the player's own heading in the last view outside a pickup
    std::optional<float> held_;
    double restoreUntil_ = -1.0;
    double lastSeconds_ = 0.0;
    Vec3 startEye_{};
    std::uint32_t startMapLoads_ = 0;
    std::uint32_t mapLoadsNow_ = 0;  // this view's map loads (step), for noteBody
    bool menuNow_ = false;           // a menu is up in this view (step), for noteBody
    std::uint32_t lastMapLoads_ = 0; // the map loads when last_ was noted
    bool died_ = false;
    Dropped dropped_ = Dropped::None;
    float moved_ = 0.0f;
};

// Camera moves: the game's own forced view of the SetViewAngles that ends a pickup with no forced view of its
// own (the Sentinel Crystal's: the set comes with the view the sync flag clears, its 3-frame forced view
// after the pickup's episode ended). A forced view that starts within kStartSeconds of the episode's end is
// taken for it until it ends; a pause menu, or a forced view that starts later (a glory kill), is not.
class EndSetWindow {
public:
    static constexpr double kStartSeconds = 0.1;

    // Each game view, before head aim: whether the pickup's episode ended with the previous view, the forced-
    // view gate as the previous view left it, a monotonic clock.
    void step(bool ended, bool forcedBefore, double seconds);
    // Whether this view's forced view (`forcedNow`, the gate after this view's update) is that set's.
    [[nodiscard]] bool covers(bool forcedNow, double seconds) const;

private:
    double since_ = -1.0; // when the last pickup ended (-1: no window)
    bool forced_ = false; // its forced view has started
};

// The heading the body keeps this game view, in order: the pickup's while it runs (Camera moves, head aim
// gave the body), a glory kill shown steady's, the pickup's restore heading on the game's forced view of its
// ending set (EndSetWindow; head aim's own views in the window turn the aim back instead). nullopt: none.
struct KeptHeading {
    std::optional<float> pickupHeld;
    std::optional<float> gloryHeld;
    std::optional<float> pickupRestore;
    bool headAimed = false;
    bool endSetView = false;
};
std::optional<float> keptHeading(const KeptHeading& k);

} // namespace evr::comfort
