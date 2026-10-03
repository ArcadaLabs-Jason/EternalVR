// Melee and equipment aim under hand aim (features/input/action_aim.hpp, docs/VR_CONTROLLERS.md "Melee and
// equipment aim").
//
// - The mapper (aimActions): runs the policy on its actions before ActionHold, so a press that aims with the
//   head or the off hand goes into the first command built after the camera hook wrote that target, and
//   publishes the target with its generation (one atomic, packTarget).
// - The camera hook (actionAimTarget in aimAngles, then noteAimWritten once head aim has added the
//   delta): the view follows the target, and the generation written is reported back to the mapper.
// - Only melee moves the view. The equipment launcher and the Flame Belch aim from the shoulder launcher's
//   muzzle joint, not from the view angles (static RE, equipment_launch_hook.cpp), so turning the view would
//   not move them: the launch hook turns a grenade, and the fire hook (actionShotRay) a Flame Belch shot,
//   which goes through idHands::FireWeapon with the held weapon's decl while its button is held
//   (docs/BHAPTICS.md).
// - endGameView (noteActionPressView): the game's view angles in the game frames after a held-back press went
//   out, against the head and both hands, for rig checks.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/log.hpp"
#include "xr_math/hand_aim.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// idHavokPhysics_Player::viewAngles (player_aim.hpp): the angles the game's last view update made.
constexpr std::size_t kPlayerViewAngles = 0x8A50 + 0x3F10;
// Game frames logged after a held-back press went out: the one that takes the press and the next.
constexpr int kPressFrames = 2;

LogCap g_targetLines{60};
LogCap g_pressLines{60};
LogCap g_shotLines{12};
LogCap g_viewLines{60};

// The camera hook's last read generation, made the written one by noteAimWritten.
std::atomic<std::uint64_t> g_readGeneration{0};
// The camera hook wrote nothing on its last frame (noteAimPaused): a press waiting for the view goes out.
std::atomic<bool> g_aimPaused{false};

std::uint64_t packTarget(input::ActionAimSource source, std::uint64_t generation) {
    return (generation << 8) | static_cast<std::uint64_t>(source);
}

input::ActionAimSource targetSource(std::uint64_t packed) {
    return static_cast<input::ActionAimSource>(packed & 0xFF);
}

std::uint64_t targetGeneration(std::uint64_t packed) {
    return packed >> 8;
}

input::Hand offHand() {
    return weaponHand() == input::Hand::Right ? input::Hand::Left : input::Hand::Right;
}

bool logDue(LogCap& cap) {
    std::uint64_t skipped = 0;
    return cap.due(GetTickCount64(), skipped);
}

} // namespace

game::GameActionSet aimActions(State& s, const game::GameActionSet& down, bool menuHold, float dt) {
    const input::ControllerSettings& cfg = settings();
    if (cfg.aim != input::AimSource::Hand || cfg.actionAim.melee == input::ActionAimSource::Same) {
        return down;
    }
    if (!s.actionAim) {
        // The equipment launcher and the Flame Belch keep the view on the weapon hand (above).
        s.actionAim.emplace(input::ActionAimSettings{cfg.actionAim.melee, input::ActionAimSource::Same});
    }
    const bool forced = s.yielding.load();
    // Only melee is aimed by the view: the equipment launcher and the Flame Belch pass straight through, so
    // they are neither held back nor end the melee's target early.
    game::GameActionSet passThrough;
    game::add(passThrough, game::GameAction::Equipment);
    game::add(passThrough, game::GameAction::FlameBelch);
    passThrough &= down;
    input::ActionAimInput in;
    in.actions = down & ~passThrough;
    // Piloting a demon the camera hook aims the demon, not the Slayer's view (demon_aim.cpp).
    // Nor while the camera hook writes nothing (a menu or popup up, a scripted camera): the press goes out.
    in.retargetable = s.attached.load(std::memory_order_acquire) && !forced && !menuHold &&
                      !pilotingDemon() && !g_aimPaused.load(std::memory_order_relaxed);
    in.forcedView = forced;
    in.targetWritten = s.actionTargetWritten.load();
    in.dtSeconds = dt;
    const std::uint64_t before = s.actionAim->generation();
    const input::ActionAimOutput out = s.actionAim->update(in);
    s.actionTarget.store(packTarget(out.target, out.generation));
    if (out.generation != before && logDue(g_targetLines)) {
        EVR_LOG("%s: action aim: the view follows the %s%s%s (generation %llu)", kTag,
                input::actionAimSourceName(out.target), out.action == input::AimedAction::None ? "" : " for ",
                out.action == input::AimedAction::None ? "" : input::aimedActionName(out.action),
                static_cast<unsigned long long>(out.generation));
    }
    if (out.released) {
        s.actionPressAction.store(static_cast<std::uint8_t>(out.pressed));
        s.actionPressFrames.store(kPressFrames);
        if (logDue(g_pressLines)) {
            EVR_LOG("%s: action aim: %s press held back %d command(s), %.1f ms, %s", kTag,
                    input::aimedActionName(out.pressed), out.waitedCommands, out.waitedSeconds * 1000.0f,
                    out.overtaken  ? "then sent as another press took the target elsewhere"
                    : out.timedOut ? "then sent anyway (the view did not take its target in time)"
                                   : "until the view took its target");
        }
    }
    return out.actions | passThrough;
}

