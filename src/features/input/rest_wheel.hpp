#pragma once

// The thumb-rest weapon wheel (docs/VR_CONTROLLERS.md, "The thumb-rest wheel"; DECISIONS T-118).
//
// A thumb resting on its controller's thumb rest turns the other hand's stick into a weapon picker, beside
// the turn stick's own down hold and the slot bindings, which stay as they are. The rest is a touch sensor
// (Touch controllers), or with ETERNALVR_THUMBREST_FACE_TOUCH a face button's touch
// (rest_touch_bindings.hpp).
//
// Modes (ETERNALVR_THUMBREST_WHEEL; off when unset, ControllerSettings):
// - edge: touch, then push. A thumb landing on a rest arms the other stick for kEdgeWindowSeconds
//   (ETERNALVR_THUMBREST_WINDOW): pushing that stick out of the centre in that time starts picking. A landing
//   counts only when both sticks are centred at that moment, so a thumb that lands while the other stick
//   moves or turns does nothing, and a thumb left resting does not stop the other stick turning or moving.
//   Nor does a thumb that comes straight from its own controls: a landing within kOwnButtonVoidSeconds of
//   that hand's A/B (X/Y) going up (a jump or a dash), or within kOwnStickVoidSeconds of its own stick being
//   out of the centre (turning or walking, then the thumb resting), opens no window. After a pick (not a
//   cancel), the window opens again once the stick is back in the centre (picks in a row).
// - full: while touched. For as long as the thumb rests, the other stick picks weapons and neither moves nor
//   turns.
// - extreme: the turn stick is the wheel. It always picks weapons; resting the other thumb on its rest gives
//   it back its turning, chainsaw and quick switch.
// - off (default).
//
// Picking (ETERNALVR_THUMBREST_PICK): the stick has to stay kDwellSeconds in one of the eight directions,
// at least kSelectThreshold out, before anything happens: under wheel a shorter flick presses nothing (under
// slots a flick past kFlickThreshold picks at once). Then
// - wheel (default): the game's wheel is held (WeaponWheel) and the stick points at it as under the stick's
//   own hold; letting go of the stick (or lifting the thumb) closes it and the game picks the highlighted
//   weapon. Once the wheel is open there is no cancel (the game keeps its highlight). The wheel is held at
//   least the game's own open delay plus kWheelOpenMarginSeconds, never less than kMinWheelHoldSeconds
//   (wheelHoldSeconds), so a quick pick is never taken for a quick switch;
// - slots: the direction is remembered and the wheel stays closed; letting go presses that direction's
//   weapon slot for one frame (weapon_directions.hpp).
//
// The stick a route uses neither turns, moves nor fires its gestures from the frame it leaves the centre,
// which comes before any turn: a snap fires at 0.70 and a smooth turn starts at 0.35, the arming at
// kArmDeadzone. After the route, that stick stays out of play until it is back in the centre.
//
// Blocked (a menu, the game suppressing buttons, a forced view, a cutscene, piloting a demon, the wheel
// held by another route): nothing arms, picking is cancelled, and an open wheel closes.
//
// Pure, no clock of its own: the mapper feeds it once per frame (input_mapper.hpp).

#include "features/input/axis2.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/rest_wheel_output.hpp"
#include "features/input/weapon_directions.hpp"
#include "features/input/wheel_mouse.hpp"
#include "game/eternal/game_action.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace evr::input {

