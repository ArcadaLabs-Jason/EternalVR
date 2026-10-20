// The shoulder launcher's model under ETERNALVR_EQUIPMENT_AIM (launcher_place.hpp).

#include "vkcore/launcher_place.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/launcher_pose.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::launcher_place {

namespace {

constexpr const char* kTag = "controllers";

// The item model's placement (0x138A480) at its stores: movss [rsp+0x38],xmm0; movzx eax,byte [r14+0xB0];
// movsd xmm0,[rsp+0x30]; and al,0xC; cmp al,0xC; je; movsd [r14+0x158],xmm0 (the model's origin).
constexpr const char* kPlaceSignature =
    "F3 0F 11 44 24 38 41 0F B6 86 B0 00 00 00 F2 0F 10 44 24 30 24 0C 3C 0C "
    "74 54 F2 41 0F 11 86 58 01 00 00";
constexpr std::size_t kPlaceHook = 6; // movzx eax, byte ptr [r14+0xB0]
constexpr std::size_t kOriginFromRsp = 0x30;
constexpr std::size_t kAxisFromRsp = 0x68;
constexpr std::size_t kHandsOwner = 0x358;     // idHands::owner
constexpr std::size_t kHandsArmsModel = 0x370; // the arms model the item's joint is read from
constexpr std::size_t kModelOrigin = 0x158;    // its render entity's current origin
constexpr std::size_t kModelAxis = 0x164;      // and axis (rows forward, left, up)
constexpr std::size_t kItemDecl = 0x8;         // idHandsItem's decl
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580;
constexpr int kLeftShoulderSlot = 5;
constexpr int kRightShoulderSlot = 6;
// A launch reads its muzzle in the tick that placed the model: a placement older than this is not that one.
constexpr double kDrawnSeconds = 0.1;

struct Drawn {
    LONGLONG qpc = 0;
    Vec3 along;
};
std::mutex g_drawnMutex;
std::array<Drawn, 2> g_drawn{}; // by slot - kLeftShoulderSlot, under g_drawnMutex

std::array<std::atomic<bool>, 2> g_seen{};
LogCap g_lines{6, 2000};

bool readFloats(const std::byte* at, float* out, std::size_t count) {
    if (!safeCopy(out, at, count * sizeof(float))) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(out[i])) {
            return false;
        }
    }
    return true;
}

xr_math::IdViewAxis axisOf(const float (&m)[9]) {
    return {{m[0], m[1], m[2]}, {m[3], m[4], m[5]}, {m[6], m[7], m[8]}};
}

