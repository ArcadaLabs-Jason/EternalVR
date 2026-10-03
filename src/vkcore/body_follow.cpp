// Room-scale body follow in the game (body_follow.hpp, docs/VR_ROOMSCALE.md "Body follow").
//
// Facts (build 25216728, docs/rig-findings/collision-query.md section 2): idPlayer::physicsObjHavok
// (idHavokPhysics_Player) is at +0x8A50; its GetOrigin (vslot 0x670) returns futureOrigin (+0x128) when the
// byte at +0x1C8 has bit 0x20, else bodyOrigin (+0xB0). The origin is at the feet. The same choice is made
// here from the fields, without calling into the game.

#include "vkcore/body_follow.hpp"

#include "vkcore/log.hpp"
#include "vkcore/player_aim.hpp"
#include "vkcore/seh_filter.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace evr::vkcore::body_follow {

namespace {

constexpr std::size_t kPlayerPhysics = 0x8A50;
constexpr std::size_t kPhysicsFlags = 0x1C8;
constexpr std::uint8_t kFutureOriginBit = 0x20;
constexpr std::size_t kFutureOrigin = 0x128;
constexpr std::size_t kBodyOrigin = 0xB0;
// The eye is this far above the origin and this close to it horizontally when the read is right (the
// standing eye is 1.657 m up, the crouched 0.876 m).
constexpr float kMinEyeAbove = 0.3f;
constexpr float kMaxEyeAbove = 2.5f;
constexpr float kMaxEyeAside = 0.5f;
// A request older than this (the camera hook stopped: menus, loading) is not sent.
constexpr double kRequestStaleSeconds = 0.1;

double nowSeconds() {
    LARGE_INTEGER t;
    LARGE_INTEGER f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
}

bool copyGuarded(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Camera hook only.
PlayerAim g_player;
std::once_flag g_playerOnce;
bool g_playerKnown = false;
int g_source = -1; // 0 physics, 1 eye
std::uint64_t g_implausible = 0;

std::atomic<bool> g_hookInstalled{false};
std::mutex g_requestMutex;
roomscale::FollowMove g_request;
double g_requestSeconds = -1.0;
std::atomic<bool> g_stick{false};
std::atomic<bool> g_jumpOrDash{false};
std::atomic<double> g_lastCommandSeconds{-1.0};
std::atomic<std::uint64_t> g_commands{0};
std::atomic<int> g_peakAxis{0};
std::atomic<int> g_testCommand{0}; // the command test's value (0: none)
std::atomic<bool> g_testSideways{false};

// The physics origin, or nullopt when it cannot be read.
std::optional<Vec3> physicsOrigin(const std::byte* player) {
    const std::byte* physics = player + kPlayerPhysics;
    std::uint8_t flags = 0;
    if (!copyGuarded(&flags, physics + kPhysicsFlags, sizeof(flags))) {
        return std::nullopt;
    }
    float v[3] = {};
    if (!copyGuarded(v, physics + ((flags & kFutureOriginBit) ? kFutureOrigin : kBodyOrigin), sizeof(v))) {
        return std::nullopt;
    }
    const Vec3 origin{v[0], v[1], v[2]};
    return finite(origin) ? std::optional<Vec3>(origin) : std::nullopt;
}

} // namespace

std::optional<float> feetHeight(const std::byte* player) {
    std::call_once(g_playerOnce, [] { g_playerKnown = g_player.init(); });
    if (!g_playerKnown || !g_player.isPlayer(player)) {
        return std::nullopt;
    }
    const auto origin = physicsOrigin(player);
    return origin ? std::optional<float>(origin->z) : std::nullopt;
}

std::optional<Vec3> playerOrigin(const std::byte* player, Vec3 eye, float unitsPerMetre) {
    std::call_once(g_playerOnce, [] { g_playerKnown = g_player.init(); });
    if (!g_playerKnown || !g_player.isPlayer(player) || !finite(eye)) {
        return std::nullopt;
    }
    const auto origin = physicsOrigin(player);
    bool plausible = false;
    if (origin) {
        const float above = (eye.z - origin->z) / unitsPerMetre;
        const float aside = std::hypot(eye.x - origin->x, eye.y - origin->y) / unitsPerMetre;
        plausible = above >= kMinEyeAbove && above <= kMaxEyeAbove && aside <= kMaxEyeAside;
        if (!plausible && ++g_implausible <= 3) {
            EVR_LOG("room: body follow: the physics origin (%.2f %.2f %.2f) is %.2f m below and %.2f m "
                    "beside the "
                    "eye (%.2f %.2f %.2f); not used",
                    origin->x, origin->y, origin->z, above, aside, eye.x, eye.y, eye.z);
        }
    }
    const int source = plausible ? 0 : 1;
    if (source != g_source) {
        g_source = source;
        EVR_LOG("room: body follow: the player's origin from %s",
                plausible ? "its physics (futureOrigin or bodyOrigin)"
                          : "the game's eye (the physics read failed)");
    }
    return plausible ? *origin : eye;
}

void publish(roomscale::FollowMove move) {
    std::lock_guard lock(g_requestMutex);
    g_request = move;
    g_requestSeconds = nowSeconds();
}

input::MoveAxes commandMove(bool moving, bool jumpOrDash, bool gameplay, float viewYawRoom) {
    if (moving) {
        g_stick.store(true, std::memory_order_relaxed);
    }
    if (jumpOrDash) {
        g_jumpOrDash.store(true, std::memory_order_relaxed);
    }
    const double now = nowSeconds();
    if (const int test = g_testCommand.load(std::memory_order_relaxed); test != 0) {
        if (!gameplay) {
            return {};
        }
        g_peakAxis.store(std::abs(test), std::memory_order_relaxed);
        return g_testSideways.load(std::memory_order_relaxed) ? input::MoveAxes{0, test}
                                                              : input::MoveAxes{test, 0};
    }
    roomscale::FollowMove request;
    {
        std::lock_guard lock(g_requestMutex);
        if (g_requestSeconds < 0.0 || now - g_requestSeconds > kRequestStaleSeconds) {
            return {};
        }
        request = g_request;
    }
    if (moving || jumpOrDash || !gameplay || request == roomscale::FollowMove{} ||
        !std::isfinite(viewYawRoom)) {
        return {};
    }
    // The request is in the room frame (yaw 0); the game moves along its view yaw.
    const input::Axis2 view = input::rotateIntoViewFrame({request.right, request.forward}, 0.0f, viewYawRoom);
    const input::MoveAxes axes = input::quantizeMove(view, input::kMaxMoveAxis);
    if (axes == input::MoveAxes{}) {
        return {};
    }
    g_lastCommandSeconds.store(now, std::memory_order_relaxed);
    const std::uint64_t count = g_commands.fetch_add(1, std::memory_order_relaxed) + 1;
    const int peak = std::max(std::abs(axes.forward), std::abs(axes.right));
    int seen = g_peakAxis.load(std::memory_order_relaxed);
    while (peak > seen && !g_peakAxis.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {
    }
    if (count <= 5) {
        EVR_LOG(
            "room: body follow: command %llu moves (%d %d) of %d (room request %.3f %.3f, view yaw %.1f deg)",
            static_cast<unsigned long long>(count), axes.forward, axes.right, input::kMaxMoveAxis,
            request.forward, request.right, viewYawRoom * 57.29578f);
    }
    return axes;
}

void setTestCommand(int value, bool sideways) {
    g_testSideways.store(sideways, std::memory_order_relaxed);
    g_testCommand.store(std::clamp(value, -input::kMaxMoveAxis, input::kMaxMoveAxis),
                        std::memory_order_relaxed);
}

void setCommandHook(bool installed) {
    g_hookInstalled.store(installed, std::memory_order_release);
}

CommandState takeCommandState() {
    CommandState out;
    out.hookInstalled = g_hookInstalled.load(std::memory_order_acquire);
    out.stick = g_stick.exchange(false, std::memory_order_relaxed);
    out.jumpOrDash = g_jumpOrDash.exchange(false, std::memory_order_relaxed);
    const double last = g_lastCommandSeconds.load(std::memory_order_relaxed);
    out.commanded = last >= 0.0 && nowSeconds() - last < roomscale::kResumeSeconds;
    return out;
}

std::uint64_t followCommands() {
    return g_commands.load(std::memory_order_relaxed);
}

int takePeakAxis() {
    return g_peakAxis.exchange(0, std::memory_order_relaxed);
}

} // namespace evr::vkcore::body_follow
