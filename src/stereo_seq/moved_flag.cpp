#include "stereo_seq/moved_flag.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

MovedFlagMode movedFlagMode(std::string_view value) {
    std::string lower;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "0" || lower == "off" || lower == "false" || lower == "no") {
        return MovedFlagMode::Off;
    }
    if (lower == "count") {
        return MovedFlagMode::Count;
    }
    return MovedFlagMode::On;
}

std::uint8_t movedFlagStatusFor(MovedFlagMode mode, Eye eye, std::uint8_t status) {
    if (mode == MovedFlagMode::On && eye == Eye::Right && status == 1) {
        return 0;
    }
    return status;
}

} // namespace evr::stereo_seq
