#pragma once

// Two-handed aim (docs/VR_HANDS_HUD.md, "Two-handed aim"; docs/ROADMAP.md feature step 5): the off hand takes
// the gun's support point (the fore-end, the barrel shroud) and the gun aims along the line between the
// hands.
//
// Each weapon that takes a support grip has a row (data/weapons/viewmodel_offsets.toml, [two_hand]): where
// the off glove's grip sits on the gun as the game's own idle holds it, from the weapon controller's grip, in
// the weapon hand's aim frame. With the weapon in the left hand the row is reflected across the gun's middle.
//
// - Engage: the off hand's grip squeezed with its controller's grip on the gun: within kTwoHandCaptureMetres
//   of the gun's support line (from the weapon hand's grip through the support point the one-handed aim puts
//   on the gun), at least kTwoHandEngageLineMetres along it and at most kTwoHandCaptureMetres past the
//   support point, and within kTwoHandCaptureDegrees of it as seen from the weapon hand's grip (the most the
//   gun turns at a grab). So the gun can be taken anywhere along its fore-end and barrel, not only where the
//   game's own hand holds it (the Plasma Rifle's support point is 76 cm ahead of the weapon hand, further
//   than an arm reaches with the other hand on the grip). A squeeze that STARTS on the gun (in that zone, or
//   within kTwoHandArriveMetres of it with the hand arriving, so the zone is reached within
//   kTwoHandArriveSeconds) has the right to take the gun for as long as the grip stays down: the gate may be
//   closed at the press (a mod switch, an equipment throw, a melee) and the hold may end for any reason, and
//   it is taken, or taken again, on the first frame the zone and the gate allow it. A squeeze that started
//   away from the gun (the Flame Belch's, say) never takes it, however the hand drifts in, so one press is
//   the gun's or the binding's, never both (the mapper holds back only a press that has the right).
// - Held: the aim is turned by the shortest turn that takes the gun's grip-to-support direction onto the line
//   from the weapon hand's grip to the off hand's. So when the off hand sits on the gun's support line,
//   nothing moves at the grab; the weapon hand keeps the gun's roll about that line (its twist is kept);
//   and the gun's own angle to that line (the support point sits a few centimetres above or below the bore's
//   line) is the gun's. The off hand may slide along the gun: the hold lasts while the off hand is no more
//   than kTwoHandReleaseMetres past the support point along the line, and the hands are at least
//   kTwoHandMinLineMetres apart (a shorter line would turn the gun a degree per few millimetres of tracking).
//   The off glove sits where the off hand is along the gun (gloveSlide), up to the support point.
// - Smoothed: with the player's aim smoothing on (ETERNALVR_AIM_SMOOTHING, Aim steadiness), the two-handed
//   aim goes through the same filter as the one-handed aim, built from the weapon hand's aim as tracked, so
//   the line's tracking noise is smoothed as the weapon hand's is, and the roll once.
// - Release: the grip let go, the off hand slid past the support point by more than kTwoHandReleaseMetres,
//   the hands too close, the gate closed or a hand lost. All but the grip let go leave the squeeze's right to
//   take the gun as it was (above), so the hold comes back by itself.
// - The turn is eased in at the grab and out at the release over kTwoHandRampSeconds (from wherever it was,
//   so a grab during a release's ease does not jump), so a trigger pulled at either edge does not shoot along
//   a sudden jump of up to 26 degrees (the Rocket Launcher). The ease ends exactly on the two-handed aim, or
//   exactly on the weapon hand's own.
//
// Pure: no OpenXR, Windows or game memory. Room space (OpenXR axes: +X right, +Y up, -Z forward), metres.

#include "common/pose.hpp"
#include "features/input/aim_smoothing.hpp"
#include "features/input/analog_button.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/offhand_policy.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace evr::input {

