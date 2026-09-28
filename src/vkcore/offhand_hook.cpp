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
// arm: the wrist at the off-hand controller's grip with its orientation (features/arm/hand_offset.hpp),
// the elbow by two-bone IK from a shoulder fixed to the head (features/arm/arm_solve.hpp), the forearm's
// roll joints sharing the wrist's twist. The attach joint's modifier is rewritten so the wrist lands on
// target; the forearm, elbow and shoulder get modifiers of the layer's own (offhand_arm.hpp). Everything
// is blended with the game's own arm by the arm policy (features/input/offhand_policy.hpp), which hands the
// arm back to the game for glory kills, melee, throws, weapon switches and every other left-arm animation.
// In probe mode the game's own plus a fixed offset, the first live check of whether the arm follows its
// attach joint. The rotation goes through a matrix of the hook's own (the pointer argument is redirected),
// so the shared lag matrix of the other joints is untouched.
//
// Fail closed: the function, its call of SetJointMod and of GetJointTransforms, and every frame offset the
// hook reads or writes are checked at install time; each tick the hook writes only while the multiplayer
// guard allows it, the hands belong to the local player, the skeleton and the animated poses check out,
// the result is plausible and the policy gives the controller a weight above zero. Otherwise the game's
// values stay as they are and the layer's own modifiers are set back to no change.

#include "vkcore/controllers_impl.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"
#include "features/arm/hand_offset.hpp"
#include "features/input/offhand_policy.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/offhand_arm.hpp"
#include "xr_math/offhand_pose.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

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
constexpr std::size_t kHandsOwner = 0x358;             // idHands::owner
constexpr std::size_t kHandsDestState = 0x1970;        // idHands::destHandsState
constexpr std::size_t kHandsLeftAttachJoint = 0x28E4;  // the left attach joint's index (the byte check)
constexpr std::size_t kHandsHiddenReasons = 0x29A0;    // idHands::hiddenReasons
constexpr std::size_t kHandsPendingAction = 0x8CD0;    // idHands::pendingAction.action
constexpr std::size_t kHandsFlags = 0x8DA0;            // idHands::handsFlags
constexpr std::size_t kPlayerFpHandsDisabled = 0x5C28; // idPlayer::disableFPHandsReasons
constexpr std::size_t kPlayerSyncMaster = 0x7DA8 + 8;  // idPlayer::syncMaster's object [inferred]

// A probe modifier never reaches further than this from the animated pose, nor the wrist in free mode.
constexpr float kMaxReachMetres = 1.5f;
// The attach joint sits about half a metre from the wrist, so turning the hand swings it further.
constexpr float kMaxAttachMetres = 3.0f;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

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
ULONGLONG g_lastPoseTrace = 0;
bool g_loggedBones = false;
std::atomic<std::uint64_t> g_rejected{0};

bool bytesMatch(const std::byte* at, std::string_view hex) {
    std::size_t i = 0;
    for (std::size_t p = 0; p + 1 < hex.size(); p += 3, ++i) {
        const auto nibble = [](char c) {
            return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
        };
        const auto expected = static_cast<std::uint8_t>(nibble(hex[p]) * 16 + nibble(hex[p + 1]));
        if (static_cast<std::uint8_t>(at[i]) != expected) {
            return false;
        }
    }
    return true;
}

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

Vec3 scaled(Vec3 v, Vec3 s) {
    return {v.x * s.x, v.y * s.y, v.z * s.z};
}

Vec3 unscaled(Vec3 v, Vec3 s) {
    return {v.x / s.x, v.y / s.y, v.z / s.z};
}

