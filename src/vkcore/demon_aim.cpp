// Piloting a demon: the demon aims where the head looks, or under the demon's hand aim where the weapon hand
// points (demon_view.hpp; ETERNALVR_DEMON_AIM, or ETERNALVR_AIM when it is unset).
//
// The demon's camera, movement and attacks follow its own view angles: the command's angles plus its physics
// deltaViewAngles. The detour on the demon's per-tick update (idDemonPlayer_Revenant, vtable slot +0x1968)
// sets those angles from the aim: the body yaw (the command's yaw, which the stick turns, plus the offset
// it had when piloting started) plus the aim's yaw, and the aim's pitch. The view is built on the same body
// yaw with the head on top, so the camera follows the head while the demon's attacks follow the aim. It calls
// the game's own setters first: the view setter (which also updates the local input buffers and the
// deltaViewAngles), then the basis setter, which attacks read before the camera update; the command handed
// to the update carries the matching angles. Build 25216728 (Steam) and 1.0.56.0 (Game Pass) addresses,
// each checked against the function's first bytes.

#include "vkcore/demon_view.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/head_aim.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// usercmd_t: 0x98 bytes; byte 9 set while the game inhibits the command (a pause, a popup, the possession
// intro), the angles as three 16-bit shorts at +0x1C.
constexpr std::size_t kCommandSize = 0x98;
constexpr std::size_t kCommandInhibited = 9;
constexpr std::size_t kCommandAngles = 0x1C;
// idDemonPlayer: physics object at +0x58E8, its deltaViewAngles at +0x3FA8.
constexpr std::size_t kDemonDeltaViewAngles = 0x58E8 + 0x3FA8;
constexpr float kMaxPitch = 89.0f;
constexpr double kFreshSeconds = 0.25;

struct Build {
    const char* name;
    unsigned update;
    unsigned setBasis;
    unsigned setView;
};
constexpr Build kBuilds[] = {{"Steam 25216728", 0x133DAA0, 0x12C07C0, 0x12C0620},
                             {"Game Pass 1.0.56.0", 0x1342080, 0x12C4DA0, 0x12C4C00}};

// The first bytes of each function; the update's 24th byte is a RIP displacement, left out.
constexpr std::array<std::uint8_t, 23> kUpdateBytes = {0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xAC,
                                                       0x24, 0x58, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC,
                                                       0xA8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05};
constexpr std::array<std::uint8_t, 24> kSetBasisBytes = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                                         0xEC, 0x30, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
                                                         0x48, 0x8B, 0xCB, 0x48, 0x8D, 0x54, 0x24, 0x20};
constexpr std::array<std::uint8_t, 24> kSetViewBytes = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                                        0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
                                                        0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56};

using UpdateFn = void (*)(void* demon, const void* previous, const void* current);
using SetBasisFn = void (*)(void* demon, const float* axis);
using SetViewFn = void (*)(void* demon, const float* angles, bool local);

UpdateFn g_update = nullptr;
SetBasisFn g_setBasis = nullptr;
SetViewFn g_setView = nullptr;

// The aim, published by the camera hook thread.
std::atomic<float> g_aimPitch{0.0f};
std::atomic<float> g_aimInputYaw{0.0f};
std::atomic<double> g_aimSeconds{-1.0};
// The body yaw, published by the update (game thread).
std::atomic<float> g_bodyYaw{0.0f};
std::atomic<float> g_aimYaw{0.0f};
std::atomic<double> g_bodySeconds{-1.0};

// Game thread only.
std::uintptr_t g_offsetDemon = 0; // the demon the offset was taken for
float g_bodyOffset = 0.0f;        // body yaw less the command's yaw
std::uint64_t g_aimed = 0;

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float shortToDegrees(std::int16_t value) {
    return static_cast<float>(value) * (360.0f / 65536.0f);
}

std::int16_t degreesToShort(float degrees) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(
        static_cast<std::int32_t>(std::lround(std::remainder(degrees, 360.0f) * (65536.0f / 360.0f)))));
}

bool readDelta(const void* demon, float (&delta)[3]) {
    return safeCopy(delta, static_cast<const std::byte*>(demon) + kDemonDeltaViewAngles, sizeof(delta)) &&
           std::isfinite(delta[0]) && std::isfinite(delta[1]) && std::isfinite(delta[2]);
}

