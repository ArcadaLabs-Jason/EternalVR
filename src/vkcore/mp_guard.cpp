// Multiplayer and online safety guard (mp_guard.hpp): the latch, the hook callbacks, install() and the
// menu poll; mp_guard_points.cpp locates, validates and hooks each detection point. Detection points,
// their signatures and the evidence for each are in docs/rig-findings/mp-guard.md; the RVAs in comments
// are Steam build 25216728 (Rev 3.2).

#include "vkcore/mp_guard.hpp"

#include "common/finishes.hpp"
#include "platform/mp_policy/guard_refusal.hpp"
#include "vkcore/game_build.hpp"
#include "vkcore/game_string.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard_points.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/status_file.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore::mp_guard {

namespace {

using mp_policy::GuardState;
using mp_policy::Signal;

mp_policy::Latch g_latch;
mp_policy::StartupPhase g_startup;
std::once_flag g_installOnce;
std::once_flag g_screenOnce;
bool g_commandLineAllowed = true;

// The idMainMenu* global (RVA 0x46AE140), set before the menu hook is installed.
std::atomic<const std::byte* const*> g_mainMenuSlot{nullptr};
std::atomic<ULONGLONG> g_testTripAt{0};
std::atomic<std::uint32_t> g_mapLoads{0};
std::mutex g_lastMapMutex;
std::string g_lastMap;              // under g_lastMapMutex
std::atomic<bool> g_playMap{false}; // the last map loaded is one the player plays in
// The main menu's screen the poll logged last (the first 200 changes; the QA suite's menu path waits on it).
std::atomic<std::int32_t> g_loggedScreen{-1};
std::atomic<std::uint32_t> g_screenLines{0};

// ---------------------------------------------------------------------------------------------------
// Tripping

// Fired once, by the call that closed the latch (Latch::trip is true for one call only).
mp_policy::TripListeners g_tripListeners;

void tripWith(Signal signal, const char* detail) {
    if (g_latch.trip(signal)) {
        // The listeners first: a log line that fails must not keep the game's state from coming back.
        g_tripListeners.fire();
        finishes([&] {
            EVR_LOG(
                "%s: TRIPPED by %s (%s): the game keeps running; camera writes, head aim and key injection "
                "are off for the rest of this process and the headset shows the flat screen; relaunch "
                "without VR for multiplayer",
                kTag, mp_policy::toString(signal), detail);
            if (const std::size_t failed = g_tripListeners.failures()) {
                EVR_LOG("%s: %zu trip listener(s) threw; the others ran", kTag, failed);
            }
        });
    }
}

// Reads game memory that may have been freed; false instead of a crash.
bool safeCopy(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

template <typename T>
bool safeRead(const std::byte* at, T& value) {
    return at && safeCopy(&value, at, sizeof(T));
}

// The main menu's idMenu, or nullptr.
const std::byte* mainMenuMenu() {
    const std::byte* const* slot = g_mainMenuSlot.load(std::memory_order_acquire);
    const std::byte* mainMenu = nullptr;
    const std::byte* menu = nullptr;
    if (!slot || !safeRead(reinterpret_cast<const std::byte*>(slot), mainMenu) || !mainMenu ||
        !safeRead(mainMenu + kMainMenuMenu, menu)) {
        return nullptr;
    }
    return menu;
}

void checkScreen(Signal signal, int screen, const char* which) {
    if (mp_policy::isOnlineMenuScreen(screen)) {
        char detail[64];
        std::snprintf(detail, sizeof(detail), "main menu %s screen %d", which, screen);
        tripWith(signal, detail);
    }
}

// ---------------------------------------------------------------------------------------------------
// Hook callbacks (game threads)

void onMapLoad(const HookRegisters& regs) {
    // A page at a time (game_string.hpp): the name often ends near the end of a page, and a fixed-size
    // copy past it faulted on a good pointer. A name not read to its NUL (a page unreadable, or longer than
    // the buffer) is a prefix that could have lost an online segment: it classifies Unknown and trips.
    char name[260] = {};
    const GameString read = copyGameString(name, sizeof(name), reinterpret_cast<const void*>(regs.rdx));
    g_startup.mapLoadStarted();
    {
        std::lock_guard lock(g_lastMapMutex);
        g_lastMap = name;
    }
    // Before the count it goes with.
    g_playMap.store(read.complete && mp_policy::isPlayMap(name), std::memory_order_release);
    g_mapLoads.fetch_add(1, std::memory_order_release);
    const mp_policy::MapClass mapClass = mp_policy::classifyMapRead(name, read.complete);
    const char* unread = read.complete ? "" : ", name unreadable or over-long";
    EVR_LOG("%s: map load '%s' (%s%s)", kTag, name, mp_policy::toString(mapClass), unread);
    if (mapClass != mp_policy::MapClass::SinglePlayer) {
        char detail[320];
        std::snprintf(detail, sizeof(detail), "map '%s', %s%s", name, mp_policy::toString(mapClass), unread);
        tripWith(Signal::MapLoad, detail);
    }
}

void onSteamLobbyJoin(const HookRegisters&) {
    tripWith(Signal::SteamLobbyJoin, "GameLobbyJoinRequested_t");
}

void onSteamRichPresenceJoin(const HookRegisters&) {
    tripWith(Signal::SteamRichPresenceJoin, "GameRichPresenceJoinRequested_t");
}

void onXboxInviteEvent(const HookRegisters&) {
    tripWith(Signal::XboxInviteEvent, "the GDK invite callback");
}

void onXboxInviteDecoded(const HookRegisters&) {
    tripWith(Signal::XboxInviteDecoded, "DecodeInvitationEvent");
}

void onInviteConsumed(const HookRegisters&) {
    tripWith(Signal::InviteConsumed, "idOnlineSessionInviteManager::ConsumeInvite");
}

void onLobbySession(const HookRegisters&) {
    tripWith(Signal::LobbySession, "idLobbyUISessionCasualBattleArena created");
}

void onBattleArenaSession(const HookRegisters&) {
    // The game builds one during its initialisation, before any map loads (mp_policy::StartupPhase).
    if (!g_startup.battleArenaSessionTrips()) {
        EVR_LOG("%s: idBattleArenaGameSession built during start-up, before any map load: not a signal",
                kTag);
        return;
    }
    tripWith(Signal::BattleArenaSession, "idBattleArenaGameSession created");
}

// In idMenu::Update, just before activeScreen takes nextScreen; rsi is the idMenu.
void onMenuTransition(const HookRegisters& regs) {
    const auto* menu = reinterpret_cast<const std::byte*>(regs.rsi);
    if (!menu || menu != mainMenuMenu()) {
        return; // HUD and other menus number their screens differently
    }
    std::int32_t next = -1;
    if (safeRead(menu + kMenuNextScreen, next)) {
        checkScreen(Signal::MenuTransition, next, "next");
    }
}

// Fail closed: a check that throws (no memory for the map's name or its class on the way) trips the guard
// with its own signal instead of letting the game go on unchecked; the mid hook thunk would otherwise only
// log it. tripWith closes the latch before anything that allocates.
template <Signal S, MidHookCallback Check>
void failClosed(const HookRegisters& regs) noexcept {
    if (!finishes([&] { Check(regs); })) {
        tripWith(S, "its check threw (no memory); tripped to be safe");
    }
}

void readTestTrip() {
    std::wstring value;
    if (readEnv(L"ETERNALVR_GUARD_TEST_TRIP_MS", value) && !value.empty()) {
        const unsigned long ms = std::wcstoul(value.c_str(), nullptr, 10);
        g_testTripAt.store(GetTickCount64() + ms);
        EVR_LOG("%s: test trip in %lu ms (ETERNALVR_GUARD_TEST_TRIP_MS)", kTag, ms);
    }
}

} // namespace

bool screenCommandLine() {
    std::call_once(g_screenOnce, [] {
        const wchar_t* line = GetCommandLineW();
        const auto refused = mp_policy::screenCommandLine(line ? line : L"");
        if (refused) {
            g_commandLineAllowed = false;
            std::string detail =
                "argument \"" + std::string(refused->pattern) + "\" requests " + std::string(refused->reason);
            tripWith(Signal::CommandLine, detail.c_str());
            EVR_LOG("%s: the layer does not load; EternalVR is single-player only", kTag);
            // The launcher shows this in place of "VR is starting" (status::starting ran before the screen).
            const std::string reason = "\"" + std::string(refused->pattern) +
                                       "\" on the command line requests " + std::string(refused->reason) +
                                       "; EternalVR is single-player only";
            status::flat(reason.c_str());
        }
    });
    return g_commandLineAllowed;
}

GuardState install() {
    std::call_once(g_installOnce, [] {
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            g_latch.refuse();
            EVR_LOG("%s: REFUSED: the game module cannot be read; VR features stay off", kTag);
            status::flat("the game's program could not be read; VR stays off");
            return;
        }
        // Every point is attempted (no short-circuit) so the log names every failure.
        const bool mapLoad = points::installMapLoad(image, &failClosed<Signal::MapLoad, &onMapLoad>);
        // The platform's join detection: Steam's join callbacks, or on Game Pass the Xbox invite path.
        const GameBuild* build = findGameBuild(image.timestamp);
        const bool xbox = build && build->kind == GameBuildKind::GamePass;
        const bool steam =
            xbox ? points::installXboxJoin(image, &failClosed<Signal::XboxInviteEvent, &onXboxInviteEvent>,
                                           &failClosed<Signal::XboxInviteDecoded, &onXboxInviteDecoded>)
                 : points::installSteamJoin(
                       image, &failClosed<Signal::SteamLobbyJoin, &onSteamLobbyJoin>,
                       &failClosed<Signal::SteamRichPresenceJoin, &onSteamRichPresenceJoin>);
        const bool invite =
            points::installInviteConsumed(image, &failClosed<Signal::InviteConsumed, &onInviteConsumed>);
        const bool lobby =
            points::installLobbySession(image, &failClosed<Signal::LobbySession, &onLobbySession>);
        const bool session = points::installBattleArenaSession(
            image, &failClosed<Signal::BattleArenaSession, &onBattleArenaSession>);
        const bool menu = points::installMainMenu(
            image, &failClosed<Signal::MenuTransition, &onMenuTransition>, g_mainMenuSlot);
        mp_policy::DetectionPoints found;
        found.mapLoad = mapLoad;
        found.joins = steam;
        found.invites = invite;
        found.lobbySession = lobby;
        found.gameSession = session;
        found.mainMenu = menu;
        const bool all = found.all();
        EVR_LOG("%s: map load %s, %s joins %s, invites %s, lobby session %s, game session %s, main menu %s",
                kTag, mapLoad ? "ok" : "MISSING", xbox ? "Xbox" : "Steam", steam ? "ok" : "MISSING",
                invite ? "ok" : "MISSING", lobby ? "ok" : "MISSING", session ? "ok" : "MISSING",
                menu ? "ok" : "MISSING");
        if (!all) {
            g_latch.refuse();
            EVR_LOG("%s: REFUSED: a detection point is missing, so online play could not be detected; camera "
                    "writes, head aim and key injection stay off",
                    kTag);
            // The launcher shows this sentence: on a known build it names the missing points (T-094).
            // status::flat copies the text before it returns, so the temporary string may end here.
            status::flat(mp_policy::guardRefusalReason(found, build != nullptr, xbox,
                                                       "Steam build 25216728 or Game Pass 1.0.56.0")
                             .c_str());
            return;
        }
        if (g_latch.arm()) {
            readTestTrip();
            EVR_LOG("%s: armed", kTag);
        } else {
            EVR_LOG("%s: not armed: already %s by %s", kTag, mp_policy::toString(g_latch.state()),
                    mp_policy::toString(g_latch.firstSignal()));
        }
    });
    return g_latch.state();
}

