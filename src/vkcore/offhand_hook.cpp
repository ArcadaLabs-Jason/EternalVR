// The off hand on the game's left arm (ETERNALVR_OFFHAND, docs/VR_HANDS_HUD.md, "Off hand").
//
// The first-person arms are one skinned model (idHands::renderModel, fp_hands.md6) that the viewmodel hook
// places at the weapon hand. Every tick idHands::UpdateWeaponLagJointMods (RVA 0x138D170) reads the
// animated model-space pose of the `lefthandattach` joint (GetJointTransforms, 0x19807F0, into
// [rsp+0x68] and [rbp+0x30]) and hands SetJointMod (0x138CF10) a translation ([rsp+0x38]) and a rotation
// matrix (the pointer at [rsp+0x20], the weapon lag shared by every attach joint) with flags 0x20B.
//
// A mid hook just before that call (RVA 0x138D903) rewrites them. The arm hangs from that joint (wrist,
// forearm, elbow, shoulder, in that order down the hierarchy), so in free mode the hook poses the whole
// arm (offhand_arm.hpp): the wrist at the off-hand controller's grip with its orientation
// (features/arm/hand_offset.hpp), the elbow by two-bone IK from a shoulder fixed to the head
// (features/arm/arm_solve.hpp), the forearm's roll joints sharing the wrist's twist. The attach joint's
// modifier is rewritten so the wrist lands on target; the forearm, elbow and shoulder get modifiers of the
// layer's own (game_arm.hpp). Everything is blended with the game's own arm by the arm policy
// (features/input/offhand_policy.hpp), which hands the arm back to the game for glory kills, melee, throws,
// weapon switches and every other left-arm animation. In probe mode the game's own plus a fixed offset, the
// first live check of whether the arm follows its attach joint. The rotation goes through a matrix of the
// hook's own (the pointer argument is redirected), so the shared lag matrix of the other joints is untouched.
// The same hook bends the weapon arm (ETERNALVR_WEAPON_ARM, weapon_arm.hpp), whatever the off hand's mode; it
// never touches the game's modifiers of the right hand.
//
// Fail closed: the function, its call of SetJointMod and of GetJointTransforms, and every frame offset the
// hook reads or writes are checked at install time; each tick the hook writes only while the multiplayer
// guard allows it, the hands belong to the local player, the skeleton and the animated poses check out,
// the result is plausible and the policy gives the controller a weight above zero. Otherwise the game's
// values stay as they are and the layer's own modifiers are set back to no change.
//
// The arms' surfaces: a weapon's mesh kit may hide an arm the layer poses (the weapon arm for most weapons,
// both arms for the ballista, BFG and unmaykr), so while the layer poses an arm its surfaces are shown and
// afterwards hidden again as the kit had them (hands_surfaces.hpp). After a multiplayer guard trip the one
// write is that give-back, once per arm, on the first tick of the hands that have them (releaseOnTrip).

#include "vkcore/controllers_impl.hpp"

#include "features/input/offhand_policy.hpp"
#include "vkcore/arm_dump.hpp"
#include "vkcore/arm_signals.hpp"
#include "vkcore/arm_solve_mode.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/left_hand_arms.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/offhand_arm.hpp"
#include "vkcore/weapon_arm.hpp"
#include "xr_math/offhand_pose.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "offhand";

constexpr const char* kLagModsSignature = "4C 8B DC 49 89 7B 20 55 49 8D AB 58 FE FF FF 48 81 EC A0 02 00 00";
constexpr const char* kSetJointModSignature = "48 83 EC 48 4C 63 D2 85 D2 0F 88 ?? ?? ?? ?? 66 41 83 F8 FF";
constexpr const char* kGetJointTransformsSignature =
    "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 30 4C 8B F9";

