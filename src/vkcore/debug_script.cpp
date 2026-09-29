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
    std::string name(command.substr(0, space));
    for (char& c : name) {
        c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (name == "map" || name == "devmap") {
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
        if (!script.steps.empty() && parsed.seconds < script.steps.back().seconds) {
            return failed(i, "the time is earlier than the step before it");
        }
        for (const std::string_view command : split(step.substr(colon + 1), ';')) {
            if (const std::string_view trimmed = trim(command); !trimmed.empty()) {
                if (const std::string why = refusal(trimmed); !why.empty()) {
                    return failed(i, "refused, " + why + ": " + std::string(trimmed));
                }
                parsed.commands.emplace_back(trimmed);
            }
        }
        if (parsed.commands.empty()) {
            return failed(i, "no command");
        }
        script.steps.push_back(std::move(parsed));
    }
    return script;
}

} // namespace evr::vkcore
