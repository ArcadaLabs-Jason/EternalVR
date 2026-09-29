#include "platform/mp_policy/mp_policy.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace evr::mp_policy {

namespace {

// ---------------------------------------------------------------------------------------------------
// Command line

struct ArgumentRule {
    std::string_view pattern; // normalised (lower case, "+cmd" form)
    std::string_view reason;
};

// launcher/data/refused-args.txt (launcher-v0), then what Steam and Xbox pass for an invite and the other
// BATTLEMODE entry points this build accepts as console commands or startup cvars.
constexpr std::array kRules{
    ArgumentRule{"game/pvp/", "a BATTLEMODE map"},
    ArgumentRule{"battlemode", "BATTLEMODE"},
    ArgumentRule{"+connect", "a multiplayer connection"}, // also +connect_lobby (Steam invite at launch)
    ArgumentRule{"+join", "a multiplayer join"},          // also +JoinShellLobby
    ArgumentRule{"+lobby", "a multiplayer lobby"},
    ArgumentRule{"+matchmaking", "matchmaking"},
    ArgumentRule{"+party", "a multiplayer party"},
    ArgumentRule{"+net_", "network settings"},
    ArgumentRule{"+si_", "multiplayer server settings"},
    ArgumentRule{"+g_gametype", "a game type other than the campaign"},
    ArgumentRule{"+steam_invit", "a Steam invite"}, // +steam_invitationCookie, +steam_inviteSenderId
    ArgumentRule{"+com_gamemode", "a game mode other than the campaign"},
    ArgumentRule{"+restartmapwithlobby", "a multiplayer lobby"},
    ArgumentRule{"battlearena", "BATTLEMODE"}, // ConnectOrHostCasualBattleArenaResult and friends
    ArgumentRule{"+pvp_", "BATTLEMODE settings"},
    ArgumentRule{"/pvp", "a BATTLEMODE map"},
    ArgumentRule{"pvp_", "a BATTLEMODE map"},
    ArgumentRule{"tutorial_demons", "the BATTLEMODE demon tutorial"},
    ArgumentRule{"invasion", "Invasion"},
    // Game Pass: what the GDK passes when the game is started from an Xbox invite or join.
    ArgumentRule{"ms-xbl-", "an Xbox invite"},
    ArgumentRule{"invitehandleaccept", "an Xbox invite"},
    ArgumentRule{"activityhandlejoin", "an Xbox join"},
    ArgumentRule{"handle=", "an Xbox invite"},
};

char narrowLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') {
        return static_cast<char>(c - L'A' + 'a');
    }
    if (c < 0x80) {
        return static_cast<char>(c);
    }
    return '?'; // no pattern contains non-ASCII text
}

bool isSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f';
}

// The launcher's SplitArguments: whitespace outside quotes separates, '"' toggles quoting and is
// dropped, and an argument that was only quotes ("") still counts.
std::vector<std::wstring> splitArguments(std::wstring_view text) {
    std::vector<std::wstring> list;
    std::wstring current;
    bool quoted = false;
    bool any = false;
    for (const wchar_t c : text) {
        if (c == L'"') {
            quoted = !quoted;
            any = true;
            continue;
        }
        if (!quoted && isSpace(c)) {
            if (any) {
                list.push_back(current);
            }
            current.clear();
            any = false;
            continue;
        }
        current.push_back(c);
        any = true;
    }
    if (any) {
        list.push_back(current);
    }
    return list;
}

bool startsWithWord(const std::string& s, std::size_t at, std::string_view word) {
    return s.compare(at, word.size(), word) == 0 && at + word.size() < s.size() && s[at + word.size()] == ' ';
}

