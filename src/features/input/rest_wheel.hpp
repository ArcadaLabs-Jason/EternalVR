#pragma once

// The thumb-rest weapon wheel (docs/VR_CONTROLLERS.md, "The thumb-rest wheel"; DECISIONS T-118).
//
// A thumb resting on its controller's thumb rest turns the other hand's stick into a weapon picker, beside
// the turn stick's own down hold and the slot bindings, which stay as they are. The rest is a touch sensor
// (Touch controllers), or with ETERNALVR_THUMBREST_FACE_TOUCH a face button's touch
// (rest_touch_bindings.hpp).
//
// The touch (DECISIONS T-120; some sensors flicker on and off very fast):
// - Landing: a touch registers on its first frame, unless the thumb is elsewhere on that controller then: a
//   face button pressed or its own stick out of the centre (hard evidence), or a touch sensor beside the rest
//   (the stick's, the face buttons' where they are not the rest, the trackpad's; the mapper reads them). Then
//   it registers once the thumb is no longer elsewhere, or once the contact (the touch with gaps shorter than
//   the hold-over bridged) has lasted kRestDebounceSeconds. A touch sensor on for kStuckVetoSeconds while the
//   rest keeps coming and going sees the resting thumb itself: it no longer holds a landing back.
// - Lift: a touch lost for less than the hold-over is a forgiven gap. The hold-over is
//   ETERNALVR_THUMBREST_HOLDOVER (kRestHoldOverSeconds by default), but never longer than the touch has been
//   on since its landing, nor shorter than kRestHoldOverFloorSeconds, so a brush lifts soon. A longer loss
//   lifts the thumb; at once when the thumb went elsewhere after the landing: hard evidence at once, a touch
//   sensor once it has been on kVetoTouchSteadySeconds (one flickering with the rest cuts nothing). The thumb
//   going elsewhere while the touch is still sensed changes nothing.
// - Under edge, a forgiven gap of at least kRestDebounceSeconds is a fresh landing (the window opens again)
//   when the other stick has been centred kOwnStickVoidSeconds, nothing is being picked and nothing was
//   cancelled since the landing.
// - A stick that leaves the centre during a hold-over, where the lift would change whose it is, waits for the
//   touch to decide: held from the game meanwhile, it starts the pick when the touch comes back (under
//   extreme, on the restore rest, when the hold-over lapses), and otherwise goes to the game at once, to the
//   turn stick's gestures as a fresh push (RestWheelOutput::deferred). A push back in the centre before the
//   touch decides, or on its way back, is not lost: its furthest point is played once, to the wheel or to the
//   gestures (RestWheelOutput::replay). A snap pushed during a hold-over so waits for the touch, at most the
//   hold-over: a flick by direction begun in a dropout picks.
// - Edge and full need the rest only to start a pick: a pick under way, and an open wheel, end on the stick
//   (or blocked), not on a lift, except that a lift cut by hard evidence (the thumb pressed a button or
//   pushed its own stick) cancels a pick still pointing with nothing committed. A push begun at most
//   kTurnBeforeLiftSeconds before the thumb left was a turn: the stick goes to the game as a fresh push; an
//   older one stays out of play until the centre. Under extreme the other rest ends a pick
//   under way only once its touch has been on kRestDebounceSeconds, so a brush does not close an open wheel
//   on its highlight.
// - A stick that leaves the centre on the frame a route starts or ends is not kept out of play: it goes to
//   the game as a fresh push, or to the wheel where the mode now gives it the stick.
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
//   turns. A stick that left the centre after the contact began, or kHeldLeadSeconds before (a flick and a
//   touch that start together: a player report, 2026-10-10, quick switches from a down flick before the rest
//   registered), picks when the touch registers instead of waiting for the centre; while a touch is sensed
//   but not registered yet (the thumb elsewhere), that stick is held back from the game, and when the contact
//   lapses unregistered, the stick is the game's again. A stick already out otherwise is left to the game
//   until the touch has been on kRestDebounceSeconds, gaps not counted (a brush while turning), then waits
//   for the centre.
// - extreme: the turn stick is the wheel, for standing players who turn with their body (the owner's spec,
//   2026-10-09/10). It picks weapons while neither thumb rests; resting the other thumb on its rest gives it
//   back its turning, chainsaw and quick switch, and a pick in progress is cancelled once that touch has
//   been registered kRestDebounceSeconds (an open game wheel still closes on its highlight); resting the turn
//   hand's thumb instead makes the other stick the picker (it does not walk meanwhile).
// - off (default).
//
// Picking (ETERNALVR_THUMBREST_PICK): the stick has to stay kDwellSeconds in one of the eight directions,
// at least kSelectThreshold out, before anything happens: under wheel a shorter flick presses nothing (under
// slots a flick past kFlickThreshold picks at once). Then
// - wheel (default): the game's wheel is held (WeaponWheel) and the stick points at it as under the stick's
//   own hold; letting go of the stick (not lifting the thumb) closes it and the game picks the highlighted
//   weapon. Once the wheel is open there is no cancel (the game keeps its highlight). The wheel is held at
//   least the game's own open delay plus kWheelOpenMarginSeconds, never less than kMinWheelHoldSeconds
//   (wheelHoldSeconds), so a quick pick is never taken for a quick switch;
// - slots: the direction is remembered and the wheel stays closed; letting go presses that direction's
//   weapon slot for one frame (weapon_directions.hpp). A direction with no weapon is no direction of its own:
//   its eighth goes to its nearest neighbours that have one (slotDirection), the dwell and the hysteresis
//   included.
//
// The stick a route uses neither turns, moves nor fires its gestures from the frame it leaves the centre,
// which comes before any turn: a snap fires at 0.70 and a smooth turn starts at 0.35, the arming at
// kArmDeadzone. After the route, that stick stays out of play until it is back in the centre.
//
// Blocked (a menu, the game suppressing buttons, a forced view, a cutscene, piloting a demon, the wheel
// held by another route): nothing arms, picking is cancelled, an open wheel closes, and a stick the wheel
// holds goes back to the game (once it has been back in the centre).
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

