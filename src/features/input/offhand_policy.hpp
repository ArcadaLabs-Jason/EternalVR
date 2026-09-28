#pragma once

// Who moves the game's left arm (docs/VR_HANDS_HUD.md, "Off hand"): the game's animation or the off-hand
// controller (ETERNALVR_OFFHAND=free).
//
// The first-person arms are one skinned model placed at the weapon hand; the left hand follows the
// `lefthandattach` joint modifier the game writes every tick (idHands::UpdateWeaponLagJointMods). In free
// mode the layer writes that modifier from the off hand's pose, except while the game animates the left
// arm itself: glory and sync kills, Blood Punch and melee, equipment throws, weapon switches, custom
// animations (ledge grabs, pickups), hidden hands, and any forced view. Those hand the arm back to the
// game at once; the controller takes it again after a short hold, blended in.
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

// idHands::handsState_t values.
inline constexpr std::int32_t kHandsStateHidden = 4;
inline constexpr std::int32_t kHandsStateChainsawRev = 10;
inline constexpr std::int32_t kHandsStateChainsawStab = 11;
inline constexpr std::int32_t kHandsStateTransitioning = 14;

// idHands::handsFlags_t bits (declaration order, [inferred]).
namespace hands_flag {
inline constexpr std::uint64_t kReloading = 1ull << 1;
inline constexpr std::uint64_t kThrowing = 1ull << 19;
inline constexpr std::uint64_t kMeleeAnim = 1ull << 20;
inline constexpr std::uint64_t kMeleeLunge = 1ull << 21;
inline constexpr std::uint64_t kChangingWeapon = 1ull << 29;
inline constexpr std::uint64_t kModChangeAnim = 1ull << 47;
inline constexpr std::uint64_t kCustomAnim = 1ull << 52;
// Every flag that means the game animates the left arm.
inline constexpr std::uint64_t kLeftArmBusy =
    kReloading | kThrowing | kMeleeAnim | kMeleeLunge | kChangingWeapon | kModChangeAnim | kCustomAnim;
} // namespace hands_flag

struct ArmSignals {
    bool offHandTracked = false;      // the off-hand controller's pose is fresh
    bool modelPlaced = false;         // the arms model was placed at the weapon hand this frame
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
};

const char* armReasonName(ArmReason reason);

struct ArmDecision {
    bool controller = false;
    ArmReason reason = ArmReason::ModeGame;
};

// The decision for this tick. Probe mode counts as free (the offset is applied while nothing else
// animates the arm).
ArmDecision decideArm(const ArmSignals& signals, OffhandMode mode);

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
