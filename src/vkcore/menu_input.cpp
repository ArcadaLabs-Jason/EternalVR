// The menu router's events into the game (menu_input.hpp).

#include "vkcore/menu_input.hpp"

#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <atomic>

namespace evr::vkcore::menu_input {

namespace {

std::atomic<bool> g_suppress{false};
std::atomic<std::uint64_t> g_moves{0};
std::atomic<std::uint64_t> g_clicks{0};
std::atomic<std::uint64_t> g_wheels{0};
std::atomic<std::uint64_t> g_keys{0};
std::atomic<std::uint64_t> g_failed{0};
std::atomic<int> g_logged{0};

const char* kindName(menu::RouterEvent::Kind kind) {
    switch (kind) {
    case menu::RouterEvent::Kind::Move:
        return "move";
    case menu::RouterEvent::Kind::ButtonDown:
        return "button down";
    case menu::RouterEvent::Kind::ButtonUp:
        return "button up";
    case menu::RouterEvent::Kind::RightButtonDown:
        return "right button down";
    case menu::RouterEvent::Kind::RightButtonUp:
        return "right button up";
    case menu::RouterEvent::Kind::Wheel:
        return "wheel";
    case menu::RouterEvent::Kind::KeyDown:
        return "key down";
    case menu::RouterEvent::Kind::KeyUp:
        return "key up";
    }
    return "?";
}

} // namespace

void send(const std::vector<menu::RouterEvent>& events) {
    void* window = gameWindow();
    for (const menu::RouterEvent& e : events) {
        bool ok = false;
        switch (e.kind) {
        case menu::RouterEvent::Kind::Move:
            ok = injectMouse(e.dx, e.dy, 0, 0, window);
            g_moves.fetch_add(1, std::memory_order_relaxed);
            break;
        case menu::RouterEvent::Kind::ButtonDown:
            ok = injectMouse(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN, 0, window);
            g_clicks.fetch_add(1, std::memory_order_relaxed);
            break;
        case menu::RouterEvent::Kind::ButtonUp:
            ok = injectMouse(0, 0, RI_MOUSE_LEFT_BUTTON_UP, 0, window);
            break;
        case menu::RouterEvent::Kind::RightButtonDown:
            ok = injectMouse(0, 0, RI_MOUSE_RIGHT_BUTTON_DOWN, 0, window);
            g_clicks.fetch_add(1, std::memory_order_relaxed);
            break;
        case menu::RouterEvent::Kind::RightButtonUp:
            ok = injectMouse(0, 0, RI_MOUSE_RIGHT_BUTTON_UP, 0, window);
            break;
        case menu::RouterEvent::Kind::Wheel:
            ok = injectMouse(0, 0, RI_MOUSE_WHEEL, e.wheel, window);
            g_wheels.fetch_add(1, std::memory_order_relaxed);
            break;
        case menu::RouterEvent::Kind::KeyDown:
            ok = injectKey(e.key, true, window);
            g_keys.fetch_add(1, std::memory_order_relaxed);
            break;
        case menu::RouterEvent::Kind::KeyUp:
            ok = injectKey(e.key, false, window);
            break;
        }
        if (!ok) {
            g_failed.fetch_add(1, std::memory_order_relaxed);
        }
        // Moves come every frame the ray moves; the log keeps the first few and every button and key.
        const char* failed = ok ? "" : " (not delivered)";
        if (e.kind == menu::RouterEvent::Kind::Move) {
            if (g_logged.fetch_add(1) < 4) {
                EVR_LOG("menu: move by %d, %d%s", e.dx, e.dy, failed);
            }
        } else if (e.kind == menu::RouterEvent::Kind::Wheel) {
            EVR_LOG("menu: wheel %d%s", e.wheel, failed);
        } else if (e.kind == menu::RouterEvent::Kind::KeyDown || e.kind == menu::RouterEvent::Kind::KeyUp) {
            EVR_LOG("menu: %s 0x%02x%s", kindName(e.kind), e.key, failed);
        } else {
            EVR_LOG("menu: %s%s", kindName(e.kind), failed);
        }
    }
}

void setSuppressGameplay(bool suppress) {
    if (g_suppress.exchange(suppress) != suppress) {
        EVR_LOG("menu: controllers' gameplay input %s", suppress ? "held back (menu)" : "back on");
    }
}

bool suppressGameplay() {
    return g_suppress.load(std::memory_order_relaxed);
}

Counters counters() {
    return {g_moves.load(), g_clicks.load(), g_wheels.load(), g_keys.load(), g_failed.load()};
}

} // namespace evr::vkcore::menu_input