inline constexpr float kRestDebounceSeconds = 0.06f;
inline constexpr float kEdgeWindowSeconds = 0.5f;
inline constexpr float kMinEdgeWindowSeconds = 0.2f;
inline constexpr float kMaxEdgeWindowSeconds = 1.0f;
// Within this the stick counts as centred (the turn stick's centre radius and the snap re-arm).
inline constexpr float kArmDeadzone = 0.25f;
// A direction counts from this deflection (the wheel pointer's own threshold, wheel_mouse.hpp).
inline constexpr float kSelectThreshold = 0.5f;
inline constexpr float kDwellSeconds = 0.10f;
// Weapon by direction: a stick this far out counts at once, without the dwell, so a flick from the thumb rest
// picks (Jason's headset test, 2026-10-09: "thumb rest + flick"); the direction is where the flick ends up
// (kOutwardStep).
inline constexpr float kFlickThreshold = 0.85f;
// A direction is kept until the stick is this far past the edge of its eighth, once the stick stops going
// further out.
inline constexpr float kOctantHysteresisDegrees = 7.5f;
// While the stick goes further out than it has been in this pick (by more than this), it points at the eighth
// it is in now, without the hysteresis: a flick that curves on its way out picks the eighth it reaches, not
// the one it crossed kSelectThreshold in (a player report, 2026-10-09: picks decided by the first
// movement).
inline constexpr float kOutwardStep = 0.02f;
// The game opens the wheel 0.18 s after the press by default and the wheel pointer moves from 0.25 s.
inline constexpr float kMinWheelHoldSeconds = 0.30f;
// The wheel is held this long past the game's own open delay (weaponWheel_HoldTimeForOpeningWheel).
inline constexpr float kWheelOpenMarginSeconds = 0.12f;
inline constexpr float kMaxWheelHoldSeconds = 2.5f;
// Edge: a landing this soon after the hand's own face button went up, or its own stick was out of the
// centre, comes from the player's own controls and opens no window.
inline constexpr float kOwnButtonVoidSeconds = 0.4f;
inline constexpr float kOwnStickVoidSeconds = 0.4f;
// Picks in a row: the stick stays centred this long before the window opens again.
inline constexpr float kRearmGapSeconds = 0.05f;
// Counted for the log (RestWheelStats): a thumb back on its rest this soon after the rest let go, and a stick
// out of the centre no longer than this when the rest registered (the flick began with the touch).
inline constexpr float kQuickReturnSeconds = 0.25f;
inline constexpr float kRecentStickSeconds = 0.2f;

enum class RestWheelMode : std::uint8_t {
    Off,
    Edge,
    Full,
    Extreme,
};

enum class RestWheelPick : std::uint8_t {
    Wheel,
    Slots,
};

// The least time the wheel is held from its press: the game's open delay
// (weaponWheel_HoldTimeForOpeningWheel, in milliseconds) plus kWheelOpenMarginSeconds, at least
// kMinWheelHoldSeconds and at most kMaxWheelHoldSeconds. Without a usable delay (not found, not finite, not
// positive), kMinWheelHoldSeconds.
float wheelHoldSeconds(std::optional<float> openDelayMilliseconds);

const char* restWheelModeName(RestWheelMode mode);
const char* restWheelPickName(RestWheelPick pick);

struct RestWheelSettings {
    RestWheelMode mode = RestWheelMode::Edge;
    RestWheelPick pick = RestWheelPick::Wheel;
    float windowSeconds = kEdgeWindowSeconds;
    WeaponDirections directions = kDefaultWeaponDirections;
};

// What the control map gives the wheel: which hands can sense a resting thumb and which sticks have a role.
struct RestWheelHands {
    std::array<bool, 2> hasRest{}; // indexed by Hand
    std::optional<Hand> moveStick;
    std::optional<Hand> turnStick;
};

struct RestWheelFrame {
    std::array<bool, 2> rest{}; // a thumb on the hand's rest now (debounced here)
    std::array<Axis2, 2> sticks{};
    std::array<bool, 2> faceButtons{}; // the hand's primary or secondary button is pressed
    bool blocked = false;
    // The least time the wheel is held from its press (wheelHoldSeconds).
    float minWheelHoldSeconds = kMinWheelHoldSeconds;
};

// What each rest's touch sensor did, counted from the start for the log (vkcore/rest_wheel.cpp prints the
// change every 10 s), so a player's log shows whether touches go missing: indexed by the rest's hand.
struct RestWheelStats {
    std::array<std::uint32_t, 2> rawTouches{}; // the sensor reported a touch
    std::array<std::uint32_t, 2>
        shortTouches{}; // ... gone again within kRestDebounceSeconds: never registered
    std::array<std::uint32_t, 2> bridgedGaps{};  // a registered touch lost for less than kRestDebounceSeconds
    std::array<std::uint32_t, 2> landings{};     // touches that registered
    std::array<std::uint32_t, 2> quickReturns{}; // ... within kQuickReturnSeconds of the rest letting go
    // Landings with the other stick out of the centre (under while touched it then waits for the centre), and
    // of those, the ones whose stick had been out no longer than kRecentStickSeconds.
    std::array<std::uint32_t, 2> stickOut{};
    std::array<std::uint32_t, 2> stickOutRecent{};
};

