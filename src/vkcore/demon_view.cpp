// Piloting a demon (demon_view.hpp).

#include "vkcore/demon_view.hpp"

#include "game/eternal/usercmd_buttons.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

namespace evr::vkcore::controllers {

namespace {

// idPlayer (build 25216728): the handle of the entity the player controls (controlledEntities' first
// entry).
constexpr std::size_t kPlayerControlledHandle = 0x88B0;

// idDemonPlayer (type info, build 25216728): the controlling player's handle and the local-control flags.
constexpr std::size_t kDemonPlayerHandle = 0x20A08;
constexpr std::size_t kDemonLocallyControlled = 0x20A48;
constexpr std::size_t kDemonViewedFirstPerson = 0x20A49;
// idDemonPlayer's own command bindings: fire, mode (the Revenant's rocket barrage), dash (afterburner), and
// two that fly (jump / jetpack).
constexpr std::size_t kDemonBindings = 0x37490;
constexpr std::size_t kDemonBindingCount = 5;

// An entity handle: generation, the generation the pointer was cached at, the pointer. A generation of
// 0x1FFFFFE marks a freed slot.
struct Handle {
    std::uint32_t generation;
    std::uint32_t cached;
    std::uintptr_t pointer;
};
constexpr std::uint32_t kFreedGeneration = 0x1FFFFFE;

constexpr double kStaleSeconds = 0.5;

std::atomic<double> g_lastDemonSeconds{-1.0};
std::atomic<bool> g_piloting{false};
std::atomic<std::uintptr_t> g_demon{0}; // the piloted demon last seen

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool enabled() {
    static const bool on = [] {
        std::wstring value;
        return !(readEnv(L"ETERNALVR_DEMON_VIEW", value) && value == L"0");
    }();
    return on;
}

// The demon's player pointer and flags, or false when the memory is not readable.
bool readHandle(const std::byte* at, Handle& handle) {
    __try {
        std::memcpy(&handle, at, sizeof(handle));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool resolved(const Handle& h) {
    return h.pointer != 0 && h.generation == h.cached && h.generation != kFreedGeneration;
}

bool readDemon(const std::byte* entity, Handle& player, std::uint8_t& controlled, std::uint8_t& firstPerson) {
    __try {
        std::memcpy(&player, entity + kDemonPlayerHandle, sizeof(player));
        controlled = *reinterpret_cast<const std::uint8_t*>(entity + kDemonLocallyControlled);
        firstPerson = *reinterpret_cast<const std::uint8_t*>(entity + kDemonViewedFirstPerson);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A first-person demon the given idPlayer controls: locally controlled, viewed first person, and its player
// handle resolves back to that idPlayer.
bool demonOf(const std::byte* demon, const std::byte* playerEntity) {
    Handle owner{};
    std::uint8_t controlled = 0;
    std::uint8_t firstPerson = 0;
    if (!readDemon(demon, owner, controlled, firstPerson) || controlled != 1 || firstPerson != 1) {
        return false;
    }
    return resolved(owner) && reinterpret_cast<const std::byte*>(owner.pointer) == playerEntity;
}

// The first entity a player is seen controlling, logged once with its flags (the offsets are checked on
// the Steam build only).
std::atomic<std::uintptr_t> g_loggedControlled{0};

void logControlled(const std::byte* playerEntity, const Handle& handle) {
    if (g_loggedControlled.exchange(handle.pointer) == handle.pointer) {
        return;
    }
    Handle owner{};
    std::uint8_t controlled = 0;
    std::uint8_t firstPerson = 0;
    const bool read =
        readDemon(reinterpret_cast<const std::byte*>(handle.pointer), owner, controlled, firstPerson);
    EVR_LOG(
        "controllers: the player %p controls entity %p (handle %u/%u); demon fields %s: locally controlled "
        "%u, first person %u, its player %p (%s)",
        static_cast<const void*>(playerEntity), reinterpret_cast<const void*>(handle.pointer),
        handle.generation, handle.cached, read ? "read" : "not readable", controlled, firstPerson,
        reinterpret_cast<const void*>(owner.pointer), resolved(owner) ? "resolved" : "not resolved");
}

bool readBindings(std::uintptr_t demon, std::uint64_t (&bindings)[kDemonBindingCount]) {
    __try {
        std::memcpy(bindings, reinterpret_cast<const std::byte*>(demon) + kDemonBindings, sizeof(bindings));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::atomic<std::uintptr_t> g_loggedBindings{0};

} // namespace

bool isPilotedDemon(const std::byte* entity, const PlayerAim& player) {
    if (!entity) {
        return false;
    }
    if (player.isPlayer(entity)) {
        // The game builds the piloted demon's view through the idPlayer: follow its controlled entity.
        Handle controlledEntity{};
        if (!readHandle(entity + kPlayerControlledHandle, controlledEntity) || !resolved(controlledEntity) ||
            reinterpret_cast<const std::byte*>(controlledEntity.pointer) == entity) {
            return false;
        }
        logControlled(entity, controlledEntity);
        if (!demonOf(reinterpret_cast<const std::byte*>(controlledEntity.pointer), entity)) {
            return false;
        }
        g_demon.store(controlledEntity.pointer, std::memory_order_relaxed);
        return true;
    }
    // A view built from the demon itself.
    Handle owner{};
    if (!readHandle(entity + kDemonPlayerHandle, owner) || !resolved(owner)) {
        return false;
    }
    const auto* ownerEntity = reinterpret_cast<const std::byte*>(owner.pointer);
    if (!player.isPlayer(ownerEntity) || !demonOf(entity, ownerEntity)) {
        return false;
    }
    g_demon.store(reinterpret_cast<std::uintptr_t>(entity), std::memory_order_relaxed);
    return true;
}

void notePilotedDemon(bool piloting) {
    const double now = nowSeconds();
    if (piloting) {
        g_lastDemonSeconds.store(now, std::memory_order_relaxed);
    }
    const double last = g_lastDemonSeconds.load(std::memory_order_relaxed);
    const bool active = last >= 0.0 && now - last <= kStaleSeconds;
    if (active != g_piloting.exchange(active)) {
        EVR_LOG(active ? "controllers: piloting a demon: the move stick follows its facing, body follow off%s"
                       : "controllers: back in the Slayer's body%s",
                enabled() ? "" : " (ETERNALVR_DEMON_VIEW=0: handling off)");
    }
}

bool pilotingDemon() {
    return enabled() && g_piloting.load(std::memory_order_relaxed);
}

std::uintptr_t pilotedDemon() {
    return g_demon.load(std::memory_order_relaxed);
}

std::uint64_t pilotedDemonButtons(const game::GameActionSet& actions, std::uint64_t buttons) {
    const std::uintptr_t demon = g_demon.load(std::memory_order_relaxed);
    std::uint64_t bindings[kDemonBindingCount] = {};
    if (!pilotingDemon() || demon == 0 || !readBindings(demon, bindings)) {
        return buttons;
    }
    if (g_loggedBindings.exchange(demon) != demon) {
        EVR_LOG("controllers: the demon's bindings: fire 0x%llx, mode 0x%llx, dash 0x%llx, fly 0x%llx 0x%llx",
                static_cast<unsigned long long>(bindings[0]), static_cast<unsigned long long>(bindings[1]),
                static_cast<unsigned long long>(bindings[2]), static_cast<unsigned long long>(bindings[3]),
                static_cast<unsigned long long>(bindings[4]));
    }
    using game::GameAction;
    struct Pair {
        GameAction action;
        std::uint64_t demonBits;
    };
    const Pair pairs[] = {{GameAction::Fire, bindings[0]},
                          {GameAction::WeaponMod, bindings[1]},
                          {GameAction::Dash, bindings[2]},
                          {GameAction::Jump, bindings[3] | bindings[4]}};
    for (const Pair& p : pairs) {
        if (p.demonBits != 0 && game::contains(actions, p.action)) {
            buttons = (buttons & ~game::usercmdButtons(p.action)) | p.demonBits;
        }
    }
    return buttons;
}

} // namespace evr::vkcore::controllers