void onUpdate(void* demon, const void* previous, const void* current) {
    const double now = nowSeconds();
    if (!mp_guard::allowsGameTouch() || !pilotingDemon() || !previous || !current ||
        reinterpret_cast<std::uintptr_t>(demon) != pilotedDemon() ||
        now - g_aimSeconds.load() > kFreshSeconds) {
        g_update(demon, previous, current);
        return;
    }
    std::array<std::byte, kCommandSize> command{};
    float delta[3] = {};
    if (!safeCopy(command.data(), current, command.size()) || command[kCommandInhibited] != std::byte{0} ||
        !readDelta(demon, delta)) {
        g_offsetDemon = 0; // taken again when the command is the player's
        g_update(demon, previous, current);
        return;
    }
    std::int16_t angles[3] = {};
    std::memcpy(angles, command.data() + kCommandAngles, sizeof(angles));
    const float commandYaw = shortToDegrees(angles[1]);
    const float aimPitch = g_aimPitch.load();
    const float aimYaw = g_aimInputYaw.load();
    if (g_offsetDemon != reinterpret_cast<std::uintptr_t>(demon)) {
        // No jump: the body starts where the demon faces less the aim's yaw.
        g_offsetDemon = reinterpret_cast<std::uintptr_t>(demon);
        g_bodyOffset = xr_math::normalize180(delta[1] - aimYaw);
        EVR_LOG("%s: demon aim: the demon follows the %s (view yaw %.1f, aim yaw %.1f pitch %.1f)", kTag,
                input::aimSourceName(input::demonAimSource(settings())),
                xr_math::normalize180(commandYaw + delta[1]), aimYaw, aimPitch);
    }
    const float bodyYaw = xr_math::normalize180(commandYaw + g_bodyOffset);
    const float desired[3] = {std::clamp(aimPitch, -kMaxPitch, kMaxPitch),
                              xr_math::normalize180(bodyYaw + aimYaw), 0.0f};
    g_setView(demon, desired, true);
    if (!readDelta(demon, delta)) {
        g_update(demon, previous, current);
        return;
    }
    for (int i = 0; i < 3; ++i) {
        angles[i] = degreesToShort(desired[i] - delta[i]);
    }
    std::memcpy(command.data() + kCommandAngles, angles, sizeof(angles));
    const xr_math::IdViewAxis axis = xr_math::axisFromAngles({desired[0], desired[1], 0.0f});
    const float basis[9] = {axis.forward.x, axis.forward.y, axis.forward.z, axis.left.x, axis.left.y,
                            axis.left.z,    axis.up.x,      axis.up.y,      axis.up.z};
    g_setBasis(demon, basis);
    g_bodyYaw.store(bodyYaw);
    g_aimYaw.store(desired[1]);
    g_bodySeconds.store(now);
    if (++g_aimed == 1) {
        EVR_LOG("%s: demon aim: first aimed update (body yaw %.1f, aim pitch %.1f yaw %.1f)", kTag, bodyYaw,
                desired[0], desired[1]);
    }
    g_update(demon, previous, command.data());
}

template <std::size_t N>
bool startsWith(const GameImage& image, unsigned rva, const std::array<std::uint8_t, N>& bytes) {
    return image.contains(image.base + rva, N) && std::memcmp(image.base + rva, bytes.data(), N) == 0;
}

} // namespace

void notePilotAim(float pitch, float yaw) {
    g_aimPitch.store(pitch);
    g_aimInputYaw.store(yaw);
    g_aimSeconds.store(nowSeconds());
}

std::optional<PilotAim> pilotAim() {
    if (!pilotingDemon() || nowSeconds() - g_bodySeconds.load() > kFreshSeconds) {
        return std::nullopt;
    }
    return PilotAim{g_bodyYaw.load(), g_aimYaw.load()};
}

bool installDemonAimHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    for (const Build& b : kBuilds) {
        if (!startsWith(image, b.update, kUpdateBytes) || !startsWith(image, b.setBasis, kSetBasisBytes) ||
            !startsWith(image, b.setView, kSetViewBytes)) {
            continue;
        }
        g_setBasis = reinterpret_cast<SetBasisFn>(const_cast<std::byte*>(image.base + b.setBasis));
        g_setView = reinterpret_cast<SetViewFn>(const_cast<std::byte*>(image.base + b.setView));
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(image.base + b.update),
                               reinterpret_cast<void*>(&onUpdate), reinterpret_cast<void**>(&g_update),
                               error)) {
            EVR_LOG("%s: demon aim hook at RVA 0x%X failed: %s", kTag, b.update, error.c_str());
            return false;
        }
        EVR_LOG("%s: demon aim hook at RVA 0x%X (%s)", kTag, b.update, b.name);
        return true;
    }
    EVR_LOG("%s: demon aim: the demon functions are not where this build has them; the demon keeps the "
            "game's aim",
            kTag);
    return false;
}

} // namespace evr::vkcore::controllers
