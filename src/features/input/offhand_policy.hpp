#pragma once

// Who moves the game's left arm (docs/VR_HANDS_HUD.md, "Off hand"): the game's animation or the off-hand
// controller (ETERNALVR_OFFHAND=free).
//
// The first-person arms are one skinned model placed at the weapon hand; the left hand follows the
// `lefthandattach` joint modifier the game writes every tick (idHands::UpdateWeaponLagJointMods). In free
// mode the layer writes that modifier from the off hand's pose, except while the game animates the left
// arm itself: glory and sync kills, Blood Punch and melee, equipment throws, weapon switches, custom
// animations (ledge grabs, pickups), hidden hands, and any forced view. Those hand the arm back to the
// game at once; the controller takes it again after a short hold, blended in. The weapon arm's IK
// (ETERNALVR_WEAPON_ARM) is handed over by the same rules.
//
// The signals are read from idHands and idPlayer (type info of build 25216728; bit positions of the
// hands flags follow the declaration order [inferred], so ETERNALVR_OFFHAND_TRACE logs them for the live
// check). Everything here is pure so the policy is tested without the game.

#include <cstdint>

namespace evr::input {

enum class OffhandMode : std::uint8_t {
    Game,  // the game's animation drives the left arm (default; nothing is written)
    Free,  // the off-hand controller drives it outside the game's own left-arm actions
    Probe, // live check: the game's modifier plus a fixed offset (ETERNALVR_OFFHAND_PROBE)
};

const char* offhandModeName(OffhandMode mode);

// Who poses the game's weapon arm (the model's right arm; docs/VR_HANDS_HUD.md, "The weapon arm"). The
// wrist is always the game's, under the gun at the weapon hand; in `ik` the forearm, elbow and upper arm
// reach it from a shoulder fixed to the head.
enum class WeaponArmMode : std::uint8_t {
    Game, // the game's animation poses the whole arm (nothing is written)
    Ik,   // the layer poses the forearm, elbow and upper arm outside the game's own arm animations (default)
};

const char* weaponArmModeName(WeaponArmMode mode);

// idHands::handsAction_t values (reflected enum, build 25216728).
enum class HandsAction : std::int32_t {
    None = 0,
    Melee = 7,
    MeleeRight = 8,
    MeleeLeft = 9,
    BringDown = 10,
    BringUp = 11,
    ThrowAttach = 12,
    ThrowItem = 13,
    CustomAnim = 16,
    GenericHideInstant = 22,
    GenericUnhide = 25,
    ChainsawFailedGk = 26,
    ChainsawStabFail = 31,
    HammerThrow = 32,
    HammerSlam = 33,
};

// idHands::handsState_t values (reflected enum, build 25216728: HS_NONE is -1, HS_IDLE 0).
inline constexpr std::int32_t kHandsStateHidden = 4;
inline constexpr std::int32_t kHandsStateChainsawRev = 10;
inline constexpr std::int32_t kHandsStateChainsawStab = 11;
inline constexpr std::int32_t kHandsStateSwimIdle = 12; // HS_SWIM_IDLE: the swim's idle with an item held
inline constexpr std::int32_t kHandsStateTransitioning = 14;

// idHands::handsFlags_t bits (declaration order, [inferred]; the reflected bitfield's order, build 25216728,
// puts `swimming` 36th, bit 35).
namespace hands_flag {
inline constexpr std::uint64_t kReloading = 1ull << 1;
inline constexpr std::uint64_t kThrowing = 1ull << 19;
inline constexpr std::uint64_t kMeleeAnim = 1ull << 20;
inline constexpr std::uint64_t kMeleeLunge = 1ull << 21;
inline constexpr std::uint64_t kChangingWeapon = 1ull << 29;
inline constexpr std::uint64_t kModChangeAnim = 1ull << 47;
inline constexpr std::uint64_t kCustomAnim = 1ull << 52;
inline constexpr std::uint64_t kSwimming = 1ull << 35;
// Every flag that means the game animates the left arm.
inline constexpr std::uint64_t kLeftArmBusy =
    kReloading | kThrowing | kMeleeAnim | kMeleeLunge | kChangingWeapon | kModChangeAnim | kCustomAnim;
} // namespace hands_flag

struct ArmSignals {
    bool offHandTracked = false;      // the off hand's pose is valid and current (view_pose_clock.hpp)
    bool modelPlaced = false;         // the arms model's placement at the weapon hand is current
    bool forcedView = false;          // the camera hook's forced-view gate (glory kills, meathook, cutscenes)
    bool syncActive = false;          // idPlayer::syncMaster is set (sync / glory kill)
    std::int32_t fpHandsDisabled = 0; // idPlayer::disableFPHandsReasons
    std::uint32_t hiddenReasons = 0;  // idHands::hiddenReasons
    std::int32_t pendingAction = 0;   // idHands::pendingAction.action
    std::int32_t destHandsState = 0;  // idHands::destHandsState
    std::uint64_t handsFlags = 0;     // idHands::handsFlags
};

enum class ArmReason : std::uint8_t {
    Controller, // the controller drives the arm
    ModeGame,   // ETERNALVR_OFFHAND=game
    Untracked,  // no off-hand pose, or the arms are not at the weapon hand
    ForcedView,
    Sync,
    HandsHidden,
    Action,     // a pending left-arm action (melee, throw, switch, custom animation, chainsaw, hammer)
    BusyFlags,  // a hands flag of a left-arm animation
    HandsState, // chainsaw or transitioning
    NoGrip,     // the weapon in the left hand: no grip of the game's read for the held item yet
};

const char* armReasonName(ArmReason reason);

struct ArmDecision {
    bool controller = false;
    ArmReason reason = ArmReason::ModeGame;
};

// The decision for this tick. Probe mode counts as free (the offset is applied while nothing else
// animates the arm).
ArmDecision decideArm(const ArmSignals& signals, OffhandMode mode);

// With the weapon in the left hand the right arm is the off hand: the mode it is posed in. `onController`:
// the held item's [left_handed_off_hand] row, for a weapon whose game left grip, reflected for the right
// glove, holds nothing (the chaingun's front handle is on its left side only), so with the game's off hand
// the right glove follows the right controller there instead, as with the free one.
OffhandMode leftHandOffMode(OffhandMode mode, bool onController);

// With the weapon in the left hand the arms hold the game's own grips of the held item, read from its pose
// while the arms are the game's (left_hand_arms.hpp): read only while the weapon arm's rules have no reason
// of their own to give the arms to the game. During a raise, a melee's fists, a quick action or a switch the
// pose is the action's: a grip read then moved the whole model 18 to 87 cm while the game had the arms (rig:
// the Unmaykr's and the Crucible's first raise, a melee after one).
bool readsGameGrip(const ArmSignals& weaponSignals, WeaponArmMode mode);

// Whether the hands swim: the hands flag `swimming`, or the swim's idle with an item held (HS_SWIM_IDLE,
// which the rig saw for a tick at a lower's start with the flag clear).
bool handsSwim(std::int32_t destHandsState, std::uint64_t handsFlags);

// The item whose pose the game's arms hold, for the rows and grips the arms are placed and held by: the held
// item `held`, but the swim's fists `swimFists` (idPlayer::weaponFistsSwim) while the hands swim (`swim`,
// handsSwim). At the water's surface the game puts the gun back in the right hand's slot now and then while
// the arms keep swimming: the gun's root, which the gun hangs from, stays where the swim parks it, about 1.5
// m from each glove and out of view, and the gloves keep the swim's pose (rig:
// docs/rig-findings/hands-rig.md, section 17). Placed by the gun's row there, the swim's right glove sat 28.3
// to 34.6 cm from the real hand (the median of the settled dump lines; the Heavy Cannon, the shotgun); held
// by the gun's grips with the weapon in the left hand, the left glove went 1.3 m behind the left hand. With
// no swim fists known (null), the held item; with the swim fists held (`heldIsSwimFists`), the held one.
const void* armsItem(const void* held, bool heldIsSwimFists, bool swim, const void* swimFists);

// After the hands stop swimming the grips are not read for this long (gripTakingAllowed): the swim's pose and
// the hands' signals change a few ticks apart (a tick at a dive). Longer than the signals' clear spells in
// the water (a lower: one dump line, 0.25 s), and as long as the game's longest raise after a teleport onto
// land (0.5 to 1.0 s) on the rig (hands-rig.md, section 17); a way out by swimming to a shore was not run. On
// land it only delays a grip not read yet by that long after a swim.
inline constexpr float kSwimGripHoldOffSeconds = 1.0f;
// The clock's value for hands that have not swum (secondsSinceSwim).
inline constexpr float kNeverSwamSeconds = 1e9f;

// Seconds since the hands last swam, one tick later: 0 while they swim (`swim`, handsSwim), else `before`
// plus the tick's `dtSeconds` (a negative or non-finite step counts as none), at most kNeverSwamSeconds.
float secondsSinceSwim(float before, bool swim, float dtSeconds);

// Whether the arms' grips of the game's are read for the held item this tick (left_hand_arms.cpp): the weapon
// arm's rules give the arms to the game for nothing of their own (`readsGameGrip`), the arms hold the held
// item itself (`armsIsHeld`: not the swim fists' pose with a gun in the slot), the hands do not swim
// (`handsSwim`), and they last swam kSwimGripHoldOffSeconds ago or more. A grip read from the swim's pose is
// kept as the gun's, by its name across levels (rig: at a dive the hands swim a tick before their pose leaves
// the gun's; a grip read there put the left glove 1.3 m behind the left hand). The swim is checked here, not
// only through `armsIsHeld`: with the swim fists unknown (idPlayer::weaponFistsSwim unread, or another
// item's) the arms' item is the held one while the hands swim.
bool gripTakingAllowed(bool readsGameGrip, bool armsIsHeld, bool handsSwim, float secondsSinceSwimEnded);

// The same rules for the weapon arm, which goes back to the game for the same actions: glory and sync
// kills, melee, throws, weapon switches, custom animations, hidden hands and forced views. Its
// `offHandTracked` is the head and weapon hand's pose (the shoulder's and the gun's), and `modelPlaced`
// that the viewmodel hook put the arms at the weapon hand.
ArmDecision decideWeaponArm(const ArmSignals& signals, WeaponArmMode mode);

// The controller's weight on the arm, 0 (the game's animation) .. 1 (the controller). Handing the arm to
// the game is quick (`blendSeconds` / 3) so a punch or a throw starts on time; taking it back waits until
// the game has not wanted it for `holdSeconds`, then blends over `blendSeconds`. The output is eased
// (smoothstep).
class ArmBlend {
public:
    float update(bool controller, float dtSeconds, float blendSeconds, float holdSeconds);
    [[nodiscard]] float weight() const;
    void reset();

private:
    float ramp_ = 0.0f;      // linear 0..1
    float sinceGame_ = 1e9f; // seconds since the game last wanted the arm
};

} // namespace evr::input