std::string normaliseTokens(const std::vector<std::wstring>& tokens, std::size_t first) {
    // Join, unify slashes, lower-case and collapse whitespace in one pass.
    std::string joined;
    for (std::size_t i = first; i < tokens.size(); ++i) {
        joined.push_back(' ');
        for (const wchar_t c : tokens[i]) {
            joined.push_back(c == L'\\' ? '/' : (isSpace(c) ? ' ' : narrowLower(c)));
        }
    }
    std::string collapsed;
    for (const char c : joined) {
        if ((c == ' ' || c == '/') && !collapsed.empty() && collapsed.back() == c) {
            continue;
        }
        collapsed.push_back(c);
    }
    // Trim.
    const std::size_t begin = collapsed.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return " ";
    }
    const std::size_t end = collapsed.find_last_not_of(' ');
    const std::string trimmed = collapsed.substr(begin, end - begin + 1);
    // "+ cmd", "+set cmd", "+seta cmd" -> "+cmd" (spaces are single here).
    std::string out = " ";
    for (std::size_t i = 0; i < trimmed.size(); ++i) {
        out.push_back(trimmed[i]);
        if (trimmed[i] != '+') {
            continue;
        }
        std::size_t j = i + 1;
        if (j < trimmed.size() && trimmed[j] == ' ') {
            ++j;
        }
        if (startsWithWord(trimmed, j, "seta")) {
            j += 5;
        } else if (startsWithWord(trimmed, j, "set")) {
            j += 4;
        }
        i = j - 1;
    }
    out.push_back(' ');
    return out;
}

std::optional<RefusedArgument> screenNormalised(const std::string& text) {
    for (const ArgumentRule& rule : kRules) {
        if (text.find(rule.pattern) != std::string::npos) {
            return RefusedArgument{rule.pattern, rule.reason};
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------
// Map paths

std::string normaliseMapPath(std::string_view path) {
    std::string s;
    for (const char c : path) {
        const char n = c == '\\' ? '/' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        if (n == '/' && !s.empty() && s.back() == '/') {
            continue;
        }
        s.push_back(n);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (const std::string_view prefix :
             {std::string_view("./"), std::string_view("/"), std::string_view("maps/")}) {
            if (s.compare(0, prefix.size(), prefix) == 0) {
                s.erase(0, prefix.size());
                changed = true;
            }
        }
    }
    for (const std::string_view suffix : {std::string_view(".map"), std::string_view(".entities")}) {
        if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
            s.erase(s.size() - suffix.size());
            break;
        }
    }
    return s;
}

bool segmentIsOnline(std::string_view segment) {
    return segment.find("pvp") != std::string_view::npos || segment == "mp" ||
           segment.find("invasion") != std::string_view::npos ||
           segment.find("battlemode") != std::string_view::npos || segment == "tutorial_demons";
}

} // namespace

std::string normaliseArguments(std::wstring_view arguments) {
    return normaliseTokens(splitArguments(arguments), 0);
}

std::optional<RefusedArgument> screenArguments(std::wstring_view arguments) {
    return screenNormalised(normaliseTokens(splitArguments(arguments), 0));
}

std::optional<RefusedArgument> screenCommandLine(std::wstring_view commandLine) {
    return screenNormalised(normaliseTokens(splitArguments(commandLine), 1));
}

MapClass classifyMap(std::string_view path) {
    const std::string s = normaliseMapPath(path);
    bool dotDot = false;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t slash = s.find('/', start);
        const std::string_view segment =
            std::string_view(s).substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segmentIsOnline(segment)) {
            return MapClass::Online;
        }
        dotDot = dotDot || segment == "..";
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    if (dotDot) {
        return MapClass::Unknown;
    }
    constexpr std::array<std::string_view, 6> kSinglePlayer{"game/sp/",  "game/dlc/",   "game/dlc2/",
                                                            "game/hub/", "game/horde/", "game/shell/"};
    for (const std::string_view prefix : kSinglePlayer) {
        if (s.size() > prefix.size() && s.compare(0, prefix.size(), prefix) == 0 && s.back() != '/') {
            return MapClass::SinglePlayer;
        }
    }
    constexpr std::string_view kTutorial = "game/tutorials/tutorial_sp";
    if (s == kTutorial ||
        (s.size() > kTutorial.size() + 1 && s.compare(0, kTutorial.size(), kTutorial) == 0 &&
         s[kTutorial.size()] == '/' && s.back() != '/')) {
        return MapClass::SinglePlayer;
    }
    return MapClass::Unknown;
}