class RestWheel {
public:
    // A window outside kMinEdgeWindowSeconds..kMaxEdgeWindowSeconds (or not finite) falls back to the
    // default.
    RestWheel(RestWheelSettings settings, RestWheelHands hands);

    // `dtSeconds`: non-finite or negative counts as zero. A non-finite stick counts as its last finite value.
    RestWheelOutput update(const RestWheelFrame& frame, float dtSeconds);

    // Presses begun under a menu are used up: picking is cancelled, an open wheel closes, and every landing
    // is forgotten (a thumb still resting has to land again).
    void cancel();

    // Whether the mode can work with these hands: edge and full need a rest on a hand whose other stick moves
    // or turns, extreme a turn stick and a rest on the other hand.
    [[nodiscard]] bool usable() const { return usable_; }
    [[nodiscard]] const RestWheelSettings& settings() const { return settings_; }
    [[nodiscard]] const RestWheelHands& hands() const { return hands_; }
    [[nodiscard]] const RestWheelStats& stats() const { return stats_; }

private:
    static constexpr float kLongAgoSeconds = 1000.0f;

    enum class Route : std::uint8_t {
        Idle,
        Taken,    // the stick is the wheel's, nothing picked yet (full, extreme)
        Pointing, // picking, nothing pressed yet
        Open,     // the game's wheel is held
        Closing,  // the wheel is held to its minimum time, then let go
    };

    struct Rest {
        bool raw = false;
        bool touched = false; // debounced
        float rawSeconds = 0.0f;
        float sinceLanding = 0.0f;
        float sinceLift = kLongAgoSeconds; // debounced
        bool landingValid = false;
        bool chainPending = false; // a pick ended with the thumb still resting (edge)
    };

    void
    debounce(const RestWheelFrame& frame, float dt, std::array<bool, 2>& landed, std::array<bool, 2>& lifted);
    void continueRoute(bool blocked, float dt, float minHold, RestWheelOutput& out);
    void startRoute(const std::array<bool, 2>& crossed, RestWheelOutput& out);
    void arm(RestWheelOutput& out);
    void point(float dt, RestWheelOutput& out);
    void endPointing(bool pick, RestWheelOutput& out);
    // `picked`: a weapon was picked, so under edge the window opens again for the next pick.
    void finishRoute(bool picked);
    [[nodiscard]] bool restGone() const;
    [[nodiscard]] bool hasRole(Hand hand) const;
    [[nodiscard]] Hand restoreHand() const;
    [[nodiscard]] bool out(Hand hand) const;
    static void note(RestWheelOutput& out, RestWheelEvent event);

    RestWheelSettings settings_;
    RestWheelHands hands_;
    bool usable_ = false;
    std::array<Rest, 2> rests_{};
    std::array<Axis2, 2> sticks_{};
    std::array<bool, 2> latched_{};
    // Seconds each stick has been centred, and since each hand's face button was last down (long ago at the
    // start).
    std::array<float, 2> centredSeconds_{kLongAgoSeconds, kLongAgoSeconds};
    std::array<float, 2> sinceFaceButton_{kLongAgoSeconds, kLongAgoSeconds};
    std::array<float, 2> outSeconds_{}; // seconds each stick has been out of the centre
    RestWheelStats stats_;
    Route route_ = Route::Idle;
    Hand restHand_ = Hand::Left;
    Hand stickHand_ = Hand::Right;
    WheelDirection octant_ = WheelDirection::None;
    float dwellSeconds_ = 0.0f;
    WheelDirection committed_ = WheelDirection::None;
    bool flickCommitted_ = false; // committed_ came from a flick (not the dwell): it follows the stick out
    float peak_ = 0.0f;           // the furthest the stick has been out in this pick
    Axis2 lastUnit_;
    float pressSeconds_ = 0.0f;
};

} // namespace evr::input
