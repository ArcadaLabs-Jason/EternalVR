#include "platform/settings/profile_name.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace evr::settings {

namespace {

constexpr std::string_view kWhitespace = " \t\r\n\v\f";
constexpr std::string_view kForbiddenCharacters = "\\/:*?\"<>|";

// Device names Windows reserves in every directory. "CON.toml" is reserved too: the check applies to
// the part before the first dot.
constexpr std::array<std::string_view, 22> kReservedNames{
    "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

char toLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalIgnoringAsciiCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return toLowerAscii(x) == toLowerAscii(y);
           });
}

bool isControl(char c) {
    const auto code = static_cast<unsigned char>(c);
    return code < 0x20 || code == 0x7F;
}

} // namespace

std::string_view trimProfileName(std::string_view name) {
    const std::size_t first = name.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = name.find_last_not_of(kWhitespace);
    return name.substr(first, last - first + 1);
}

bool sameProfileName(std::string_view a, std::string_view b) {
    return equalIgnoringAsciiCase(trimProfileName(a), trimProfileName(b));
}

bool isFileSafeProfileName(std::string_view trimmedName) {
    if (trimmedName.empty() || trimmedName.back() == '.') {
        // Windows drops a trailing dot, so "Alice." would silently become "Alice".
        return false;
    }
    const bool badCharacter = std::any_of(trimmedName.begin(), trimmedName.end(), [](char c) {
        return isControl(c) || kForbiddenCharacters.find(c) != std::string_view::npos;
    });
    if (badCharacter) {
        return false;
    }
    const std::string_view stem = trimProfileName(trimmedName.substr(0, trimmedName.find('.')));
    return std::none_of(kReservedNames.begin(), kReservedNames.end(),
                        [stem](std::string_view reserved) { return equalIgnoringAsciiCase(stem, reserved); });
}

} // namespace evr::settings
