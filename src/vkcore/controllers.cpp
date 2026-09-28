// Motion controllers: installing the game hooks (controllers.hpp).

#include "vkcore/controllers.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

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
    s.userCmdHook = installUserCmdHooks(cfg.path != input::InputPath::XInput, angle);
    s.angleHook = angle;
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
    if (knownBuild) {
        bool fire = false;
        s.setViewAnglesHook = installAimHooks(fire);
        s.fireHook = fire;
        if (cfg.viewmodel) {
            s.viewmodelHook = installViewmodelHook();
        }
        // The off hand on the game's left arm needs the arms at the weapon hand (docs/VR_HANDS_HUD.md).
        if (s.viewmodelHook && (cfg.offhand != input::OffhandMode::Game || cfg.offhandTrace)) {
            s.offhandHook = installOffhandHook();
        }
    }
    EVR_LOG(
        "%s: game hooks: user command %s, turn %s, virtual gamepad %s, forced view %s, shots %s, viewmodel "
        "%s, off hand %s (%s), rumble %s",
        kTag, onOff(s.userCmdHook), onOff(s.angleHook), onOff(s.xinputActive.load()),
        onOff(s.setViewAnglesHook), onOff(s.fireHook), onOff(s.viewmodelHook), onOff(s.offhandHook),
        input::offhandModeName(cfg.offhand), onOff(s.rumbleHook));
    if (!s.userCmdHook && !s.xinputActive.load()) {
        EVR_LOG("%s: no input path to the game: controller buttons and movement do nothing", kTag);
    }
}

} // namespace

XrSpace weaponAimSpace() {
    State& s = state();
    if (!settings().enabled || settings().aim != input::AimSource::Hand || !s.attached.load()) {
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

void installGameHooks() {
    if (!settings().enabled) {
        return;
    }
    // The caller checked the guard; asked again here because installing is what the guard gates.
    if (!mp_guard::allowsGameTouch()) {
        EVR_LOG("%s: the multiplayer guard is not armed; no controller hooks", kTag);
        return;
    }
    std::call_once(g_installOnce, &install);
}

} // namespace evr::vkcore::controllers