input::ArmSignals readSignals(const std::byte* hands, const std::byte* player) {
    input::ArmSignals s;
    safeRead(hands + kHandsPendingAction, s.pendingAction);
    safeRead(hands + kHandsDestState, s.destHandsState);
    safeRead(hands + kHandsHiddenReasons, s.hiddenReasons);
    safeRead(hands + kHandsFlags, s.handsFlags);
    safeRead(player + kPlayerFpHandsDisabled, s.fpHandsDisabled);
    const std::byte* sync = nullptr;
    s.syncActive = safeRead(player + kPlayerSyncMaster, sync) && sync != nullptr;
    return s;
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

bool poseTraceDue(const input::ControllerSettings& cfg) {
    if (!cfg.offhandTrace) {
        return false;
    }
    const ULONGLONG ticks = GetTickCount64();
    if (ticks - g_lastPoseTrace < 1000) {
        return false;
    }
    g_lastPoseTrace = ticks;
    return true;
}

// The free off hand: the arm solved for the controller and mixed with the game's by `weight`. Writes the
// layer's modifiers (forearm, elbow, shoulder) and returns the attach joint's; nullopt when the arm cannot
// be used this tick (nothing written).
std::optional<xr_math::JointMod> freeArm(const std::byte* hands,
                                         const xr_math::ModelPose& animatedAttach,
                                         const xr_math::JointMod& game,
                                         const WorldHand& world,
                                         const ModelPlacement& model,
                                         const input::ControllerSettings& cfg,
                                         float weight) {
    using arm::ArmJoint;
    using arm::index;
    std::int16_t attachJoint = -1;
    if (!safeRead(hands + kHandsLeftAttachJoint, attachJoint)) {
        return std::nullopt;
    }
    const auto read = offhand_arm::readArm(hands, attachJoint, animatedAttach);
    if (!read) {
        return std::nullopt;
    }
    const arm::ArmPoses& animated = read->animated;
    const float upm = world.unitsPerMetre;

    // The targets in the arms model's space.
    const game::WeaponOffset o = input::offhandOffsetFor(cfg.offhandOffset, cfg.handedness);
    const arm::HandOffset offset{Vec3{o.forward, o.left, o.up} * upm, {o.pitch, o.yaw, o.roll}};
    arm::ArmTargets targets;
    targets.hand = arm::wristFromGrip(xr_math::inModelSpace(model.pose, world.offGrip), offset);
    targets.shoulder =
        input::shoulderAnchorFor(cfg.offhandShoulder, cfg.handedness) == input::ShoulderAnchor::Model
            ? animated[index(ArmJoint::UpperArm)].position
            : xr_math::inModelSpace(model.pose, {world.offShoulder, model.pose.axis}).position;
    targets.pole = arm::toLocal(model.pose.axis, world.offElbow);
    const auto ours = arm::solveArm(animated, targets);
    if (!ours) {
        return std::nullopt;
    }

    // The game's arm: its modifier moves the attach joint and the rest follow. Mixed at the wrist, and the
    // attach joint placed to carry the mixed wrist.
    xr_math::JointMod gameInUnits = game;
    gameInUnits.translation = scaled(game.translation, read->scale);
    const arm::ArmPoses gameArm =
        arm::followAttach(animated, xr_math::applyJointMod(animated[index(ArmJoint::Attach)], gameInUnits));
    arm::ArmPoses mixed;
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        mixed[i] = arm::blendPose(gameArm[i], ours->joints[i], weight);
    }
    mixed[index(ArmJoint::Attach)] =
        arm::parentFor(mixed[index(ArmJoint::Hand)],
                       arm::relative(animated[index(ArmJoint::Attach)], animated[index(ArmJoint::Hand)]));

    xr_math::JointMod mod =
        xr_math::jointModToward(animated[index(ArmJoint::Attach)], mixed[index(ArmJoint::Attach)]);
    mod.translation = unscaled(mod.translation, read->scale);
    const float wristMove =
        length(mixed[index(ArmJoint::Hand)].position - animated[index(ArmJoint::Hand)].position);
    if (!(wristMove <= kMaxReachMetres * upm) || !xr_math::plausibleJointMod(mod, kMaxAttachMetres * upm) ||
        !mp_guard::allowsGameTouch() || !offhand_arm::writeArm(hands, mixed, read->scale, read->origin)) {
        return std::nullopt;
    }

    if (!g_loggedBones) {
        g_loggedBones = true;
        EVR_LOG("%s: arm: upper arm %.3f, forearm %.3f (animated, game units), model scale (%.3f %.3f %.3f), "
                "shoulder %s, wrist offset (%.3f %.3f %.3f) m (%.1f %.1f %.1f) deg",
                kTag, ours->upper, ours->lower, read->scale.x, read->scale.y, read->scale.z,
                input::shoulderAnchorName(input::shoulderAnchorFor(cfg.offhandShoulder, cfg.handedness)),
                o.forward, o.left, o.up, o.pitch, o.yaw, o.roll);
    }
    if (poseTraceDue(cfg)) {
        const Vec3 w = mixed[index(ArmJoint::Hand)].position;
        const Vec3 t = targets.hand.position;
        const Vec3 e = mixed[index(ArmJoint::ForeArm)].position;
        const Vec3 s = mixed[index(ArmJoint::UpperArm)].position;
        EVR_LOG(
            "%s: ik: wrist (%.2f %.2f %.2f) target (%.2f %.2f %.2f), reach %.2f%s, elbow (%.2f %.2f %.2f), "
            "shoulder (%.2f %.2f %.2f), twist %.0f deg, weight %.2f; %llu rejected",
            kTag, w.x, w.y, w.z, t.x, t.y, t.z, ours->ik.reach, ours->ik.clamped ? " (clamped)" : "", e.x,
            e.y, e.z, s.x, s.y, s.z, ours->twist * kDegreesPerRadian, weight,
            static_cast<unsigned long long>(g_rejected.load()));
    }
    return mod;
}

