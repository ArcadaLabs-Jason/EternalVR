#include "features/input/binding_text.hpp"

#include "features/input/binding_keys.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>

namespace evr::input {

namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r";
    const std::size_t first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kSpace);
    return text.substr(first, last - first + 1);
}

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::string_view kTableHeader = "[bindings]";

bool isBareKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.';
}

bool isQuote(char c) {
    return c == '"' || c == '\'';
}

// Reads a TOML basic ("...") or literal ('...') string at the start of `text`, advancing `text` past
// it. Escapes are not needed for binding names, so a backslash in a basic string is rejected rather
// than half-supported; a literal string has no escapes and takes its contents as written.
std::optional<std::string_view> takeQuoted(std::string_view& text, std::string& error) {
    if (text.empty() || !isQuote(text.front())) {
        error = "expected a quoted string";
        return std::nullopt;
    }
    const char quote = text.front();
    const std::size_t close = text.find(quote, 1);
    if (close == std::string_view::npos) {
        error = "unterminated string";
        return std::nullopt;
    }
    const std::string_view inner = text.substr(1, close - 1);
    if (quote == '"' && inner.find('\\') != std::string_view::npos) {
        error = "escape sequences are not supported";
        return std::nullopt;
    }
    text.remove_prefix(close + 1);
    return inner;
}

std::optional<std::string_view> takeKey(std::string_view& text, std::string& error) {
    if (!text.empty() && isQuote(text.front())) {
        return takeQuoted(text, error);
    }
    const auto end = std::find_if_not(text.begin(), text.end(), isBareKeyChar);
    const auto length = static_cast<std::size_t>(end - text.begin());
    if (length == 0) {
        error = "expected a key";
        return std::nullopt;
    }
    const std::string_view key = text.substr(0, length);
    text.remove_prefix(length);
    return key;
}

struct Entry {
    std::string_view key;
    std::string_view value;
};

std::optional<Entry> parseLine(std::string_view line, std::string& error) {
    const auto key = takeKey(line, error);
    if (!key) {
        return std::nullopt;
    }
    line = trim(line);
    if (line.empty() || line.front() != '=') {
        error = "expected '=' after the key";
        return std::nullopt;
    }
    line = trim(line.substr(1));
    const auto value = takeQuoted(line, error);
    if (!value) {
        return std::nullopt;
    }
    line = trim(line);
    if (!line.empty() && line.front() != '#') {
        error = "unexpected text after the value";
        return std::nullopt;
    }
    return Entry{*key, *value};
}

// Sort key for formatting; see formatBindingText.
using Rank = std::tuple<int, int, int, int>;

Rank rankOf(std::string_view key) {
    struct Ranker {
        Rank operator()(const WeaponHandKey& /*key*/) const { return {0, 0, 0, 0}; }
        Rank operator()(const StickRoleKey& key) const { return {1, static_cast<int>(key.hand), 0, 0}; }
        Rank operator()(const ButtonKey& key) const {
            return {1, static_cast<int>(key.hand), 1,
                    static_cast<int>(key.input) * 3 + static_cast<int>(key.kind)};
        }
        Rank operator()(const GestureKey& key) const {
            return {1, static_cast<int>(key.hand), 2, static_cast<int>(key.gesture)};
        }
    };
    if (const auto parsed = parseBindingKey(key)) {
        return std::visit(Ranker{}, *parsed);
    }
    return {2, 0, 0, 0};
}

// Writes `text` as a TOML string that reads back unchanged without escapes: a basic string, or a
// literal one when the text holds a character a basic string would need to escape. Text read by
// parseBindingText always fits one of the two.
std::string quotedString(std::string_view text) {
    const bool needsLiteral = text.find_first_of("\"\\") != std::string_view::npos;
    const char quote = needsLiteral ? '\'' : '"';
    return quote + std::string(text) + quote;
}

} // namespace

BindingTextResult parseBindingText(std::string_view text) {
    BindingTextResult result;
    // Editors on Windows often save UTF-8 with a byte order mark, which TOML allows.
    if (text.starts_with(kUtf8Bom)) {
        text.remove_prefix(kUtf8Bom.size());
    }
    int lineNumber = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string_view line = trim(text.substr(start, newline - start));
        ++lineNumber;
        start = newline == std::string_view::npos ? text.size() + 1 : newline + 1;

        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            // The table the bindings are written under. Any other table holds something else.
            if (trim(line.substr(0, line.find('#'))) != kTableHeader) {
                result.issues.push_back(
                    issueOf(BindingIssueKind::Syntax, {}, lineNumber,
                            "only a [bindings] table header is expected in binding text"));
            }
            continue;
        }
        std::string error;
        const auto entry = parseLine(line, error);
        if (!entry) {
            result.issues.push_back(issueOf(BindingIssueKind::Syntax, {}, lineNumber, error));
            continue;
        }
        const auto [position, inserted] = result.entries.emplace(entry->key, entry->value);
        if (inserted) {
            result.lineOf.emplace(entry->key, lineNumber);
        } else {
            result.issues.push_back(
                issueOf(BindingIssueKind::DuplicateKey, position->first, lineNumber,
                        "'" + position->first + "' is set more than once; the first value is used"));
        }
    }
    return result;
}

std::string formatBindingText(const BindingMap& entries) {
    std::vector<std::pair<Rank, const BindingMap::value_type*>> ordered;
    ordered.reserve(entries.size());
    for (const auto& entry : entries) {
        ordered.emplace_back(rankOf(entry.first), &entry);
    }
    // Stable, so keys of equal rank (unknown ones) stay in the map's alphabetical order.
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::string text(kTableHeader);
    text += '\n';
    for (const auto& [rank, entry] : ordered) {
        text += quotedString(entry->first);
        text += " = ";
        text += quotedString(entry->second);
        text += '\n';
    }
    return text;
}

} // namespace evr::input
