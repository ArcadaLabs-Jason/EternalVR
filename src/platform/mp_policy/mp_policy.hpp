#pragma once

// Multiplayer and online safety policy (REQ-16, T-109, ARCHITECTURE section 4a). Portable so the rules
// can be tested without Windows or the game; vkcore/mp_guard.hpp feeds it what the game does.
//
// Three decisions live here:
// - which command lines the layer refuses to arm under (the same normalisation as the launcher's
//   ArgumentPolicy, plus the arguments Steam passes when it starts the game from an invite);
// - which map paths are single-player, which are BATTLEMODE, and which are unknown (unknown counts as
//   online: the guard fails closed);
// - which main-menu screens belong to BATTLEMODE or other online play.
// The latch that turns every game-touching feature off for the rest of the process is here too.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace evr::mp_policy {

// ---------------------------------------------------------------------------------------------------
// Command line

// The argument text as the game receives it, normalised for matching: split with the launcher's rule
// (whitespace separates, '"' toggles quoting and is removed), joined with single spaces, '\' read as
// '/', runs of '/' collapsed, and "+ cmd", "+set cmd" and "+seta cmd" read as "+cmd". ASCII letters are
// lower-cased. The result has one leading and one trailing space so a pattern can match whole words.
std::string normaliseArguments(std::wstring_view arguments);

struct RefusedArgument {
    std::string_view pattern; // as matched, normalised
    std::string_view reason;  // what the pattern requests
};

// Screens a full command line (GetCommandLineW): the first token, the program path, is skipped. Returns
// the first refused pattern found, or nothing when the arguments are allowed.
std::optional<RefusedArgument> screenCommandLine(std::wstring_view commandLine);

// Screens argument text alone (no program path), as the launcher's ArgumentPolicy.Check does.
std::optional<RefusedArgument> screenArguments(std::wstring_view arguments);

// ---------------------------------------------------------------------------------------------------
// Map paths

enum class MapClass : std::uint8_t {
    SinglePlayer, // campaign, DLC, hub, Horde, the shell (main menu), the campaign tutorial
    Online,       // BATTLEMODE maps and tutorials, Invasion, anything naming pvp or mp
    Unknown,      // anything else: treated as online
};

// Classifies a map path as the engine names it ("game/sp/e1m1_intro/e1m1_intro"). Case, '\', repeated
// slashes, a leading "/", "./" or "maps/" and a ".map" or ".entities" suffix are ignored.
MapClass classifyMap(std::string_view path);

// True when loading this map must trip the guard (Online or Unknown).
inline bool mapTripsGuard(std::string_view path) {
    return classifyMap(path) != MapClass::SinglePlayer;
}

const char* toString(MapClass mapClass);

// ---------------------------------------------------------------------------------------------------
// Start-up

// The game builds one idBattleArenaGameSession during its own initialisation (the BATTLEMODE global
// settings, called from "DOOMEternal initialization"), before any map loads, in every process (seen on
// the rig, build 25216728). BATTLEMODE play needs the main menu, which is a map load (game/shell/shell),
// and a launch straight into online play is refused by the command-line screen. So a session built
// before the first map load is the start-up one and not a signal; from the first map load on, every one
// is. Process-wide, lock-free.
class StartupPhase {
public:
    // Called at every map load (before its classification).
    void mapLoadStarted() { mapLoadSeen_.store(true, std::memory_order_release); }
    // True when an idBattleArenaGameSession built now must trip the guard.
    [[nodiscard]] bool battleArenaSessionTrips() const {
        return mapLoadSeen_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> mapLoadSeen_{false};
};

// ---------------------------------------------------------------------------------------------------
// Main-menu screens (mainMenuElementID_t, type-info enum of build 25216728)

// True for the BATTLEMODE, match browser, private and public match, Invasion, multiplayer and play-online
// screens and the BATTLEMODE series and leaderboard selection.
bool isOnlineMenuScreen(int screenId);

// ---------------------------------------------------------------------------------------------------
// The latch

enum class Signal : std::uint8_t {
    None,
    CommandLine,           // refused arguments at layer start
    MapLoad,               // idMapInstanceLocal::LoadMap with an online or unknown map
    SteamLobbyJoin,        // Steam GameLobbyJoinRequested_t (an invite or "join game" accepted)
    SteamRichPresenceJoin, // Steam GameRichPresenceJoinRequested_t (also +connect_lobby at launch)
    XboxInviteEvent,       // Game Pass: the GDK invite callback (an Xbox invite or join URI arrived)
    XboxInviteDecoded,     // Game Pass: idXboxliveOnlineSessionInviteProvider::DecodeInvitationEvent
    InviteConsumed,        // idOnlineSessionInviteManager::ConsumeInvite (any platform's invite accepted)
    LobbySession,          // idLobbyUISessionCasualBattleArena created (BATTLEMODE lobby or its tutorial)
    BattleArenaSession,    // idBattleArenaGameSession created (BATTLEMODE play state)
    MenuTransition,        // the main menu switched to an online screen
    MenuPoll,              // the main menu was found showing an online screen
    Test,                  // injected by a development test
};

const char* toString(Signal signal);

enum class GuardState : std::uint8_t {
    Unarmed, // not yet installed: features stay off
    Armed,   // every detection point installed: features may act
    Refused, // a detection point could not be installed: features stay off for the process
    Tripped, // an online signal fired: features stay off for the process
};

const char* toString(GuardState state);

// Process-wide, lock-free. Nothing ever leads back to Unarmed or Armed: Armed is reachable only from
// Unarmed, Tripped is final, and Refused can only become Tripped. trip() works from any state, so a
// signal seen before arming still counts and a refused guard still records the first signal it saw.
class Latch {
public:
    // Unarmed -> Armed. False (and no change) from any other state.
    bool arm();
    // Unarmed or Armed -> Refused. False when already Refused or Tripped.
    bool refuse();
    // Any state -> Tripped; the first signal is kept. True only for the call that tripped it.
    bool trip(Signal signal);

    [[nodiscard]] GuardState state() const { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] Signal firstSignal() const { return signal_.load(std::memory_order_acquire); }
    // The one question every game-touching feature asks before acting.
    [[nodiscard]] bool allowsGameTouch() const { return state() == GuardState::Armed; }

private:
    std::atomic<GuardState> state_{GuardState::Unarmed};
    std::atomic<Signal> signal_{Signal::None};
};

} // namespace evr::mp_policy