const char* toString(MapClass mapClass) {
    switch (mapClass) {
    case MapClass::SinglePlayer:
        return "single-player";
    case MapClass::Online:
        return "online";
    case MapClass::Unknown:
        return "unknown";
    }
    return "?";
}

bool isOnlineMenuScreen(int screenId) {
    switch (screenId) {
    case 6:  // MAIN_MENU_SCREEN_MATCH_BROWSER
    case 8:  // MAIN_MENU_SCREEN_BATTLE_ARENA
    case 9:  // MAIN_MENU_SCREEN_BATTLE_ARENA_LOBBY
    case 10: // MAIN_MENU_SCREEN_BATTLE_ARENA_LOBBY_OPTIONS
    case 11: // MAIN_MENU_SCREEN_BATTLE_ARENA_UPGRADE_PREVIEW
    case 12: // MAIN_MENU_SCREEN_BATTLE_ARENA_WEAPON_WHEEL
    case 18: // MAIN_MENU_SCREEN_INVASION
    case 21: // MAIN_MENU_SCREEN_MULTIPLAYER (the BATTLEMODE entry from the root menu)
    case 22: // MAIN_MENU_SCREEN_PLAY_ONLINE
    case 23: // MAIN_MENU_SCREEN_PRIVATE_MATCH
    case 24: // MAIN_MENU_SCREEN_PRIVATE_MATCH_OPTIONS
    case 25: // MAIN_MENU_SCREEN_PRIVATE_MATCH_PLAYER_SETUP
    case 26: // MAIN_MENU_SCREEN_PUBLIC_MATCH_PLAYER_SETUP
    case 28: // MAIN_MENU_SCREEN_PVP_SERIES
    case 40: // BATTLE_ARENA_LEADERBOARD_SELECTION
        return true;
    default:
        return false;
    }
}

const char* toString(Signal signal) {
    switch (signal) {
    case Signal::None:
        return "none";
    case Signal::CommandLine:
        return "command line";
    case Signal::MapLoad:
        return "map load";
    case Signal::SteamLobbyJoin:
        return "Steam lobby join request";
    case Signal::SteamRichPresenceJoin:
        return "Steam rich presence join request";
    case Signal::XboxInviteEvent:
        return "Xbox invite or join";
    case Signal::XboxInviteDecoded:
        return "Xbox invite decoded";
    case Signal::InviteConsumed:
        return "invite accepted";
    case Signal::LobbySession:
        return "BATTLEMODE lobby session";
    case Signal::BattleArenaSession:
        return "BATTLEMODE game session";
    case Signal::MenuTransition:
        return "online menu screen";
    case Signal::MenuPoll:
        return "online menu screen (poll)";
    case Signal::Test:
        return "test";
    }
    return "?";
}

const char* toString(GuardState state) {
    switch (state) {
    case GuardState::Unarmed:
        return "unarmed";
    case GuardState::Armed:
        return "armed";
    case GuardState::Refused:
        return "refused";
    case GuardState::Tripped:
        return "tripped";
    }
    return "?";
}

bool Latch::arm() {
    GuardState expected = GuardState::Unarmed;
    return state_.compare_exchange_strong(expected, GuardState::Armed, std::memory_order_acq_rel);
}

bool Latch::refuse() {
    GuardState current = state_.load(std::memory_order_acquire);
    while (current == GuardState::Unarmed || current == GuardState::Armed) {
        if (state_.compare_exchange_weak(current, GuardState::Refused, std::memory_order_acq_rel)) {
            return true;
        }
    }
    return false;
}

bool Latch::trip(Signal signal) {
    // The signal is published before the state, so a reader that sees Tripped also sees a signal.
    Signal none = Signal::None;
    signal_.compare_exchange_strong(none, signal, std::memory_order_acq_rel);
    return state_.exchange(GuardState::Tripped, std::memory_order_acq_rel) != GuardState::Tripped;
}

} // namespace evr::mp_policy
