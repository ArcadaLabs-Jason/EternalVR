// Piloting a demon: the Revenant's first-person models stay where a flat screen shows them (demon_view.hpp,
// docs/VR_CONTROLLERS.md "Piloting a demon").
//
// The shoulder cannons (the 'horns' model, idDemonPlayer +0xB270) and its arms (the 'hands' model, +0xC818)
// are view-attached models the game places every tick inside the demon's update: the cannons at the demon
// camera's origin with its axis, the arms at the demon's eye with the camera axis turned by their 'camera'
// joint. On a flat screen the camera is the view, so the eyes only ever see those models from the front. In
// the headset the eyes follow the head: under hand aim the camera is the weapon hand's aim (demon_aim.cpp),
// so a hand turned away from where the head looks swung the cannons about the eye and the eyes saw them from
// the side, the open inside of the shoulders included; under head aim the head's roll and the room-scale
// offset did the same in small. A mid-hook just before each model's pose stores re-seats the pose the game
// computed onto the view the eyes are built on (xr_math/view_attached.hpp): the camera's origin plus the
// head's offset, turned to the head on this tick's body yaw. The game's own stores then carry it into the
// render model, one tick behind the head like the Slayer's hands. The cannons' muzzle joints move with them;
// whether the rockets start from those joints has not been checked.
//
// Steam build 25216728 [static, rig]: the sites are found by signature and the origin load checked at
// install; a build without them keeps the game's pose (logged).

#include "vkcore/demon_view.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/view_attached.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// The first check of each model's pose stores (r14 = the model's owner object), with the axis load that
// follows: movups xmm0,[rbp-0x78] for the cannons, [rbp-0x38] for the arms.
constexpr const char* kHornsSignature =
    "49 8B 4E 58 0F B6 81 B0 00 00 00 24 0C 3C 0C 74 ?? 0F 10 45 88 0F 11 81 64 01 00 00";
constexpr const char* kHandsSignature =
    "49 8B 4E 38 0F B6 81 B0 00 00 00 24 0C 3C 0C 74 ?? 0F 10 45 C8 0F 11 81 64 01 00 00";
// At +0x60 both sites load the origin: movsd xmm0,[rsp+0x50].
constexpr std::size_t kOriginLoadAt = 0x60;
constexpr std::uint8_t kOriginLoad[] = {0xF2, 0x0F, 0x10, 0x44, 0x24, 0x50};
constexpr std::size_t kOriginFromRsp = 0x50;
constexpr std::intptr_t kHornsAxisFromRbp = -0x78;
constexpr std::intptr_t kHandsAxisFromRbp = -0x38;

// idDemonPlayer (type info, build 25216728): the two models, and the camera's origin and axis.
constexpr std::size_t kDemonHorns = 0xB270;
constexpr std::size_t kDemonHands = 0xC818;
constexpr std::size_t kDemonCameraOrigin = 0x10150 + 0x28;
constexpr std::size_t kDemonCameraAxis = 0x10150 + 0x34;

constexpr double kFreshSeconds = 0.25;

struct Site {
    const char* name;
    std::size_t member;
    std::intptr_t axisFromRbp;
    std::atomic<std::uint64_t> placed{0};
    std::atomic<bool> loggedOther{false};
};
Site g_horns{"cannons", kDemonHorns, kHornsAxisFromRbp};
Site g_hands{"arms", kDemonHands, kHandsAxisFromRbp};

// The last piloted view, published by the camera hook: the head's offset and the view axis, both in the frame
// of the body yaw the view was built on.
struct PilotView {
    bool valid = false;
    LONGLONG qpc = 0;
    Vec3 offset;              // body frame: forward, left, up (world units)
    xr_math::IdViewAxis axis; // body frame
    float bodyYaw = 0.0f;
};
std::mutex g_viewMutex;
PilotView g_view;

bool finite(const float* values, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(values[i])) {
            return false;
        }
    }
    return true;
}

xr_math::IdViewAxis axisOf(const float (&m)[9]) {
    return {{m[0], m[1], m[2]}, {m[3], m[4], m[5]}, {m[6], m[7], m[8]}};
}

