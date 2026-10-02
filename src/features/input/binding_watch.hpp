#pragma once

// Whether the runtime binds the mod's controls at all.
//
// SteamVR keeps a controller binding per app key, and the game runs as steam.app.782330, a key every
// OpenXR mod of DOOM Eternal shares. A binding chosen there for another mod (a workshop or saved binding)
// names other actions: the session runs, xrSyncActions succeeds and SteamVR reports an interaction profile,
// yet none of our actions is active and the pose spaces never locate. The XR side lists the bound sources
// of the gameplay actions once a profile has settled, and the watch here says when; the verdict on those
// lists, and the warning when no hand pose has ever been valid while the headset was tracked, are decided
// here too.
//
// Nothing here calls OpenXR; the XR side feeds what each sync saw.

#include "features/input/controller_state.hpp"
#include "features/input/xr_action_set.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

// The bound sources are listed once a hand's profile has been reported for this long (the runtime applies
// its binding when it reports the profile).
inline constexpr double kBindingSettleSeconds = 2.0;
// No hand pose valid for this long (synced, the head tracked, a profile reported) is warned about once.
inline constexpr double kPosesNeverValidSeconds = 10.0;
// A longer gap between two syncs (a pause, a lost session) does not count towards that time.
inline constexpr double kBindingWatchMaxStep = 0.5;

// What one sync saw.
struct BindingWatchSample {
    double seconds = 0.0;            // a monotonic clock
    bool synced = false;             // the session is focused and xrSyncActions succeeded
    bool headTracked = false;        // the view space located (the headset is worn and tracked)
    std::array<bool, 2> profile{};   // the runtime reports an interaction profile for the hand (by Hand)
    std::array<bool, 2> poseValid{}; // the hand's aim or grip pose located
};

struct BindingWatchActions {
    bool listSources = false;     // list the bound sources now (once)
    bool posesNeverValid = false; // warn now that no hand pose has been valid (once)
};

class BindingWatch {
public:
    BindingWatchActions update(const BindingWatchSample& sample);

    // A hand pose has been valid at least once.
    [[nodiscard]] bool anyPoseSeen() const { return poseSeen_; }

private:
    std::optional<double> profileSince_; // when a profile was first reported, while one is
    std::optional<double> last_;         // the previous sample's time
    double waiting_ = 0.0;               // seconds synced and tracked with a profile and no pose yet
    bool waitingPrev_ = false;           // the previous sample counted towards waiting_
    bool listed_ = false;
    bool warned_ = false;
    bool poseSeen_ = false;
};

// The sources the runtime binds to one action (xrEnumerateBoundSourcesForAction), as paths.
struct BoundAction {
    XrActionId id = XrActionId::Trigger;
    std::vector<std::string> sources;
};

struct BindingVerdict {
    std::size_t actions = 0;                        // actions listed
    std::size_t boundActions = 0;                   // actions with at least one source
    std::array<std::vector<XrActionId>, 2> unbound; // per hand (by Hand): the actions with no source on it
    [[nodiscard]] bool noneBound() const { return actions > 0 && boundActions == 0; }
};

// The hand a source path belongs to ("/user/hand/left/input/trigger/value"), or nullopt.
std::optional<Hand> sourceHand(std::string_view path);

BindingVerdict judgeBoundSources(std::span<const BoundAction> actions);

// The action names of `ids` ("trigger, aim_pose"), or "none".
std::string actionNames(std::span<const XrActionId> ids);

// What the player should do when the runtime binds none of the mod's controls: SteamVR's binding UI for
// SteamVR, the runtime's own binding settings otherwise.
std::string_view unboundAdvice(std::string_view runtimeName);

} // namespace evr::input