// Bytes expected at offsets from the start of UpdateWeaponLagJointMods (build 25216728).
struct Expect {
    std::size_t at;
    const char* bytes;
};
constexpr Expect kExpected[] = {
    {0x4E, "0F B7 99 E4 28 00 00"},  // movzx ebx, [rcx+0x28E4]   the left hand's joint index
    {0xA2, "48 8D 4D 30"},           // lea rcx, [rbp+0x30]       its animated axis out
    {0xB5, "48 8D 4C 24 68"},        // lea rcx, [rsp+0x68]       its animated position out
    {0x6AA, "8B 97 D0 28 00 00"},    // mov edx, [rdi+0x28D0]     the left hand's modifier
    {0x6B0, "48 8D 45 08"},          // lea rax, [rbp+8]          the lag matrix
    {0x6BD, "4C 8D 4C 24 38"},       // lea r9, [rsp+0x38]        the translation
    {0x6D7, "48 89 44 24 20"},       // mov [rsp+0x20], rax       the rotation argument
    {0x793, "F3 44 0F 11 6D 00 E8"}, // the hook site, then the call of SetJointMod
};
constexpr std::size_t kGetJointCall = 0xCB;
constexpr std::size_t kSetJointCall = 0x799;
constexpr std::size_t kHookSite = 0x793;

// The frame at the hook site.
constexpr std::size_t kTranslationFromRsp = 0x38;
constexpr std::size_t kRotationArgFromRsp = 0x20;
constexpr std::size_t kAnimPositionFromRsp = 0x68;
constexpr std::ptrdiff_t kAnimAxisFromRbp = 0x30;

// Type info (build 25216728).
constexpr std::size_t kHandsOwner = 0x358; // idHands::owner

// Install time, then read-only: the weapon arm passed its checks.
bool g_weaponArm = false;

// Hook thread (the game thread running idHands::Update) only.
input::ArmBlend g_blend;
LONGLONG g_lastQpc = 0;
thread_local float g_rotation[9] = {};
struct TraceKey {
    input::ArmReason reason = input::ArmReason::ModeGame;
    bool controller = false;
    std::int32_t action = -1;
    std::int32_t state = -1;
    std::uint64_t flags = 0;
    std::uint32_t hidden = 0;
    std::int32_t fpDisabled = 0;
    bool sync = false;
    friend bool operator==(const TraceKey&, const TraceKey&) = default;
};
TraceKey g_lastTrace;
std::uint64_t g_traceLines = 0;
bool g_tripReleased = false;
std::atomic<std::uint64_t> g_rejected{0};

const std::byte* callTarget(const std::byte* call) {
    return call + 5 + readI32(call + 1);
}

Vec3 readVec3(const std::byte* at, bool& ok) {
    float v[3] = {};
    ok = ok && safeCopy(v, at, sizeof(v));
    return {v[0], v[1], v[2]};
}

xr_math::Mat3Rows readMat3(const std::byte* at, bool& ok) {
    xr_math::Mat3Rows m{};
    ok = ok && safeCopy(m.data(), at, sizeof(float) * 9);
    return m;
}

void trace(const input::ArmSignals& sig, const input::ArmDecision& d, float weight) {
    const TraceKey key{d.reason,       d.controller,      sig.pendingAction,   sig.destHandsState,
                       sig.handsFlags, sig.hiddenReasons, sig.fpHandsDisabled, sig.syncActive};
    if (key == g_lastTrace || g_traceLines >= 400) {
        return;
    }
    g_lastTrace = key;
    ++g_traceLines;
    EVR_LOG("%s: arm %s (%s), weight %.2f; action %d, state %d, flags 0x%016llX, hidden 0x%X, fp hands "
            "disabled 0x%X, sync %d, forced %d",
            kTag, d.controller ? "controller" : "game", input::armReasonName(d.reason), weight,
            sig.pendingAction, sig.destHandsState, static_cast<unsigned long long>(sig.handsFlags),
            sig.hiddenReasons, sig.fpHandsDisabled, sig.syncActive ? 1 : 0, sig.forcedView ? 1 : 0);
}