bool addTripListener(TripListener listener) {
    return g_tripListeners.add(listener);
}

bool allowsGameTouch() {
    return g_latch.allowsGameTouch();
}

GuardState state() {
    return g_latch.state();
}

std::uint32_t mapLoads() {
    return g_mapLoads.load(std::memory_order_acquire);
}

std::string lastMapLoaded() {
    std::lock_guard lock(g_lastMapMutex);
    return g_lastMap;
}

bool inPlayMap() {
    return g_playMap.load(std::memory_order_acquire);
}

namespace {

void pollMenu() {
    if (g_latch.state() == GuardState::Tripped) {
        return;
    }
    const ULONGLONG testAt = g_testTripAt.load(std::memory_order_relaxed);
    if (testAt && GetTickCount64() >= testAt) {
        tripWith(Signal::Test, "ETERNALVR_GUARD_TEST_TRIP_MS");
        return;
    }
    const std::byte* menu = mainMenuMenu();
    std::int32_t active = -1;
    std::int32_t next = -1;
    if (menu && safeRead(menu + kMenuActiveScreen, active) && safeRead(menu + kMenuNextScreen, next)) {
        checkScreen(Signal::MenuPoll, active, "active");
        checkScreen(Signal::MenuPoll, next, "next");
        if (g_loggedScreen.exchange(active, std::memory_order_relaxed) != active &&
            g_screenLines.fetch_add(1, std::memory_order_relaxed) < 200) { // a failed line never trips
            finishes([&] { EVR_LOG("%s: main menu screen %d (next %d)", kTag, active, next); });
        }
    }
}

} // namespace

void poll() noexcept {
    // Fail closed, as the hook callbacks: a poll that throws trips the guard.
    if (!finishes(pollMenu)) {
        tripWith(Signal::MenuPoll, "the menu poll threw (no memory); tripped to be safe");
    }
}

} // namespace evr::vkcore::mp_guard
