// Motion controllers at the camera hook (controllers.hpp): the hands at the time the head was predicted for,
// and the weapon hand's aim smoothing (features/input/aim_smoothing.hpp), so the gun, the shots and the
// reticle share one ray.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/controllers.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <shared_mutex>

namespace evr::vkcore::controllers {

namespace {

// Smooths the weapon hand's aim orientation in LOCAL (a recenter changes room space, not the hand), and
// returns the ray as tracked and as used, both in LOCAL.
WeaponAim smoothWeaponAim(State& s, XrTime poseTime, Pose& roomAim) {
    Pose roomFromLocal;
    {
        std::lock_guard lock(s.roomMutex);
        roomFromLocal = s.roomFromLocal;
    }
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
    if (!s.attached.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    updateForcedView(player, cutscene, cameraAnimation);
    noteBhapticsFrame(player);
    refreshTestInput();
    GameViewPoses poses;
    {
        std::shared_lock lock(s.xrMutex);
        if (!s.attached.load()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 2; ++i) {
            poses.aimValid[i] = locate(s.xr, s.xr.aimSpaces[i], poseTime, poses.aim[i]);
            poses.gripValid[i] = locate(s.xr, s.xr.gripSpaces[i], poseTime, poses.grip[i]);
        }
    }
    if (const auto test = testInput()) {
        for (const input::Hand hand : {input::Hand::Left, input::Hand::Right}) {
            if (const auto pose = input::testHandPose(*test, hand, headTracking.position)) {
                const auto i = static_cast<std::size_t>(hand);
                poses.aimValid[i] = poses.gripValid[i] = true;
                poses.aim[i] = poses.grip[i] = *pose;
            }
        }
    }
    // Under hand aim (the demon's while piloting one) the weapon hand's ray is smoothed before anything reads
    // it: the viewmodel and fire hooks and the aim take it from `poses`, the reticle from the view's record.
    std::optional<WeaponAim> weapon;
    const auto hand = static_cast<std::size_t>(weaponHand());
    if (activeAim() == input::AimSource::Hand && poses.aimValid[hand]) {
        weapon = smoothWeaponAim(s, poseTime, poses.aim[hand]);
    } else if (s.aimFilter) {
        s.aimFilter->reset();
    }
    poses.valid = true;
    poses.head = headTracking;
    std::lock_guard viewLock(s.viewMutex);
    s.poses = poses;
    return weapon;
}

} // namespace evr::vkcore::controllers