// A touch begun with the thumb elsewhere registers once it has lasted this long.
inline constexpr float kRestDebounceSeconds = 0.06f;
// A touch lost for less than this is a gap, not a lift (the default of ETERNALVR_THUMBREST_HOLDOVER).
inline constexpr float kRestHoldOverSeconds = 0.15f;
inline constexpr float kMinRestHoldOverSeconds = 0.10f;
inline constexpr float kMaxRestHoldOverSeconds = 0.30f;
// The hold-over is never shorter than this, however briefly the touch was on.
inline constexpr float kRestHoldOverFloorSeconds = 0.05f;
// A touch sensor beside the rest cuts a hold-over short only once it has been on this long.
inline constexpr float kVetoTouchSteadySeconds = 0.04f;
// A touch sensor on this long while the rest comes and goes no longer holds a landing back.
inline constexpr float kStuckVetoSeconds = 1.5f;
// A pick still pointing when the thumb leaves for its own controls, its push begun at most this long before
// the rest's touch first went (a flicker back does not restart it), was a turn (the push and the thumb
// leaving together): the stick goes to the game.
inline constexpr float kTurnBeforeLiftSeconds = 0.05f;
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
// A direction is kept until the stick is this far past the edge of its eighth (under slots, of its part of
// the circle), once the stick stops going further out.
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
// While touched: a stick that left the centre no earlier than this before its rest's contact began is held
// while the touch registers (the flick and the touch start together, in either order).
inline constexpr float kHeldLeadSeconds = 0.10f;

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
    float holdOverSeconds = kRestHoldOverSeconds;
};

// What the control map gives the wheel: which hands can sense a resting thumb and which sticks have a role.
struct RestWheelHands {
    std::array<bool, 2> hasRest{}; // indexed by Hand
    std::optional<Hand> moveStick;
    std::optional<Hand> turnStick;
};

struct RestWheelFrame {
    std::array<bool, 2> rest{}; // a thumb on the hand's rest now (debounced here)
    // A touch sensor says the thumb is elsewhere on that controller: its stick touched, or a face button or
    // the trackpad touched where those are not the rest. A face button pressed and the hand's own stick out
    // of the centre count too, as hard evidence (read here).
    std::array<bool, 2> thumbElsewhere{};
    std::array<Axis2, 2> sticks{};
    std::array<bool, 2> faceButtons{}; // the hand's primary or secondary button is pressed
    bool blocked = false;
    // The turn stick's own down sweep holds the game's wheel: as blocked, except that it does not stop a
    // stick being held while a rest's touch registers (holding the stick cancels the sweep).
    bool sweepDown = false;
    // The least time the wheel is held from its press (wheelHoldSeconds).
    float minWheelHoldSeconds = kMinWheelHoldSeconds;
};

// What each rest's touch sensor did, counted from the start for the log (vkcore/rest_wheel.cpp prints the
// change every 10 s), so a player's log shows whether touches go missing: indexed by the rest's hand.
struct RestWheelStats {
    std::array<std::uint32_t, 2> rawTouches{};     // the sensor reported a touch
    std::array<std::uint32_t, 2> shortTouches{};   // ... gone again within kRestDebounceSeconds (flutter)
    std::array<std::uint32_t, 2> vetoedLandings{}; // ... begun with the thumb elsewhere: held back
    std::array<std::uint32_t, 2> bridgedGaps{};    // a registered touch lost under its hold-over
    std::array<std::uint32_t, 2> landings{};       // touches that registered
    std::array<std::uint32_t, 2> quickReturns{};   // ... within kQuickReturnSeconds of the rest letting go
    std::array<std::uint32_t, 2> cutLifts{};       // lifts made at once: the thumb went elsewhere
    std::array<std::uint32_t, 2> stuckVetoes{};    // landings a stuck touch sensor no longer held back
    // Landings with the other stick out of the centre (under while touched it then waits for the centre), and
    // of those, the ones whose stick had been out no longer than kRecentStickSeconds.
    std::array<std::uint32_t, 2> stickOut{};
    std::array<std::uint32_t, 2> stickOutRecent{};
    // Pushes of the other stick held while the touch registered (under while touched) or during a hold-over
    // until the touch came back, then picking.
    std::array<std::uint32_t, 2> heldPushes{};
};