void onPlace(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    const auto* item = reinterpret_cast<const std::byte*>(regs.r13);
    const auto* hands = reinterpret_cast<const std::byte*>(regs.r12);
    int slot = -1;
    const std::byte* owner = nullptr;
    if (cfg.aim != input::AimSource::Hand || cfg.actionAim.equipment == input::ActionAimSource::Same ||
        !safeRead(item, slot) || (slot != kLeftShoulderSlot && slot != kRightShoulderSlot) ||
        !safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return;
    }
    if (!s.attached.load(std::memory_order_acquire) || s.yielding.load()) {
        return; // forced views (glory kills, cutscenes) keep the game's own hands animation
    }
    const TickView view = tickView();
    const WorldHand& world = view.world;
    const std::byte* arms = nullptr;
    float eye[3];
    float armsOrigin[3];
    float armsAxis[9];
    auto* originAt = reinterpret_cast<std::byte*>(regs.rsp + kOriginFromRsp);
    auto* axisAt = reinterpret_cast<std::byte*>(regs.rsp + kAxisFromRsp);
    float origin[3];
    float axis[9];
    if (!view.worldCurrent || !readFloats(owner + kPlayerFirstPersonOrigin, eye, 3) ||
        !safeRead(hands + kHandsArmsModel, arms) || !arms ||
        !readFloats(arms + kModelOrigin, armsOrigin, 3) || !readFloats(arms + kModelAxis, armsAxis, 9) ||
        !readFloats(originAt, origin, 3) || !readFloats(axisAt, axis, 9)) {
        return;
    }
    const Vec3 eyeAt{eye[0], eye[1], eye[2]};
    const bool offHand = cfg.actionAim.equipment == input::ActionAimSource::OffHand && world.offAimValid;
    const Vec3 along = offHand ? world.offAim.axis.forward : world.head.axis.forward;
    const std::optional<xr_math::EyeRelativePose> moved = xr_math::launcherAtHead(
        {Vec3{armsOrigin[0], armsOrigin[1], armsOrigin[2]} - eyeAt, axisOf(armsAxis)},
        {Vec3{origin[0], origin[1], origin[2]} - eyeAt, axisOf(axis)}, world.head, along);
    if (!moved || !mp_guard::allowsGameTouch()) {
        return;
    }
    const Vec3 at = eyeAt + moved->offset;
    const float newOrigin[3] = {at.x, at.y, at.z};
    const float newAxis[9] = {moved->axis.forward.x, moved->axis.forward.y, moved->axis.forward.z,
                              moved->axis.left.x,    moved->axis.left.y,    moved->axis.left.z,
                              moved->axis.up.x,      moved->axis.up.y,      moved->axis.up.z};
    safeCopy(originAt, newOrigin, sizeof(newOrigin));
    safeCopy(axisAt, newAxis, sizeof(newAxis));
    {
        std::lock_guard lock(g_drawnMutex);
        g_drawn[static_cast<std::size_t>(slot - kLeftShoulderSlot)] = {nowQpc(), along};
    }
    const bool first = !g_seen[static_cast<std::size_t>(slot - kLeftShoulderSlot)].exchange(true);
    std::uint64_t skipped = 0;
    const game::GameActionSet held = heldActions();
    if (first || ((game::contains(held, game::GameAction::FlameBelch) ||
                   game::contains(held, game::GameAction::Equipment)) &&
                  g_lines.due(GetTickCount64(), skipped))) {
        const std::byte* decl = nullptr;
        const std::string name =
            safeRead(item + kItemDecl, decl) && decl ? itemDeclName(decl) : std::string{};
        const Vec3 fromHead = moved->offset - world.head.offset;
        EVR_LOG("%s: shoulder launcher (slot %d, '%s'): model (%.2f %.2f %.2f) dir (%.3f %.3f %.3f) -> (%.2f "
                "%.2f %.2f) dir (%.3f %.3f %.3f) at the head, along the %s; arms model (%.2f %.2f %.2f) dir "
                "(%.3f %.3f %.3f), eye (%.2f %.2f %.2f), launcher %.2f from the head",
                kTag, slot, name.c_str(), origin[0], origin[1], origin[2], axis[0], axis[1], axis[2],
                newOrigin[0], newOrigin[1], newOrigin[2], newAxis[0], newAxis[1], newAxis[2],
                offHand ? "off hand" : "head", armsOrigin[0], armsOrigin[1], armsOrigin[2], armsAxis[0],
                armsAxis[1], armsAxis[2], eye[0], eye[1], eye[2],
                std::sqrt(dot(fromHead, fromHead)) / world.unitsPerMetre);
    }
}

} // namespace

std::optional<Vec3> drawnAlong(int slot) {
    if (slot != kLeftShoulderSlot && slot != kRightShoulderSlot) {
        return std::nullopt;
    }
    Drawn drawn;
    {
        std::lock_guard lock(g_drawnMutex);
        drawn = g_drawn[static_cast<std::size_t>(slot - kLeftShoulderSlot)];
    }
    if (drawn.qpc == 0 || secondsSince(drawn.qpc) > kDrawnSeconds) {
        return std::nullopt;
    }
    return drawn.along;
}

bool install() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "shoulder launcher placement", kPlaceSignature);
    if (!site) {
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site + kPlaceHook), &onPlace, error)) {
        EVR_LOG("%s: shoulder launcher hook at RVA 0x%X failed: %s", kTag, image.rva(site + kPlaceHook),
                error.c_str());
        return false;
    }
    EVR_LOG("%s: shoulder launcher hook at RVA 0x%X", kTag, image.rva(site + kPlaceHook));
    return true;
}

} // namespace evr::vkcore::controllers::launcher_place