// The off hand's grip engages within this of the gun's support line (about a palm's width round the
// fore-end), and up to this past the support point.
inline constexpr float kTwoHandCaptureMetres = 0.08f;
// ... and within this of the support line as seen from the weapon hand's grip: the most a grab turns the gun
// (what 8 cm round the combat shotgun's support point gives; 8 cm round the Rocket Launcher's, 16 cm from the
// grip, would turn it by up to 26 degrees).
inline constexpr float kTwoHandCaptureDegrees = 15.0f;
// A held grip lets go when slid past the support point by more than this (twice the capture's reach past it,
// so it does not chatter).
inline constexpr float kTwoHandReleaseMetres = 0.16f;
// A squeeze made within this of the gun's support line (the hand on its way to the gun) keeps the right to
// take the gun for kTwoHandArriveSeconds, to reach the zone in. One and a half times the capture's radius: a
// hand a few centimetres from the fore-end counts as arriving, and a Flame Belch squeeze made with the off
// hand further from the gun does not (the squeeze is then spent on the belch's hold-back, so no wider).
inline constexpr float kTwoHandArriveMetres = 0.12f;
// An arriving hand is already at least kTwoHandEngageLineMetres less this far along the line (the capture's
// 12 cm): nearer the weapon hand than that, it cannot take the gun in the window however it moves, so the
// squeeze is the Flame Belch's. 3 cm is what a slow hand (15 cm a second) covers in kTwoHandArriveSeconds.
inline constexpr float kTwoHandArriveMarginMetres = 0.03f;
// How long an arriving hand has to enter the capture zone after the squeeze starts. Hands close the grip
// while they arrive: a pull of the grip takes ~0.1 s to pass the press threshold, and a hand moving at half a
// metre a second covers the arriving radius in 0.24 s (a metre a second, 0.12 s), so 0.2 s lets a hand that
// starts closing on the way in make it and is still shorter than a deliberate belch squeeze held with the
// hand drifting about.
inline constexpr float kTwoHandArriveSeconds = 0.2f;
// The hands closer than this end the hold: the line's direction would follow a few millimetres of tracking.
inline constexpr float kTwoHandMinLineMetres = 0.10f;
// A hold starts only with the hands at least this far apart, so a hand hovering at the shortest line does not
// take and drop the gun on alternate frames (the Rocket Launcher's support point is 16 cm ahead of the grip,
// so a hand inside its capture radius can be 8 cm from the other).
inline constexpr float kTwoHandEngageLineMetres = 0.12f;
// The turn eases in at a grab and out at a release over this long.
inline constexpr float kTwoHandRampSeconds = 0.07f;

// A [two_hand] row: metres along the weapon hand's aim forward, left and up (id Tech's axes) from its grip.
struct SupportRow {
    float forward = 0.0f;
    float left = 0.0f;
    float up = 0.0f;

    friend constexpr bool operator==(const SupportRow&, const SupportRow&) = default;
};

// The row for the hand holding the weapon: as measured (the weapon in the right hand), or reflected across
// the gun's middle (left negated) with the weapon in the left hand, as the arms swap roles (DECISIONS T-140).
SupportRow supportRowFor(SupportRow row, Hand weaponHand);

// The row in the aim pose's own frame (OpenXR: +X right, +Y up, -Z forward).
Vec3 supportInAim(SupportRow row);

// The support point for the weapon hand's grip at `grip` with its aim's orientation `aim` (room space).
Vec3 supportPoint(Vec3 grip, Quat aim, SupportRow row);

// Where the off hand is against the gun's support line (from `grip` through the support point): metres along
// it from the grip, and metres off it; nullopt when an input is not finite or the row is empty.
struct OnGun {
    float along = 0.0f;
    float off = 0.0f;
};
std::optional<OnGun> onGun(Vec3 grip, Quat aim, Vec3 offGrip, SupportRow row);

// Whether a squeeze there takes the gun (the capture above; the hands' line is checked by the caller).
bool withinCapture(OnGun at, SupportRow row);