class RestWheel {
public:
    // A window outside kMinEdgeWindowSeconds..kMaxEdgeWindowSeconds, or a hold-over outside
    // kMinRestHoldOverSeconds..kMaxRestHoldOverSeconds (or not finite), falls back to the default.
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
        bool touched = false;       // debounced
        bool elsewhere = false;     // evidence that cuts a hold-over short, last frame
        bool wentElsewhere = false; // ... and that began after the landing
        bool vetoTouch = false;     // a touch sensor beside the rest, last frame
        bool vetoToggled = false;   // ... and the rest came on again while it was
        float vetoSeconds = 0.0f;   // ... on without a break
        float rawSeconds = 0.0f;    // since the sensor last changed
        // Since the touch first went in this loss: a flicker back that is forgiven does not restart it (the
        // loss is over once the touch is back for the debounce, or the rest lands or lifts).
        bool gap = false;
        float gapSeconds = 0.0f;
        // The contact: the touch with gaps shorter than the hold-over bridged, and since its first frame.
        bool contact = false;
        float contactSeconds = 0.0f;
        // The sensor on since the landing (gaps not counted): the hold-over is never longer, and the brush
        // guards count this.
        float onSeconds = 0.0f;
        bool cancelled = false; // a pick from this rest was cancelled since the landing (edge)
        float sinceLanding = 0.0f;
        float sinceLift = kLongAgoSeconds; // debounced
        bool landingValid = false;
        bool chainPending = false; // a pick ended with the thumb still resting (edge)
    };

    // `hardLift`: lifted at once on hard evidence (a press, its own stick out).
    void debounce(const RestWheelFrame& frame,
                  float dt,
                  std::array<bool, 2>& landed,
                  std::array<bool, 2>& lifted,
                  std::array<bool, 2>& hardLift);
    // Under while touched: whether `hand`'s rest holds the other stick while its touch registers.
    [[nodiscard]] bool holds(Hand hand, bool blocked) const;
    // A push of `stick` during `rest`'s hold-over: whether it waits for the touch (the lift would change
    // whose it is), and once the touch has decided, whether the wheel takes it.
    [[nodiscard]] bool pushWaits(Hand rest, Hand stick) const;
    [[nodiscard]] bool wheelTakes(Hand rest, Hand stick) const;
    // `fresh`: a held push the wheel now takes; `toWheel`: ... back in the centre, played to the wheel this
    // frame from its furthest point.
    void holdOverPushes(const std::array<bool, 2>& crossed,
                        bool stopped,
                        std::array<bool, 2>& fresh,
                        std::array<std::optional<Axis2>, 2>& toWheel,
                        RestWheelOutput& o);
    void continueRoute(bool blocked, float dt, float minHold, RestWheelOutput& out);
    // `fresh`: a push held during a hold-over that the wheel now takes.
    void
    startRoute(const std::array<bool, 2>& crossed, const std::array<bool, 2>& fresh, RestWheelOutput& out);
    void arm(RestWheelOutput& out);
    void point(float dt, RestWheelOutput& out);
    void endPointing(bool pick, RestWheelOutput& out);
    // `picked`: a weapon was picked, so under edge the window opens again for the next pick.
    void finishRoute(bool picked);
    [[nodiscard]] bool restGone() const;
    // Under extreme: the stick that picks now (the turn stick, the other stick while only the turn hand's
    // thumb rests, or none while the other thumb rests).
    [[nodiscard]] std::optional<Hand> extremeStick() const;
    // ... were `hand`'s rest touched or not.
    [[nodiscard]] std::optional<Hand> extremeStickIf(Hand hand, bool touched) const;
    // Under extreme a pick in progress is cancelled by the other thumb landing (it wants turning back).
    [[nodiscard]] bool restCancels() const;
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
    // Under while touched: the stick is held while the other hand's rest has a touch that has not registered
    // yet (or registered this frame and no route took the stick).
    std::array<bool, 2> held_{};
    // A stick pushed during the other hand's hold-over, held until the touch decides (pushWaits); and this
    // frame, a stick such a wait gave to the game, which a route that ends does not keep out of play.
    std::array<bool, 2> holdOver_{};
    std::array<bool, 2> givenBack_{};
    std::array<Axis2, 2> pushPeak_{}; // the furthest a held push went
    // This frame: the stick left the centre, and a stick handed to the game as a fresh push (to the gestures
    // it is centred for this frame, so they claim it anew on the next).
    std::array<bool, 2> crossedNow_{};
    std::array<bool, 2> handBack_{};
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