// After a multiplayer guard trip, once per arm: the weapon arm given back (its modifiers and its surface),
// the left arm's modifiers back to no change, so it is the game's again instead of frozen in the last pose
// written, and its surfaces hidden again if the layer showed them, and a test weight put back (development
// only). The only writes after a trip: they undo the layer's own changes, like the cvar restore. Done here,
// not from a trip listener: the modifier lists and the arms' caches belong to this hook's game thread, and
// the hands pointer is only known here; a listener could run on any thread while the blend reads the lists.
// Each arm waits for the hands its modifiers were added to (another hands' tick passes); nothing to do when
// no hands have them.
void releaseOnTrip(const std::byte* hands) {
    if (mp_guard::state() != mp_policy::GuardState::Tripped) {
        return;
    }
    if (g_weaponArm) {
        weapon_arm::releaseOnTrip(hands);
    }
    if (settings().testArmModWeight) {
        game_arm::releaseModWeight(hands); // the test weight back, on the hands that held it
    }
    // Whatever the weapon arm did: hidden arms stop hiding (logged, nothing written), and what a cutscene hid
    // on the right arm is shown again (the weapon arm's own give-back skips an arm it never posed).
    hands_surfaces::releaseAfterTrip(arm::ArmSide::Right, hands);
    hands_surfaces::releaseAfterTrip(arm::ArmSide::Left, hands);
    if (g_tripReleased) {
        return;
    }
    const std::byte* armHands = game_arm::armHands(arm::ArmSide::Left);
    if (armHands && hands != armHands) {
        return;
    }
    g_tripReleased = true;
    const bool released = armHands && game_arm::releaseArm(arm::ArmSide::Left, hands, true);
    const auto left = game_arm::overridesLeft(arm::ArmSide::Left, hands);
    EVR_LOG("%s: the multiplayer guard tripped: %s (overrides left per list %d/%d)", kTag,
            released
                ? "the layer's arm modifiers set back to no change in both lists; the left arm is the game's"
                : "no arm modifiers of the layer's in place; the left arm is the game's",
            left[0], left[1]);
}