void resetActionAim(State& s) {
    if (s.actionAim) {
        s.actionAim->reset();
        s.actionTarget.store(packTarget(input::ActionAimSource::Same, s.actionAim->generation()));
    }
}

input::ActionAimSource actionAimTarget(State& s) {
    const std::uint64_t packed = s.actionTarget.load();
    g_readGeneration.store(targetGeneration(packed));
    return targetSource(packed);
}

void noteAimPaused() {
    g_aimPaused.store(true, std::memory_order_relaxed);
}

void noteAimWritten() {
    g_aimPaused.store(false, std::memory_order_relaxed);
    State& s = state();
    const std::uint64_t generation = g_readGeneration.load();
    std::uint64_t written = s.actionTargetWritten.load();
    while (written < generation && !s.actionTargetWritten.compare_exchange_weak(written, generation)) {
    }
}

const xr_math::EyeRelativePose*
actionShotRay(const WorldHand& world, const std::byte* hands, bool belchShot) {
    const input::ControllerSettings& cfg = settings();
    if (!cfg.actionAim.any()) {
        return nullptr;
    }
    const game::GameActionSet held = heldActions();
    const bool belch = game::contains(held, game::GameAction::FlameBelch);
    if ((belch || game::contains(held, game::GameAction::Melee) ||
         game::contains(held, game::GameAction::Equipment)) &&
        logDue(g_shotLines)) {
        // Which of these actions fire through this hook, and with which decl.
        EVR_LOG("%s: action aim: a %sshot while%s%s%s held, decl '%s'", kTag, belchShot ? "Flame Belch " : "",
                belch ? " flame_belch" : "", game::contains(held, game::GameAction::Melee) ? " melee" : "",
                game::contains(held, game::GameAction::Equipment) ? " equipment" : "",
                itemDeclName(heldItemDecl(hands)).c_str());
    }
    if (!belchShot || cfg.actionAim.equipment == input::ActionAimSource::Same) {
        return nullptr;
    }
    if (cfg.actionAim.equipment == input::ActionAimSource::OffHand && world.offAimValid) {
        return &world.offAim;
    }
    return &world.head;
}

void noteActionPressView(State& s, const std::byte* player, const xr_math::IdViewAxis& body) {
    int frames = s.actionPressFrames.load();
    if (frames <= 0 || !s.player.isPlayer(player) ||
        !s.actionPressFrames.compare_exchange_strong(frames, frames - 1) || !logDue(g_viewLines)) {
        return;
    }
    float view[2] = {};
    if (!safeCopy(view, player + kPlayerViewAngles, sizeof(view))) {
        return;
    }
    const GameViewPoses& p = s.poses; // the caller holds viewMutex
    const auto angles = [&p](input::Hand hand, char* out, std::size_t size) {
        const auto i = static_cast<std::size_t>(hand);
        if (!p.aimValid[i]) {
            std::snprintf(out, size, "not tracked");
            return;
        }
        const xr_math::IdAngles a = xr_math::handAimAngles(p.aim[i].orientation);
        std::snprintf(out, size, "%.1f %.1f", a.pitch, a.yaw);
    };
    char weapon[32];
    char off[32];
    angles(weaponHand(), weapon, sizeof(weapon));
    angles(offHand(), off, sizeof(off));
    const xr_math::IdAngles head =
        xr_math::headAngles(xr_math::openXrToIdTech(normalize(p.head.orientation)));
    const float bodyYaw = xr_math::anglesFromAxis(body).yaw;
    EVR_LOG(
        "%s: action aim: %s press, game frame %d after it went out: game view %.1f %.1f (pitch, yaw from the "
        "body); head %.1f %.1f, weapon hand %s, off hand %s; the view follows the %s",
        kTag, input::aimedActionName(static_cast<input::AimedAction>(s.actionPressAction.load())),
        kPressFrames - frames + 1, view[0], xr_math::normalize180(view[1] - bodyYaw), head.pitch, head.yaw,
        weapon, off, input::actionAimSourceName(targetSource(s.actionTarget.load())));
}

} // namespace evr::vkcore::controllers
