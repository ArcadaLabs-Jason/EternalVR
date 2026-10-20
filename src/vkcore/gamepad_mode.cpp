// Gamepad mode in the game: the pads the game reads, and the game-view reads (gamepad_mode.hpp).

#include "vkcore/gamepad_mode.hpp"

#include "features/input/pad_watch.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/room_scale.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

std::mutex g_padMutex;
input::PadWatch g_pads;
bool g_bothSticks = false; // under g_padMutex: the last value logged
std::atomic<bool> g_loggedPad{false};
std::atomic<bool> g_view{false};

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

void notePadRead(unsigned user, const std::optional<input::PadState>& pad) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const double now = nowSeconds();
    bool both = false;
    bool changed = false;
    {
        std::lock_guard lock(g_padMutex);
        g_pads.update(user, pad, now);
        both = g_pads.bothSticks(now);
        changed = both != g_bothSticks;
        g_bothSticks = both;
    }
    if (pad && !g_loggedPad.exchange(true)) {
        EVR_LOG("%s: gamepad mode: the game reads a pad (user %u); it is watched, never changed", kTag, user);
    }
    if (changed) {
        EVR_LOG("%s: gamepad mode: both sticks %s", kTag, both ? "pressed (the recenter hold)" : "let go");
    }
    // The pad's chord is the press itself: no mapper hold has passed before it, so the long press counts it
    // all.
    noteRecenterBinding(both, 0.0f);
}

ArtificialMotion padMotion() {
    input::PadMotion m;
    {
        std::lock_guard lock(g_padMutex);
        m = g_pads.motion(nowSeconds());
    }
    ArtificialMotion out;
    out.move = m.move;
    out.turnStick = m.turn;
    return out;
}

bool gamepadView() {
    return g_view.load(std::memory_order_acquire);
}

void setGamepadView() {
    g_view.store(true, std::memory_order_release);
}

} // namespace evr::vkcore::controllers
