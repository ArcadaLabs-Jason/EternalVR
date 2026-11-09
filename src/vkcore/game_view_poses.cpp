// Motion controllers at the camera hook (controllers.hpp): the hands at the time the head was predicted for,
// the weapon hand's aim smoothing (features/input/aim_smoothing.hpp) and two-handed aim (two_hand.hpp), so
// the gun, the shots and the reticle share one ray.

#include "vkcore/controllers_impl.hpp"

#include "features/input/controller_tilt.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/gamepad_mode.hpp"
#include "vkcore/two_hand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>

namespace evr::vkcore::controllers {

namespace {

Pose roomFromLocalOf(State& s) {
    std::lock_guard lock(s.roomMutex);
    return s.roomFromLocal;
}

// Smooths the weapon hand's aim orientation in LOCAL (a recenter changes room space, not the hand), and
// returns the ray as tracked and as used, both in LOCAL.
WeaponAim smoothWeaponAim(State& s, XrTime poseTime, Pose& roomAim) {
    const Pose roomFromLocal = roomFromLocalOf(s);
    WeaponAim aim;
    aim.tracked = compose(inverse(roomFromLocal), roomAim);
    aim.used = aim.tracked;
    if (s.aimFilter) {
        aim.used.orientation =
            s.aimFilter->update(aim.tracked.orientation, static_cast<double>(poseTime) * 1e-9);
        roomAim = compose(roomFromLocal, aim.used);
        roomAim.orientation = normalize(roomAim.orientation);
    }
    return aim;
}

} // namespace

std::optional<WeaponAim> beginGameView(
    XrTime poseTime, const Pose& headTracking, const std::byte* player, bool cutscene, bool cameraAnimation) {
    State& s = state();
    // Head aim keeps the view on a climbable wall with the controllers attached or not (climb_hook.cpp).
    const bool wallClimb = climbFrame();
    if (!s.attached.load(std::memory_order_acquire)) {
        if (gamepadView()) {
            // Gamepad mode (gamepad_mode.hpp): the gate, bHaptics and the piloted demon read the game's view;
            // there are no hands.
            updateForcedView(player, cutscene, cameraAnimation, wallClimb);
            noteBhapticsFrame(player);
            notePilotedDemon(isPilotedDemon(player, s.player));
        }
        two_hand::reset("no controllers");
        return std::nullopt;
    }
    updateForcedView(player, cutscene, cameraAnimation, wallClimb);
    noteBhapticsFrame(player);
    refreshTestInput();
    GameViewPoses poses;
    std::array<input::ControllerFit, 2> fits{};
    {
        std::shared_lock lock(s.xrMutex);
        if (!s.attached.load()) {
            two_hand::reset("no controllers");
            return std::nullopt;
        }
        fits = s.xr.fits;
        // Each hand's positions through their guards (pose_guards.hpp), as at the worker's sync.
        using pose_guards::Slot;
        for (std::size_t i = 0; i < 2; ++i) {
            poses.aimValid[i] = locate(s.xr, s.xr.aimSpaces[i], poseTime, poses.aim[i], nullptr, nullptr,
                                       nullptr, i == 0 ? Slot::ViewAimLeft : Slot::ViewAimRight);
            poses.gripValid[i] = locate(s.xr, s.xr.gripSpaces[i], poseTime, poses.grip[i], nullptr, nullptr,
                                        nullptr, i == 0 ? Slot::ViewGripLeft : Slot::ViewGripRight);
        }
    }
    if (const auto test = testInput()) {
        static std::uint64_t noiseFrame = 0; // the scripted hands' noise, new every frame (camera hook only)
        ++noiseFrame;
        for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
            if (const auto pose =
                    input::testHandPose(*test, hand, headTracking.position, qpcToSeconds(nowQpc()))) {
                const auto i = static_cast<std::size_t>(hand);
                poses.aimValid[i] = poses.gripValid[i] = true;
                // The fit and the controller angle, which the runtime gives the hand spaces
                // (controller_tilt.hpp).
                poses.aim[i] = poses.grip[i] = input::testHandNoise(
                    *test, hand, input::scriptedHandPose(*pose, fits[i], settings(), hand, weaponHand()),
                    noiseFrame);
            }
        }
    }
    // Under hand aim (the demon's while piloting one) the weapon hand's ray is smoothed before anything reads
    // it: the viewmodel and fire hooks and the aim take it from `poses`, the reticle from the view's record.
    std::optional<WeaponAim> weapon;
    const auto hand = static_cast<std::size_t>(weaponHand());
    if (activeAim() == input::AimSource::Hand && poses.aimValid[hand]) {
        weapon = smoothWeaponAim(s, poseTime, poses.aim[hand]);
        // The off hand as endGameView takes it (its grip, else its aim ray), for the frame log.
        const std::size_t off = 1 - hand;
        weapon->offValid = poses.gripValid[off] || poses.aimValid[off];
        if (weapon->offValid) {
            const Pose localFromRoom = inverse(roomFromLocalOf(s));
            weapon->offGrip = compose(localFromRoom, poses.gripValid[off] ? poses.grip[off] : poses.aim[off]);
            weapon->offAim = compose(localFromRoom, poses.aimValid[off] ? poses.aim[off] : poses.grip[off]);
        }
    } else if (s.aimFilter) {
        s.aimFilter->reset();
    }
    // The off hand on the gun's support point turns that ray to the line between the hands (two_hand.hpp).
    two_hand::aim(s, player, poses, weapon);
    poses.valid = true;
    poses.head = headTracking;
    std::lock_guard viewLock(s.viewMutex);
    s.poses = poses;
    return weapon;
}

} // namespace evr::vkcore::controllers
