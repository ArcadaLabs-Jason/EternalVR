// Multiplayer and online safety guard (mp_guard.hpp). Detection points, their signatures and the
// evidence for each are in docs/rig-findings/mp-guard.md; the RVAs in comments are Steam build
// 25216728 (Rev 3.2).

#include "vkcore/mp_guard.hpp"

#include "vkcore/game_build.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/status_file.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace evr::vkcore::mp_guard {

namespace {

using mp_policy::GuardState;
using mp_policy::Signal;

constexpr const char* kTag = "mp guard";

// The main-menu screen ids (mainMenuElementID_t) were read from this build's type info.

// idMainMenu::menu and the idMenu screen fields (type info of the same build).
constexpr std::size_t kMainMenuSize = 0xD80;
constexpr std::size_t kMainMenuMenu = 0x70;
constexpr std::int32_t kMenuActiveScreen = 0x90;
constexpr std::int32_t kMenuNextScreen = 0x94;
constexpr std::int32_t kMenuTransition = 0x98;

mp_policy::Latch g_latch;
mp_policy::StartupPhase g_startup;
std::once_flag g_installOnce;
std::once_flag g_screenOnce;
bool g_commandLineAllowed = true;

// The idMainMenu* global (RVA 0x46AE140), set before the menu hook is installed.
std::atomic<const std::byte* const*> g_mainMenuSlot{nullptr};
std::atomic<ULONGLONG> g_testTripAt{0};
std::atomic<std::uint32_t> g_mapLoads{0};

// ---------------------------------------------------------------------------------------------------
// Tripping

// Fired once, by the call that closed the latch (Latch::trip is true for one call only).
mp_policy::TripListeners g_tripListeners;

void tripWith(Signal signal, const char* detail) {
    if (g_latch.trip(signal)) {
        EVR_LOG(
            "%s: TRIPPED by %s (%s): the game keeps running; camera writes, head aim and key injection are "
            "off for the rest of this process and the headset shows the flat screen; relaunch without VR "
            "for multiplayer",
            kTag, mp_policy::toString(signal), detail);
        g_tripListeners.fire();
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
    char name[260] = {};
    const auto* text = reinterpret_cast<const std::byte*>(regs.rdx);
    if (!text || !safeCopy(name, text, sizeof(name) - 1)) {
        // The name may sit near the end of a page: read it byte by byte up to the first NUL.
        std::size_t i = 0;
        for (; text && i + 1 < sizeof(name) && safeRead(text + i, name[i]) && name[i]; ++i) {
        }
        name[i] = '\0';
    }
    name[sizeof(name) - 1] = '\0';
    g_startup.mapLoadStarted();
    g_mapLoads.fetch_add(1, std::memory_order_release);
    const mp_policy::MapClass mapClass = mp_policy::classifyMap(name);
    EVR_LOG("%s: map load '%s' (%s)", kTag, name, mp_policy::toString(mapClass));
    if (mapClass != mp_policy::MapClass::SinglePlayer) {
        std::string detail = "map '" + std::string(name) + "', " + mp_policy::toString(mapClass);
        tripWith(Signal::MapLoad, detail.c_str());
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

// ---------------------------------------------------------------------------------------------------
// Detection points

bool hookAt(const GameImage& image, const char* name, const std::byte* at, MidHookCallback callback) {
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(at), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, name, image.rva(at), error.c_str());
        return false;
    }
    EVR_LOG("%s: %s hook at RVA 0x%X", kTag, name, image.rva(at));
    return true;
}

bool vtableIs(const GameImage& image, const std::byte* dispAt, std::string_view expected) {
    const std::byte* vtable = ripTarget(image, dispAt, dispAt + 4);
    return vtable && rttiName(image, vtable) == expected;
}

bool fail(const char* name, const char* why) {
    EVR_LOG("%s: %s: %s", kTag, name, why);
    return false;
}

// idMapInstanceLocal::LoadMap logs "----------- LoadMap(%s) ------------\n" with the map name in rdx
// (RVA 0x6D3FED; the hook goes on the lea of the format string, RVA 0x6D4004).
bool installMapLoad(const GameImage& image) {
    constexpr const char* kName = "map load";
    const std::byte* at = findUnique(image, kTag, kName,
                                     "49 8B 57 20 48 8D 8D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 95 ?? ?? ?? ?? "
                                     "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??");
    if (!at) {
        return false;
    }
    // mov rdx, [rbp+X+8] reads the data pointer of the idStr built at [rbp+X].
    if (readI32(at + 19) != readI32(at + 7) + 8) {
        return fail(kName, "the name is not read from the idStr just built");
    }
    if (stringAt(image, ripTarget(image, at + 26, at + 30)) != "----------- LoadMap(%s) ------------\n") {
        return fail(kName, "the format string is not LoadMap's");
    }
    return hookAt(image, kName, at + 23, &onMapLoad);
}

// idSteamOnlineSessionInviteProvider registers its Steam callbacks (RVA 0x1BC3AB6): id 333
// (GameLobbyJoinRequested_t) with handler RVA 0x1BC4110, id 337 (GameRichPresenceJoinRequested_t) with
// handler RVA 0x1BC4290. Both handlers are hooked at entry.
bool installSteamJoin(const GameImage& image) {
    constexpr const char* kName = "Steam join callbacks";
    const std::byte* at = findUnique(image, kTag, kName,
                                     "BA 4D 01 00 00 48 8D 05 ?? ?? ?? ?? 48 89 01 48 83 C1 10 48 8D 05 ?? "
                                     "?? ?? ?? 48 89 01 48 8D 05 ?? ?? ?? "
                                     "?? 48 89 41 18 40 88 71 08 89 71 0C 48 89 59 10 FF 15 ?? ?? ?? ?? 48 "
                                     "8D 4B 30 BA 51 01 00 00 48 8D 05 "
                                     "?? ?? ?? ?? 40 88 71 08 48 89 01 48 8D 05 ?? ?? ?? ?? 48 89 41 18 89 "
                                     "71 0C 48 89 59 10 FF 15 ?? ?? ?? ??");
    if (!at) {
        return false;
    }
    if (!vtableIs(image, at + 8, ".?AVidSteamOnlineSessionInviteProvider@@") ||
        !vtableIs(image, at + 22,
                  ".?AV?$CCallback@VidSteamOnlineSessionInviteProvider@@UGameLobbyJoinRequested_t@@$0A@@@") ||
        !vtableIs(image, at + 69,
                  ".?AV?$CCallback@VidSteamOnlineSessionInviteProvider@@UGameRichPresenceJoinRequested_t@@$"
                  "0A@@@")) {
        return fail(kName, "the callback objects are not the invite provider's join callbacks");
    }
    const std::byte* lobby = ripTarget(image, at + 32, at + 36);
    const std::byte* rich = ripTarget(image, at + 83, at + 87);
    // The lobby handler first checks that the Steam ID is a chat (lobby) ID.
    if (!matchesAt(
            image, lobby,
            "4C 8B DC 56 41 57 48 83 EC 58 48 8B 41 08 4C 8B FA 48 8B F1 48 85 C0 0F 84 ?? ?? ?? ?? 48 83 "
            "78 08 00 0F 84 ?? ?? ?? ?? 8B 4A 04 8B C1 25 00 00 F0 00 3D 00 00 80 00")) {
        return fail(kName, "the lobby join handler does not check for a lobby ID");
    }
    // The rich presence handler looks for "+connect_lobby" in the connect string.
    bool connectLobby = false;
    for (const std::byte* p = rich; p && image.inText(p, 7) && p < rich + 0x200 && !connectLobby; ++p) {
        connectLobby = p[0] == std::byte{0x48} && p[1] == std::byte{0x8D} && p[2] == std::byte{0x15} &&
                       stringAt(image, ripTarget(image, p + 3, p + 7)) == "+connect_lobby";
    }
    if (!connectLobby) {
        return fail(kName, "the rich presence join handler does not parse +connect_lobby");
    }
    // Both hooks are attempted so the log names every failure.
    const bool lobbyHooked = hookAt(image, "Steam lobby join", lobby, &onSteamLobbyJoin);
    const bool richHooked = hookAt(image, "Steam rich presence join", rich, &onSteamRichPresenceJoin);
    return lobbyHooked && richHooked;
}

// Game Pass (analysis/gamepass/xbox-join.md): there is no Steam code; an Xbox invite accept or friend join
// reaches the game through the GDK invite callback that idFirstPartyPlatformLocalXboxlive::Initialize
// registers (GP RVA 0x1E56370, found through its registration site and the failure string next to it),
// then idXboxliveOnlineSessionInviteProvider::DecodeInvitationEvent (GP RVA 0x1BFB020, found through its
// "Activation missing 'handle'" string). Both are hooked, like the two Steam join handlers.
bool installXboxJoin(const GameImage& image) {
    constexpr const char* kName = "Xbox joins";
    const std::byte* site =
        findUnique(image, kTag, "Xbox invite registration",
                   "4C 8D 4F 28 33 D2 4C 8D 05 ?? ?? ?? ?? 33 C9 E8 ?? ?? ?? ?? 85 C0 79 0E 8B "
                   "D0 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??");
    if (!site) {
        return false;
    }
    if (stringAt(image, ripTarget(image, site + 29, site + 33)) !=
        "idFirstPartyPlatformLocalXboxlive::Initialize - Failed to register for invites: 0x%lX") {
        return fail(kName, "the registration is not idFirstPartyPlatformLocalXboxlive's invite registration");
    }
    const std::byte* callback = ripTarget(image, site + 9, site + 13);
    if (!matchesAt(
            image, callback,
            "40 53 48 83 EC 30 48 8B DA C7 44 24 20 FF FF FF FF 48 8B CB 48 8D 15 ?? ?? ?? ?? 45 33 C9 41 B0 "
            "01 E8 ?? ?? ?? ??") ||
        stringAt(image, ripTarget(image, callback + 23, callback + 27)) != "://") {
        return fail(kName, "the invite callback does not parse the invite URI");
    }
    const std::byte* decode =
        findUnique(image, kTag, "Xbox DecodeInvitationEvent",
                   "85 D2 0F 85 ?? ?? ?? ?? 4C 8B DC 55 49 8D AB 48 FF FF FF 48 81 EC B0 01 00 00");
    if (!decode) {
        return false;
    }
    // The decode function logs this when the activation has no handle.
    bool handleString = false;
    for (const std::byte* p = decode; p && image.inText(p, 7) && p < decode + 0x400 && !handleString; ++p) {
        handleString = p[0] == std::byte{0x48} && p[1] == std::byte{0x8D} &&
                       (std::to_integer<int>(p[2]) & 0xC7) == 0x05 &&
                       stringAt(image, ripTarget(image, p + 3, p + 7)).find("Activation missing 'handle'") !=
                           std::string_view::npos;
    }
    if (!handleString) {
        return fail(kName, "DecodeInvitationEvent does not check the activation's handle");
    }
    // Both hooks are attempted so the log names every failure. The decode hook goes after its first
    // instruction (a jne rel32 on the event type), where every invitation event passes.
    const bool eventHooked = hookAt(image, "Xbox invite callback", callback, &onXboxInviteEvent);
    const bool decodeHooked = hookAt(image, "Xbox invite decode", decode + 8, &onXboxInviteDecoded);
    return eventHooked && decodeHooked;
}

// idOnlineSessionInviteManager::ConsumeInvite (RVA 0x1A2C7A0), found through its log string; its
// prologue is shared with another function, so the string is the anchor.
bool installInviteConsumed(const GameImage& image) {
    constexpr const char* kName = "invite accepted";
    const std::byte* text = findUniqueString(
        image, "idOnlineSessionInviteManager::ConsumeInvite: ignoring unknown invite handle");
    if (!text) {
        return fail(kName, "log string not found exactly once");
    }
    const auto refs = findLeaReferences(image, text);
    if (refs.size() != 1) {
        return fail(kName, "the log string is not referenced exactly once");
    }
    const std::byte* start = functionStart(image, refs.front());
    if (!start || refs.front() - start > 0x400 ||
        !matchesAt(image, start,
                   "40 55 53 56 57 41 54 41 55 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 05 "
                   "?? ?? ?? ?? "
                   "48 33 C4")) {
        return fail(kName, "the function using the log string does not start as expected");
    }
    return hookAt(image, kName, start, &onInviteConsumed);
}

// The idLobbyUISessionCasualBattleArena constructor (RVA 0x101BC40): the lobby UI session of BATTLEMODE
// matchmaking (idLobbyUIManager session type 2) and of its tutorial (type 3).
bool installLobbySession(const GameImage& image) {
    constexpr const char* kName = "BATTLEMODE lobby session";
    const std::byte* at =
        findUnique(image, kTag, kName,
                   "48 89 5C 24 08 57 48 83 EC 30 C7 41 20 00 00 05 00 48 8D 05 ?? ?? ?? ?? 48 89 01");
    if (!at) {
        return false;
    }
    if (!vtableIs(image, at + 20, ".?AVidLobbyUISessionCasualBattleArena@@")) {
        return fail(kName, "the constructor does not install idLobbyUISessionCasualBattleArena's vtable");
    }
    return hookAt(image, kName, at, &onLobbySession);
}

// idBattleModePlayState creates the idBattleArenaGameSession (factory RVA 0x1514629, constructor RVA
// 0x14A2860, hooked at entry).
bool installBattleArenaSession(const GameImage& image) {
    constexpr const char* kName = "BATTLEMODE game session";
    const std::byte* at =
        findUnique(image, kTag, kName,
                   "BA 3E 00 00 00 B9 70 47 00 00 E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 74 23 48 8B 56 "
                   "08 48 8D 05 ?? ?? ?? ?? 48 8D 4B 08 48 89 03 E8 ?? ?? ?? ??");
    if (!at) {
        return false;
    }
    if (!vtableIs(image, at + 30, ".?AVidBattleModePlayState@session@@")) {
        return fail(kName, "the factory does not build idBattleModePlayState");
    }
    const std::byte* ctor = ripTarget(image, at + 42, at + 46);
    if (!matchesAt(
            image, ctor,
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 40 4C 8D 79 "
            "58 48 89 49 38 48 89 51 48 48 8D B9 ?? ?? ?? ?? 48 89 79 08 48 8D A9 ?? ?? ?? ?? 48 89 69 10 48 "
            "8D B1 ?? ?? ?? ?? 48 89 71 18 4C 8D B1 ?? ?? ?? ?? 4C 89 71 20 48 8D 05 ?? ?? ?? ?? 48 89 41 30 "
            "4C 8B E1 41 C6 47 0A 00 48 8D 05 ?? ?? ?? ?? 48 89 41 28 4C 8B EA 48 8D 05 ?? ?? ?? ?? 48 89 "
            "01") ||
        !vtableIs(image, ctor + 120, ".?AVidBattleArenaGameSession@@")) {
        return fail(kName, "the constructor called is not idBattleArenaGameSession's");
    }
    return hookAt(image, kName, ctor, &onBattleArenaSession);
}

// The idMainMenu singleton (getter RVA 0x174EF70, global RVA 0x46AE140) and the screen change in
// idMenu::Update (RVA 0x11CBD2C; hooked at RVA 0x11CBD3E, before activeScreen takes nextScreen).
bool installMainMenu(const GameImage& image) {
    constexpr const char* kName = "main menu";
    if (!findGameBuild(image.timestamp)) {
        return fail(
            kName, "the screen ids are from the known builds (game_build.hpp) and this exe is another build");
    }
    const std::byte* getter =
        findUnique(image, kTag, "main menu getter",
                   "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 75 2D 8D 50 53 B9 ?? ?? ?? ?? E8 ?? "
                   "?? ?? ?? 48 85 C0 74 14 48 8B C8 E8 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??");
    const std::byte* change =
        findUnique(image, kTag, "menu screen change",
                   "48 8B 03 48 8B CB 8B 96 ?? ?? ?? ?? FF 90 ?? ?? ?? ?? 44 8B 8E ?? ?? ?? ?? 48 8B "
                   "CE 8B 96 ?? ?? ?? ?? 44 8B 86 ?? ?? ?? ?? 48 8B 06 C7 86 ?? ?? ?? ?? FF FF FF "
                   "FF 44 89 86 ?? ?? ?? ?? FF 50 70");
    if (!getter || !change) {
        return false;
    }
    const std::byte* slot = ripTarget(image, getter + 7, getter + 11);
    if (!slot || slot != ripTarget(image, getter + 45, getter + 49) || !image.contains(slot, 8) ||
        static_cast<std::size_t>(readI32(getter + 20)) != kMainMenuSize) {
        return fail(kName, "the getter does not allocate and keep an idMainMenu");
    }
    const bool layout = readI32(change + 8) == kMenuTransition && readI32(change + 21) == kMenuTransition &&
                        readI32(change + 30) == kMenuActiveScreen &&
                        readI32(change + 37) == kMenuNextScreen && readI32(change + 46) == kMenuTransition &&
                        readI32(change + 57) == kMenuActiveScreen;
    if (!layout) {
        return fail(kName,
                    "the screen change does not use idMenu's activeScreen, nextScreen and transitionType");
    }
    // The screen change sits just after idMenu::Update's "no associated screen class" message.
    const std::byte* message = findUniqueString(
        image, "idMenu::Update() - Next Screen ID '%d' does not have an associated screen class");
    const auto refs = message ? findLeaReferences(image, message) : std::vector<const std::byte*>{};
    if (refs.size() != 1 || change < refs.front() || change - refs.front() > 0x100) {
        return fail(kName, "the screen change is not in idMenu::Update");
    }
    g_mainMenuSlot.store(reinterpret_cast<const std::byte* const*>(slot), std::memory_order_release);
    EVR_LOG("%s: main menu pointer at RVA 0x%X", kTag, image.rva(slot));
    return hookAt(image, "menu screen change", change + 18, &onMenuTransition);
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
        const bool mapLoad = installMapLoad(image);
        // The platform's join detection: Steam's join callbacks, or on Game Pass the Xbox invite path.
        const GameBuild* build = findGameBuild(image.timestamp);
        const bool xbox = build && build->kind == GameBuildKind::GamePass;
        const bool steam = xbox ? installXboxJoin(image) : installSteamJoin(image);
        const bool invite = installInviteConsumed(image);
        const bool lobby = installLobbySession(image);
        const bool session = installBattleArenaSession(image);
        const bool menu = installMainMenu(image);
        const bool all = mapLoad && steam && invite && lobby && session && menu;
        EVR_LOG("%s: map load %s, %s joins %s, invites %s, lobby session %s, game session %s, main menu %s",
                kTag, mapLoad ? "ok" : "MISSING", xbox ? "Xbox" : "Steam", steam ? "ok" : "MISSING",
                invite ? "ok" : "MISSING", lobby ? "ok" : "MISSING", session ? "ok" : "MISSING",
                menu ? "ok" : "MISSING");
        if (!all) {
            g_latch.refuse();
            EVR_LOG("%s: REFUSED: a detection point is missing, so online play could not be detected; camera "
                    "writes, head aim and key injection stay off",
                    kTag);
            status::flat(
                !findGameBuild(image.timestamp)
                    ? "this DOOM Eternal version is not one this EternalVR supports (Steam build 25216728 or "
                      "Game Pass 1.0.56.0); VR stays off until an EternalVR update"
                    : "the mod could not find everything it needs in the game; VR stays off");
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

void poll() {
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
    }
}

} // namespace evr::vkcore::mp_guard