void onLeftHandMod(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    const auto* hands = reinterpret_cast<const std::byte*>(regs.rdi);
    if (!s.attached.load(std::memory_order_acquire)) {
        offhand_arm::releaseArm(hands);
        return;
    }
    const std::byte* owner = nullptr;
    if (!safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return;
    }
    WorldHand world;
    ModelPlacement model;
    {
        std::lock_guard lock(s.viewMutex);
        world = s.world;
        model = s.model;
    }
    input::ArmSignals sig = readSignals(hands, owner);
    sig.forcedView = s.yielding.load();
    sig.offHandTracked = world.valid && world.offValid && secondsSince(world.qpc) <= kWorldStaleSeconds;
    sig.modelPlaced = model.valid && secondsSince(model.qpc) <= kWorldStaleSeconds;
    const input::ArmDecision decision = input::decideArm(sig, cfg.offhand);
    const LONGLONG now = nowQpc();
    const float dt = g_lastQpc ? static_cast<float>(secondsSince(g_lastQpc)) : 0.0f;
    g_lastQpc = now;
    const float weight =
        g_blend.update(decision.controller, dt, cfg.offhandBlendSeconds, cfg.offhandHoldSeconds);
    if (cfg.offhandTrace) {
        trace(sig, decision, weight);
    }
    if (weight <= 0.0f || cfg.offhand != input::OffhandMode::Free) {
        offhand_arm::releaseArm(hands);
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
        offhand_arm::releaseArm(hands);
        return;
    }
    const float upm = world.unitsPerMetre;
    xr_math::JointMod mixed = game;
    if (cfg.offhand == input::OffhandMode::Probe) {
        const game::WeaponOffset& p = cfg.offhandProbe;
        xr_math::JointMod ours = game;
        ours.translation = game.translation + Vec3{p.forward, p.left, p.up} * upm;
        mixed = xr_math::blendJointMods(game, ours, weight);
        if (!xr_math::plausibleJointMod(mixed, kMaxReachMetres * upm)) {
            g_rejected.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (poseTraceDue(cfg)) {
            EVR_LOG(
                "%s: animated (%.2f %.2f %.2f), game move (%.2f %.2f %.2f), ours (%.2f %.2f %.2f), weight "
                "%.2f; %llu rejected",
                kTag, animated.position.x, animated.position.y, animated.position.z, game.translation.x,
                game.translation.y, game.translation.z, ours.translation.x, ours.translation.y,
                ours.translation.z, weight, static_cast<unsigned long long>(g_rejected.load()));
        }
    } else {
        const auto arm = freeArm(hands, animated, game, world, model, cfg, weight);
        if (!arm) {
            g_rejected.fetch_add(1, std::memory_order_relaxed);
            offhand_arm::releaseArm(hands);
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

} // namespace

bool installOffhandHook() {
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
        if (!bytesMatch(start + e.at, e.bytes)) {
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
    // Free mode poses the whole arm and needs the arm's own checks; probe and trace do not.
    if (cfg.offhand == input::OffhandMode::Free &&
        !offhand_arm::install(image, start, getJoints, setJointMod)) {
        return false;
    }
    std::string error;
    std::byte* site = const_cast<std::byte*>(start + kHookSite);
    if (!installMidHook(site, &onLeftHandMod, error)) {
        EVR_LOG("%s: hook at RVA 0x%X failed: %s", kTag, image.rva(site), error.c_str());
        return false;
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
            mirrored ? ", mirrored for the right controller (weapon in the left hand)" : "",
            cfg.offhandTrace ? ", trace on" : "");
    return true;
}

} // namespace evr::vkcore::controllers