void reseat(const HookRegisters& regs, Site& site) {
    if (!mp_guard::allowsGameTouch() || !pilotingDemon()) {
        return;
    }
    const std::uintptr_t demon = regs.r14 - site.member;
    if (demon != pilotedDemon()) {
        // Another demon's models (or a stale pointer): left as the game placed them. Logged once per site.
        if (!site.loggedOther.exchange(true)) {
            EVR_LOG("%s: demon %s of %p placed while piloting %p: left to the game (another demon's)", kTag,
                    site.name, reinterpret_cast<const void*>(demon),
                    reinterpret_cast<const void*>(pilotedDemon()));
        }
        return;
    }
    PilotView view;
    {
        std::lock_guard lock(g_viewMutex);
        view = g_view;
    }
    if (!view.valid || secondsSince(view.qpc) > kFreshSeconds) {
        return;
    }
    // The body yaw this tick's view will be built on (demon_aim.cpp sets it before the game's update runs);
    // without the demon's aim, the one the published view was built on.
    const std::optional<PilotAim> aim = pilotAim();
    const xr_math::IdViewAxis body = xr_math::axisFromAngles({0.0f, aim ? aim->bodyYaw : view.bodyYaw, 0.0f});
    const xr_math::IdViewAxis viewAxis = xr_math::axisFromFrame(view.axis, body);
    const auto* demonBytes = reinterpret_cast<const std::byte*>(demon);
    float camera[3];
    float cameraAxis[9];
    if (!safeCopy(camera, demonBytes + kDemonCameraOrigin, sizeof(camera)) ||
        !safeCopy(cameraAxis, demonBytes + kDemonCameraAxis, sizeof(cameraAxis)) || !finite(camera, 3) ||
        !finite(cameraAxis, 9) || !xr_math::isOrthonormal(axisOf(cameraAxis), 1e-2f)) {
        return;
    }
    auto* originAt = reinterpret_cast<std::byte*>(regs.rsp + kOriginFromRsp);
    auto* axisAt = reinterpret_cast<std::byte*>(static_cast<std::intptr_t>(regs.rbp) + site.axisFromRbp);
    float origin[3];
    float axis[9];
    if (!safeCopy(origin, originAt, sizeof(origin)) || !safeCopy(axis, axisAt, sizeof(axis)) ||
        !finite(origin, 3) || !finite(axis, 9)) {
        return;
    }
    const xr_math::AttachedPose placed = xr_math::reseatOnView(
        {{origin[0], origin[1], origin[2]}, axisOf(axis)}, {camera[0], camera[1], camera[2]},
        axisOf(cameraAxis), xr_math::fromFrame(view.offset, body), viewAxis);
    const float newOrigin[3] = {placed.origin.x, placed.origin.y, placed.origin.z};
    const float newAxis[9] = {placed.axis.forward.x, placed.axis.forward.y, placed.axis.forward.z,
                              placed.axis.left.x,    placed.axis.left.y,    placed.axis.left.z,
                              placed.axis.up.x,      placed.axis.up.y,      placed.axis.up.z};
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    safeCopy(originAt, newOrigin, sizeof(newOrigin));
    safeCopy(axisAt, newAxis, sizeof(newAxis));
    const std::uint64_t n = site.placed.fetch_add(1, std::memory_order_relaxed);
    if (n < 2 || (n + 1) % 5000 == 0) {
        EVR_LOG(
            "%s: demon %s on the view: game origin (%.2f %.2f %.2f) -> (%.2f %.2f %.2f), camera (%.2f %.2f "
            "%.2f) forward (%.2f %.2f %.2f), view forward (%.2f %.2f %.2f); %llu placed",
            kTag, site.name, origin[0], origin[1], origin[2], newOrigin[0], newOrigin[1], newOrigin[2],
            camera[0], camera[1], camera[2], cameraAxis[0], cameraAxis[1], cameraAxis[2], viewAxis.forward.x,
            viewAxis.forward.y, viewAxis.forward.z, static_cast<unsigned long long>(n + 1));
    }
}

void onHorns(const HookRegisters& regs) {
    reseat(regs, g_horns);
}

void onHands(const HookRegisters& regs) {
    reseat(regs, g_hands);
}

bool installSite(const GameImage& image, const char* signature, MidHookCallback callback, const char* name) {
    const std::byte* site = findUnique(image, kTag, name, signature);
    if (!site) {
        return false;
    }
    if (!image.contains(site + kOriginLoadAt, sizeof(kOriginLoad)) ||
        std::memcmp(site + kOriginLoadAt, kOriginLoad, sizeof(kOriginLoad)) != 0) {
        EVR_LOG("%s: the demon %s store does not load its origin from [rsp+0x50]; left to the game", kTag,
                name);
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), callback, error)) {
        EVR_LOG("%s: demon %s hook at RVA 0x%X failed: %s", kTag, name, image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: demon %s hook at RVA 0x%X: they stay on the view while piloting", kTag, name,
            image.rva(site));
    return true;
}

} // namespace

void notePilotView(const xr_math::IdViewAxis& body, Vec3 headOffset, const xr_math::IdViewAxis& view) {
    const float bodyYaw = std::atan2(body.forward.y, body.forward.x) * 57.29578f;
    std::lock_guard lock(g_viewMutex);
    g_view = {true, nowQpc(), xr_math::toFrame(headOffset, body), xr_math::axisToFrame(view, body), bodyYaw};
}

bool installDemonModelHooks() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const bool horns = installSite(image, kHornsSignature, &onHorns, "cannons");
    const bool hands = installSite(image, kHandsSignature, &onHands, "arms");
    return horns || hands;
}

} // namespace evr::vkcore::controllers
