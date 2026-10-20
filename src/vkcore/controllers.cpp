// Motion controllers: installing the game hooks (controllers.hpp).

#include "vkcore/controllers.hpp"

#include "features/input/gamepad_mode.hpp"
#include "vkcore/arm_model.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/gamepad_mode.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/launcher_place.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/room_scale.hpp"

#include <mutex>
#include <shared_mutex>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

std::once_flag g_installOnce;

const char* onOff(bool on) {
    return on ? "on" : "off";
}

void install() {
    const input::ControllerSettings& cfg = settings();
    State& s = state();
    // The shot, viewmodel and hand-aim hooks use idPlayer and idHands offsets read from this build's type
    // info, so they need the build PlayerAim recognises; the user-command hooks carry their own checks.
    const bool knownBuild = s.player.init();
    if (!knownBuild) {
        EVR_LOG("%s: unknown game build: no hand aim, shot or viewmodel hooks", kTag);
    }
    // The pause action is the Escape key.
    installKeyInjection();
    bool angle = false;
    const input::CommandHookUse use =
        input::commandHookUse(true, cfg.path, roomScaleSettings().follow.enabled);
    const bool command = installUserCmdHooks(use, true, angle);
    s.userCmdHook = command && use == input::CommandHookUse::Controllers;
    s.angleHook = angle;
    // The thumb-rest wheel's cvars: the game's wheel open delay, and its slowdown (rest_wheel.cpp).
    locateWheelCvars();
    const bool wantPad =
        cfg.path == input::InputPath::XInput || (cfg.path == input::InputPath::Auto && !s.userCmdHook);
    if (wantPad) {
        s.xinputHook = installXInputHook();
        s.xinputActive.store(s.xinputHook, std::memory_order_release);
    }
    // The game's rumble for the vibration; the signature is its own check.
    if (cfg.haptics > 0.0f) {
        s.rumbleHook = installRumbleHook();
    }
    // bHaptics reads the player from the camera hook and the shots from the fire hook (below).
    startBhaptics();
    // The game's prompts name the VR buttons; the signatures are their own check.
    if (cfg.buttonPrompts) {
        s.promptHooks = installPromptHooks();
    }
    bool launcherHook = false; // the shoulder launcher drawn at the head (launcher_place.hpp)
    if (knownBuild) {
        bool fire = false;
        s.setViewAnglesHook = installAimHooks(fire);
        s.fireHook = fire;
        if (cfg.viewmodel) {
            s.viewmodelHook = installViewmodelHook();
        }
        s.demonAimHook = installDemonAimHook();
        s.facingHook = installFacingHook();
        s.climbHook = installClimbHook();
        // The equipment launcher aims from its own joint, not the view: only for ETERNALVR_EQUIPMENT_AIM.
        if (cfg.aim == input::AimSource::Hand && cfg.actionAim.equipment != input::ActionAimSource::Same) {
            s.equipmentHook = installEquipmentLaunchHook();
            s.belchAxisHook = installBelchAxisHook();
            launcherHook = launcher_place::install();
            if (!launcherHook) {
                EVR_LOG(
                    "%s: WARNING: equipment aim %s without the shoulder launcher hook: the launcher stays at "
                    "the weapon hand, so the Flame Belch's flames leave it there (along the %s) and a launch "
                    "starts at the %s ray's start",
                    kTag, input::actionAimSourceName(cfg.actionAim.equipment),
                    input::actionAimSourceName(cfg.actionAim.equipment),
                    input::actionAimSourceName(cfg.actionAim.equipment));
            }
        }
        // The off hand on the game's left arm and the weapon arm's IK need the arms at the weapon hand
        // (docs/VR_HANDS_HUD.md); without the viewmodel hook both arms stay the game's. Hidden arms use the
        // same hook (its tick hides them).
        if (s.viewmodelHook && (cfg.offhand != input::OffhandMode::Game || cfg.offhandTrace ||
                                cfg.weaponArm == input::WeaponArmMode::Ik || cfg.armsHidden)) {
            bool weaponArm = false;
            s.offhandHook = installOffhandHook(weaponArm);
            s.weaponArmHook = s.offhandHook && weaponArm;
        }
        // Rig tests: the hands' model (ETERNALVR_ARM_MODEL); nothing is installed for the game's.
        if (cfg.armModel != input::ArmModel::Game) {
            arm_model::install();
        }
        if (cfg.armsHidden && !s.offhandHook) {
            EVR_LOG("%s: the arms stay shown (ETERNALVR_ARMS=hidden needs the viewmodel and hands hooks)",
                    kTag);
        }
    }
    EVR_LOG(
        "%s: game hooks: user command %s, turn %s, virtual gamepad %s, forced view %s, shots %s, viewmodel "
        "%s, off hand %s (%s), weapon arm %s (%s), rumble %s, demon aim %s, look-at triggers %s, climbable "
        "walls %s, button prompts %s, equipment launch %s, Flame Belch plume %s, shoulder launcher %s",
        kTag, s.userCmdHook ? "on" : (command ? "body follow only" : "off"), onOff(s.angleHook),
        onOff(s.xinputActive.load()), onOff(s.setViewAnglesHook), onOff(s.fireHook), onOff(s.viewmodelHook),
        onOff(s.offhandHook && (cfg.offhand != input::OffhandMode::Game || cfg.offhandTrace)),
        input::offhandModeName(cfg.offhand), onOff(s.weaponArmHook), input::weaponArmModeName(cfg.weaponArm),
        onOff(s.rumbleHook), onOff(s.demonAimHook), onOff(s.facingHook), onOff(s.climbHook),
        onOff(s.promptHooks), onOff(s.equipmentHook), onOff(s.belchAxisHook), onOff(launcherHook));
    EVR_LOG("%s: hooks in use: %d of %d mid hooks, %d of %d inline hooks", kTag, midHookCount(), kMaxMidHooks,
            inlineHookCount(), kMaxInlineHooks);
    if (!s.userCmdHook && !s.xinputActive.load()) {
        EVR_LOG("%s: no input path to the game: controller buttons and movement do nothing", kTag);
    }
}