// The off glove's move from the game's own grip of the support point to where the off hand is along the gun,
// while held: along the line from `grip` to `offGrip` (the hands' line, which the held gun's support line
// is), back by as much as that line is shorter than `supportLength`; none at or past the support point. Any
// units, the same for all three; zero for a line with no length or inputs that are not finite.
Vec3 gloveSlide(Vec3 grip, Vec3 offGrip, float supportLength);

// The turn (room space, applied after `aim`) that puts the support point on the line from `grip` to
// `offGrip`: the shortest turn taking the gun's grip-to-support direction onto that line. nullopt when the
// hands are closer than kTwoHandMinLineMetres, the row is empty or an input is not finite.
std::optional<Quat> twoHandTurn(Vec3 grip, Quat aim, Vec3 offGrip, SupportRow row);

// The aim turned by twoHandTurn.
std::optional<Quat> twoHandAim(Vec3 grip, Quat aim, Vec3 offGrip, SupportRow row);

// Whether the gun is up in the hand for a two-handed hold: what the arm policy hands the arms to the game for
// (a weapon switch or its raise, a melee, a throw, a custom animation, a sync, hidden hands, the chainsaw) is
// not going on. A reload is not in it: the Super Shotgun reloads after every shot, and the hands stay on the
// gun. Tracking and forced views are judged by the caller.
bool gunUp(ArmSignals signals);

// Why two-handed aim cannot hold this frame (the first that applies, in this order).
enum class TwoHandBlock : std::uint8_t {
    None,
    Off,         // the setting off, or the new arms not in use
    NotHandAim,  // head or view aim (the gun at the head, T-143), or no controllers
    Dead,        // the player is dead (the death screen and the respawn)
    Demon,       // piloting a demon
    ForcedView,  // the game drives the view: cutscenes, glory kills, the Meathook's pull, a melee lunge
    WeaponWheel, // the weapon wheel is up
    Menu,        // a menu or popup holds gameplay input back
    Swimming,    // the swim's fists are in the hands
    GunDown,     // a switch, a raise, a melee, a throw (gunUp)
    NoSupport,   // the held item takes no support grip (no [two_hand] row)
};

const char* twoHandBlockName(TwoHandBlock block);

struct TwoHandGates {
    bool enabled = false;
    bool handAim = false;
    bool dead = false;
    bool demon = false;
    bool forcedView = false;
    bool weaponWheel = false;
    bool menu = false;
    bool swimming = false;
    bool gunUp = false;
    bool support = false;
};

TwoHandBlock twoHandBlock(const TwoHandGates& gates);

struct TwoHandFrame {
    TwoHandBlock block = TwoHandBlock::Off;
    float offGrip = 0.0f; // the off hand's grip, 0..1
    bool weaponTracked = false;
    bool offTracked = false;
    Vec3 weaponGrip; // the weapon hand's grip position
    Quat aim;        // the weapon hand's aim orientation as the one-handed aim uses it (smoothed)
    // The weapon hand's aim orientation as tracked, before the one-handed aim's smoothing: the held aim is
    // built from it and smoothed by the hold's own filter (TwoHandHold's `smoothing`); `aim` when not given.
    std::optional<Quat> trackedAim;
    Vec3 offGripPoint;  // the off hand's grip position
    SupportRow support; // supportRowFor's
    float dt = 0.0f;    // seconds since the last frame (the ease's clock)
    // The held item's name, for a miss's line: read at the press (the view must outlive the update call
    // only).
    std::string_view weapon;
};

// A squeeze on a gun that takes a support grip, ended (the grip let go) without taking the gun: what the
// layer's log line shows.
struct TwoHandMiss {
    float peak = 0.0f;       // the grip's highest level during the squeeze
    bool pressKnown = false; // the off hand and the weapon hand were tracked at the press
    float along = 0.0f;      // at the press: the off hand along the gun's support line from the grip (m)
    float off = 0.0f;        // ... and off it (m)
    TwoHandBlock pressBlock = TwoHandBlock::None; // the gate at the press
    TwoHandBlock endBlock = TwoHandBlock::None;   // ... and at the release
    bool kept = false;                            // the squeeze had the right to take the gun until it ended
    std::string weapon;                           // the held item's name at the press (TwoHandFrame::weapon)
};

