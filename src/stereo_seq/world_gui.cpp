#include "stereo_seq/world_gui.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

WorldGuiMode worldGuiMode(std::string_view value) {
    std::string lower;
    for (const char c : value) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "0" || lower == "off" || lower == "false" || lower == "no") {
        return WorldGuiMode::Off;
    }
    if (lower == "count") {
        return WorldGuiMode::Count;
    }
    return WorldGuiMode::On;
}

WorldGuiStamp classifyWorldGuiStamp(std::uint32_t committed, std::uint32_t current) {
    if (committed == current) {
        return WorldGuiStamp::Current;
    }
    if (committed + 1u == current) {
        return WorldGuiStamp::OneBehind;
    }
    return WorldGuiStamp::Older;
}

std::uint32_t worldGuiFrameFor(WorldGuiMode mode, Eye eye, std::uint32_t committed, std::uint32_t current) {
    if (mode == WorldGuiMode::On && eye == Eye::Right &&
        classifyWorldGuiStamp(committed, current) == WorldGuiStamp::OneBehind) {
        return committed;
    }
    return current;
}

} // namespace evr::stereo_seq