// Gamepad mode (the controllers off, features/input/gamepad_mode.hpp): the game's own pad, keyboard and mouse
// input stays as it is. The user-command hook goes in for body follow alone (usercmd_hook_use.hpp);
// everything else here only reads the game and its pad.
void installGamepadMode() {
    const input::ControllerSettings& cfg = settings();
    State& s = state();
    if (input::commandHookUse(false, cfg.path, roomScaleSettings().follow.enabled) ==
        input::CommandHookUse::BodyFollow) {
        bool angle = false;
        const bool command = installUserCmdHooks(input::CommandHookUse::BodyFollow, false, angle);
        EVR_LOG(
            "room: body follow: %s (motion controllers off): only body follow's move goes into the game's "
            "command, the game's own pad, keyboard and mouse input stays as the game builds it",
            command ? "the user-command hook is in place for body follow alone"
                    : "the user-command hook could not be installed");
    }
    const input::GamepadModeHooks use = input::gamepadModeHooks(s.player.init(), cfg.bhaptics);
    const bool pad = use.padWatch && installPadWatch();
    if (use.viewGate) {
        // Read only without the controllers: SetViewAngles is counted, the fire hook only notes shots.
        bool fire = false;
        s.setViewAnglesHook = installAimHooks(fire);
        s.fireHook = fire;
        setGamepadView();
    }
    if (use.demonAim) {
        s.demonAimHook = installDemonAimHook();
    }
    if (use.bhaptics) {
        startBhaptics();
    }
    EVR_LOG(
        "%s: gamepad mode (motion controllers off): the game's pad, keyboard and mouse input stays as the "
        "game builds it; pad watch %s (the recenter hold, the comfort vignette), forced view %s, shots %s, "
        "demon aim %s%s",
        kTag, onOff(pad), onOff(s.setViewAnglesHook), onOff(s.fireHook), onOff(s.demonAimHook),
        use.viewGate ? "" : " (unknown game build: no forced-view gate)");
}

} // namespace

input::AimSource activeAim() {
    return pilotingDemon() ? input::demonAimSource(settings()) : settings().aim;
}

XrSpace weaponAimSpace() {
    State& s = state();
    if (!settings().enabled || activeAim() != input::AimSource::Hand || !s.attached.load()) {
        return XR_NULL_HANDLE;
    }
    std::shared_lock lock(s.xrMutex);
    return s.xr.aimSpaces[weaponHand() == input::Hand::Left ? 0 : 1];
}

XrSpace offHandGripSpace() {
    State& s = state();
    if (!settings().enabled || !s.attached.load()) {
        return XR_NULL_HANDLE;
    }
    std::shared_lock lock(s.xrMutex);
    return s.xr.gripSpaces[weaponHand() == input::Hand::Left ? 1 : 0];
}

XrSpace weaponHandAimSpace() {
    State& s = state();
    if (!settings().enabled || !s.attached.load()) {
        return XR_NULL_HANDLE;
    }
    std::shared_lock lock(s.xrMutex);
    return s.xr.aimSpaces[weaponHand() == input::Hand::Left ? 0 : 1];
}

void installGameHooks() {
    const bool controllers = settings().enabled;
    // The caller checked the guard; asked again here because installing is what the guard gates.
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: the multiplayer guard is not armed; no controller hooks", kTag);
        return;
    }
    std::call_once(g_installOnce, controllers ? &install : &installGamepadMode);
}

} // namespace evr::vkcore::controllers
