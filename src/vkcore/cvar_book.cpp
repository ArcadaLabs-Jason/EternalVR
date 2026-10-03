#include "vkcore/cvar_book.hpp"

#include "platform/mp_policy/saved_cvars.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore::cvar_book {

namespace {

constexpr const char* kTag = "cvar restore";

struct Target {
    std::byte* object = nullptr;
    SetStringFn set = nullptr;
};

std::mutex g_mutex;
mp_policy::SavedCvars<Target> g_saved;
std::once_flag g_listenOnce;
bool g_listening = false;

// The cvar's value as text: its values block keeps the integer at +0x08 and the float at +0x0C (the reads of
// runtime_cvars.cpp and taa_locate.cpp).
std::string valueText(const std::byte* object) {
    const std::byte* values = nullptr;
    std::memcpy(&values, object, sizeof(values));
    int integer = 0;
    float number = 0.0f;
    if (values) {
        std::memcpy(&integer, values + 0x08, sizeof(integer));
        std::memcpy(&number, values + 0x0C, sizeof(number));
    }
    return mp_policy::cvarValueText(integer, number);
}

// The trip listener, on the thread that tripped, once. The one write after a trip: each cvar the layer wrote
// goes back to its value before the layer's first write, so a player who carries on flat (and may go online)
// does not keep the layer's values.
void restoreOnTrip() {
    std::lock_guard lock(g_mutex);
    std::size_t restored = 0;
    std::size_t left = 0;
    for (const auto& e : g_saved.takeAll()) {
        const std::string now = valueText(e.target.object);
        if (const char* why = mp_policy::cvarLeftOnTrip(e.name)) {
            ++left;
            EVR_LOG("%s: %s left at %s, not set back to %s (the multiplayer guard tripped; %s)", kTag,
                    e.name.c_str(), now.c_str(), e.value.c_str(), why);
            continue;
        }
        ++restored;
        if (now != e.value) {
            e.target.set(e.target.object, e.value.c_str(), true);
        }
        EVR_LOG("%s: %s %s -> %s, its value before the layer's first write (the multiplayer guard tripped; "
                "reads %s)",
                kTag, e.name.c_str(), now.c_str(), e.value.c_str(), valueText(e.target.object).c_str());
    }
    EVR_LOG("%s: the multiplayer guard tripped: %zu cvar(s) the layer wrote set back, %zu left as they are; "
            "cvars the launcher set on the command line keep the command line's values",
            kTag, restored, left);
}

} // namespace

void listen() {
    // Never with g_mutex held: after a trip the listener runs at once and takes it.
    std::call_once(g_listenOnce, [] {
        g_listening = mp_guard::addTripListener(&restoreOnTrip);
        if (!g_listening) {
            EVR_LOG("%s: no room in the multiplayer guard's trip listeners; the layer writes no cvars", kTag);
        }
    });
}

bool write(std::string_view name, std::byte* object, SetStringFn set, const char* value) {
    if (!object || !set || !value) {
        return false;
    }
    listen();
    std::lock_guard lock(g_mutex);
    if (!g_listening || !mp_guard::allowsGameTouch()) {
        return false;
    }
    g_saved.beforeWrite(name, valueText(object), Target{object, set});
    set(object, value, true);
    return true;
}

} // namespace evr::vkcore::cvar_book
