#pragma once

// Rules for profile names, which double as file names.

#include <string_view>

namespace evr::settings {

// `name` without leading and trailing whitespace.
std::string_view trimProfileName(std::string_view name);

// True when two names refer to the same profile: equal after trimming, ignoring ASCII case.
bool sameProfileName(std::string_view a, std::string_view b);

// True when a trimmed, non-empty name can be a file name on Windows, macOS and Linux.
bool isFileSafeProfileName(std::string_view trimmedName);

} // namespace evr::settings
