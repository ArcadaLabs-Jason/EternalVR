#include "platform/mp_policy/mp_policy.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using evr::mp_policy::classifyMap;
using evr::mp_policy::GuardState;
using evr::mp_policy::isOnlineMenuScreen;
using evr::mp_policy::Latch;
using evr::mp_policy::MapClass;
using evr::mp_policy::mapTripsGuard;
using evr::mp_policy::normaliseArguments;
using evr::mp_policy::screenArguments;
using evr::mp_policy::screenCommandLine;
using evr::mp_policy::Signal;
using evr::mp_policy::StartupPhase;

namespace {

constexpr std::wstring_view kExe =
    L"\"E:\\SteamLibrary\\steamapps\\common\\DOOMEternal\\DOOMEternalx64vk.exe\"";

std::wstring withExe(std::wstring_view args) {
    return std::wstring(kExe) + L" " + std::wstring(args);
}

bool refused(std::wstring_view args) {
    return screenArguments(args).has_value();
}

std::string_view refusedPattern(std::wstring_view args) {
    const auto r = screenArguments(args);
    return r ? r->pattern : std::string_view{};
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Argument normalisation (mirrors the launcher's ArgumentPolicy)

TEST_CASE("arguments are normalised like the launcher's ArgumentPolicy") {
    CHECK(normaliseArguments(L"  +a \"b c\"   +d ") == " +a b c +d ");
    CHECK(normaliseArguments(L"") == " ");
    CHECK(normaliseArguments(L"   ") == " ");
    CHECK(normaliseArguments(L"+set com_skipIntroVideo 1") == " +com_skipintrovideo 1 ");
    CHECK(normaliseArguments(L"+seta r_fullscreen 0") == " +r_fullscreen 0 ");
    CHECK(normaliseArguments(L"+ SET  net_x 1") == " +net_x 1 ");
    CHECK(normaliseArguments(L"+ devmap x") == " +devmap x ");
    CHECK(normaliseArguments(L"+settings x") == " +settings x ");
    CHECK(normaliseArguments(L"+set") == " +set ");
    CHECK(normaliseArguments(L"+map game\\\\pvp//pvp_laser") == " +map game/pvp/pvp_laser ");
    CHECK(normaliseArguments(L"+map \"game/\"\"pvp/x\"") == " +map game/pvp/x ");
}

TEST_CASE("ordinary single-player arguments are allowed") {
    CHECK_FALSE(refused(L""));
    CHECK_FALSE(refused(L"+s_volume 0"));
    CHECK_FALSE(refused(L"+com_skipIntroVideo 1 +r_swapInterval 0 +rs_enable 0"));
    CHECK_FALSE(refused(L"+map game/sp/e1m1_intro/e1m1_intro"));
    CHECK_FALSE(refused(L"+devmap game/dlc2/e5m1_spear/e5m1_spear"));
    CHECK_FALSE(refused(L"+set hands_fovScale 1"));
    CHECK_FALSE(refused(L"+map game/sp/e1m2_battle/e1m2_battle")); // "battle" alone is a campaign map
}

TEST_CASE("BATTLEMODE maps are refused however they are spelled") {
    CHECK(refusedPattern(L"+map game/pvp/pvp_laser/pvp_laser") == "game/pvp/");
    CHECK(refused(L"+map GAME\\PVP\\pvp_zap"));
    CHECK(refused(L"+map game//pvp///pvp_zap"));
    CHECK(refused(L"+devmap \"game/pvp/pvp_inferno\""));
    CHECK(refused(L"+map \"game\\\\pvp\"/pvp_bronco"));
    CHECK(refused(L"+map game/tutorials/tutorial_pvp_laser"));
    CHECK(refused(L"+map game/tutorials/tutorial_demons"));
    CHECK(refused(L"+map game/tutorials/tutorial_invasion"));
    CHECK(refused(L"+map pvp_thunder"));
}

TEST_CASE("multiplayer commands are refused in their +set and spaced forms") {
    CHECK(refusedPattern(L"+connect 1.2.3.4") == "+connect");
    CHECK(refusedPattern(L"+ connect 1.2.3.4") == "+connect");
    CHECK(refusedPattern(L"+set connect 1.2.3.4") == "+connect");
    CHECK(refusedPattern(L"+seta   Connect x") == "+connect");
    CHECK(refused(L"+set net_serverDedicated 1"));
    CHECK(refused(L"+ seta si_map x"));
    CHECK(refused(L"+matchmaking_start"));
    CHECK(refused(L"+party"));
    CHECK(refused(L"+lobby"));
    CHECK(refused(L"+g_gametype 2"));
    CHECK(refused(L"+set com_gameMode 2"));
    CHECK(refused(L"+JoinShellLobby 1"));
    CHECK(refused(L"+restartmapwithlobby"));
    CHECK(refused(L"+ConnectOrHostCasualBattleArenaResult 1"));
    CHECK(refused(L"+pvp_skipReadyUp 1"));
    CHECK(refused(L"+debug_battlemode_tutorial_screenfade 0"));
}

TEST_CASE("the arguments Steam passes for an invite are refused") {
    CHECK(refusedPattern(L"+connect_lobby 109775241234567890") == "+connect");
    CHECK(refused(L"+connect_lobby 1 +steam_invitationCookie abc"));
    CHECK(refusedPattern(L"+steam_inviteSenderId 7656119") == "+steam_invit");
}

TEST_CASE("the program path is skipped when screening a full command line") {
    CHECK_FALSE(screenCommandLine(kExe).has_value());
    CHECK_FALSE(
        screenCommandLine(L"\"E:\\games\\pvp_backup\\DOOMEternalx64vk.exe\" +s_volume 0").has_value());
    CHECK_FALSE(screenCommandLine(L"C:\\invasion\\DOOMEternalx64vk.exe").has_value());
    CHECK(screenCommandLine(withExe(L"+connect_lobby 1")).has_value());
    CHECK(screenCommandLine(withExe(L"+s_volume 0 +map game/pvp/pvp_zap")).has_value());
    CHECK_FALSE(screenCommandLine(withExe(L"+s_volume 0 +com_skipIntroVideo 1")).has_value());
    CHECK_FALSE(screenCommandLine(L"").has_value());
}

TEST_CASE("the reason names what the pattern requests") {
    const auto r = screenArguments(L"+connect_lobby 1");
    REQUIRE(r.has_value());
    CHECK(r->reason == "a multiplayer connection");
}

// ---------------------------------------------------------------------------------------------------
// Map paths

TEST_CASE("campaign, DLC, hub, Horde, shell and campaign tutorial maps are single-player") {
    for (const char* map : {"game/sp/e1m1_intro/e1m1_intro", "game/sp/e3m2_hell_b/e3m2_hell_b",
                            "game/dlc/e4m1_rig/e4m1_rig", "game/dlc/hub/hub", "game/dlc2/e5m4_boss/e5m4_boss",
                            "game/hub/hub", "game/horde/e6m1_cult_horde/e6m1_cult_horde", "game/shell/shell",
                            "game/tutorials/tutorial_sp", "game/sp/e1m2_battle/e1m2_battle"}) {
        CAPTURE(map);
        CHECK((classifyMap(map) == MapClass::SinglePlayer));
        CHECK_FALSE(mapTripsGuard(map));
    }
}

TEST_CASE("map paths are normalised before classification") {
    CHECK((classifyMap("GAME\\SP\\E1M1_INTRO\\e1m1_intro") == MapClass::SinglePlayer));
    CHECK((classifyMap("maps/game/sp/challenges/ch01.map") == MapClass::SinglePlayer));
    CHECK((classifyMap("/game//sp///e1m3_cult/e1m3_cult.entities") == MapClass::SinglePlayer));
    CHECK((classifyMap("./maps/game/hub/hub") == MapClass::SinglePlayer));
    CHECK((classifyMap("game/tutorials/tutorial_sp.map") == MapClass::SinglePlayer));
}

TEST_CASE("BATTLEMODE, its tutorials and Invasion maps are online") {
    for (const char* map : {"game/pvp/pvp_laser/pvp_laser", "game/pvp/pvp_zap/pvp_zap", "GAME/PVP/PVP_BRONCO",
                            "maps\\game\\pvp\\pvp_thunder.map", "game/tutorials/tutorial_pvp_laser",
                            "game/tutorials/tutorial_pvp_laser/tutorial_pvp_laser",
                            "game/tutorials/tutorial_demons", "game/tutorials/tutorial_invasion",
                            "game/mp/anything", "game/sp/e1m1_intro/pvp_test", "game/sp/invasion_arena"}) {
        CAPTURE(map);
        CHECK((classifyMap(map) == MapClass::Online));
        CHECK(mapTripsGuard(map));
    }
}

TEST_CASE("unknown map paths fail closed") {
    for (const char* map :
         {"", "/", "game/sp/", "game/sp", "game/unknown/x", "e1m1_intro", "game/tutorials/tutorial_spx",
          "game/tutorials/tutorial_sp/", "game/sp/../pvp_x", "game/sp/../../x", "sp/e1m1_intro"}) {
        CAPTURE(map);
        CHECK((classifyMap(map) != MapClass::SinglePlayer));
        CHECK(mapTripsGuard(map));
    }
}

// ---------------------------------------------------------------------------------------------------
// Start-up

TEST_CASE("the start-up BATTLEMODE game session is not a signal; any after the first map load is") {
    StartupPhase startup;
    CHECK_FALSE(startup.battleArenaSessionTrips()); // the one the game builds while it initialises
    CHECK_FALSE(startup.battleArenaSessionTrips());
    startup.mapLoadStarted(); // game/shell/shell: the main menu
    CHECK(startup.battleArenaSessionTrips());
    startup.mapLoadStarted();
    CHECK(startup.battleArenaSessionTrips()); // never goes back
}

// ---------------------------------------------------------------------------------------------------
// Menu screens

TEST_CASE("BATTLEMODE and online main-menu screens are recognised") {
    for (const int id : {6, 8, 9, 10, 11, 12, 18, 21, 22, 23, 24, 25, 26, 28, 40}) {
        CAPTURE(id);
        CHECK(isOnlineMenuScreen(id));
    }
    // Start, root, difficulty, customize, campaign, mission select, master levels, settings, Horde, codex.
    for (const int id : {-1, 0, 1, 2, 3, 4, 5, 7, 13, 14, 15, 16, 17, 29, 31, 33, 36, 44, 54, 1000}) {
        CAPTURE(id);
        CHECK_FALSE(isOnlineMenuScreen(id));
    }
}

// ---------------------------------------------------------------------------------------------------
// The latch

TEST_CASE("the latch starts unarmed and denies game touches until armed") {
    Latch latch;
    CHECK((latch.state() == GuardState::Unarmed));
    CHECK_FALSE(latch.allowsGameTouch());
    CHECK(latch.arm());
    CHECK(latch.allowsGameTouch());
    CHECK_FALSE(latch.arm()); // only once
}

TEST_CASE("a trip is permanent") {
    Latch latch;
    REQUIRE(latch.arm());
    CHECK(latch.trip(Signal::MapLoad));
    CHECK((latch.state() == GuardState::Tripped));
    CHECK_FALSE(latch.allowsGameTouch());
    CHECK((latch.firstSignal() == Signal::MapLoad));
    CHECK_FALSE(latch.trip(Signal::MenuTransition)); // later signals do not re-trip or replace the first
    CHECK((latch.firstSignal() == Signal::MapLoad));
    CHECK_FALSE(latch.arm());
    CHECK_FALSE(latch.refuse());
    CHECK((latch.state() == GuardState::Tripped));
}

TEST_CASE("a refused guard never arms, and a signal still trips it") {
    Latch latch;
    CHECK(latch.refuse());
    CHECK_FALSE(latch.refuse());
    CHECK_FALSE(latch.arm());
    CHECK_FALSE(latch.allowsGameTouch());
    CHECK(latch.trip(Signal::SteamLobbyJoin));
    CHECK((latch.state() == GuardState::Tripped));
    CHECK((latch.firstSignal() == Signal::SteamLobbyJoin));
}

TEST_CASE("an armed guard can still be refused, and a signal before arming prevents arming") {
    Latch armed;
    REQUIRE(armed.arm());
    CHECK(armed.refuse());
    CHECK_FALSE(armed.allowsGameTouch());

    Latch early;
    CHECK(early.trip(Signal::CommandLine));
    CHECK_FALSE(early.arm());
    CHECK_FALSE(early.allowsGameTouch());
}

TEST_CASE("concurrent trips trip exactly once and keep one signal") {
    Latch latch;
    REQUIRE(latch.arm());
    std::atomic<int> winners{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&latch, &winners, i] {
            const Signal s = i % 2 ? Signal::MapLoad : Signal::LobbySession;
            if (latch.trip(s)) {
                winners.fetch_add(1);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    CHECK(winners.load() == 1);
    CHECK((latch.state() == GuardState::Tripped));
    const Signal first = latch.firstSignal();
    CHECK((first == Signal::MapLoad || first == Signal::LobbySession));
}

TEST_CASE("every signal and state has a name") {
    for (const Signal s :
         {Signal::None, Signal::CommandLine, Signal::MapLoad, Signal::SteamLobbyJoin,
          Signal::SteamRichPresenceJoin, Signal::InviteConsumed, Signal::LobbySession,
          Signal::BattleArenaSession, Signal::MenuTransition, Signal::MenuPoll, Signal::Test}) {
        CHECK(std::string_view(evr::mp_policy::toString(s)) != "?");
    }
    for (const GuardState s :
         {GuardState::Unarmed, GuardState::Armed, GuardState::Refused, GuardState::Tripped}) {
        CHECK(std::string_view(evr::mp_policy::toString(s)) != "?");
    }
}