enum class TwoHandEnd : std::uint8_t {
    None,
    GripLetGo,
    TooFar,   // slid past the support point by more than kTwoHandReleaseMetres
    TooClose, // the hands within kTwoHandMinLineMetres
    Blocked,  // the gate closed (TwoHandFrame::block)
    Untracked,
};

const char* twoHandEndName(TwoHandEnd end);

struct TwoHandResult {
    bool engaged = false;
    // A squeeze now takes the gun: the gate open, both hands tracked, the off hand within the capture radius,
    // the hands kTwoHandEngageLineMetres apart, and the grip up or squeezed with the right to take the gun
    // (`keeps`). The mapper holds the grip's binding back meanwhile (MapperContext::supportHandOnWeapon).
    bool capturable = false;
    // The grip is down and its squeeze has the right to take the gun (started on it, or arriving), whether
    // or not the gate lets it now: the mapper holds the binding back for the whole squeeze.
    bool keeps = false;
    // The grip is up and a squeeze made now would have the right to take the gun by arriving (the hand within
    // kTwoHandArriveMetres of the support line, the gate one a squeeze may keep its right in): the mapper
    // holds the grip's binding back from the press itself, so the Flame Belch does not fire as the hand comes
    // in.
    bool arrivable = false;
    // The squeeze that just ended (the grip let go) took nothing; see TwoHandMiss.
    std::optional<TwoHandMiss> missed;
    Quat aim;               // the aim to use: two-handed while engaged (eased in), else the frame's own
    Quat target;            // engaged: the two-handed aim the ease goes to
    bool easing = false;    // the turn is still easing in or out
    float distance = -1.0f; // the off hand from the gun's one-handed support line (metres; -1 when not known)
    float along = -1.0f;    // ... and along it from the weapon hand's grip (metres; -1 when not known)
    float slide = 0.0f;     // engaged: the off hand past the support point along the line (metres; < 0 short)
    bool started = false;   // engaged this frame
    TwoHandEnd ended = TwoHandEnd::None; // released this frame, and why
};

class TwoHandHold {
public:
    // `smoothing`: the one-handed aim's filter (aimSmoothingParams of the player's setting), run on the held
    // aim; none for no smoothing.
    explicit TwoHandHold(AnalogThresholds grip = kGripThresholds,
                         std::optional<OneEuroParams> smoothing = std::nullopt);

    TwoHandResult update(const TwoHandFrame& frame);
    // Lets go at once, with no ease, and forgets the grip (a level load, the controllers detached).
    void reset();
    [[nodiscard]] bool engaged() const { return engaged_; }

private:
    AnalogButton grip_;
    bool engaged_ = false;
    bool wasDown_ = true;          // the grip was down last frame (true at the start and after a reset:
                                   // a grip held through either has no right to take the gun)
    bool right_ = false;           // this squeeze has the right to take the gun
    bool landed_ = false;          // ... and its hand has been in the capture zone
    float arriveLeft_ = 0.0f;      // seconds left for an arriving hand to land
    bool squeezed_ = false;        // a squeeze (a press seen) is under way
    bool tookThisSqueeze_ = false; // ... and has taken the gun
    TwoHandMiss squeeze_;          // ... and what is known of it for a miss's line
    Quat applied_;                 // the turn applied last frame
    Quat from_;                    // the turn the current ease started from
    float ease_ = 1.0f;            // 0..1 through the current ease; 1 settled
    std::optional<OneEuroRotation> filter_; // the held aim's smoothing
    double clock_ = 0.0;                    // the filter's clock (the frames' dt summed)
};

} // namespace evr::input
