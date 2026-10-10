#include "vkcore/debug_script.hpp"

#include "platform/mp_policy/mp_policy.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>
#include <utility>

namespace evr::vkcore {

namespace {

std::string_view trim(std::string_view text) {
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits `text` at every `separator`; the pieces keep their surrounding spaces.
std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> pieces;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == separator) {
            pieces.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return pieces;
}

// The commands a schedule may run, compared without case: every one the rig's scripts and the QA suite use.
// Cvars are set by their name followed by the value.
constexpr std::string_view kAllowedCommands[] = {
    // The QA suite's token pickup (tools/rig/qa/qa-scenarios.ps1).
    "god", "sync_printInteractionAndAnimationName", "g_debugTriggers", "setviewpos", "where",
    "selectDebugEntity",
    // Rig scripts: positions, pickups, damage, triggers and checkpoints.
    "getviewpos", "teleport", "teleportposition", "notarget", "noclip", "kill", "damage", "hurt", "health",
    "give", "trigger", "activatetargets", "activateCheckPoint",
    // Demons (docs/rig-findings/debug-commands.md).
    "ai_Show", "ai_Hide", "nextActiveAI", "nextAI", "prevAI", "ai_teleportToPlayer", "moveToFacingPlayer",
    "ai_forceIdle", "ai_forceFreeze", "ai_forceAnim", "killAI", "removeAI", "healAI", "encounter_clearWait",
    "gibalicious",
    // Cvars set in the map (rig scripts, tools/rig/cpu-cvar-ab.ps1); a change of r_hdrDisplay makes the next
    // frame recreate the swapchain.
    "g_dumpSpawnedEntities", "g_setting_hud_auto_dismiss_tutorials", "g_setting_tutorials",
    "p_debugAnimatedCamera", "r_sharpening", "is_update", "is_defrag", "r_skipGPUParticles", "r_hdrDisplay"};

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool plain(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
           c == '\t' || c == '_' || c == '-' || c == '.' || c == '/';
}

// Why the allow-list leaves `command` (trimmed, not empty) out, or empty.
std::string_view offList(std::string_view command) {
    for (const char c : command) {
        if (!plain(c)) {
            return "a character other than letters, digits, spaces and _ - . /";
        }
    }
    const std::string_view name = command.substr(0, command.find_first_of(" \t"));
    for (const std::string_view allowed : kAllowedCommands) {
        if (sameName(name, allowed)) {
            return {};
        }
    }
    return "not a test command";
}

DebugScript failed(std::size_t step, const std::string& why) {
    DebugScript script;
    script.error = "step " + std::to_string(step + 1) + ": " + why;
    return script;
}

// Why the multiplayer policy refuses this console command, or empty. The command is screened as the
// command-line argument "+<command>" would be, and a map load must name a single-player map.
std::string refusal(std::string_view command) {
    std::wstring argument = L"+";
    for (const char c : command) {
        argument.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    if (const auto refused = mp_policy::screenArguments(argument)) {
        return std::string(refused->reason) + " (" + std::string(refused->pattern) + ")";
    }
    const std::size_t space = command.find_first_of(" \t");
    const std::string_view name = command.substr(0, space);
    if (sameName(name, "map") || sameName(name, "devmap")) {
        const std::string_view map =
            space == std::string_view::npos ? std::string_view{} : trim(command.substr(space));
        if (map.empty() || mp_policy::mapTripsGuard(map)) {
            return "a map that is not single-player";
        }
    }
    return {};
}

} // namespace

DebugScript parseDebugScript(std::string_view text) {
    DebugScript script;
    if (trim(text).empty()) {
        return script;
    }
    const std::vector<std::string_view> steps = split(text, '|');
    double last = 0.0; // the time of the step before, kept or not
    std::vector<std::string> refused;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const std::string_view step = trim(steps[i]);
        if (step.empty()) {
            continue;
        }
        const std::size_t colon = step.find(':');
        if (colon == std::string_view::npos) {
            return failed(i, "no ':' between the time and the commands");
        }
        const std::string_view time = trim(step.substr(0, colon));
        DebugStep parsed;
        const auto [end, code] = std::from_chars(time.data(), time.data() + time.size(), parsed.seconds);
        if (time.empty() || code != std::errc{} || end != time.data() + time.size() ||
            !(parsed.seconds >= 0.0) || !std::isfinite(parsed.seconds)) {
            return failed(i, "the time is not a finite non-negative number of seconds");
        }
        if (parsed.seconds < last) {
            return failed(i, "the time is earlier than the step before it");
        }
        last = parsed.seconds;
        std::size_t given = 0;
        for (const std::string_view command : split(step.substr(colon + 1), ';')) {
            const std::string_view trimmed = trim(command);
            if (trimmed.empty()) {
                continue;
            }
            ++given;
            if (const std::string why = refusal(trimmed); !why.empty()) {
                return failed(i, "refused, " + why + ": " + std::string(trimmed));
            }
            if (const std::string_view why = offList(trimmed); !why.empty()) {
                refused.push_back("step " + std::to_string(i + 1) + ": " + std::string(trimmed) + " (" +
                                  std::string(why) + ")");
                continue;
            }
            parsed.commands.emplace_back(trimmed);
        }
        if (given == 0) {
            return failed(i, "no command");
        }
        if (!parsed.commands.empty()) {
            script.steps.push_back(std::move(parsed));
        }
    }
    script.refused = std::move(refused);
    return script;
}

} // namespace evr::vkcore