void poseLeftHand(const HookRegisters& regs, const ArmsTick& arms) {
    const auto* hands = reinterpret_cast<const std::byte*>(regs.rdi);
    if (!mp_guard::allowsGameTouch()) {
        releaseOnTrip(hands);
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    if (cfg.armsHidden || hands_surfaces::holdPending()) {
        // Every tick, whatever else runs below: the weapon's kit shows arms again on an equip, and fists,
        // melee and glory kills show them too; a cutscene's hold hides them and gives them back
        // (hands_surfaces.hpp).
        hands_surfaces::update(arm::ArmSide::Right, hands, false);
        hands_surfaces::update(arm::ArmSide::Left, hands, false);
    }
    if (!s.attached.load(std::memory_order_acquire)) {
        offhand_arm::giveBack(hands);
        if (offhand_mods::leftGlove()) {
            left_hand_arms::release(hands); // both arms, their blends and the gun's placement
        } else if (g_weaponArm) {
            weapon_arm::release(hands);
        }
        return;
    }
    const std::byte* owner = nullptr;
    if (!safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return;
    }
    const TickView view = tickView();
    const WorldHand& world = view.world;
    const ModelPlacement& model = view.model;
    arm_dump::noteView(world, model);
    input::ArmSignals sig = readArmSignals(hands, owner);
    sig.forcedView = s.yielding.load();
    sig.modelPlaced = view.modelCurrent;
    const LONGLONG now = nowQpc();
    const float dt = g_lastQpc ? static_cast<float>(secondsSince(g_lastQpc)) : 0.0f;
    g_lastQpc = now;
    input::ArmSignals weaponSig = sig;
    weaponSig.offHandTracked = view.worldCurrent; // the weapon arm's: the head and the weapon hand
    sig.offHandTracked = view.worldCurrent && world.offValid;
    if (offhand_mods::leftGlove()) {
        left_hand_arms::tick(hands, arms, weaponSig, sig, world, model,
                             dt); // the left attach joint stays the game's
        return;
    }
    if (g_weaponArm) {
        weapon_arm::tick(hands, arms, weaponSig, world, model, dt);
    }
    const input::ArmDecision decision = input::decideOffHand(sig, cfg.offhand);
    const float weight =
        g_blend.updateOffHand(decision.controller, decision.reason == input::ArmReason::HandsHidden, dt,
                              cfg.offhandBlendSeconds, cfg.offhandHoldSeconds);
    if (cfg.offhandTrace) {
        trace(sig, decision, weight);
    }
    arm_dump::noteGameArm(arm::ArmSide::Left, weight);
    if (weight <= 0.0f || cfg.offhand != input::OffhandMode::Free) {
        offhand_arm::giveBack(hands);
    }
    if (weight <= 0.0f || cfg.offhand == input::OffhandMode::Game) {
        return;
    }

    const auto* rsp = reinterpret_cast<std::byte*>(regs.rsp);
    const auto* rbp = reinterpret_cast<const std::byte*>(regs.rbp);
    bool ok = true;
    const xr_math::ModelPose animated{readVec3(rsp + kAnimPositionFromRsp, ok),
                                      xr_math::fromRows(readMat3(rbp + kAnimAxisFromRbp, ok))};
    const float* gameRotation = nullptr;
    ok = ok && safeRead(rsp + kRotationArgFromRsp, gameRotation) && gameRotation;
    xr_math::JointMod game;
    game.translation = readVec3(rsp + kTranslationFromRsp, ok);
    if (ok) {
        game.rotation = readMat3(reinterpret_cast<const std::byte*>(gameRotation), ok);
    }
    if (!ok || !xr_math::plausibleAnimatedPose(animated)) {
        g_rejected.fetch_add(1, std::memory_order_relaxed);
        offhand_arm::giveBack(hands);
        return;
    }
    const float upm = world.unitsPerMetre;
    xr_math::JointMod mixed = game;
    if (cfg.offhand == input::OffhandMode::Probe) {
        const game::WeaponOffset& p = cfg.offhandProbe;
        xr_math::JointMod ours = game;
        ours.translation = game.translation + Vec3{p.forward, p.left, p.up} * upm;
        mixed = xr_math::blendJointMods(game, ours, weight);
        if (!xr_math::plausibleJointMod(mixed, offhand_arm::kMaxReachMetres * upm)) {
            g_rejected.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (offhand_arm::poseTraceDue(cfg)) {
            EVR_LOG(
                "%s: animated (%.2f %.2f %.2f), game move (%.2f %.2f %.2f), ours (%.2f %.2f %.2f), weight "
                "%.2f; %llu rejected",
                kTag, animated.position.x, animated.position.y, animated.position.z, game.translation.x,
                game.translation.y, game.translation.z, ours.translation.x, ours.translation.y,
                ours.translation.z, weight, static_cast<unsigned long long>(g_rejected.load()));
        }
    } else {
        const auto arm =
            offhand_arm::pose(hands, animated, game, world, model, cfg, weight, g_rejected.load());
        if (!arm) {
            g_rejected.fetch_add(1, std::memory_order_relaxed);
            offhand_arm::giveBack(hands);
            return;
        }
        mixed = *arm;
    }
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const float translation[3] = {mixed.translation.x, mixed.translation.y, mixed.translation.z};
    std::memcpy(g_rotation, mixed.rotation.data(), sizeof(g_rotation));
    const float* rotation = g_rotation;
    auto* writable = reinterpret_cast<std::byte*>(regs.rsp);
    if (safeCopy(writable + kTranslationFromRsp, translation, sizeof(translation)) &&
        safeCopy(writable + kRotationArgFromRsp, &rotation, sizeof(rotation))) {
        s.offhandWrites.fetch_add(1, std::memory_order_relaxed);
    }
}

// The change SetJointMod is about to be handed for the left attach joint, whoever made it (a solve that
// writes past its targets only).
void noteAttachMod(const HookRegisters& regs) {
    bool ok = true;
    const auto* rsp = reinterpret_cast<const std::byte*>(regs.rsp);
    const float* rotation = nullptr;
    xr_math::JointMod mod;
    mod.translation = readVec3(rsp + kTranslationFromRsp, ok);
    ok = ok && safeRead(rsp + kRotationArgFromRsp, rotation) && rotation;
    if (ok) {
        mod.rotation = readMat3(reinterpret_cast<const std::byte*>(rotation), ok);
    }
    offhand_arm::noteAttachMod(reinterpret_cast<const void*>(regs.rdi),
                               ok ? std::optional(mod) : std::nullopt);
}

void onLeftHandMod(const HookRegisters& regs) {
    // The arms' item as the viewmodel hook read it this tick, for the arms and the dump alike: for the
    // player's hands only. The hook runs for whatever idHands calls UpdateWeaponLagJointMods, and other hands
    // would make the arms' item forget the player's (its swim fists and the swim's clock). Both users check
    // the owner again before they read `arms`.
    const auto* hands = reinterpret_cast<const std::byte*>(regs.rdi);
    const std::byte* owner = nullptr;
    ArmsTick arms;
    arms.hands = hands;
    if (mp_guard::allowsGameTouch() && safeRead(hands + kHandsOwner, owner) && isPlayerSafe(owner)) {
        arms = armsTick(hands);
    }
    poseLeftHand(regs, arms);
    if (armSolveMode().writesPast) {
        noteAttachMod(regs);
    }
    if (arm_dump::on()) {
        arm_dump::tick(arms);
    }
}

} // namespace

bool installOffhandHook(bool& weaponArmInstalled) {
    weaponArmInstalled = false;
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* start = findUnique(image, kTag, "UpdateWeaponLagJointMods", kLagModsSignature);
    const std::byte* setJointMod = findUnique(image, kTag, "SetJointMod", kSetJointModSignature);
    const std::byte* getJoints = findUnique(image, kTag, "GetJointTransforms", kGetJointTransformsSignature);
    if (!start || !setJointMod || !getJoints || !image.inText(start, kSetJointCall + 5)) {
        EVR_LOG("%s: a signature did not match exactly once (see above); off hand stays the game's", kTag);
        return false;
    }
    for (const Expect& e : kExpected) {
        if (!matchesAt(image, start + e.at, e.bytes)) {
            EVR_LOG("%s: UpdateWeaponLagJointMods differs at +0x%zX; off hand stays the game's", kTag, e.at);
            return false;
        }
    }
    if (callTarget(start + kSetJointCall) != setJointMod || callTarget(start + kGetJointCall) != getJoints) {
        EVR_LOG(
            "%s: UpdateWeaponLagJointMods does not call SetJointMod / GetJointTransforms where expected; off "
            "hand stays the game's",
            kTag);
        return false;
    }
    const input::ControllerSettings& cfg = settings();
    // Free mode and the weapon arm pose whole arms and need the arms' own checks; probe and trace do not.
    const offhand_mods::ArmSet wanted{cfg.offhand == input::OffhandMode::Free || input::leftHandArms(cfg),
                                      cfg.weaponArm == input::WeaponArmMode::Ik};
    const offhand_mods::ArmSet ready = wanted.offHand || wanted.weapon
                                           ? game_arm::install(image, start, getJoints, setJointMod, wanted)
                                           : offhand_mods::ArmSet{};
    const bool offHandHooks = cfg.offhand != input::OffhandMode::Game || cfg.offhandTrace;
    if ((cfg.offhand == input::OffhandMode::Free && !ready.offHand) ||
        (!offHandHooks && !ready.weapon && !cfg.armsHidden)) {
        return false;
    }
    if ((ready.offHand || ready.weapon || cfg.armsHidden) &&
        !hands_surfaces::install(image, cfg.armsHidden) && cfg.armsHidden) {
        EVR_LOG("%s: the arms' surfaces could not be reached (above); the arms stay shown", kTag);
    }
    std::string error;
    std::byte* site = const_cast<std::byte*>(start + kHookSite);
    g_weaponArm = ready.weapon;
    if (!installMidHook(site, &onLeftHandMod, error)) {
        g_weaponArm = false;
        EVR_LOG("%s: hook at RVA 0x%X failed: %s", kTag, image.rva(site), error.c_str());
        return false;
    }
    weaponArmInstalled = g_weaponArm;
    if (g_weaponArm) {
        weapon_arm::logSettings();
    }
    // As used: mirrored left to right with the weapon in the left hand.
    const bool mirrored = cfg.handedness != game::Handedness::Right;
    const game::WeaponOffset w = input::offhandOffsetFor(cfg.offhandOffset, cfg.handedness);
    const game::WeaponOffset e = input::offhandOffsetFor(cfg.offhandElbow, cfg.handedness);
    const game::WeaponOffset sh = input::offhandOffsetFor(cfg.offhandShoulderOffset, cfg.handedness);
    EVR_LOG("%s: left hand modifier hook at RVA 0x%X: mode %s, blend %.2f s, hold %.2f s, wrist offset (%.3f "
            "%.3f "
            "%.3f) m (%.1f %.1f %.1f) deg, shoulder %s (%.2f %.2f %.2f) m, elbow toward (%.2f %.2f %.2f)%s%s",
            kTag, image.rva(site), input::offhandModeName(cfg.offhand), cfg.offhandBlendSeconds,
            cfg.offhandHoldSeconds, w.forward, w.left, w.up, w.pitch, w.yaw, w.roll,
            input::shoulderAnchorName(input::shoulderAnchorFor(cfg.offhandShoulder, cfg.handedness)),
            sh.forward, sh.left, sh.up, e.forward, e.left, e.up,
            offhand_mods::leftGlove()
                ? ", the arms take each other's roles (weapon in the left hand, left_hand_arms.hpp)"
            : mirrored ? ", mirrored for the right controller (weapon in the left hand)"
                       : "",
            cfg.offhandTrace ? ", trace on" : "");
    return true;
}

} // namespace evr::vkcore::controllers
