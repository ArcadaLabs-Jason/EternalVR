// Head aim's gun (head_gun.hpp).

#include "vkcore/head_gun.hpp"

#include "features/input/gun_placement.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::vkcore::controllers::head_gun {

namespace {

constexpr const char* kTag = "controllers";
constexpr ULONGLONG kStatsMilliseconds = 10000;

std::mutex g_mutex;
input::HeadOffsetNote g_note;       // under g_mutex
const std::byte* g_hands = nullptr; // under g_mutex: the hands of the last tick
std::int64_t g_handsSince = 0;      // under g_mutex: their first tick

std::atomic<std::uint64_t> g_moves{0};
std::atomic<std::uint64_t> g_logged{0};
std::atomic<ULONGLONG> g_lastStats{0};

} // namespace

void noteView(Vec3 headOffset) {
    const LONGLONG now = nowQpc();
    std::lock_guard lock(g_mutex);
    g_note = {headOffset, now, true};
}

void forget() {
    std::lock_guard lock(g_mutex);
    g_note = {};
    g_hands = nullptr;
}

bool moveToHead(const std::byte* hands, std::byte* originAt, Vec3 eye) {
    const input::ViewClockRead views = viewClockRead();
    input::HeadOffsetNote note;
    std::int64_t since = 0;
    {
        std::lock_guard lock(g_mutex);
        if (hands != g_hands) {
            g_hands = hands; // new hands: a level load, or the first tick
            g_handsSince = views.now;
        }
        note = g_note;
        since = g_handsSince;
    }
    float game[3] = {};
    if (!safeCopy(game, originAt, sizeof(game)) || !mp_guard::allowsGameTouch()) {
        return false;
    }
    const std::optional<Vec3> moved = input::headGunOrigin({game[0], game[1], game[2]}, note, since, views);
    if (!moved) {
        return false;
    }
    const float written[3] = {moved->x, moved->y, moved->z};
    if (!safeCopy(originAt, written, sizeof(written))) {
        return false;
    }
    const std::uint64_t moves = g_moves.fetch_add(1, std::memory_order_relaxed) + 1;
    const ULONGLONG ticks = GetTickCount64();
    ULONGLONG last = g_lastStats.load();
    const bool first = g_logged.fetch_add(1) < 5;
    if (first || (ticks - last >= kStatsMilliseconds && g_lastStats.compare_exchange_strong(last, ticks))) {
        // The model's origin from the eye, before and after: the game's own placement is next to the eye.
        const Vec3 fromEye = *moved - eye;
        EVR_LOG("%s: viewmodel: head aim: game origin (%.2f %.2f %.2f) -> at the head (%.2f %.2f %.2f); eye "
                "(%.2f %.2f %.2f), head offset (%.2f %.2f %.2f), model from the eye %.2f; %llu placed",
                kTag, game[0], game[1], game[2], moved->x, moved->y, moved->z, eye.x, eye.y, eye.z,
                note.offset.x, note.offset.y, note.offset.z,
                std::sqrt(fromEye.x * fromEye.x + fromEye.y * fromEye.y + fromEye.z * fromEye.z),
                static_cast<unsigned long long>(moves));
    }
    return true;
}

} // namespace evr::vkcore::controllers::head_gun
