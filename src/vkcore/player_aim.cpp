#include "vkcore/player_aim.hpp"

#include "vkcore/game_build.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <cstring>

namespace evr::vkcore {

namespace {

constexpr std::size_t kPhysics = 0x8A50;
constexpr std::size_t kCommandAngles = kPhysics + 0x3DE0 + 0x1C;
constexpr std::size_t kViewAngles = kPhysics + 0x3F10;
constexpr std::size_t kDeltaViewAngles = kPhysics + 0x3F1C;
constexpr std::size_t kStateDeltaViewAngles = kPhysics + 0x3F28 + 0x80;

xr_math::IdAngles readAngles(const std::byte* at) {
    float v[3];
    std::memcpy(v, at, sizeof(v));
    return {v[0], v[1], v[2]};
}

} // namespace

bool PlayerAim::init() {
    const auto* module = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    const GameBuild* build = currentGameBuild();
    if (!build) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
        EVR_LOG("aim: game build timestamp 0x%08lx is not one the player layout was read from; head aim off",
                static_cast<unsigned long>(nt->FileHeader.TimeDateStamp));
        return false;
    }
    playerVtable_ = module + build->playerVtable;
    available_ = true;
    return true;
}

bool PlayerAim::isPlayer(const std::byte* object) const {
    if (!available_ || !object) {
        return false;
    }
    const std::byte* vtable = nullptr;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable == playerVtable_;
}

PlayerAim::Sample PlayerAim::read(const std::byte* player) const {
    Sample s;
    s.view = readAngles(player + kViewAngles);
    s.delta = readAngles(player + kDeltaViewAngles);
    s.stateDelta = readAngles(player + kStateDeltaViewAngles);
    std::int16_t cmd[3];
    std::memcpy(cmd, player + kCommandAngles, sizeof(cmd));
    constexpr float kShortToDegrees = 360.0f / 65536.0f;
    s.command = {cmd[0] * kShortToDegrees, cmd[1] * kShortToDegrees, cmd[2] * kShortToDegrees};
    return s;
}

xr_math::IdAngles PlayerAim::addDelta(std::byte* player, DeltaField field, float pitch, float yaw) const {
    std::byte* at = player + (field == DeltaField::Physics ? kDeltaViewAngles : kStateDeltaViewAngles);
    float v[3];
    std::memcpy(v, at, sizeof(v));
    v[0] = xr_math::normalize180(v[0] + pitch);
    v[1] = xr_math::normalize180(v[1] + yaw);
    std::memcpy(at, v, sizeof(v));
    return {v[0], v[1], v[2]};
}

} // namespace evr::vkcore
